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

#ifndef _XCP_NG_ASYNC_IO_IO_REQ_H_
#define _XCP_NG_ASYNC_IO_IO_REQ_H_

#include <assert.h>
#include <stdint.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "xcp-ng/async-io/io-global.h"

// =============================================================================

typedef enum {
  XcpIoOpcodeRead = 1 << 0,
  XcpIoOpcodeWrite = 1 << 1,
  XcpIoOpcodeReadV = 1 << 2,
  XcpIoOpcodeWriteV = 1 << 3
} XcpIoOpcode;

XCP_DECL_UNUSED static inline const char *xcp_io_opcode_to_str (XcpIoOpcode opcode) {
  switch (opcode) {
    case XcpIoOpcodeRead: return "read";
    case XcpIoOpcodeWrite: return "write";
    case XcpIoOpcodeReadV: return "readv";
    case XcpIoOpcodeWriteV: return "writev";
  }
}

// -----------------------------------------------------------------------------

typedef struct XcpIoReq XcpIoReq;

typedef void (*XcpIoReqCb)(XcpIoReq *req, int err, void *userArg);

typedef struct XcpIoReq {
  XcpIoReqCb cb;  // Completion callback.
  void *userData; // User data passed to the completion callback.

  XcpIoOpcode opcode;
  int fd;

  // If opcode is either Read or Write, this field contains the addr buf and the buf size.
  // Otherwise (ReadV or WriteV), it contains an iovec and the iovec length.
  struct iovec iov;
  off_t offset;

  struct {
    STAILQ_ENTRY(XcpIoReq) next; // Next request to schedule.
  } pImpl; // Private implementation, do not touch!
} XcpIoReq;

XCP_DECL_UNUSED static inline void xcp_io_req_prep_rw (
  XcpIoReq *req, XcpIoOpcode opcode, int fd, void *addr, size_t len, off_t offset
) {
  req->opcode = opcode;
  req->fd = fd;
  req->iov.iov_base = addr;
  req->iov.iov_len = len;
  req->offset = offset;
}

XCP_DECL_UNUSED static inline void xcp_io_req_set_cb (XcpIoReq *req, XcpIoReqCb cb) {
  req->cb = cb;
}

XCP_DECL_UNUSED static inline void xcp_io_req_set_user_data (XcpIoReq *req, void *userData) {
  req->userData = userData;
}

XCP_DECL_UNUSED static inline void *xcp_io_req_get_addr (const XcpIoReq *req) {
  const uint8_t opcode = req->opcode;
  XCP_UNUSED(opcode);
  assert(
    opcode == XcpIoOpcodeRead ||
    opcode == XcpIoOpcodeWrite ||
    opcode == XcpIoOpcodeReadV ||
    opcode == XcpIoOpcodeWriteV
  );
  return req->iov.iov_base;
}

XCP_DECL_UNUSED static inline off_t xcp_io_req_get_offset (const XcpIoReq *req) {
  const uint8_t opcode = req->opcode;
  XCP_UNUSED(opcode);
  assert(
    opcode == XcpIoOpcodeRead ||
    opcode == XcpIoOpcodeWrite ||
    opcode == XcpIoOpcodeReadV ||
    opcode == XcpIoOpcodeWriteV
  );
  return req->offset;
}

XCP_DECL_UNUSED static inline size_t xcp_io_req_get_size (const XcpIoReq *req) {
  switch (req->opcode) {
    case XcpIoOpcodeRead:
    case XcpIoOpcodeWrite:
      return req->iov.iov_len;

    case XcpIoOpcodeReadV:
    case XcpIoOpcodeWriteV: {
      size_t size = 0;
      const struct iovec *vec = (const struct iovec *)req->iov.iov_base;
      for (uint32_t i = 0; i < req->iov.iov_len; ++i) {
        assert(vec[i].iov_len);
        size += vec[i].iov_len;
      }
      return size;
    }

    default: assert(false);
  }
  return 0;
}

#endif // ifndef _XCP_NG_ASYNC_IO_IO_REQ_H_
