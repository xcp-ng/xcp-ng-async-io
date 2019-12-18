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

#ifndef _XCP_NG_ASYNC_IO_IO_QUEUE_H_
#define _XCP_NG_ASYNC_IO_IO_QUEUE_H_

#include <liburing.h>
#include <sys/queue.h>

#include "xcp-ng/async-io/io-global.h"

// =============================================================================

typedef struct XcpIoReq XcpIoReq;

typedef struct XcpIoQueue {
  // Max number of requests that can be processed at the same time.
  size_t capacity;

  // All requests before processing.
  STAILQ_HEAD(, XcpIoReq) reqs;

  // Number of requests currently processed.
  size_t inflightCount;

  // Number of requests in the "reqs" list.
  size_t pendingCount;

  // Event fd to be notified when there is a change in the ring.
  // Note: unusable if polling is activated.
  int eventFd;

  // Used on specific devices like NVMe.
  bool usePolling;

  struct {
    struct io_uring ring;
  } pImpl; // Private implementation, do not touch!
} XcpIoQueue;

// -----------------------------------------------------------------------------

int xcp_io_queue_init (XcpIoQueue *queue, size_t capacity, bool usePolling);
void xcp_io_queue_uninit (XcpIoQueue *queue);

void xcp_io_queue_insert (XcpIoQueue *queue, XcpIoReq *req);

int xcp_io_queue_submit (XcpIoQueue *queue);
int xcp_io_queue_submit_n (XcpIoQueue *queue, unsigned int n);
int xcp_io_queue_cancel (XcpIoQueue *queue);

// Must be called when a notification is received via event fd.
int xcp_io_queue_process_responses (XcpIoQueue *queue);

XCP_DECL_UNUSED static inline uint64_t xcp_io_queue_get_inflight_count (const XcpIoQueue *queue) {
  return queue->inflightCount;
}

XCP_DECL_UNUSED static inline uint64_t xcp_io_queue_get_pending_count (const XcpIoQueue *queue) {
  return queue->pendingCount;
}

XCP_DECL_UNUSED static inline bool xcp_io_queue_is_empty (const XcpIoQueue *queue) {
  return !queue->inflightCount && !queue->pendingCount;
}

XCP_DECL_UNUSED static inline bool xcp_io_queue_is_full (const XcpIoQueue *queue) {
  return queue->inflightCount + queue->pendingCount >= queue->capacity;
}

XCP_DECL_UNUSED static inline int xcp_io_queue_get_event_fd (const XcpIoQueue *queue) {
  return queue->eventFd;
}

XCP_DECL_UNUSED static inline bool xcp_io_queue_polling_enabled (const XcpIoQueue *queue) {
  return queue->usePolling;
}

#endif // ifndef _XCP_NG_ASYNC_IO_IO_QUEUE_H_
