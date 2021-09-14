#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "script.h"
#include "ptr.h"
#include "media.h"
#include "pwindow.h"
#include "image.h"
#include "pfont.h"
#include "display.h"
#include "i2c.h"
#include "gpio.h"
#include "spi.h"
#include "sd1306.h"
#include "debug.h"
#include "xalloc.h"

typedef struct {
  int width, height;
  i2c_provider_t *i2cp;
  i2c_t *i2c;
  gpio_provider_t *gpio;
  spi_provider_t *spip;
  spi_t *spi;
  int dc, rst;
  uint8_t buf[1024];
} libsd1306_t;

// a command word consists of a control byte, which defines Co and D/C, plus a data byte
// bit 7: Co  = 0: the last control byte, only data bytes to follow
// bit 7: Co  = 1: next two bytes are a data byte and another control byte
// bit 6: D/C = 0: the data byte is for command operation
// bit 6: D/C = 1: the data byte is for RAM operation

static int sd1306_i2c_command(libsd1306_t *sd1306, uint8_t c) {
  uint8_t data[2];
  int r;

  data[0] = 0x00; // Co = 0, D/C = 0
  data[1] = c;

  r = sd1306->i2cp->write(sd1306->i2c, data, 2) == 2 ? 0 : -1;
  if (r == -1) {
    debug(DEBUG_ERROR, "SD1306", "i2c write cmd failed");
  }

  return r;
}

static int sd1306_spi_command(libsd1306_t *sd1306, uint8_t c) {
  uint8_t r;

  sd1306->gpio->output(sd1306->dc, 0);
  sd1306->spip->transfer(sd1306->spi, &c, &r, 1);

  return 0;
}

int sd1306_command(void *wire, uint8_t c) {
  libsd1306_t *sd1306;

  sd1306 = (libsd1306_t *)wire;
  if (sd1306->i2cp) {
    return sd1306_i2c_command(sd1306, c);
  }
  if (sd1306->spip) {
    return sd1306_spi_command(sd1306, c);
  }

  return -1;
}

int sd1306_i2c_data(libsd1306_t *sd1306, uint8_t *d, int len) {
  uint8_t data[32];
  int r;

  if (len < sizeof(data)-1) {
    data[0] = 0x40; // Co = 0, D/C = 1
    memcpy(data+1, d, len);

    r = sd1306->i2cp->write(sd1306->i2c, data, len+1) == len+1 ? 0 : -1;
    if (r == -1) {
      debug(DEBUG_ERROR, "SD1306", "i2c write data failed");
    }

    return r;
  }

  return -1;
}

int sd1306_spi_data(libsd1306_t *sd1306, uint8_t *d, int n) {
  uint8_t r[256];

  sd1306->gpio->output(sd1306->dc, 1);
  sd1306->spip->transfer(sd1306->spi, d, r, n);

  return 0;
}

int sd1306_data(void *wire, uint8_t *d, int len) {
  libsd1306_t *sd1306;

  sd1306 = (libsd1306_t *)wire;
  if (sd1306->i2cp) {
    return sd1306_i2c_data(sd1306, d, len);
  }
  if (sd1306->spip) {
    return sd1306_spi_data(sd1306, d, len);
  }

  return -1;
}

static int sd1306_enable(void *data, int enable) {
  return enable ? sd1306_start(data) : sd1306_stop(data);
}

static uint32_t sd1306_rgb(void *data, int red, int green, int blue) {
  return (red || green || blue) ? 1 : 0;
}

static int display_drawf(void *wire, int x, int y, int encoding, int width, int height, unsigned char *frame) {
  libsd1306_t *sd1306;
  int i, j, k, r = -1;

  sd1306 = (libsd1306_t *)wire;

  if (encoding == ENC_RGBA && sd1306->width == width && sd1306->height == height && x == 0 && y == 0) {
    for (i = 0, k = 0; i < 64; i += 8, k += 128) {
      for (j = 0; j < 128; j++) {
        if (sd1306->buf[k + j] != frame[k + j]) {
          xmemcpy(&sd1306->buf[k], &frame[k], 128);
          sd1306_xy(wire, j, i);
          sd1306_row(wire, 128 - j, &sd1306->buf[k + j]);
          break;
        }
      }
    }
    r = 0;

  } else {
    debug(DEBUG_ERROR, "SD1306", "invalid encoding, x, y, width or height");
  }

  return r;
}

static int display_cls(void *wire, uint32_t bg) {
  libsd1306_t *sd1306;

  sd1306 = (libsd1306_t *)wire;
  return sd1306_cls(wire, sd1306->width, sd1306->height, bg);
}

static void display_destructor(void *p) {
  libdisplay_t *display;
  libsd1306_t *sd1306;

  display = (libdisplay_t *)p;
  if (display) {
    sd1306 = (libsd1306_t *)display->data;
    if (sd1306->i2cp) {
      sd1306->i2cp->close(sd1306->i2c);
    }
    if (sd1306->spip) {
      sd1306->spip->close(sd1306->spi);
    }
    xfree(sd1306);
    xfree(display);
  }
}

static int libsd1306_i2c_create(int pe) {
  i2c_provider_t *i2c;
  libdisplay_t *display;
  libsd1306_t *sd1306;
  script_int_t bus, addr, width, height;
  int ptr, r = -1;

  if ((i2c = script_get_pointer(pe, I2C_PROVIDER)) == NULL) {
    debug(DEBUG_ERROR, "SD1306", "i2c provider not found");
    return -1;
  }

  if (script_get_integer(pe, 0, &bus) == 0 &&
      script_get_integer(pe, 1, &addr) == 0 &&
      script_get_integer(pe, 2, &width) == 0 &&
      script_get_integer(pe, 3, &height) == 0) {

    if ((display = xcalloc(1, sizeof(libdisplay_t))) != NULL) {
      if ((sd1306 = xcalloc(1, sizeof(libsd1306_t))) != NULL) {
        if ((sd1306->i2c = i2c->open(bus, addr)) != NULL) {
          sd1306->i2cp = i2c;
          sd1306->width = width;
          sd1306->height = height;
          display->width = width;
          display->height = height;
          display->data = sd1306;
          display->enable = sd1306_enable;
          display->contrast = sd1306_contrast;
          display->printchar = sd1306_printchar;
          display->drawf = display_drawf;
          display->cls = display_cls;
          display->rgb = sd1306_rgb;
          display->fg = sd1306_rgb(sd1306, 255, 255, 255);
          display->bg = sd1306_rgb(sd1306, 0, 0, 0);
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

static int libsd1306_spi_create(int pe) {
  gpio_provider_t *gpio;
  spi_provider_t *spi;
  libdisplay_t *display;
  libsd1306_t *sd1306;
  script_int_t cs, dc, rst, speed, width, height;
  int ptr, r = -1;

  if ((gpio = script_get_pointer(pe, GPIO_PROVIDER)) == NULL) {
    debug(DEBUG_ERROR, "SD1306", "gpio provider not found");
    return -1;
  }

  if ((spi = script_get_pointer(pe, SPI_PROVIDER)) == NULL) {
    debug(DEBUG_ERROR, "SD1306", "spi provider not found");
    return -1;
  }

  if (script_get_integer(pe, 0, &cs) == 0 &&
      script_get_integer(pe, 1, &dc) == 0 &&
      script_get_integer(pe, 2, &rst) == 0 &&
      script_get_integer(pe, 3, &speed) == 0 &&
      script_get_integer(pe, 4, &width) == 0 &&
      script_get_integer(pe, 5, &height) == 0) {

    if ((display = xcalloc(1, sizeof(libdisplay_t))) != NULL) {
      if ((sd1306 = xcalloc(1, sizeof(libsd1306_t))) != NULL) {
        if ((sd1306->spi = spi->open(cs, speed)) != NULL) {
          sd1306->gpio = gpio;
          sd1306->spip = spi;
          sd1306->dc = dc;
          sd1306->rst = rst;
          sd1306->width = width;
          sd1306->height = height;
          display->width = width;
          display->depth = 1;
          display->height = height;
          display->data = sd1306;
          display->enable = sd1306_enable;
          display->contrast = sd1306_contrast;
          display->printchar = sd1306_printchar;
          display->drawf = display_drawf;
          display->cls = display_cls;
          display->rgb = sd1306_rgb;
          display->fg = sd1306_rgb(sd1306, 255, 255, 255);
          display->bg = sd1306_rgb(sd1306, 0, 0, 0);
          display->tag = TAG_DISPLAY;

          if ((ptr = ptr_new(display, display_destructor)) != -1) {
            r = script_push_integer(pe, ptr);
          } else {
            spi->close(sd1306->spi);
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
  script_add_function(pe, obj, "i2c", libsd1306_i2c_create);
  script_add_function(pe, obj, "spi", libsd1306_spi_create);

  return 0;
}
