#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "script.h"
#include "ptr.h"
#include "pwindow.h"
#include "image.h"
#include "pfont.h"
#include "display.h"
#include "i2c.h"
#include "lcdc.h"
#include "debug.h"
#include "xalloc.h"

#define CHAR_WIDTH  5
#define CHAR_HEIGHT 7

static font_t fixedfont = {
  CHAR_WIDTH, CHAR_HEIGHT,
  32, 127+8,
  NULL, NULL
};

typedef struct liblcd_t {
  i2c_provider_t *i2cp;
  i2c_t *i2c;
  int fd, rows, cols;
  int lcd_backlight_value;
} liblcd_t;

int lcd_defchar(int ptr, int location, unsigned char *charmap) {
  libdisplay_t *display;
  liblcd_t *liblcd;
  int r = -1;

  display = ptr_lock(ptr, TAG_DISPLAY);

  if (display) {
    liblcd = (liblcd_t *)display->data;
    if (liblcd) {
      lcdc_createchar(liblcd->i2cp, liblcd->i2c, location, charmap, liblcd->lcd_backlight_value);
      r = 0;
    }
    ptr_unlock(ptr, TAG_DISPLAY);
  }

  return r;
}

static int liblcd_defchar(int pe) {
  unsigned char charmap[8];
  script_int_t ptr, location, aux;
  int i, r = -1;

  if (script_get_integer(pe, 0, &ptr) == 0 &&
      script_get_integer(pe, 1, &location) == 0) {

    for (i = 0; i < 8; i++) {
      if (script_get_integer(pe, i + 2, &aux) == 0) {
        charmap[i] = aux;
      }
    }
    for (; i < 8; i++) {
      charmap[i] = 0;
    }

    r = lcd_defchar(ptr, location, charmap);
  }

  return script_push_boolean(pe, r == 0);
}

static int display_enable(void *data, int enable) {
  liblcd_t *liblcd;

  liblcd = (liblcd_t *)data;

  if (enable) {
    lcdc_init(liblcd->i2cp, liblcd->i2c, liblcd->cols, liblcd->rows);
    lcdc_setcursor(liblcd->i2cp, liblcd->i2c, 0, 0, liblcd->lcd_backlight_value);
  } else {
    lcdc_finish(liblcd->i2cp, liblcd->i2c);
  }

  return 0;
}

static int display_backlight(void *data, int backlight) {
  liblcd_t *liblcd;

  liblcd = (liblcd_t *)data;
  liblcd->lcd_backlight_value = backlight;

  return 0;
}

static int display_printchar(void *data, int x, int y, uint8_t c, font_t *f, uint32_t fg, uint32_t bg) {
  liblcd_t *liblcd;

  liblcd = (liblcd_t *)data;
  lcdc_setcursor(liblcd->i2cp, liblcd->i2c, x / CHAR_WIDTH, y / CHAR_HEIGHT, liblcd->lcd_backlight_value);
  if (c >= 128 && c < 128+8) {
    c -= 128; // user defined char
  }
  lcdc_write(liblcd->i2cp, liblcd->i2c, (char *)&c, 1, liblcd->lcd_backlight_value);
  return 0;
}

static int display_cls(void *data, uint32_t bg) {
  liblcd_t *liblcd;

  liblcd = (liblcd_t *)data;
  lcdc_clear(liblcd->i2cp, liblcd->i2c, liblcd->lcd_backlight_value);
  return 0;
}

static uint32_t display_rgb(void *data, int red, int green, int blue) {
  return (red || green || blue) ? 1 : 0;
}

static void display_destructor(void *p) {
  libdisplay_t *display;
  liblcd_t *liblcd;

  display = (libdisplay_t *)p;
  if (display) {
    liblcd = (liblcd_t *)display->data;
    liblcd->i2cp->close(liblcd->i2c);
    xfree(liblcd);
    xfree(display);
  }
}

static int liblcd_create(int pe) {
  i2c_provider_t *i2c;
  libdisplay_t *display;
  liblcd_t *liblcd;
  script_int_t bus, addr, cols, rows;
  int ptr, r = -1;

  if ((i2c = script_get_pointer(pe, I2C_PROVIDER)) == NULL) {
    debug(DEBUG_ERROR, "LCD", "i2c provider not found");
    return -1;
  }

  if (script_get_integer(pe, 0, &bus) != 0 ||
      script_get_integer(pe, 1, &addr) != 0 ||
      script_get_integer(pe, 2, &cols) != 0 ||
      script_get_integer(pe, 3, &rows) != 0) {
    return -1;
  }

  if ((display = xcalloc(1, sizeof(libdisplay_t))) != NULL) {
    if ((liblcd = xcalloc(1, sizeof(liblcd_t))) != NULL) {
      if ((liblcd->i2c = i2c->open(bus, addr)) != NULL) {
        liblcd->i2cp = i2c;
        liblcd->cols = cols;
        liblcd->rows = rows;
        liblcd->lcd_backlight_value = 1;
        display->width = cols * CHAR_WIDTH;
        display->height = rows * CHAR_HEIGHT;
        display->data = liblcd;
        display->enable = display_enable;
        display->contrast = NULL;
        display->backlight = display_backlight;
        display->printchar = display_printchar;
        display->cls = display_cls;
        display->line = NULL;
        display->draw = NULL;
        display->drawf = NULL;
        display->rgb = display_rgb;
        display->fg = display_rgb(liblcd, 255, 255, 255);
        display->bg = display_rgb(liblcd, 0, 0, 0);
        display->fixedfont = 1;
        display->f = &fixedfont;
        display->tag = TAG_DISPLAY;

        if ((ptr = ptr_new(display, display_destructor)) != -1) {
          r = script_push_integer(pe, ptr);
        } else {
          i2c->close(liblcd->i2c);
          xfree(liblcd);
          xfree(display);
        }
      } else {
        xfree(liblcd);
        xfree(display);
      }
    } else {
      xfree(display);
    }
  }

  return r;
}

int liblcd_init(int pe, script_ref_t obj) {
  script_add_function(pe, obj, "create",  liblcd_create);
  script_add_function(pe, obj, "defchar", liblcd_defchar);

  return 0;
}
