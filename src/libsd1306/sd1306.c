#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "script.h"
#include "io.h"
#include "i2c.h"
#include "font.h"
#include "sd1306.h"
#include "debug.h"

#define SD1306_SETLOWCOLUMN   0x00
#define SD1306_SETHIGHCOLUMN  0x10
#define SD1306_SETSTARTLINE   0x40
#define SD1306_SETCONTRAST    0x81
#define SD1306_SEGREMAP       0xA0
#define SD1306_DISPLAYALLOFF  0xA4
#define SD1306_DISPLAYALLON   0xA5
#define SD1306_NORMALDISPLAY  0xA6
#define SD1306_INVERTDISPLAY  0xA7
#define SD1306_DISPLAYOFF     0xAE
#define SD1306_DISPLAYON      0xAF
#define SD1306_SETPAGEADDR    0xB0
#define SD1306_COMSCANINC     0xC0
#define SD1306_COMSCANDEC     0xC8

#define SD1306_LCDWIDTH  128
#define SD1306_LCDHEIGHT 64

// a command word consists of a control byte, which defines Co and D/C, plus a data byte
// bit 7: Co  = 0: the last control byte, only data bytes to follow
// bit 7: Co  = 1: next two bytes are a data byte and another control byte
// bit 6: D/C = 0: the data byte is for command operation
// bit 6: D/C = 1: the data byte is for RAM operation

static uint8_t init_seq[] = {
  0xae,              // display off
  0xd5, 0x80,        // clock divide ratio (0x00=1) and oscillator frequency (0x8)
  0xa8, 0x3f,

  0xd3, 0x00,

  0x40,              // start line

  0x8d, 0x14,        // [2] charge pump setting (p62): 0x14 enable, 0x10 disable

  0x20, 0x00,
  0xa1,              // segment remap a0/a1
  0xc8,              // c0: scan dir normal, c8: reverse
  0xda, 0x12,        // com pin HW config, sequential com pin config (bit 4), disable left/right remap (bit 5)
  0x81, 0xcf,        // [2] set contrast control
  0xd9, 0xf1,        // [2] pre-charge period 0x22/f1
  0xdb, 0x40,        // vcomh deselect level

  0x2e,              // deactivate scroll
  0xa4,              // output ram to display
  0xa6,              // none inverted normal display mode
  0xaf               // display on
};

static int sd1306_command(i2c_provider_t *i2cp, i2c_t *i2c, uint8_t c) {
  uint8_t data[2];
  int r;

  data[0] = 0x00; // Co = 0, D/C = 0
  data[1] = c;

  r = i2cp->write(i2c, data, 2) == 2 ? 0 : -1;
  if (r == -1) {
    debug(DEBUG_ERROR, "SD1306", "write cmd failed");
  }

  return r;
}

static int sd1306_data(i2c_provider_t *i2cp, i2c_t *i2c, uint8_t *d, int len) {
  uint8_t data[32];
  int r;

  if (len < sizeof(data)-1) {
    data[0] = 0x40; // Co = 0, D/C = 1
    memcpy(data+1, d, len);

    r = i2cp->write(i2c, data, len+1) == len+1 ? 0 : -1;
    if (r == -1) {
      debug(DEBUG_ERROR, "SD1306", "write data failed");
    }

    return r;
  }

  return -1;
}

int sd1306_start(i2c_provider_t *i2cp, i2c_t *i2c) {
  int i;

  for (i = 0; i < sizeof(init_seq); i++) {
    if (sd1306_command(i2cp, i2c, init_seq[i]) != 0) return -1;
  }

  return 0;
}

int sd1306_stop(i2c_provider_t *i2cp, i2c_t *i2c) {
  return sd1306_command(i2cp, i2c, SD1306_DISPLAYOFF);
}

int sd1306_contrast(i2c_provider_t *i2cp, i2c_t *i2c, uint8_t contrast) {
  if (sd1306_command(i2cp, i2c, SD1306_SETCONTRAST) != 0) return -1;
  return sd1306_command(i2cp, i2c, contrast);
}

static int sd1306_xy(i2c_provider_t *i2cp, i2c_t *i2c, uint8_t x, uint8_t y) {
  if (sd1306_command(i2cp, i2c, SD1306_SETPAGEADDR   | ((y >> 3) & 0x07)) != 0) return -1;  // page address
  if (sd1306_command(i2cp, i2c, SD1306_SETLOWCOLUMN  | (x & 0x0F)) != 0) return -1;         // lower column address
  if (sd1306_command(i2cp, i2c, SD1306_SETHIGHCOLUMN | (x >> 4)) != 0) return -1;           // higher column address
  return 0;
}

static int sd1306_char(i2c_provider_t *i2cp, i2c_t *i2c, int x, int y, uint8_t c, int i, font_t *f, int fg, int bg) {
  uint8_t *d;

  if (sd1306_xy(i2cp, i2c, x, y) != 0) return -1;

  d = (i ? f->font1 : f->font0) + (c - f->min) * f->width;

  return sd1306_data(i2cp, i2c, d, f->width);
}

int sd1306_printchar(i2c_provider_t *i2cp, i2c_t *i2c, int x, int y, uint8_t c, font_t *f, int fg, int bg) {
  if (f->height == 8) {
    return sd1306_char(i2cp, i2c, x, y, c, 1, f, fg, bg);
  }

  if (sd1306_char(i2cp, i2c, x, y, c, 0, f, fg, bg) != 0) return -1;
  if (sd1306_char(i2cp, i2c, x, y+8, c, 1, f, fg, bg) != 0) return -1;

  return 0;
}

int sd1306_cls(i2c_provider_t *i2cp, i2c_t *i2c, int bg) {
  uint8_t aux[16];
  int i, n;

  sd1306_xy(i2cp, i2c, 0, 0);
  memset(aux, 0, sizeof(aux));
  n = (SD1306_LCDWIDTH * SD1306_LCDHEIGHT) / 8;

  for (i = 0; i < n; i += sizeof(aux)) {
    sd1306_data(i2cp, i2c, aux, sizeof(aux));
  }

  return 0;
}

void sd1306_display_size(int *width, int *height) {
  *width = SD1306_LCDWIDTH;
  *height = SD1306_LCDHEIGHT;
}
