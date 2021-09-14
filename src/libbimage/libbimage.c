#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>

#include "script.h"
#include "pwindow.h"
#include "image.h"
#include "ptr.h"
#include "util.h"
#include "media.h"
#include "debug.h"
#include "xalloc.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

struct image_t {
  char *tag;
  int x, y;
  int width, height;
  int numEntries, transpIndex;
  uint32_t colorTable[256];
  uint32_t color, depth, rowBytes;
  uint8_t *buffer;
  window_provider_t *wp;
  window_t *w;
  texture_t *t;
  uint8_t *rgba;
};

static image_provider_t provider;

#define MAX_PAL 256
#include "palette.h"

static void validate_range(int *a, int *b) {
  int aux;

  if (*a > *b) {
    aux = *a;
    *a = *b;
    *b = aux;
  }
}

static void validate_coord(int *a, int max) {
  if (*a < 0) *a = 0;
  else if (*a >= max) *a = max - 1;
}

static void validate_coord_len(int *a, int *d, int max) {
  int a1, a2;

  a1 = *a;
  a2 = *a + *d - 1;

  validate_range(&a1, &a2);
  if ((a2 < 0) || (a1 >= max)) {
    *a = *d = 0;
    return;
  }

  validate_coord(&a1, max);
  validate_coord(&a2, max);

  *a = a1;
  *d = a2 - a1 + 1;
}

static int image_init(void) {
  return 0;
}

static image_t *image_create(int width, int height, int depth) {
  image_t *image;
  uint32_t rowBytes;
  int i, numEntries;

  debug(DEBUG_TRACE, "BIMAGE", "image_create %d,%d %d", width, height, depth);

  switch (depth) {
    case 1:  numEntries = 2;   rowBytes = (width + 7) / 8; break;
    case 2:  numEntries = 4;   rowBytes = (width + 3) / 4; break;
    case 4:  numEntries = 16;  rowBytes = (width + 1) / 2; break;
    case 8:  numEntries = 256; rowBytes = width; break;
    case 16: numEntries = 0;   rowBytes = width * 2; break;
    case 32: numEntries = 0;   rowBytes = width * 4; break;
    default:
      debug(DEBUG_ERROR, "BIMAGE", "image_create: invalid depth %d", depth);
      return NULL;
  }

  if ((image = xcalloc(1, sizeof(image_t))) != NULL) {
    image->tag = TAG_IMAGE;
    image->width = width;
    image->height = height;
    image->numEntries = numEntries;
    image->depth = depth;
    image->rowBytes = rowBytes;
    image->buffer = xcalloc(1, height * rowBytes);
    image->transpIndex = -1;  // no transparency
    for (i = 0; i < numEntries; i++) {
      image->colorTable[i] = defaultPalette[i];
    }

    if (image->buffer == NULL) {
      xfree(image);
      image = NULL;
    }
  }

  return image;
}

static image_t *image_load(char *filename, int depth) {
  image_t *image = NULL;
  uint8_t *data, *p, a, r, g, b;
  uint16_t w, *p16;
  int width, height, comp, len, i, j;

  if ((data = stbi_load(filename, &width, &height, &comp, 4)) != NULL) {
    if ((image = image_create(width, height, depth)) != NULL) {
      len = height * width * 4;
      p = data;
      switch (depth) {
        case 16:
          p16 = (uint16_t *)image->buffer;
          for (i = 0, j = 0; i < len; i += 4) {
            r = p[i];
            g = p[i+1];
            b = p[i+2];
            a = p[i+3];
            w = r >> 3;
            w <<= 6;
            w |= g >> 2;
            w <<= 5;
            w |= b >> 3;
            p16[j++] = w;
          }
          break;
        case 32:
          for (i = 0; i < len; i += 4) {
            r = p[i];
            g = p[i+1];
            b = p[i+2];
            a = p[i+3];
            p[i] = b;
            p[i+1] = g;
            p[i+2] = r;
            p[i+3] = a;
          }
          xmemcpy(image->buffer, data, len);
          break;
      }
    }
    stbi_image_free(data);
  }

  return image;
}

static int image_blend(image_t *image, int blend) {
  int r = -1;

  if (image) {
    r = 0;
  }

  return r;
}

static image_t *image_copy(image_t *src) {
  image_t *dst = NULL;

  if (src) {
    if ((dst = image_create(src->width, src->height, src->depth)) != NULL) {
      xmemcpy(dst->buffer, src->buffer, src->height * src->rowBytes);
      xmemcpy(dst->colorTable, src->colorTable, src->numEntries * sizeof(uint32_t));
      dst->numEntries = src->numEntries;
      dst->transpIndex = src->transpIndex;
    }
  }

  return dst;
}

static image_t *image_copy_resize(image_t *src, int dst_width, int dst_height) {
  image_t *dst = NULL;

  debug(DEBUG_ERROR, "BIMAGE", "image_copy_resize not implemented");

  return dst;
}

static int image_clear(image_t *image) {
  int r = -1;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE)) {
    xmemset(image->buffer, 0xFF, image->height * image->rowBytes);
  }

  return r;
}

static uint8_t image_best_color(image_t *image, uint8_t red, uint8_t green, uint8_t blue) {
  uint32_t i, d, dr, dg, db, dmin, imin;
  uint8_t r, g, b;

  dmin = 0xffffffff;
  imin = 0;

  for (i = 0; i < image->numEntries; i++) {
    r = (image->colorTable[i] >> 16) & 0xff;
    g = (image->colorTable[i] >> 8) & 0xff;
    b = image->colorTable[i] & 0xff;
    if (red == r && green == g && blue == b) {
      return i;
    }
    dr = red - r;
    dr = dr * dr;
    dg = green - g;
    dg = dg * dg;
    db = blue - b;
    db = db * db;
    d = dr + dg + db;
    if (d < dmin) {
      dmin = d;
      imin = i;
    }
  }

  return imin;
}

static int image_fill_rgb565(image_t *image, unsigned char *rgb565, int w, int h, int scale) {
  int i, j, row, len;
  uint16_t *p16, *pp16;
  uint32_t *p32, d32, w16, r, g, b;
  uint8_t *p;
  int changed = 0;

  if (image == NULL || rgb565 == NULL) {
    return -1;
  }

  pp16 = (uint16_t *)rgb565;
  p = image->buffer;
  p16 = (uint16_t *)p;
  p32 = (uint32_t *)p;
  len = image->width * image->height;
  row = 0;

  for (i = 0, j = 0; i < len; i++) {
    switch (image->depth) {
      case 16:
        if ((row / scale) < h && (j / scale) < w) {
          p16[j] = pp16[(row / scale) * w + j / scale];
        } else {
          p16[j] = 0;
        }
        j++;
        break;
      case 32:
        //if ((row / scale) < h && (j / scale) < w) {
          w16 = pp16[(row / scale) * w + j / scale];
          r = (w16 << 8) & 0x00F80000;
          g = (w16 << 5) & 0x0000FC00;
          b = (w16 << 3) & 0x000000F8;
          d32 = 0xFF000000 | r | g | b;
          if (p32[j] != d32) {
            p32[j] = d32;
            changed = 1;
          }
        //} else {
          //p32[j] = 0xFF000000;
        //}
        j++;
        break;
    }

    if (j >= image->width) {
      row++;
      p = &image->buffer[row * image->rowBytes];
      p16 = (uint16_t *)p;
      p32 = (uint32_t *)p;
      j = 0;
    }
  }

  return changed;
}

static int image_fill_rgba(image_t *image, unsigned char *rgba) {
  int i, j, row, rgba_len;
  uint32_t *p32, *pp;
  uint16_t *p16, w;
  uint8_t *p, red, green, blue;

  if (image == NULL || rgba == NULL) {
    return -1;
  }

  pp = (uint32_t *)rgba;
  p = image->buffer;
  rgba_len = image->width * image->height;
  row = 0;

  for (i = 0, j = 0; i < rgba_len; i++) {
    switch (image->depth) {
      case 8:
        red = (pp[i] >> 16) & 0xff; 
        green = (pp[i] >> 8) & 0xff;
        blue = pp[i] & 0xff;
        p[j++] = image_best_color(image, red, green, blue);
        break;
      case 16:
        red = (pp[i] >> 16) & 0xff; 
        green = (pp[i] >> 8) & 0xff;
        blue = pp[i] & 0xff;
        p16 = (uint16_t *)p;
        w = red >> 3;
        w <<= 6;
        w |= green >> 2;
        w <<= 5;
        w |= blue >> 3;
        p16[j++] = w;
        break;
      case 32:
        p32 = (uint32_t *)p;
        p32[j++] = pp[i];
        break;
    }

    if (j >= image->width) {
      row++;
      p = &image->buffer[row * image->rowBytes];
      j = 0;
    }
  }

  return 0;
}

static int image_fill_rgb(image_t *image, unsigned char *rgb) {
  int i, j, row, rgb_len;
  uint32_t *p3, *pp;
  uint32_t rgb_r, gb_rg, b_rgb;
  uint32_t r1, b1, g1, r2, b2, g2, r3, b3, g3, r4, g4, b4;

  if (image == NULL || rgb == NULL) {
    return -1;
  }

  if (image->depth != 32) {
    return -1;
  }

  p3 = (uint32_t *)rgb;
  pp = (uint32_t *)image->buffer;
  rgb_len = image->width * image->height * 3;
  rgb_len /= 4;
  row = 0;

  for (i = 0, j = 0; i < rgb_len;) {
    // BGR
    rgb_r = p3[i+0];
    gb_rg = p3[i+1];
    b_rgb = p3[i+2];
    i += 3;

    b1 =  rgb_r        & 0xff;
    g1 = (rgb_r >>  8) & 0xff;
    r1 = (rgb_r >> 16) & 0xff;

    b2 =  rgb_r >> 24;
    g2 =  gb_rg        & 0xff;
    r2 = (gb_rg >>  8) & 0xff;

    b3 = (gb_rg >> 16) & 0xff;
    g3 =  gb_rg >> 24;
    r3 =  b_rgb        & 0xff;

    b4 = (b_rgb >>  8) & 0xff;
    g4 = (b_rgb >> 16) & 0xff;
    r4 =  b_rgb >> 24;

    // BGRA
    pp[j+0] = b1 | (g1 << 8) | (r1 << 16) | 0xFF000000;
    pp[j+1] = b2 | (g2 << 8) | (r2 << 16) | 0xFF000000;
    pp[j+2] = b3 | (g3 << 8) | (r3 << 16) | 0xFF000000;
    pp[j+3] = b4 | (g4 << 8) | (r4 << 16) | 0xFF000000;
    j += 4;

    if (j >= image->width) {
      row++;
      pp = (uint32_t *)&image->buffer[row * image->rowBytes];
      j = 0;
    }
  }

  return 0;
}

static int image_extract_rgb(image_t *image, unsigned char *rgb) {
  return 0;
}

static int image_extract_raw(image_t *image, unsigned char *raw) {
  if (image == NULL || raw == NULL) {
    return -1;
  }

  xmemcpy(raw, image->buffer, image->rowBytes * image->height);

  return 0;
}

static int image_extract_rgba(image_t *image, unsigned char *rgba) {
  int i, j, row, rgba_len;
  uint32_t *p32, *prgba, c, d;
  uint16_t *p16, w;
  uint8_t *p;

  if (image == NULL || rgba == NULL) {
    return -1;
  }

  prgba = (uint32_t *)rgba;
  rgba_len = image->width * image->height;
  row = 0;

  switch (image->depth) {
    case 8:
      p = image->buffer;
      for (i = 0, j = 0; i < rgba_len;) {
        c = p[j++];
        d = image->colorTable[c];
        if (c == image->transpIndex) {
          d &= 0xffffff;
        }
        prgba[i++] = d;

        if (j >= image->width) {
          row++;
          p = &image->buffer[row * image->rowBytes];
          j = 0;
        }
      }
      break;
    case 16:
      p16 = (uint16_t *)image->buffer;
      for (i = 0, j = 0; i < rgba_len;) {
        w = p16[j++];
        d  = (w & 0xF800) << 8;
        d |= (w & 0x07E0) << 5;
        d |= (w & 0x001F) << 3;
        if (w != image->transpIndex) {
          d |= 0xff000000;
        }
        prgba[i++] = d;

        if (j >= image->width) {
          row++;
          p16 = (uint16_t *)&image->buffer[row * image->rowBytes];
          j = 0;
        }
      }
      break;
    case 32:
      if (image->rowBytes == image->width * 4) {
        xmemcpy(rgba, image->buffer, rgba_len * 4);
      } else {
        p32 = (uint32_t *)image->buffer;
        for (i = 0, j = 0; i < rgba_len;) {
          prgba[i++] = p32[j++];
          if (j >= image->width) {
            row++;
            p32 = (uint32_t *)&image->buffer[row * image->rowBytes];
            j = 0;
          }
        }
      }
      break;
  }

  return 0;
}

static int image_extract_rgb565(image_t *image, unsigned char *rgb565) {
  int r = -1;

  if (image != NULL || rgb565 != NULL) {
    if (image->depth == 16) {
      xmemcpy(rgb565, image->buffer, image->width * image->height * 2);
      r = 0;
    }
  }

  return r;
}

static int image_overlay_rgba(image_t *image, unsigned char *rgba, int len) {
  return 0;
}

static int image_size(image_t *image, int *x, int *y) {
  int r = -1;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE)) {
    *x = image->width;
    *y = image->height;
    r = 0;
  }

  return r;
}

static int image_rgb(image_t *image, int c, uint8_t *red, uint8_t *green, uint8_t *blue, uint8_t *alpha) {
  int r = -1;
  uint32_t d;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE)) {
    switch (image->depth) {
      case 8:
        if (c >= 0 && c < image->numEntries) {
          if (red)   *red   = (image->colorTable[c] >> 16) & 0xff;
          if (green) *green = (image->colorTable[c] >> 8) & 0xff;
          if (blue)  *blue  = image->colorTable[c] & 0xff;
          if (c == image->transpIndex) {
            if (alpha) *alpha = 0;
          } else {
            if (alpha) *alpha = 0xff;
          }
          r = 0;
        }
        break;
      case 16:
        d = c;
        if (red)   *red   = (d >> 11) & 0x1f; 
        //if (green) *green = (d >>  6) & 0x3f; 
        if (green) *green = (d >>  5) & 0x3f; 
        if (blue)  *blue  =  d        & 0x1f; 
          if (c == image->transpIndex) {
            if (alpha) *alpha = 0;
          } else {
            if (alpha) *alpha = 0xff;
          }
        r = 0;
        break;
      case 32:
        d = c;
        if (alpha) *alpha =  d >> 24;
        if (red)   *red   = (d >> 16) & 0xff; 
        if (green) *green = (d >>  8) & 0xff;
        if (blue)  *blue  =  d        & 0xff;
        r = 0;
        break;
    }
  }

  return r;
}

static int image_color(image_t *image, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
  uint32_t d = 0;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE)) {
    switch (image->depth) {
      case 8:
        d = image_best_color(image, red, green, blue);
        break;
      case 16:
        d = red >> 3;
        d <<= 6;
        d |= green >> 2;
        d <<= 5;
        d |= blue >> 3;
        break;
      case 32:
        d = blue | (((uint32_t)green) << 8) | (((uint32_t)red) << 16) | (((uint32_t)alpha) << 24);
        //d = red | (((uint32_t)green) << 8) | (((uint32_t)blue) << 16) | (((uint32_t)alpha) << 24);
        break;
    }
  }

  return d;
}

static int image_icolor(image_t *image, int index, uint8_t red, uint8_t green, uint8_t blue) {
  int r = -1;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE)) {
    if (image->depth < 16 && index >= 0 && index < image->numEntries) {
      image->colorTable[index] = blue | (((uint32_t)green) << 8) | (((uint32_t)red) << 16) | 0xff000000;
      r = 0;
    }
  }

  return r;
}

static int image_setcolor(image_t *image, int color) {
  int r = -1;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE)) {
    image->color = color;
    r = 0;
  }

  return r;
}

static int image_getpixel(image_t *image, int x, int y) {
  uint8_t *p;
  uint16_t *p16;
  uint32_t *p32;
  int c = 0;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE)) {
    if (x >= 0 && x < image->width && y >= 0 && y < image->height) {
      switch (image->depth) {
        case 8:
          p = &image->buffer[y * image->rowBytes + x];
          c = *p;
          break;
        case 16:
          p16 = (uint16_t *)&image->buffer[y * image->rowBytes + x * 2];
          c = *p16;
          break;
        case 32:
          p32 = (uint32_t *)&image->buffer[y * image->rowBytes + x * 4];
          c = *p32;
          break;
      }
    }
  }

  return c;
}

#define SETPIXEL(x,y,c) \
    if (x >= 0 && x < image->width && y >= 0 && y < image->height) { \
      switch (image->depth) { \
        case 8: \
          p = &image->buffer[y * image->rowBytes + x]; \
          *p = c; \
          r = 0; \
          break; \
        case 16: \
          p16 = (uint16_t *)&image->buffer[y * image->rowBytes + x * 2]; \
          *p16 = c; \
          r = 0; \
          break; \
        case 32: \
          p32 = (uint32_t *)&image->buffer[y * image->rowBytes + x * 4]; \
          *p32 = c; \
          r = 0; \
          break; \
      } \
    }

static int image_setpixel(image_t *image, int x, int y, int color) {
  uint8_t *p;
  uint16_t *p16;
  uint32_t *p32;
  int r = -1;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE)) {
    SETPIXEL(x, y, color);
  }

  return r;
}

static int image_gettransp(image_t *image) {
  int r = -1;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE)) {
    switch (image->depth) {
      case 32:
        break;
      default:
        r = image->transpIndex;
        break;
    }
  }

  return r;
}

static int image_settransp(image_t *image, int color) {
  int r = -1;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE)) {
    switch (image->depth) {
      case 32:
        break;
      default:
        image->transpIndex = color;
        r = 0;
        break;
    }
  }

  return r;
}

static int image_line(image_t *image, int x1, int y1, int x2, int y2) {
  int dx, dy, sx, sy, i, err, e2;
  uint8_t *p;
  uint16_t *p16;
  uint32_t *p32;
  int r = -1;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE)) {
    if (x1 == x2) {
      if (y1 > y2) { i = y1; y1 = y2; y2 = i; }
      for (i = y1; i <= y2; i++) {
        SETPIXEL(x1, i, image->color);
      }
    } else if (y1 == y2) {
      if (x1 > x2) { i = x1; x1 = x2; x2 = i; }
      for (i = x1; i <= x2; i++) {
        SETPIXEL(i, y1, image->color);
      }
    } else {
      dx = x2 - x1;
      if (dx < 0) dx = -dx;
      sx = x1 < x2 ? 1 : -1;
      dy = y2 - y1;
      if (dy > 0) dy = -dy;
      sy = y1 < y2 ? 1 : -1;
      err = dx + dy;

      for (;;) {
        SETPIXEL(x1, y1, image->color);
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
    }

    image->x = x2;
    image->y = y2;
    r = 0;
  }

  return r;
}

static int image_lineto(image_t *image, int x, int y) {
  int r = -1;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE)) {
    r = image_line(image, image->x, image->y, x, y);
    image->x = x;
    image->y = y;
  }

  return r;
}

static int image_rectangle(image_t *image, int x1, int y1, int x2, int y2, int filled) {
  uint8_t *p;
  uint16_t *p16;
  uint32_t *p32;
  int x, y, r = -1;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE)) {
    validate_range(&x1, &x2);
    validate_range(&y1, &y2);
    validate_coord(&x1, image->width);
    validate_coord(&x2, image->width);
    validate_coord(&y1, image->height);
    validate_coord(&y2, image->height);

    if (filled) {
      for (y = y1; y <= y2; y++) {
        switch (image->depth) {
          case 8:
            p = &image->buffer[y * image->rowBytes];
            for (x = x1; x <= x2; x++) p[x] = image->color;
            break;
          case 16:
            p16 = (uint16_t *)&image->buffer[y * image->rowBytes];
            for (x = x1; x <= x2; x++) p16[x] = image->color;
            break;
          case 32:
            p32 = (uint32_t *)&image->buffer[y * image->rowBytes];
            for (x = x1; x <= x2; x++) p32[x] = image->color;
            break;
        }
      }
    } else {
      switch (image->depth) {
        case 8:
          for (y = y1; y <= y2; y++) {
            p = &image->buffer[y * image->rowBytes];
            p[x1] = image->color;
            p[x2] = image->color;
          }
          p = &image->buffer[y1 * image->rowBytes];
          for (x = x1; x <= x2; x++) p[x] = image->color;
          p = &image->buffer[y2 * image->rowBytes];
          for (x = x1; x <= x2; x++) p[x] = image->color;
          break;
        case 16:
          for (y = y1; y <= y2; y++) {
            p16 = (uint16_t *)&image->buffer[y * image->rowBytes];
            p16[x1] = image->color;
            p16[x2] = image->color;
          }
          p16 = (uint16_t *)&image->buffer[y1 * image->rowBytes];
          for (x = x1; x <= x2; x++) p16[x] = image->color;
          p16 = (uint16_t *)&image->buffer[y2 * image->rowBytes];
          for (x = x1; x <= x2; x++) p16[x] = image->color;
          break;
        case 32:
          for (y = y1; y <= y2; y++) {
            p32 = (uint32_t *)&image->buffer[y * image->rowBytes];
            p32[x1] = image->color;
            p32[x2] = image->color;
          }
          p32 = (uint32_t *)&image->buffer[y1 * image->rowBytes];
          for (x = x1; x <= x2; x++) p32[x] = image->color;
          p32 = (uint32_t *)&image->buffer[y2 * image->rowBytes];
          for (x = x1; x <= x2; x++) p32[x] = image->color;
          break;
      }
    }
    r = 0;
  }

  return r;
}

static int image_polygon(image_t *image, point_t *points, int n, int filled) {
  int i, r = -1;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE) && points && n > 0) {
    if (filled) {
      // XXX

    } else {
      image->x = points[0].x;
      image->y = points[0].y;
      for (i = 1; i < n; i++) {
        image_lineto(image, points[i].x, points[i].y);
      }
    }
    r = 0;
  }

  return r;
}

static int image_ellipse(image_t *image, int x, int y, int rx, int ry, int filled) {
  int g, x0, y0, x1, y1, r = -1;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE)) {
    if (filled) {
      // XXX

    } else {
      for (g = 0; g <= 90; g += 1) {
        x1 = rx * sin((g * M_PI) / 180.0);
        y1 = ry * cos((g * M_PI) / 180.0);
        if (g > 0) {
          image_line(image, x+x0, y+y0, x+x1, y+y1);
          image_line(image, x+x0, y-y0, x+x1, y-y1);
          image_line(image, x-x0, y+y0, x-x1, y+y1);
          image_line(image, x-x0, y-y0, x-x1, y-y1);
        }
        x0 = x1;
        y0 = y1;
      }
    }
    r = 0;
  }

  return r;
}

static int image_clip(image_t *image, int x1, int y1, int x2, int y2) {
  int r = -1;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE)) {
    if (x1 < 0 || y1 < 0 || x2 < 0 || y2 < 0) {
      x1 = y1 = 0;
      x2 = image->width-1;
      y2 = image->height-1;
    }
    //gdImageSetClip(image->img, x1, y1, x2, y2);
    r = 0;
  }

  return r;
}

static int image_draw2(image_t *image, int x, int y, image_t *obj, int srcX, int srcY, int w, int h) {
  uint8_t *p, *q, d;
  uint16_t *p16, *q16, d16;
  uint32_t *p32, *q32;
  int i, j, r = -1;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE) && obj && ptr_check_tag(obj->tag, TAG_IMAGE) && image->depth == obj->depth) {
    validate_coord_len(&srcX, &w, obj->width);
    validate_coord_len(&srcY, &h, obj->height);

    if (w > 0 && h > 0) {
      validate_coord_len(&x, &w, image->width);
      validate_coord_len(&y, &h, image->height);

      if (w > 0 && h > 0) {
        for (i = 0; i < h; i++) {
          switch (image->depth) {
            case 8:
              p = &image->buffer[(y + i) * image->rowBytes + x];
              q = &obj->buffer[(srcY + i) * obj->rowBytes + srcX];
              for (j = 0; j < w; j++) {
                d = q[j];
                if (obj->transpIndex < 0 || obj->transpIndex != d) {
                  p[j] = d;
                }
              }
              break;
            case 16:
              p16 = (uint16_t *)&image->buffer[(y + i) * image->rowBytes + x * 2];
              q16 = (uint16_t *)&obj->buffer[(srcY + i) * obj->rowBytes + srcX * 2];
              for (j = 0; j < w; j++) {
                d16 = q16[j];
                if (obj->transpIndex < 0 || obj->transpIndex != d16) {
                  p16[j] = d16;
                }
              }
              break;
            case 32:
              p32 = (uint32_t *)&image->buffer[(y + i) * image->rowBytes + x * 4];
              q32 = (uint32_t *)&obj->buffer[(srcY + i) * obj->rowBytes + srcX * 4];
              xmemcpy(p32, q32, w*4);
              break;
          }
        }
      }
    }
    r = 0;
  }

  return r;
}

static int image_draw(image_t *image, int x, int y, image_t *obj) {
  int r = -1;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE) && obj && ptr_check_tag(obj->tag, TAG_IMAGE) && image->depth == obj->depth) {
    r = image_draw2(image, x, y, obj, 0, 0, obj->width, obj->height);
  }

  return r;
}

static image_t *image_slice(image_t *image, int x, int y, int width, int height) {
  image_t *slice = NULL;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE)) {
    if ((slice = image_create(width, height, image->depth)) != NULL) {
      image_draw2(slice, 0, 0, image, x, y, width, height);
    }
  }

  return slice;
}

static int image_text(image_t *image, int x, int y, char *font, double size, double angle, char *text, int *bounds) {
  int r = -1;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE) && font && text && bounds) {
    //gdImageStringFT(image->img, bounds, image->color, font, size, angle, x, y, text);
    r = 0;
  }

  return r;
}

static int image_save_file(image_t *image, FILE *f, char *ext, int mode, time_t ts, double lon, double lat, double alt) {
  int r = -1;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE) && f) {
    if (!strcasecmp(ext, "png")) {
      //gdImagePng(image->img, f);
      r = 0;
    } else if (!strcasecmp(ext, "jpg")) {
/*
      gdImageJpeg(image->img, f, DEFAULT_JPEG_QUALITY);
      if (mode >= 2) {
        insert_gps_exif(f, gdImageSX(image->img), gdImageSY(image->img), lat, lon, alt, mode);
      }
*/
      r = 0;
    } else {
      debug(DEBUG_ERROR, "BIMAGE", "unknown type \"%s\"", ext);
    }
  }

  return r;
}

static int image_save_exif(image_t *image, char *filename, int mode, time_t ts, double lon, double lat, double alt) {
  FILE *f;
  char *ext;
  int r = -1;

  debug(DEBUG_TRACE, "BIMAGE", "save begin");

  if (image && ptr_check_tag(image->tag, TAG_IMAGE) && filename) {
    if ((ext = getext(filename)) != NULL) {
     if ((f = fopen(filename, "w")) != NULL) {
       r = image_save_file(image, f, ext, mode, ts, lon, lat, alt);
       fclose(f);
     } else {
       debug_errno("BIMAGE", "fopen \"%s\"", filename);
     }
    } else {
      debug(DEBUG_ERROR, "BIMAGE", "invalid type \"%s\"", filename);
    }
  }

  debug(DEBUG_TRACE, "BIMAGE", "save end");
  return r;
}

static int image_save(image_t *image, char *filename) {
  return image_save_exif(image, filename, -1, 0, 0, 0, 0);
}

static int image_show(image_t *image, int fullscreen, window_provider_t *wp) {
  int width, height, r = -1;

  if (image && wp) {
    if (image_size(image, &width, &height) == 0) {
      if (image->rgba == NULL) {
        image->rgba = xcalloc(1, width * height * sizeof(uint32_t));
      }
      if (image->rgba != NULL) {
        if (image_extract_rgba(image, image->rgba) == 0) {
          if (image->w == NULL) {
            image->w = wp->create(ENC_RGBA, &width, &height, 1, 1, 0, fullscreen, 0);
            image->wp = wp;
            if (image->w != NULL) {
              image->t = wp->create_texture(image->w, width, height);
            }
          }
          if (image->t != NULL) {
            wp->update_texture(image->w, image->t, image->rgba);
            r = wp->draw_texture(image->w, image->t, 0, 0);
            wp->render(image->w);
          }
        }
      }
    }
  }

  return r;
}

static int image_event(image_t *image, int *key, int *mods, int *buttons) {
  int r = -1;

  if (image && image->wp && image->w) {
    r = image->wp->event(image->w, 0, 1, key, mods, buttons);
  }

  return r;
}

static int image_destroy(image_t *image) {
  int r = -1;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE)) {
    if (image->buffer) xfree(image->buffer);
    if (image->rgba) xfree(image->rgba);
    if (image->wp && image->t) image->wp->destroy_texture(image->w, image->t);
    if (image->wp && image->w) image->wp->destroy(image->w);
    xfree(image);
    r = 0;
  }

  return r;
}

static uint8_t *image_raw(image_t *image) {
  uint8_t *raw = NULL;

  if (image && ptr_check_tag(image->tag, TAG_IMAGE)) {
    raw = image->buffer;
  }

  return raw;
}

int libbimage_load(void) {
  provider.load = image_load;
  provider.create = image_create;
  provider.copy = image_copy;
  provider.copy_resize = image_copy_resize;
  provider.slice = image_slice;
  provider.clear = image_clear;
  provider.fill_rgb = image_fill_rgb;
  provider.fill_rgba = image_fill_rgba;
  provider.fill_rgb565 = image_fill_rgb565;
  provider.extract_rgb = image_extract_rgb;
  provider.extract_rgba = image_extract_rgba;
  provider.extract_rgb565 = image_extract_rgb565;
  provider.extract_raw = image_extract_raw;
  provider.overlay_rgba = image_overlay_rgba;
  provider.size = image_size;
  provider.blend = image_blend;
  provider.rgb = image_rgb;
  provider.color = image_color;
  provider.icolor = image_icolor;
  provider.setcolor = image_setcolor;
  provider.getpixel = image_getpixel;
  provider.setpixel = image_setpixel;
  provider.gettransp = image_gettransp;
  provider.settransp = image_settransp;
  provider.line = image_line;
  provider.lineto = image_lineto;
  provider.rectangle = image_rectangle;
  provider.polygon = image_polygon;
  provider.ellipse = image_ellipse;
  provider.clip = image_clip;
  provider.draw = image_draw;
  provider.draw2 = image_draw2;
  provider.text = image_text;
  provider.save = image_save;
  provider.save_exif = image_save_exif;
  provider.show = image_show;
  provider.event = image_event;
  provider.destroy = image_destroy;
  provider.raw = image_raw;

  return image_init();
}

int libbimage_init(int pe, script_ref_t obj) {
  debug(DEBUG_INFO, "BIMAGE", "registering %s", IMAGE_PROVIDER);
  script_set_pointer(pe, IMAGE_PROVIDER, &provider);

  return 0;
}
