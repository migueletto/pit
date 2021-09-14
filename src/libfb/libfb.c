#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "script.h"
#include "ptr.h"
#include "media.h"
#include "pwindow.h"
#include "image.h"
#include "font.h"
#include "display.h"
#include "sys.h"
#include "debug.h"
#include "xalloc.h"

typedef struct {
  int width, height, depth;
  int fd, len;
  uint16_t *p16;
  uint16_t *b16;
  uint32_t *p32;
  uint32_t *b32;
  void *p, *buf;
} libfb_t;

static int fb_setpixel16(libfb_t *fb, uint16_t c, int x, int y) {
  fb->p16[y * fb->width + x] = c;
  return 0;
}

static int fb_setpixel32(libfb_t *fb, uint32_t c, int x, int y) {
  fb->p32[y * fb->width + x] = c;
  return 0;
}
static int fb_setpixel(libfb_t *fb, uint32_t c, int x, int y) {
  int r = -1;

  switch (fb->depth) {
    case 16:
      r = fb_setpixel16(fb, c, x, y);
      break;
    case 32:
      r = fb_setpixel32(fb, c, x, y);
      break;
  }

  return r;
}

static int fb_cls16(libfb_t *fb, uint16_t bg, int x, int y, int width, int height) {
  int i, j, k;

  for (i = 0, k = y * fb->width + x; i < height; i++, k += fb->width) {
    for (j = 0; j < width; j++) {
      fb->p16[k+j] = bg;
    }
  }

  return 0;
}

static int fb_cls32(libfb_t *fb, uint32_t bg, int x, int y, int width, int height) {
  int i, j, k;

  for (i = 0, k = y * fb->width + x; i < height; i++, k += fb->width) {
    for (j = 0; j < width; j++) {
      fb->p32[k+j] = bg;
    }
  }

  return 0;
}

static int fb_cls(libfb_t *fb, uint32_t bg, int x, int y, int width, int height) {
  int r = -1;

  switch (fb->depth) {
    case 16:
      r = fb_cls16(fb, bg, x, y, width, height);
      break;
    case 32:
      r = fb_cls32(fb, bg, x, y, width, height);
      break;
  }

  return r;
}

static int fb_draw16(libfb_t *fb, uint16_t *pic, int x, int y, int width, int height) {
  int i, j, k, l;
  int64_t t;

  t = sys_get_clock();

  for (i = 0, k = y * fb->width + x, l = 0; i < height; i++, k += fb->width) {
    for (j = 0; j < width; j++) {
      fb->p16[k+j] = pic[l++];
    }
  }

  t = sys_get_clock() - t;
  debug(DEBUG_TRACE, "FB", "draw %dx%d @ %d,%d: %ld us", width, height, x, y, t);

  return 0;
}

static int fb_draw32(libfb_t *fb, uint32_t *pic, int x, int y, int width, int height) {
  int i, j, k, l;
  int64_t t;

  t = sys_get_clock();

  for (i = 0, k = y * fb->width + x, l = 0; i < height; i++, k += fb->width) {
    for (j = 0; j < width; j++) {
      fb->p32[k+j] = pic[l++];
    }
  }

  t = sys_get_clock() - t;
  debug(DEBUG_TRACE, "FB", "draw %dx%d @ %d,%d: %ld us", width, height, x, y, t);

  return 0;
}

static int fb_char16(libfb_t *fb, int x, int y, uint8_t c, int i, font_t *f, uint16_t fg, uint16_t bg) {
  uint16_t data[256];
  uint8_t *d, b;
  int j, k;

  d = (i ? f->font1 : f->font0) + (c - f->min) * f->width;
  k = 0;

  for (j = 0; j < f->width && k < 256; j++) {
    b = d[j];
    data[k++] = (b & 0x01) ? fg : bg;
  }
  for (j = 0; j < f->width && k < 256; j++) {
    b = d[j];
    data[k++] = (b & 0x02) ? fg : bg;
  }
  for (j = 0; j < f->width && k < 256; j++) {
    b = d[j];
    data[k++] = (b & 0x04) ? fg : bg;
  }
  for (j = 0; j < f->width && k < 256; j++) {
    b = d[j];
    data[k++] = (b & 0x08) ? fg : bg;
  }
  for (j = 0; j < f->width && k < 256; j++) {
    b = d[j];
    data[k++] = (b & 0x10) ? fg : bg;
  }
  for (j = 0; j < f->width && k < 256; j++) {
    b = d[j];
    data[k++] = (b & 0x20) ? fg : bg;
  }
  for (j = 0; j < f->width && k < 256; j++) {
    b = d[j];
    data[k++] = (b & 0x40) ? fg : bg;
  }
  for (j = 0; j < f->width && k < 256; j++) {
    b = d[j];
    data[k++] = (b & 0x80) ? fg : bg;
  }

  return fb_draw16(fb, data, x, y, f->width, 8);
}

static int fb_char32(libfb_t *fb, int x, int y, uint8_t c, int i, font_t *f, uint32_t fg, uint32_t bg) {
  uint32_t data[256];
  uint8_t *d, b;
  int j, k;

  d = (i ? f->font1 : f->font0) + (c - f->min) * f->width;
  k = 0;

  for (j = 0; j < f->width && k < 256; j++) {
    b = d[j];
    data[k++] = (b & 0x01) ? fg : bg;
  }
  for (j = 0; j < f->width && k < 256; j++) {
    b = d[j];
    data[k++] = (b & 0x02) ? fg : bg;
  }
  for (j = 0; j < f->width && k < 256; j++) {
    b = d[j];
    data[k++] = (b & 0x04) ? fg : bg;
  }
  for (j = 0; j < f->width && k < 256; j++) {
    b = d[j];
    data[k++] = (b & 0x08) ? fg : bg;
  }
  for (j = 0; j < f->width && k < 256; j++) {
    b = d[j];
    data[k++] = (b & 0x10) ? fg : bg;
  }
  for (j = 0; j < f->width && k < 256; j++) {
    b = d[j];
    data[k++] = (b & 0x20) ? fg : bg;
  }
  for (j = 0; j < f->width && k < 256; j++) {
    b = d[j];
    data[k++] = (b & 0x40) ? fg : bg;
  }
  for (j = 0; j < f->width && k < 256; j++) {
    b = d[j];
    data[k++] = (b & 0x80) ? fg : bg;
  }

  return fb_draw32(fb, data, x, y, f->width, 8);
}

static int fb_char(libfb_t *fb, int x, int y, uint8_t c, int i, font_t *f, uint32_t fg, uint32_t bg) {
  int r = -1;

  switch (fb->depth) {
    case 16:
      r = fb_char16(fb, x, y, c, i, f, fg, bg);
      break;
    case 32:
      r = fb_char32(fb, x, y, c, i, f, fg, bg);
      break;
  }

  return r;
}

static int display_printchar(void *data, int x, int y, uint8_t c, font_t *f, uint32_t fg, uint32_t bg) {
  libfb_t *fb;

  fb = (libfb_t *)data;

  if (f->height == 8) {
    return fb_char(fb, x, y, c, 1, f, fg, bg);
  }

  if (fb_char(fb, x, y,   c, 0, f, fg, bg) != 0) return -1;
  if (fb_char(fb, x, y+8, c, 1, f, fg, bg) != 0) return -1;

  return 0;
}

static int display_cls(void *data, uint32_t bg) {
  libfb_t *fb;

  fb = (libfb_t *)data;

  return fb_cls(fb, bg, 0, 0, fb->width, fb->height);
}

static int draw_line(libfb_t *fb, int x1, int y1, int x2, int y2, uint32_t fg) {
  int dx, dy, sx, sy, err, e2;

  dx = x2 - x1;
  if (dx < 0) dx = -dx;
  sx = x1 < x2 ? 1 : -1;
  dy = y2 - y1;
  if (dy > 0) dy = -dy;
  sy = y1 < y2 ? 1 : -1;
  err = dx + dy;

  for (;;) {
    fb_setpixel(fb, fg, x1, y1);
    if (x1 == x2 && y1 == y2) break;

    e2 = 2*err;
    if (e2 >= dy) {
      err += dy;
      x1 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y1 += sy;
    }
  }

  return 0;
}

static int display_line(void *data, int x1, int y1, int x2, int y2, uint32_t fg, int style) {
  libfb_t *fb;
  int aux, r = -1;

  fb = (libfb_t *)data;

  if (fb) {
    switch (style) {
      case DISPLAY_LINE_LINE:
        if (y1 == y2) {
          if (x1 > x2) { aux = x1; x1 = x2; x2 = aux; }
          r = fb_cls(fb, fg, x1, y1, x2-x1+1, 1);
        } else if (x1 == x2) {
          if (y1 > y2) { aux = y1; y1 = y2; y2 = aux; }
          r = fb_cls(fb, fg, x1, y1, 1, y2-y1+1);
        } else {
          r = draw_line(fb, x1, y1, x2, y2, fg);
        }
        break;
      case DISPLAY_LINE_RECT:
        if (x1 > x2) { aux = x1; x1 = x2; x2 = aux; }
        if (y1 > y2) { aux = y1; y1 = y2; y2 = aux; }
        r  = fb_cls(fb, fg, x1, y1, x2-x1+1, 1);
        r += fb_cls(fb, fg, x1, y2, x2-x1+1, 1);
        r += fb_cls(fb, fg, x1, y1, 1, y2-y1+1);
        r += fb_cls(fb, fg, x2, y1, 1, y2-y1+1);
        break;
      case DISPLAY_LINE_FILLED:
        if (x1 > x2) { aux = x1; x1 = x2; x2 = aux; }
        if (y1 > y2) { aux = y1; y1 = y2; y2 = aux; }
        r = fb_cls(fb, fg, x1, y1, x2-x1+1, y2-y1+1);
        break;
    }
  }

  return r;
}

static int display_ellipse(void *data, int x, int y, int rx, int ry, uint32_t fg, int style) {
  libfb_t *fb;
  double d;
  int i, k, r = -1;

  fb = (libfb_t *)data;

  if (fb) {
    switch (style) {
      case DISPLAY_ELLIPSE_LINE:
        for (i = 0; i < ry; i++) {
          d = (double)i / (double)ry;
          d = sqrt(1.0 - d * d) * (double)rx;
          k = (int)d;
          fb_setpixel(fb, fg, x+k, y+i);
          fb_setpixel(fb, fg, x-k, y+i);
          fb_setpixel(fb, fg, x+k, y-i);
          fb_setpixel(fb, fg, x-k, y-i);
        }
        for (i = 0; i < rx; i++) {
          d = (double)i / (double)rx;
          d = sqrt(1.0 - d * d) * (double)ry;
          k = (int)d;
          fb_setpixel(fb, fg, x+i, y+k);
          fb_setpixel(fb, fg, x+i, y-k);
          fb_setpixel(fb, fg, x-i, y+k);
          fb_setpixel(fb, fg, x-i, y-k);
        }
        break;
      case DISPLAY_ELLIPSE_FILLED:
        for (i = 0; i < ry; i++) {
          d = (double)i / (double)ry;
          d = sqrt(1.0 - d * d) * (double)rx;
          k = (int)d;
          display_line(data, x, y+i, x+k, y+i, fg, DISPLAY_LINE_LINE);
          display_line(data, x, y+i, x-k, y+i, fg, DISPLAY_LINE_LINE);
          display_line(data, x, y-i, x+k, y-i, fg, DISPLAY_LINE_LINE);
          display_line(data, x, y-i, x-k, y-i, fg, DISPLAY_LINE_LINE);
        }
        break;
    }
  }

  return r;
}

static uint32_t display_rgb(void *data, int red, int green, int blue) {
  libfb_t *fb;
  uint32_t c = 0;

  fb = (libfb_t *)data;
  red &= 0xFF;
  green &= 0xFF;
  blue &= 0xFF;

  switch (fb->depth) {
    case 16:
      c = red >> 3;
      c <<= 6;
      c |= green >> 2;
      c <<= 5;
      c |= blue >> 3;
      break;
    case 32:
      c = 0xFF000000 | (red << 16) | (green << 8) | blue;
      break;
  }

  return c;
}

static int display_draw(void *data, int x, int y, image_provider_t *image, image_t *img) {
  libfb_t *fb;
  int width, height;
  int r = -1;

  fb = (libfb_t *)data;
  image->size(img, &width, &height);

  switch (fb->depth) {
    case 16:
      image->extract_rgb565(img, (uint8_t *)fb->b16);
      r = fb_draw16(fb, fb->b16, x, y, width, height);
      break;
    case 32:
      image->extract_rgba(img, (uint8_t *)fb->b32);
      r = fb_draw32(fb, fb->b32, x, y, width, height);
      break;
  }

  return r;
}

static int display_drawf(void *data, int x, int y, int encoding, int width, int height, unsigned char *frame) {
  libfb_t *fb;
  int r = -1;

  fb = (libfb_t *)data;

  switch (fb->depth) {
    case 16:
      if (encoding == ENC_RGB565) {
        r = fb_draw16(fb, (uint16_t *)frame, x, y, width, height);
      } else {
        debug(DEBUG_ERROR, "FB", "invalid encoding %s for 16 bpp", video_encoding_name(encoding));
      }
      break;
    case 32:
      if (encoding == ENC_RGBA) {
        r = fb_draw32(fb, (uint32_t *)frame, x, y, width, height);
      } else {
        debug(DEBUG_ERROR, "FB", "invalid encoding %s for 32 bpp", video_encoding_name(encoding));
      }
      break;
  }

  return r;
}

static void display_destructor(void *p) {
  libdisplay_t *display;
  libfb_t *fb;
  uint16_t bg;

  display = (libdisplay_t *)p;
  if (display) {
    fb = (libfb_t *)display->data;
    bg = display_rgb(fb, 0, 0, 0);
    fb_cls(fb, bg, 0, 0, fb->width, fb->height);
    munmap(fb->p, fb->len);
    close(fb->fd);
    xfree(fb->buf);
    xfree(fb);
    xfree(display);
  }
}

static int libdisplay_create(int pe) {
  struct fb_var_screeninfo vinfo;
  struct fb_fix_screeninfo finfo;
  script_int_t num;
  libdisplay_t *display;
  libfb_t *fb;
  void *p;
  char device[16];
  int fd, ptr, r = -1;

  if (script_get_integer(pe, 0, &num) == 0) {
    snprintf(device, sizeof(device)-1, "/dev/fb%d", num);

    if ((fd = open(device, O_RDWR)) != -1) {
      debug(DEBUG_INFO, "FB", "framebuffer %s open", device);

      if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) != -1 &&
          ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) != -1) {
        debug(DEBUG_INFO, "FB", "framebuffer is %dx%d (%d bpp)", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);

        if ((p = (uint16_t *)mmap(0, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) != NULL) {
          if ((display = xcalloc(1, sizeof(libdisplay_t))) != NULL) {
            if ((fb = xcalloc(1, sizeof(libfb_t))) != NULL) {
              fb->fd = fd;
              fb->p = p;
              fb->width = vinfo.xres;
              fb->height = vinfo.yres;
              fb->depth = vinfo.bits_per_pixel;
              fb->len = finfo.smem_len;
              fb->buf = xcalloc(1, fb->len);
              if (fb->depth == 16) {
                fb->p16 = p;
                fb->b16 = fb->buf;
              } else {
                fb->p32 = p;
                fb->b32 = fb->buf;
              }
              display->width = fb->width;
              display->height = fb->height;
              display->depth = fb->depth;
              display->data = fb;
              display->printchar = display_printchar;
              display->cls = display_cls;
              display->line = display_line;
              display->ellipse = display_ellipse;
              display->draw = display_draw;
              display->drawf = display_drawf;
              display->rgb = display_rgb;
              display->fg = display_rgb(fb, 255, 255, 255);
              display->bg = display_rgb(fb, 0, 0, 0);
              display->tag = TAG_DISPLAY;

              if ((ptr = ptr_new(display, display_destructor)) != -1) {
                r = script_push_integer(pe, ptr);
              } else {
                munmap(p, finfo.smem_len);
                close(fd);
                xfree(fb->b16);
                xfree(fb);
                xfree(display);
              }
            } else {
              munmap(p, finfo.smem_len);
              close(fd);
              xfree(display);
            }
          } else {
            munmap(p, finfo.smem_len);
            close(fd);
          }
        }
      } else {
        debug_errno(DEBUG_ERROR, "FB", "ioctl");
        close(fd);
      }
    } else {
      debug_errno(DEBUG_ERROR, "FB", "open");
    }
  }

  return r;
}

int libfb_init(int pe, script_ref_t obj) {
  script_add_function(pe, obj, "create", libdisplay_create);

  return 0;
}
