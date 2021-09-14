#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "script.h"
#include "media.h"
#include "ptr.h"
#include "pwindow.h"
#include "pfont.h"
#include "image.h"
#include "display.h"
#include "debug.h"
#include "xalloc.h"

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64

typedef struct {
  window_provider_t *wp;
  window_t *w;
  texture_t *t;
  uint32_t rgba[SCREEN_WIDTH * SCREEN_HEIGHT];
} libsd1306_t;

static void update(libsd1306_t *sd1306) {
  sd1306->wp->update_texture(sd1306->w, sd1306->t, (uint8_t *)sd1306->rgba);
  sd1306->wp->draw_texture(sd1306->w, sd1306->t, 0, 0);
  sd1306->wp->render(sd1306->w);
}

static int sd1306_char(libsd1306_t *sd1306, int x, int y, uint8_t c, int i, font_t *f, int fg, int bg) {
  uint32_t index;
  uint8_t *d, b;
  int j, k;

  d = (i ? f->font1 : f->font0) + (c - f->min) * f->width;

  for (j = 0; j < f->width; j++) {
    b = d[j];
    index = y * SCREEN_WIDTH + x + j;
    for (k = 0; k < 8; k++) {
      sd1306->rgba[index] = (b & 0x01) ? fg : bg;
      index += SCREEN_WIDTH;
      b = b >> 1;
    }
  }

  return 0;
}

static int sd1306_printchar(libsd1306_t *sd1306, int x, int y, uint8_t c, font_t *f, int fg, int bg) {
  if (x < 0 || x > (SCREEN_WIDTH - f->width) || y < 0 || y > (SCREEN_HEIGHT - f->height)) {
    debug(DEBUG_ERROR, "SD1306", "sd1306_printchar (%d,%d) out of screen");
    return 0;
  }

  if (f->height == 8) {
    return sd1306_char(sd1306, x, y, c, 1, f, fg, bg);
  }

  if (sd1306_char(sd1306, x, y, c, 0, f, fg, bg) != 0) return -1;
  if (sd1306_char(sd1306, x, y+8, c, 1, f, fg, bg) != 0) return -1;

  return 0;
}

static int display_printstr(void *data, int x, int y, char *s, font_t *f, uint32_t fg, uint32_t bg) {
  libsd1306_t *sd1306;
  int i;

  sd1306 = (libsd1306_t *)data;
  for (i = 0; s[i]; i++, x += f->width) {
    sd1306_printchar(sd1306, x, y, s[i], f, fg, bg);
  }
  update(sd1306);

  return 0;
}

static int display_cls(void *data, uint32_t bg) {
  libsd1306_t *sd1306;
  int i, n;

  sd1306 = (libsd1306_t *)data;
  for (i = 0, n = SCREEN_WIDTH * SCREEN_HEIGHT; i < n; i++) {
    sd1306->rgba[i] = 0xFF000000;
  }
  update(sd1306);

  return 0;
}

static uint32_t display_rgb(void *data, int red, int green, int blue) {
  return (red || green || blue) ? 0xFF00FFFF : 0xFF000000;
}

static void display_destructor(void *p) {
  libdisplay_t *display;
  libsd1306_t *sd1306;

  display = (libdisplay_t *)p;
  if (display) {
    sd1306 = (libsd1306_t *)display->data;
    sd1306->wp->destroy(sd1306->w);
    xfree(sd1306);
    xfree(display);
  }
}

static int libdisplay_create(int pe) {
  libdisplay_t *display;
  libsd1306_t *sd1306;
  window_provider_t *wp;
  int ptr, width, height, r = -1;

  if ((wp = script_get_pointer(pe, WINDOW_PROVIDER)) == NULL) {
    debug(DEBUG_ERROR, "SD1306", "window provider not found");
    return -1;
  }

  if ((display = xcalloc(1, sizeof(libdisplay_t))) != NULL) {
    if ((sd1306 = xcalloc(1, sizeof(libsd1306_t))) != NULL) {
      width = SCREEN_WIDTH;
      height = SCREEN_HEIGHT;
      if ((sd1306->w = wp->create(ENC_RGBA, &width, &height, 1, 1, 0, 0, 0)) != NULL) {
        sd1306->wp = wp;
        sd1306->t = sd1306->wp->create_texture(sd1306->w, width, height);
        display->width = SCREEN_WIDTH;
        display->height = SCREEN_HEIGHT;
        display->data = sd1306;
        display->printstr = display_printstr;
        display->cls = display_cls;
        display->rgb = display_rgb;
        display->fg = display_rgb(sd1306, 255, 255, 255);
        display->bg = display_rgb(sd1306, 0, 0, 0);
        display->tag = TAG_DISPLAY;

        if ((ptr = ptr_new(display, display_destructor)) != -1) {
          r = script_push_integer(pe, ptr);
        } else {
          wp->destroy(sd1306->w);
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

  return r;
}

int libsd1306g_init(int pe, script_ref_t obj) {
  if (script_get_pointer(pe, WINDOW_PROVIDER) == NULL) {
    debug(DEBUG_ERROR, "SD1306", "window provider not found");
    return -1;
  }

  script_add_iconst(pe, obj,   "width",  SCREEN_WIDTH);
  script_add_iconst(pe, obj,   "height", SCREEN_HEIGHT);
  script_add_function(pe, obj, "create", libdisplay_create);

  return 0;
}
