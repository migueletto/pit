#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "script.h"
#include "pit_io.h"
#include "sys.h"
#include "ptr.h"
#include "thread.h"
#include "sock.h"
#include "pwindow.h"
#include "image.h"
#include "pfont.h"
#include "display.h"
#include "debug.h"
#include "xalloc.h"

#define TAG_SD1306A  "SD1306A"

#define CMD_ENABLE  0x01
#define CMD_DISABLE 0x02
#define CMD_CLS     0x03
#define CMD_STR     0x04
#define CMD_END     0x05

#define MAX_STR     64

#define DISP_WIDTH  128
#define DISP_HEIGHT 64

#define CHAR_WIDTH  16
#define CHAR_HEIGHT 16

#define SD1306A_CONNECTED    1
#define SD1306A_DISCONNECTED 2
#define SD1306A_BUTTON       3

typedef struct {
  int handle;
} libsd1306a_display_t;

typedef struct {
  int pe;
  script_ref_t ref;
} libsd1306a_sock_t;

static font_t fixedfont = {
  CHAR_WIDTH, CHAR_HEIGHT,
  32, 127,
  NULL, NULL
};

static int libsd1306a_sock_callback(int event, io_addr_t *addr, unsigned char *buf, int len, int fd, int handle, void **_data) {
  libsd1306a_sock_t *data;
  script_arg_t ret;
  uint8_t b[2];
  int i;

  data = (libsd1306a_sock_t *)(*_data);

  switch (event) {
    case IO_CONNECT:
      debug(DEBUG_INFO, "SD1306A", "connected");
      script_call(data->pe, data->ref, &ret, "I", SD1306A_CONNECTED);
      break;
    case IO_DATA:
      for (i = 0; i < len; i++) {
        script_call(data->pe, data->ref, &ret, "II", SD1306A_BUTTON, buf[i]);
      }
      break;
    case IO_CMD:
      // let io send data to fd
      break;
    case IO_TIMEOUT:
      // force disconnect
      return 1;
    case IO_DISCONNECT:
      debug(DEBUG_INFO, "SD1306A", "disconnected");
      b[0] = CMD_CLS;
      b[1] = CMD_DISABLE;
      sys_write(fd, b, 2);
      script_call(data->pe, data->ref, &ret, "I", SD1306A_DISCONNECTED);
      script_remove_ref(data->pe, data->ref);
      xfree(data);
  }

  return 0;
}

static int display_enable(void *_data, int enable) {
  libsd1306a_display_t *display_data;
  unsigned char b;

  display_data = (libsd1306a_display_t *)_data;
  b = enable ? CMD_ENABLE : CMD_DISABLE;

  return io_write_handle(TAG_SD1306A, display_data->handle, &b, 1);
}

static int display_printstr(void *_data, int x, int y, char *s, font_t *f, uint32_t fg, uint32_t bg) {
  libsd1306a_display_t *display_data;
  unsigned char b[MAX_STR];
  int i, j;

  display_data = (libsd1306a_display_t *)_data;
  i = 0;
  b[i++] = CMD_STR;
  b[i++] = (x & 0x7F) | 0x80;
  b[i++] = (y & 0x3F) | 0x80;

  for (j = 0; i < MAX_STR-1 && s[j]; j++) {
    b[i++] = s[j] < 32 || s[j] > 127 ? 32 : s[j];
  }
  b[i++] = CMD_END;

  return io_write_handle(TAG_SD1306A, display_data->handle, b, i);
}

static int display_cls(void *_data, uint32_t bg) {
  libsd1306a_display_t *display_data;
  unsigned char b;

  display_data = (libsd1306a_display_t *)_data;
  b = CMD_CLS;

  return io_write_handle(TAG_SD1306A, display_data->handle, &b, 1);
}

static void display_destructor(void *p) {
  libdisplay_t *display;
  libsd1306a_display_t *display_data;

  display = (libdisplay_t *)p;
  if (display) {
    display_data = (libsd1306a_display_t *)display->data;
    if (display_data) {
      if (display_data->handle > 0) io_stream_close(TAG_SD1306A, display_data->handle);
      xfree(display_data);
    }
    xfree(display);
  }
}

static void display_destructorh(void *p) {
  libdisplay_t *display;
  libsd1306a_display_t *display_data;

  display = (libdisplay_t *)p;
  if (display) {
    display_data = (libsd1306a_display_t *)display->data;
    if (display_data) {
      xfree(display_data);
    }
    xfree(display);
  }
}

static int libsd1306a_sock_client(int pe, io_callback_f cb, int index, void *data) {
  script_int_t port, timeout;
  bt_provider_t *bt;
  char *peer = NULL;
  io_addr_t addr;
  int handle = -1;

  memset(&addr, 0, sizeof(addr));

  if (script_get_string(pe, index, &peer) == 0 &&
      script_get_integer(pe, index+1, &port) == 0 &&
      script_get_integer(pe, index+2, &timeout) == 0 &&
      io_fill_addr(peer, port, &addr) == 0) {

    bt = script_get_pointer(pe, BT_PROVIDER);
    handle =  io_stream_client(TAG_SD1306A, &addr, cb, NULL, data, timeout, bt);
  }

  if (peer) xfree(peer);

  return handle;
}

static int libsd1306a_create(int pe) {
  script_ref_t ref;
  libsd1306a_sock_t *sock_data;
  libsd1306a_display_t *display_data;
  libdisplay_t *display;
  int ptr, r = -1;

  if (script_get_function(pe, 0, &ref) == 0) {
    if ((sock_data = xcalloc(1, sizeof(libsd1306a_sock_t))) != NULL) {
      sock_data->pe = pe;
      sock_data->ref = ref;

      if ((display_data = xcalloc(1, sizeof(libsd1306a_display_t))) != NULL) {
        if ((display = xcalloc(1, sizeof(libdisplay_t))) != NULL) {
          display->data = display_data;
          display->enable = display_enable;
          display->printstr = display_printstr;
          display->cls = display_cls;
          display->width = DISP_WIDTH;
          display->height = DISP_HEIGHT;
          display->fixedfont = 1;
          display->f = &fixedfont;
          display->tag = TAG_DISPLAY;

          if ((ptr = ptr_new(display, display_destructor)) != -1) {
            if ((display_data->handle = libsd1306a_sock_client(pe, libsd1306a_sock_callback, 1, sock_data)) != -1) {
              r = script_push_integer(pe, ptr);
            } else {
              ptr_free(ptr, TAG_DISPLAY);
              xfree(display);
              xfree(display_data);
              xfree(sock_data);
            }
          } else {
            xfree(display);
            xfree(display_data);
            xfree(sock_data);
          }
        } else {
          xfree(display_data);
          xfree(sock_data);
        }
      } else {
        xfree(sock_data);
      }
    }
  }

  return r;
}

static int libsd1306a_set(int pe) {
  libsd1306a_display_t *display_data;
  libdisplay_t *display;
  script_int_t handle;
  int ptr, r = -1;

  if (script_get_integer(pe, 0, &handle) == 0) {
    if ((display_data = xcalloc(1, sizeof(libsd1306a_display_t))) != NULL) {
      if ((display = xcalloc(1, sizeof(libdisplay_t))) != NULL) {
        display_data->handle = handle;
        display->data = display_data;
        display->enable = display_enable;
        display->printstr = display_printstr;
        display->cls = display_cls;
        display->width = DISP_WIDTH;
        display->height = DISP_HEIGHT;
        display->fixedfont = 1;
        display->f = &fixedfont;
        display->tag = TAG_DISPLAY;

        if ((ptr = ptr_new(display, display_destructorh)) != -1) {
          r = script_push_integer(pe, ptr);
        } else {
          xfree(display);
          xfree(display_data);
        }
      } else {
        xfree(display_data);
      }
    }
  }

  return r;
}

int libsd1306a_init(int pe, script_ref_t obj) {
  script_add_function(pe, obj, "create", libsd1306a_create);
  script_add_function(pe, obj, "set",    libsd1306a_set);

  script_add_iconst(pe, obj, "CONNECTED",    SD1306A_CONNECTED);
  script_add_iconst(pe, obj, "DISCONNECTED", SD1306A_DISCONNECTED);
  script_add_iconst(pe, obj, "BUTTON",       SD1306A_BUTTON);

  return 0;
}
