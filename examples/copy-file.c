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
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "xcp-ng/async-io.h"

// =============================================================================

#define QUEUE_CAPACITY 64
#define QUEUE_BLOCK_SIZE 32 * 1024

// -----------------------------------------------------------------------------

static inline int get_file_size (int fd, off_t *size) {
  struct stat st;
  if (fstat(fd, &st) < 0)
    return -1;

  if (S_ISREG(st.st_mode)) {
    *size = st.st_size;
    return 0;
  }
  if (S_ISBLK(st.st_mode))
    return ioctl(fd, BLKGETSIZE64, size) ? -1 : 0;

  return -1;
}

// -----------------------------------------------------------------------------

typedef struct {
  XcpIoQueue *queue;
  int out;
} WriteContext;

static void write_completion_cb (XcpIoReq *req, int err, void *userArg) {
  XCP_UNUSED(userArg);

  if (err)
    fprintf(stderr, "Write error: %s\n", strerror(-err));
  free(req);
}

static void read_completion_cb (XcpIoReq *req, int err, void *userArg) {
  if (err) {
    fprintf(stderr, "Read error: %s\n", strerror(-err));
    free(req);
    return;
  }

  const WriteContext *writeContext = userArg;
  const size_t blockSize = xcp_io_req_get_size(req);
  const off_t offset = xcp_io_req_get_offset(req);

  xcp_io_req_prep_rw(req, XcpIoOpcodeWrite, writeContext->out, (char *)req + sizeof *req, blockSize, offset);
  xcp_io_req_set_cb(req, write_completion_cb);
  xcp_io_req_set_user_data(req, NULL);
  xcp_io_queue_insert(writeContext->queue, req);
}

static int queue_read (XcpIoQueue *queue, int in, size_t blockSize, off_t offset, WriteContext *context) {
  XcpIoReq *req = malloc(sizeof *req + blockSize);
  if (!req)
    return -ENOMEM;

  xcp_io_req_prep_rw(req, XcpIoOpcodeRead, in, (char *)req + sizeof *req, blockSize, offset);
  xcp_io_req_set_cb(req, read_completion_cb);
  xcp_io_req_set_user_data(req, context);
  xcp_io_queue_insert(queue, req);
  return 0;
}

// -----------------------------------------------------------------------------

static inline int queue_submit (XcpIoQueue *queue) {
  int ret;
  if ((ret = xcp_io_queue_submit(queue)) < 0)
    fprintf(stderr, "Failed to submit reqs: %s\n", strerror(-ret));
  return ret;
}

// -----------------------------------------------------------------------------

static int copy (XcpIoQueue *queue, int in, int out, off_t inSize) {
  off_t offset = 0;

  WriteContext writeContext = { queue, out };
  while (inSize || !xcp_io_queue_is_empty(queue)) {
    // 1. Read from in.
    while (inSize && !xcp_io_queue_is_full(queue)) {
      off_t blockSize = inSize;
      if (blockSize > QUEUE_BLOCK_SIZE)
        blockSize = QUEUE_BLOCK_SIZE;

      int ret;
      if ((ret = queue_read(queue, in, (size_t)blockSize, offset, &writeContext)))
        return ret;

      inSize -= blockSize;
      offset += blockSize;
    }

    int ret;
    if ((ret = queue_submit(queue)) < 0)
      return ret;

    // 2. Write to out.
    if (!xcp_io_queue_polling_enabled(queue)) {
      struct pollfd fds;
      fds.events = POLLIN;
      fds.fd = xcp_io_queue_get_event_fd(queue);
      fds.revents = 0;

      do {
        ret = poll(&fds, 1, 0);
      } while (ret == -1 && errno == EINTR);
      if (!ret)
        continue;
      if (ret < 0)
        return -errno;
    }

    if ((ret = xcp_io_queue_process_responses(queue)) < 0)
      return ret;
  }

  assert(xcp_io_queue_get_inflight_count(queue) == 0);
  assert(xcp_io_queue_get_pending_count(queue) == 0);
  assert(xcp_io_queue_is_empty(queue));

  return 0;
}

// -----------------------------------------------------------------------------

static void usage (const char *progname) {
  printf("Usage: %s [OPTIONS]\n", progname);
  puts("  --int                    input file");
  puts("  --out                    output file");
  puts("  --polling                use polling");
  puts("  --help                   print this help and exit");
}

// -----------------------------------------------------------------------------

int main(int argc, char *argv[]) {
  const struct option longopts[] = {
    { "in", 1, NULL, 'i' },
    { "out", 1, NULL, 'o' },
    { "polling", 0, NULL, 'p' },
    { "help", 0, NULL, 'h' },
    { NULL, 0, 0, 0 }
  };

  char *inPath = NULL;
  char *outPath = NULL;

  bool usePolling = false;

  int option;
  int longindex = 0;
  while ((option = getopt_long_only(argc, argv, "", longopts, &longindex)) != -1) {
    switch (option) {
      case 'i':
        inPath = optarg;
        break;
      case 'o':
        outPath = optarg;
        break;
      case 'p':
        usePolling = true;
        break;
      case 'h':
        usage(argv[0]);
        return EXIT_SUCCESS;
      case '?':
        if (optopt == 0)
          fprintf(stderr, "Unknown option: `%s`.\n", argv[optind - 1]);
        else
          fprintf(stderr, "Error parsing option: `-%c`\n", optopt);
        fprintf(stderr, "Try `%s --help` for more information.\n", *argv);
        return EXIT_FAILURE;
    }
  }

  if (!inPath || !outPath) {
    fprintf(stderr, "in and/or out are not set!\n");
    return EXIT_FAILURE;
  }

  const int in = open(inPath, O_RDONLY);
  if (in < 0) {
    perror("Failed to open input file");
    return EXIT_FAILURE;
  }

  const int out = open(outPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (out < 0) {
    perror("Failed to open output file");
    return EXIT_FAILURE;
  }

  off_t inSize;
  if (get_file_size(in, &inSize)) {
    fprintf(stderr, "Unable to get size of input file.\n");
    return EXIT_FAILURE;
  }

  int ret;

  XcpIoQueue queue;
  if ((ret = xcp_io_queue_init(&queue, QUEUE_CAPACITY, usePolling)) < 0) {
    fprintf(stderr, "Failed to initialize queue: %s\n", strerror(-ret));
    return EXIT_FAILURE;
  }

  ret = copy(&queue, in, out, inSize);
  xcp_io_queue_uninit(&queue);

  close(in);
  close(out);

  return ret;
}
