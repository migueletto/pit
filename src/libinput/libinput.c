#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include "script.h"
#include "thread.h"
#include "pwindow.h"
#include "input.h"
#include "bytes.h"
#include "sys.h"
#include "debug.h"
#include "xalloc.h"

#define TAG_INPUT "INPUT"

struct input_t {
  int fd, width, height;
  int pe;
  script_ref_t ref;
};

static input_provider_t provider;

static input_t *libinput_input_create(int num, int width, int height) {
  input_t *input;
  char device[32];

  if ((input = xcalloc(1, sizeof(input_t))) != NULL) {
    input->width = width;
    input->height = height;

    snprintf(device, sizeof(device)-1, "/dev/input/event%d", num);
    if ((input->fd = open(device, O_RDONLY)) == -1) {
      debug_errno("INPUT", "open device");
      xfree(input);
      input = NULL;
    } else {
      debug(DEBUG_INFO, "INPUT", "device %s open", device);
    }
  }

  return input;
}

// 2021-04-29 17:13:12.516 I 10910 PalmOS   INPUT: 0000: 28 E9 8A 60 3F C9 07 00 01 00 4A 01 01 00 00 00
// 2021-04-29 17:13:12.516 I 10910 PalmOS   INPUT: 0010: 28 E9 8A 60 3F C9 07 00 03 00 00 00 12 03 00 00
// 2021-04-29 17:13:12.516 I 10910 PalmOS   INPUT: 0020: 28 E9 8A 60 3F C9 07 00 03 00 01 00 2D 0D 00 00
// 2021-04-29 17:13:12.516 I 10910 PalmOS   INPUT: 0030: 28 E9 8A 60 3F C9 07 00 03 00 18 00 85 00 00 00
// 2021-04-29 17:13:12.516 I 10910 PalmOS   INPUT: 0040: 28 E9 8A 60 3F C9 07 00 00 00 00 00 00 00 00 00

static int libinput_input_event(input_t *input, int *x, int *y, uint32_t us) {
  uint8_t buf[24];
  uint32_t value;
  int32_t ivalue;
  uint16_t type, code;
  int i, len, nread, hast, hasx, hasy, down, ev = -1;

  if (input && input->fd > 0) {
    hast = hasx = hasy = down = 0;
    len = sizeof(struct timeval) + 8; // struct timeval can be 16 bytes or 24 bytes
    for (; ev == -1;) {
      switch (sys_read_timeout(input->fd, buf, len, &nread, us)) {
        case -1:
          return -1;
        case 0:
          ev =  0;
          break;
        default:
          debug(DEBUG_TRACE, "INPUT", "read %d bytes", nread);
          //debug_bytes(DEBUG_TRACE, "INPUT", buf, nread);
          if (nread == len) {
            i = len - 8; // ignore struct timeval
            i += get2l(&type, buf, i);
            i += get2l(&code, buf, i);
            i += get4l(&value, buf, i);
            debug(DEBUG_TRACE, "INPUT", "type %u code %u value %u", type, code, value);

            // types and codes defined in /usr/include/linux/input-event-codes.h
            switch (type) {
              case 0x00: // EV_SYN
                if (hast) {
                  ev = down ? WINDOW_BUTTONDOWN : WINDOW_BUTTONUP;
                  debug(DEBUG_TRACE, "INPUT", "event button %d", down);
                } else if (hasx || hasy) {
                  ev = WINDOW_MOTION;
                  debug(DEBUG_TRACE, "INPUT", "event move %d,%d", *x, *y);
                } else {
                  ev = 0;
                }
                break;
              case 0x01: // EV_KEY
                switch (code) {
                  case 0x110: // BTN_LEFT (for mouse)
                  case 0x14A: // BTN_TOUCH (for touch screen)
                    down = value ? 1 : 0;
                    hast = 1;
                    debug(DEBUG_TRACE, "INPUT", "BTN %s", down ? "down" : "up");
                    break;
                }
                break;
              case 0x02: // EV_REL
                ivalue = value;
                switch (code) {
                  case 0x00: // REL_X
                    *x += ivalue;
                    if (*x < 0) *x = 0;
                    else if (*x >= input->width) *x = input->width-1;
                    hasx = 1;
                    break;
                  case 0x01: // REL_Y
                    *y += ivalue;
                    if (*y < 0) *y = 0;
                    else if (*y >= input->height) *y = input->height-1;
                    hasy = 1;
                    break;
                }
                break;
              case 0x03: // EV_ABS
                switch (code) {
                  case 0x00: // ABS_X
                    if (value < 0x100) value = 0x100;
                    else if (value >= 0xF00) value = 0xEFF;
                    value = ((value - 0x100) * input->width) / 0xE00;
                    if (value >= input->width) value = input->width-1;
                    *x = value;
                    hasx = 1;
                    break;
                  case 0x01: // ABS_Y
                    if (value < 0x100) value = 0x100;
                    else if (value >= 0xF00) value = 0xEFF;
                    value = ((value - 0x100) * input->height) / 0xE00;
                    if (value >= input->height) value = input->height-1;
                    *y = value;
                    hasy = 1;
                    break;
                  case 0x18: // ABS_PRESSURE
                    break;
                }
                break;
            }
          }
          break;
      }
    }
  }

  return ev;
}

static int libinput_input_destroy(input_t *input) {
  int r = -1;

  if (input) {
    if (input->fd > 0) sys_close(input->fd);
    xfree(input);
    r = 0;
  }

  return r;
}

static int input_action(void *arg) {
  input_t *input;
  script_arg_t ret;
  unsigned char *buf;
  unsigned int n;
  int ev, x, y, r;

  input = (input_t *)arg;
  x = input->width / 2;
  y = input->height / 2;

  for (; !thread_must_end();) {
    if ((ev = libinput_input_event(input, &x, &y, 10000)) == -1) break;

    if (ev) {
      script_call(input->pe, input->ref, &ret, "III", mkint(ev), mkint(x), mkint(y));
    }
    
    if ((r = thread_server_read_timeout(0, &buf, &n)) == -1) break;
    if (buf) xfree(buf);
  }

  script_remove_ref(input->pe, input->ref);
  libinput_input_destroy(input);

  return 0;
}

int libinput_create(int pe) {
  script_int_t num, width, height;
  script_ref_t ref;
  input_t *input;
  int handle, r = -1;

  if (script_get_integer(pe, 0, &num) == 0 &&
      script_get_integer(pe, 1, &width) == 0 &&
      script_get_integer(pe, 2, &height) == 0 &&
      script_get_function(pe, 3, &ref) == 0) {

    if ((input = libinput_input_create(num, width, height)) != NULL) {
      input->pe = pe;
      input->ref = ref;

      if ((handle = thread_begin(TAG_INPUT, input_action, input)) != -1) {
        r = script_push_integer(pe, handle);
      } else {
        script_remove_ref(pe, ref);
        libinput_input_destroy(input);
      }
    } else {
      script_remove_ref(pe, ref);
    }
  }

  return r;
}

int libinput_destroy(int pe) {
  script_int_t handle;
  int r = -1;

  if (script_get_integer(pe, 0, &handle) == 0) {
    r = thread_end(TAG_INPUT, handle);
  }

  return r;
}

int libinput_load(void) {
  provider.create  = libinput_input_create;
  provider.event   = libinput_input_event;
  provider.destroy = libinput_input_destroy;

  return 0;
}

int libinput_init(int pe, script_ref_t obj) {
  debug(DEBUG_INFO, "INPUT", "registering provider %s", INPUT_PROVIDER);
  script_set_pointer(pe, INPUT_PROVIDER, &provider);

  script_add_function(pe, obj, "create",  libinput_create);
  script_add_function(pe, obj, "destroy", libinput_destroy);

  script_add_iconst(pe, obj, "motion", WINDOW_MOTION);
  script_add_iconst(pe, obj, "down", WINDOW_BUTTONDOWN);
  script_add_iconst(pe, obj, "up", WINDOW_BUTTONUP);

  return 0;
}
