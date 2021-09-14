#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#include "pfont.h"
#include "sd1306.h"
#include "debug.h"
#include "xalloc.h"

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

int sd1306_start(void *wire) {
  int i;

  for (i = 0; i < sizeof(init_seq); i++) {
    if (sd1306_command(wire, init_seq[i]) != 0) return -1;
  }

  return 0;
}

int sd1306_stop(void *wire) {
  return sd1306_command(wire, SD1306_DISPLAYOFF);
}

int sd1306_contrast(void *wire, uint8_t contrast) {
  if (sd1306_command(wire, SD1306_SETCONTRAST) != 0) return -1;
  return sd1306_command(wire, contrast);
}

int sd1306_xy(void *wire, uint8_t x, uint8_t y) {
  if (sd1306_command(wire, SD1306_SETPAGEADDR   | ((y >> 3) & 0x07)) != 0) return -1;  // page address
  if (sd1306_command(wire, SD1306_SETLOWCOLUMN  | (x & 0x0F)) != 0) return -1;         // lower column address
  if (sd1306_command(wire, SD1306_SETHIGHCOLUMN | (x >> 4)) != 0) return -1;           // higher column address
  return 0;
}

static int sd1306_char(void *wire, int x, int y, uint8_t c, int i, font_t *f, int fg, int bg) {
  uint32_t fw, j;
  uint8_t aux[32];
  uint8_t *d;

  if (sd1306_xy(wire, x, y) != 0) return -1;

  d = (i ? f->font1 : f->font0) + (c - f->min) * f->width;

  fw = f->width < 32 ? f->width : 32;
  if (fg && !bg) {
    xmemcpy(aux, d, fw);
  } else if (!fg && bg) {
    for (j = 0; j < fw; j++) aux[j] = d[j] ^ 0xff;
  } else if (fg && bg) {
    xmemset(aux, 0xff, fw);
  } else {
    xmemset(aux, 0x00, fw);
  }

  return sd1306_data(wire, aux, fw);
}

int sd1306_printchar(void *wire, int x, int y, uint8_t c, font_t *f, uint32_t fg, uint32_t bg) {
  if (f->height == 8) {
    return sd1306_char(wire, x, y, c, 1, f, fg, bg);
  }

  if (sd1306_char(wire, x, y, c, 0, f, fg, bg) != 0) return -1;
  if (sd1306_char(wire, x, y+8, c, 1, f, fg, bg) != 0) return -1;

  return 0;
}

int sd1306_cls(void *wire, int width, int height, uint32_t bg) {
  uint8_t aux[16];
  int i , j;

  bg = bg ? 0xff : 0x00;
  xmemset(aux, bg, sizeof(aux));

  for (i = 0; i < height/8; i++) {
    sd1306_xy(wire, 0, i*8);
    for (j = 0; j < width/16; j++) {
      sd1306_data(wire, aux, 16);
    }
  }

  return 0;
}

int sd1306_row(void *wire, int width, uint8_t *p) {
  int j;

  for (j = 0; j < width; j += 16) {
    sd1306_data(wire, &p[j], j+16 <= width ? 16 : width-j);
  }

  return 0;
}
