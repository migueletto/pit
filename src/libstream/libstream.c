#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/time.h>

#include "script.h"
#include "pit_io.h"
#include "sock.h"
#include "debug.h"
#include "xalloc.h"

#define TAG_STREAM  "STREAM"

typedef struct {
  int pe;
  script_ref_t ref;
} conn_t;

static void *libstream_sock_getdata(int pe, script_ref_t ref) {
  conn_t *conn;

  if ((conn = xcalloc(1, sizeof(conn_t))) != NULL) {
    conn->pe = pe;
    conn->ref = ref;
  }

  return conn;
}

static int libstream_script_call(int pe, script_ref_t ref, int event, io_addr_t *addr, unsigned char *buf, int len, int handle) {
  script_arg_t ret;
  script_lstring_t lstr;
  int r = 0;

  lstr.s = (char *)buf;
  lstr.n = len;

  switch (addr->addr_type) {
    case IO_DEVICE_ADDR:
      if (script_call(pe, ref, &ret, "IISIL", mkint(event), mkint(handle), addr->addr.dev.device, mkint(addr->addr.dev.baud), &lstr) == 0) {
        r = script_returned_value(&ret);
      }
      break;
    case IO_IP_ADDR:
      if (script_call(pe, ref, &ret, "IISIL",
          mkint(event), mkint(handle), addr->addr.ip.host, mkint(addr->addr.ip.port), &lstr) == 0) {
        r = script_returned_value(&ret);
      }
      break;
    case IO_BT_ADDR:
      if (script_call(pe, ref, &ret, "IISIL",
          mkint(event), mkint(handle), addr->addr.bt.addr, mkint(addr->addr.bt.port), &lstr) == 0) {
        r = script_returned_value(&ret);
      }
  }

  return r;
}

static int libstream_sock_callback(int event, io_addr_t *addr, unsigned char *buf, int len, int fd, int handle, void **data) {
  conn_t *conn;
  void *new_data;
  int r = 0;

  conn = (conn_t *)(*data);

  switch (event) {
    case IO_BIND:
    case IO_BIND_ERROR:
    case IO_TIMEOUT:
    case IO_CONNECT:
    case IO_CONNECT_ERROR:
    case IO_DATA:
    case IO_LINE:
      r = libstream_script_call(conn->pe, conn->ref, event, addr, buf, len, handle);
      break;
    case IO_ACCEPT:
      r = libstream_script_call(conn->pe, conn->ref, event, addr, buf, len, handle);
      if (r == 0) {
        if ((new_data = libstream_sock_getdata(conn->pe, conn->ref ? script_dup_ref(conn->pe, conn->ref) : 0)) != NULL) {
          *data = new_data;
        } else {
          r = 1;
        }
      }
      break;
    case IO_CMD:
      r = 0;
      break;
    case IO_DISCONNECT:
      r = libstream_script_call(conn->pe, conn->ref, event, addr, buf, len, handle);
      debug(DEBUG_INFO, "STREAM", "releasing resources on disconnect");
      script_remove_ref(conn->pe, conn->ref);
      xfree(conn);
      break;
    case IO_CLOSE:
      r = libstream_script_call(conn->pe, conn->ref, event, addr, buf, len, handle);
      debug(DEBUG_INFO, "STREAM", "releasing resources on close");
      script_remove_ref(conn->pe, conn->ref);
      xfree(conn);
      break;
  }

  return r;
}

static int libstream_sock_server(int pe) {
  bt_provider_t *bt;
  script_ref_t ref;
  void *data;
  int r = -1;

  if (script_get_function(pe, 0, &ref) == 0) {
    if ((data = libstream_sock_getdata(pe, ref)) != NULL) {
      bt = script_get_pointer(pe, BT_PROVIDER);
      r = sock_stream_server(TAG_STREAM, pe, libstream_sock_callback, 1, data, bt);
    }
  }

  return r;
}

static int libstream_sock_close(int pe) {
  return sock_stream_close(TAG_STREAM, pe);
}

static int libstream_sock_open(int pe) {
  bt_provider_t *bt;
  script_ref_t ref;
  void *data;
  int r = -1;

  if (script_get_function(pe, 0, &ref) == 0) {
    if ((data = libstream_sock_getdata(pe, ref)) != NULL) {
      bt = script_get_pointer(pe, BT_PROVIDER);
      r = sock_client(TAG_STREAM, pe, libstream_sock_callback, 1, data, bt);
    }
  }

  return r;
}

static int libstream_sock_write(int pe) {
  return sock_write(TAG_STREAM, pe);
}

int libstream_init(int pe, script_ref_t obj) {
  script_add_iconst(pe, obj, "BIND",          IO_BIND);
  script_add_iconst(pe, obj, "BIND_ERROR",    IO_BIND_ERROR);
  script_add_iconst(pe, obj, "ACCEPT",        IO_ACCEPT);
  script_add_iconst(pe, obj, "CONNECT",       IO_CONNECT);
  script_add_iconst(pe, obj, "CONNECT_ERROR", IO_CONNECT_ERROR);
  script_add_iconst(pe, obj, "DISCONNECT",    IO_DISCONNECT);
  script_add_iconst(pe, obj, "DATA",          IO_DATA);
  script_add_iconst(pe, obj, "LINE",          IO_LINE);
  script_add_iconst(pe, obj, "TIMEOUT",       IO_TIMEOUT);
  script_add_iconst(pe, obj, "CLOSE",         IO_CLOSE);

  script_add_iconst(pe, obj, "RFCOMM",        BT_RFCOMM);
  script_add_iconst(pe, obj, "L2CAP",         BT_L2CAP);

  script_add_function(pe, obj, "server", libstream_sock_server);
  script_add_function(pe, obj, "open",   libstream_sock_open);
  script_add_function(pe, obj, "close",  libstream_sock_close);
  script_add_function(pe, obj, "write",  libstream_sock_write);

  return 0;
}
