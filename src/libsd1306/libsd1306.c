#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "script.h"
#include "ptr.h"
#include "pwindow.h"
#include "image.h"
#include "font.h"
#include "display.h"
#include "i2c.h"
#include "sd1306.h"
#include "debug.h"
#include "xalloc.h"

typedef struct {
  i2c_provider_t *i2cp;
  i2c_t *i2c;
} libsd1306_t;

static int display_enable(void *data, int enable) {
  libsd1306_t *sd1306;

  sd1306 = (libsd1306_t *)data;
  return enable ? sd1306_start(sd1306->i2cp, sd1306->i2c) : sd1306_stop(sd1306->i2cp, sd1306->i2c);
}

static int display_contrast(void *data, uint8_t contrast) {
  libsd1306_t *sd1306;

  sd1306 = (libsd1306_t *)data;
  return sd1306_contrast(sd1306->i2cp, sd1306->i2c, contrast);
}

static int display_printchar(void *data, int x, int y, uint8_t c, font_t *f, uint32_t fg, uint32_t bg) {
  libsd1306_t *sd1306;

  sd1306 = (libsd1306_t *)data;
  return sd1306_printchar(sd1306->i2cp, sd1306->i2c, x, y, c, f, fg, bg);
}

static int display_cls(void *data, uint32_t bg) {
  libsd1306_t *sd1306;

  sd1306 = (libsd1306_t *)data;
  return sd1306_cls(sd1306->i2cp, sd1306->i2c, bg);
}

static uint32_t display_rgb(void *data, int red, int green, int blue) {
  return (red || green || blue) ? 1 : 0;
}

static void display_destructor(void *p) {
  libdisplay_t *display;
  libsd1306_t *sd1306;

  display = (libdisplay_t *)p;
  if (display) {
    sd1306 = (libsd1306_t *)display->data;
    sd1306->i2cp->close(sd1306->i2c);
    xfree(sd1306);
    xfree(display);
  }
}

static int libdisplay_create(int pe) {
  i2c_provider_t *i2c;
  libdisplay_t *display;
  libsd1306_t *sd1306;
  script_int_t bus, addr;
  int ptr, r = -1;

  if ((i2c = script_get_pointer(pe, I2C_PROVIDER)) == NULL) {
    debug(DEBUG_ERROR, "SD1306", "i2c provider not found");
    return -1;
  }

  if (script_get_integer(pe, 0, &bus) == 0 &&
      script_get_integer(pe, 1, &addr) == 0) {

    if ((display = xcalloc(1, sizeof(libdisplay_t))) != NULL) {
      if ((sd1306 = xcalloc(1, sizeof(libsd1306_t))) != NULL) {
        if ((sd1306->i2c = i2c->open(bus, addr)) != NULL) {
          sd1306->i2cp = i2c;
          sd1306_display_size(&display->width, &display->height);
          display->data = sd1306;
          display->enable = display_enable;
          display->contrast = display_contrast;
          display->printchar = display_printchar;
          display->cls = display_cls;
          display->rgb = display_rgb;
          display->fg = display_rgb(sd1306, 255, 255, 255);
          display->bg = display_rgb(sd1306, 0, 0, 0);
          display->tag = TAG_DISPLAY;

          if ((ptr = ptr_new(display, display_destructor)) != -1) {
            r = script_push_integer(pe, ptr);
          } else {
            i2c->close(sd1306->i2c);
            xfree(sd1306);
            xfree(display);
          }
        } else {
          xfree(sd1306);
          xfree(display);
        }
      } else {
        xfree(display);
      }
    }
  }

  return r;
}

int libsd1306_init(int pe, script_ref_t obj) {
  int width, height;

  sd1306_display_size(&width, &height);

  script_add_iconst(pe, obj,   "width",  width);
  script_add_iconst(pe, obj,   "height", height);
  script_add_function(pe, obj, "create", libdisplay_create);

  return 0;
}
