/*
 * Copyright (C) 2019  Vates SAS - ronan.abhamon@vates.fr
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifdef __alpha__
  #ifndef __NR_io_uring_enter
    #define __NR_io_uring_enter 536
  #endif // ifndef __NR_io_uring_enter
#else
  #ifndef __NR_io_uring_enter
    #define __NR_io_uring_enter 426
  #endif // ifndef __NR_io_uring_enter
#endif // ifdef __alpha__

#include "xcp-ng/async-io/io-queue.h"
#include "xcp-ng/async-io/io-req.h"

// =============================================================================

// Move [HEAD1, LAST] to the new list HEAD2.
// It's an optimization to avoid remove/insert.
#define STAILQ_CUT(HEAD1, LAST, HEAD2, FIELD) \
  do { \
    assert((LAST)); \
    (HEAD2)->stqh_first = (HEAD1)->stqh_first; \
    (HEAD2)->stqh_last = &(LAST)->FIELD.stqe_next; \
    (LAST)->FIELD.stqe_next = NULL; \
    if (!((HEAD1)->stqh_first = (LAST)->FIELD.stqe_next)) \
      (HEAD1)->stqh_last = &(HEAD1)->stqh_first; \
  } while (false)

// -----------------------------------------------------------------------------

// Call the request callback after completion.
static inline void process_response (XcpIoReq *req, int res) {
  int err;
  if (XCP_UNLIKELY(res < 0))
    err = res;
  else if (XCP_LIKELY((size_t)res == xcp_io_req_get_size(req)))
    err = 0;
  else // TODO: Reschedule instead.
    err = -EIO;

  if (req->cb)
    req->cb(req, err, req->userData);
}

// Fetch responses in the queue and notify user.
static inline unsigned int fetch_responses (XcpIoQueue *queue) {
  struct io_uring *ring = &queue->pImpl.ring;

  // How many responses are ready?
  const unsigned int count = io_uring_cq_ready(ring);
  if (XCP_UNLIKELY(!count))
    return 0;
  assert(count <= queue->capacity);

  // Fill response array.
  unsigned int head = *ring->cq.khead;
  const unsigned int mask = *ring->cq.kring_mask;

  for (const unsigned int last = head + count; head != last; ++head) {
    const struct io_uring_cqe *cqe = &ring->cq.cqes[head & mask];
    process_response((XcpIoReq *)cqe->user_data, cqe->res);
  }

  // Mark responses as read in the ring.
  const struct io_uring_cq *cq = &ring->cq;
  io_uring_smp_store_release(cq->khead, *cq->khead + count);

  assert(queue->inflightCount >= count);
  queue->inflightCount -= count;

  return count;
}

// Cancel all given requests.
static inline void cancel_requests (XcpIoReq *reqs, int err) {
  while (reqs) {
    XcpIoReq *nextReq = STAILQ_NEXT(reqs, pImpl.next);
    reqs->cb(reqs, err, reqs->userData);
    reqs = nextReq;
  }
}

// Fill a io_uring_sqe instance from a XcpIoReq.
static inline void set_sqe_from_req (const XcpIoReq *req, struct io_uring_sqe *sqe) {
  sqe->opcode = req->opcode == XcpIoOpcodeRead || req->opcode == XcpIoOpcodeReadV
    ? IORING_OP_READV
    : IORING_OP_WRITEV;
  sqe->flags = 0;
  sqe->ioprio = 0;
  sqe->fd = req->fd;
  sqe->off = (uint64_t)req->offset;
  sqe->addr = (uint64_t)&req->iov;
  sqe->len = req->opcode == XcpIoOpcodeRead || req->opcode == XcpIoOpcodeWrite ? 1 : (uint32_t)req->iov.iov_len;
  sqe->rw_flags = 0;
  sqe->user_data = (uint64_t)req;
  sqe->__pad2[0] = sqe->__pad2[1] = sqe->__pad2[2] = 0;
  sqe->buf_index = 0;
}

// -----------------------------------------------------------------------------

int xcp_io_queue_init (XcpIoQueue *queue, size_t capacity, bool usePolling) {
  // 1. Init fields.
  memset(queue, 0, sizeof *queue);
  if (!capacity)
    return -EINVAL;

  queue->eventFd = -1;
  queue->usePolling = usePolling;

  STAILQ_INIT(&queue->reqs);

  int err = 0;

  // 2. Create an eventfd to be notified when a request ends.
  if (!usePolling && (queue->eventFd = eventfd(0, 0)) < 0)
    return -errno;

  // 3. Init ring.
  struct io_uring *ring = &queue->pImpl.ring;
  if ((err = io_uring_queue_init((unsigned int)capacity, ring, usePolling ? IORING_SETUP_IOPOLL : 0)) < 0) {
    close(queue->eventFd);
    queue->eventFd = -1;
  } else if (!usePolling && (err = io_uring_register_eventfd(ring, queue->eventFd)) < 0)
    xcp_io_queue_uninit(queue);
  else
    queue->capacity = capacity;

  return err;
}

void xcp_io_queue_uninit (XcpIoQueue *queue) {
  if (!queue->capacity)
    return;

  if (queue->eventFd != -1) {
    close(queue->eventFd);
    queue->eventFd = -1;
  }
  io_uring_queue_exit(&queue->pImpl.ring);
}

void xcp_io_queue_insert (XcpIoQueue *queue, XcpIoReq *req) {
  STAILQ_INSERT_TAIL(&queue->reqs, req, pImpl.next);
  ++queue->pendingCount;
}

int xcp_io_queue_submit (XcpIoQueue *queue) {
  struct io_uring *ring = &queue->pImpl.ring;

  // 1. Insert requests in the ring.
  size_t n = 0;
  XcpIoReq *req = STAILQ_FIRST(&queue->reqs);
  if (XCP_LIKELY(req)) {
    assert(queue->pendingCount);

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (XCP_LIKELY(sqe)) {
      set_sqe_from_req(req, sqe);

      for (++n; n < queue->pendingCount; ++n) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
        if (!sqe)
          break;
        req = STAILQ_NEXT(req, pImpl.next);
        set_sqe_from_req(req, sqe);
      }
    }
  }

  // 2. Submit requests and clean pending req list.
  int ret = 0;
  if (XCP_LIKELY(n)) {
    STAILQ_HEAD(, XcpIoReq) reqsToSubmit;
    STAILQ_INIT(&reqsToSubmit);
    STAILQ_CUT(&queue->reqs, req, &reqsToSubmit, pImpl.next);
    do {
      ret = io_uring_submit(ring);
    } while (ret < 0 && errno == EAGAIN);

    // Fatal error, discard requests.
    if (XCP_UNLIKELY(ret < 0))
      cancel_requests(STAILQ_FIRST(&reqsToSubmit), -errno);
    else
      queue->inflightCount += n;

    assert(queue->pendingCount >= n);
    queue->pendingCount -= n;
  } else if (queue->usePolling && queue->inflightCount) {
    // We must call explicitly io_uring_enter in this case to get responses.
    do {
      ret = (int)syscall(__NR_io_uring_enter, ring->ring_fd, 0, 0, IORING_ENTER_GETEVENTS, NULL, _NSIG / 8);
    } while (ret < 0 && errno == EAGAIN);
  }

  return ret ? ret : (int)n;
}

int xcp_io_queue_cancel (XcpIoQueue *queue) {
  cancel_requests(STAILQ_FIRST(&queue->reqs), -EIO);
  STAILQ_INIT(&queue->reqs);

  const size_t pendingCount = queue->pendingCount;
  queue->pendingCount = 0;
  return (int)pendingCount;
}

int xcp_io_queue_process_responses (XcpIoQueue *queue) {
  // Fetch responses directly if polling is used.
  if (queue->usePolling)
    return (int)fetch_responses(queue);

  // Get current response count.
  uint64_t responseCount;
  if (XCP_UNLIKELY(read(queue->eventFd, &responseCount, sizeof responseCount) < 0))
    return -errno;
  if (XCP_UNLIKELY(responseCount == 0))
    return 0;

  // Note: The number of responses given by fetch_responses can be greater or lower than response count because
  // the ring counter can be updated by the kernel just after our previous read.
  return (int)fetch_responses(queue);
}
