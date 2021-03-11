#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "script.h"
#include "thread.h"
#include "media.h"
#include "mime.h"
#include "io.h"
#include "sys.h"
#include "stream.h"
#include "debug.h"
#include "xalloc.h"

#define NAME "MEDIA_STREAM"

#define STREAM_BUFFER 256

#define TAG_STREAM  "STREAMN"

typedef struct {
  int handle;
  unsigned char *buf;
  int alloc, len;
} stream_node_t;

typedef struct {
  int node;
  int pe;
  script_ref_t ref;
  unsigned char *buf;
  int alloc, len;
} stream_server_t;

static int libmedia_stream_process(media_frame_t *frame, void *data);
static int libmedia_stream_destroy(void *data);

static node_dispatch_t stream_dispatch = {
  libmedia_stream_process,
  NULL,
  NULL,
  libmedia_stream_destroy
};

static int stream_getbuf_callback(void *_data, void *_arg) {
  stream_node_t *data;
  stream_server_t *server;
  int r = 0;

  data = (stream_node_t *)_data;
  server = (stream_server_t *)_arg;
  server->len = 0;

  if (data->buf && data->len) {
    if (server->buf == NULL) {
      server->buf = xcalloc(1, data->len);
      if (server->buf) server->alloc = data->len;
    } else if (data->len > server->alloc) {
      server->buf = xrealloc(server->buf, data->len);
      if (server->buf) server->alloc = data->len;
    }

    if (server->buf) {
      server->len = data->len;
      memcpy(server->buf, data->buf, data->len);
    } else {
      r = -1;
    }
  }

  return r;
}

static int stream_callback(int event, io_addr_t *addr, unsigned char *buf, int len, int fd, int handle, void **data) {
  script_arg_t ret;
  stream_server_t *server, *new_server;
  char *saddr, buffer[STREAM_BUFFER];
  int n, r = 0;

  server = (stream_server_t *)(*data);

  switch (event) {
      case IO_CLOSE:
        debug(DEBUG_INFO, "STREAM", "handle %d close", handle);
        if (server->ref) script_remove_ref(server->pe, server->ref);
        xfree(server);
        break;
      case IO_ACCEPT:
        new_server = xcalloc(1, sizeof(stream_server_t));
        if (new_server) {
          new_server->node = server->node;
          new_server->pe = server->pe;
          new_server->ref = server->ref ? script_dup_ref(server->pe, server->ref) : 0;
          *data = new_server;
          saddr = addr->addr.ip.host; // only IO_IP_ADDR is supported
          debug(DEBUG_INFO, "STREAM", "handle %d accepted new client %s", handle, saddr);
          if (new_server->ref) script_call(new_server->pe, new_server->ref, &ret, "IB", mkint(handle), 1);
        } else {
          r = -1;
        }
        break;
      case IO_DISCONNECT:
        saddr = addr->addr.ip.host; // only IO_IP_ADDR is supported
        debug(DEBUG_INFO, "STREAM", "handle %d client %s disconnected", handle, saddr);
        if (server->ref) {
          script_call(server->pe, server->ref, &ret, "IB", mkint(handle), 0);
          script_remove_ref(server->pe, server->ref);
        }
        if (server->buf) xfree(server->buf);
        xfree(server);
        r = -1;
        break;
      case IO_DATA:
        if (len == 0) {
          for (;;) {
            if (thread_must_end()) break;
            server->len = 0;
            if (node_call(server->node, NAME, stream_getbuf_callback, server) == -1) {
              debug(DEBUG_ERROR, "STREAM", "node call failed");
              r = -1;
              break;
            }
            if (server->len) break;
          }

/*
    snprintf(buffer, STREAM_BUFFER-1, "HTTP/1.0 200 OK\r\nConnection: close\r\nCache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0\r\nPragma: no-cache\r\nExpires: Mon, 1 Jan 2000 00:00:00 GMT\r\nContent-Type: multipart/x-mixed-replace;boundary=%s\r\n\r\n%s", server->boundary+2, server->boundary);

    snprintf(buffer, STREAM_BUFFER-1, "Content-Type: %s\r\nContent-Length: %d\r\n\r\n", MIME_TYPE_JPEG, server->len);
*/

          if (server->len) {
            snprintf(buffer, STREAM_BUFFER-1, "HTTP/1.0 200 OK\r\nConnection: keep-alive\r\nCache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0\r\nPragma: no-cache\r\nExpires: Mon, 1 Jan 2000 00:00:00 GMT\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n", MIME_TYPE_JPEG, server->len);
            n = strlen(buffer);
            if (sys_write(fd, (uint8_t *)buffer, n) == n &&
                sys_write(fd, server->buf, server->len) == server->len) {
              debug(DEBUG_TRACE, "STREAM", "sent jpeg frame (%d bytes)", server->len);
            } else {
              debug(DEBUG_ERROR, "STREAM", "write failed");
              r = -1;
            }
          }

        } else {
          //n = len;
          //if (n > STREAM_BUFFER-1) n = STREAM_BUFFER-1;
          //memcpy(buffer, buf, n);
          //buffer[n] = 0;
          //debug(DEBUG_TRACE, "STREAM", "handle %d buffer \"%s\"", handle, buffer);
        }
        break;
  }

  return r;
}

static int libmedia_stream_process(media_frame_t *frame, void *_data) {
  stream_node_t *data;
  int r = -1;

  if (frame->len == 0) {
    return 0;
  }

  if (frame->meta.type != FRAME_TYPE_VIDEO) {
    debug(DEBUG_ERROR, "STREAM", "not a video frame");
    return -1;
  }

  if (frame->meta.av.v.encoding != ENC_JPEG) {
    debug(DEBUG_ERROR, "STREAM", "invalid encoding %s", video_encoding_name(frame->meta.av.v.encoding));
    return -1;
  }

  data = (stream_node_t *)_data;

  if (data->buf == NULL) {
    data->buf = xcalloc(1, frame->len);
    if (data->buf) data->alloc = frame->len;
  } else if (frame->len > data->alloc) {
    data->buf = xrealloc(data->buf, frame->len);
    if (data->buf) data->alloc = frame->len;
  }

  if (data->buf) {
    data->len = frame->len;
    memcpy(data->buf, frame->frame, frame->len);
    r = 1;
  }

  return r;
}

static int libmedia_stream_destroy(void *_data) {
  stream_node_t *data;

  data = (stream_node_t *)_data;

  if (data->handle > 0) io_stream_close(TAG_STREAM, data->handle);
  if (data->buf) xfree(data->buf);
  xfree(data);

  return 0;
}

int libmedia_node_stream(int pe) {
  stream_node_t *data;
  bt_provider_t *bt;
  stream_server_t *server;
  io_addr_t addr;
  script_ref_t ref;
  char *host = NULL;
  script_int_t port, node = -1;

  if (script_get_function(pe, 0, &ref) == 0 &&
      script_get_string(pe, 1, &host) == 0 &&
      script_get_integer(pe, 2, &port) == 0) {

    if ((data = xcalloc(1, sizeof(stream_node_t))) != NULL) {
      if ((server = xcalloc(1, sizeof(stream_server_t))) != NULL) {
        if ((node = node_create(NAME, &stream_dispatch, data)) != -1) {
          server->node = node;
          server->pe = pe;
          server->ref = ref;

          memset(&addr, 0, sizeof(addr));
          addr.addr_type = IO_IP_ADDR;
          strncpy(addr.addr.ip.host, host, MAX_HOST-1);
          addr.addr.ip.port = port;
          bt = script_get_pointer(pe, BT_PROVIDER);

          if ((data->handle = io_stream_server(TAG_STREAM, &addr, stream_callback, NULL, server, 0, bt)) == -1) {
            node_destroy(node);
            xfree(server);
            xfree(data);
            node = -1;
          }
        } else {
          xfree(server);
          xfree(data);
        }
      } else {
        xfree(data);
      }
    }
  }

  if (host) xfree(host);

  return node != -1 ? script_push_integer(pe, node) : -1;
}
