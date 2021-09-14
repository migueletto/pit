#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "pfont.h"
#include "font5x8.h"
#include "font6x8coco.h"
#include "font6x8apple.h"
#include "font8x8.h"
#include "font10x16.h"
#include "font12x16coco.h"
#include "font16x16.h"
#include "font8x8zx81.h"

#include "script.h"
#include "pwindow.h"
#include "image.h"
#include "display.h"
#include "media.h"
#include "ptr.h"
#include "debug.h"
#include "xalloc.h"

typedef struct {
  int x, y, ptr;
  unsigned char *frame;
} libdisplay_video_t;

static int libdisplay_node_process(media_frame_t *frame, void *data);
static int libdisplay_node_destroy(void *data);

static node_dispatch_t display_dispatch = {
  libdisplay_node_process,
  NULL,
  NULL,
  libdisplay_node_destroy
};

static int libdisplay_destroy(int pe) {
  script_arg_t arg;
  int ptr, r;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg) == 0) {
    ptr = arg.value.i;
    if (ptr_free(ptr, TAG_DISPLAY) == 0) {
      r = script_push_boolean(pe, 1);
    }
  }

  return r;
}

static int libdisplay_enable(int pe) {
  script_arg_t arg[2];
  libdisplay_t *display;
  int r, ptr, en;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg[0]) == 0 &&
      script_get_value(pe, 1, SCRIPT_ARG_BOOLEAN, &arg[1]) == 0) {

    ptr = arg[0].value.i;
    en = arg[1].value.i;

    if ((display = ptr_lock(ptr, TAG_DISPLAY)) != NULL) {
      if (display->enable == NULL || display->enable(display->data, en) != -1) {
        r = script_push_boolean(pe, 1);
      }
      ptr_unlock(ptr, TAG_DISPLAY);
    }
  }

  return r;
}

static int libdisplay_contrast(int pe) {
  script_arg_t arg[2];
  libdisplay_t *display;
  int r, ptr, contrast;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg[0]) == 0 &&
      script_get_value(pe, 1, SCRIPT_ARG_INTEGER, &arg[1]) == 0) {

    ptr = arg[0].value.i;
    contrast = arg[1].value.i;

    if ((display = ptr_lock(ptr, TAG_DISPLAY)) != NULL) {
      if (display->contrast == NULL || display->contrast(display->data, contrast) != -1) {
        r = script_push_boolean(pe, 1);
      }
      ptr_unlock(ptr, TAG_DISPLAY);
    }
  }

  return r;
}

static int libdisplay_backlight(int pe) {
  script_arg_t arg[2];
  libdisplay_t *display;
  int r, ptr, backlight;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg[0]) == 0 &&
      script_get_value(pe, 1, SCRIPT_ARG_BOOLEAN, &arg[1]) == 0) {

    ptr = arg[0].value.i;
    backlight = arg[1].value.i;

    if ((display = ptr_lock(ptr, TAG_DISPLAY)) != NULL) {
      if (display->backlight == NULL || display->backlight(display->data, backlight) != -1) {
        r = script_push_boolean(pe, 1);
      }
      ptr_unlock(ptr, TAG_DISPLAY);
    }
  }

  return r;
}

static int libdisplay_print(int pe) {
  script_int_t ptr, x, y;
  libdisplay_t *display;
  char *s = NULL;
  uint8_t c;
  int r, i;

  r = -1;

  if (script_get_integer(pe, 0, &ptr) == 0 &&
      script_get_integer(pe, 1, &x) == 0 &&
      script_get_integer(pe, 2, &y) == 0 &&
      script_get_string(pe,  3, &s) == 0) {

    if (s[0] && (display = ptr_lock(ptr, TAG_DISPLAY)) != NULL) {
      if (display->printstr) {
        if (display->printstr(display->data, x, y, s, display->f, display->fg, display->bg) == 0) {
          r = script_push_boolean(pe, 1);
        }

      } else {
        for (i = 0; s[i] && (x + display->f->width) <= display->width; i++, x += display->f->width) {
          c = s[i];
          if (c < display->f->min || c > display->f->max) c = display->f->min;
          if (display->printchar && display->printchar(display->data, x, y, c, display->f, display->fg, display->bg) == -1) break;
        }
        if (!s[i]) {
          r = script_push_boolean(pe, 1);
        }
      }
      ptr_unlock(ptr, TAG_DISPLAY);
    }
  }

  if (s) xfree(s);

  return r;
}

static int libdisplay_cls(int pe) {
  script_arg_t arg;
  libdisplay_t *display;
  int r, ptr;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg) == 0) {
    ptr = arg.value.i;

    if ((display = ptr_lock(ptr, TAG_DISPLAY)) != NULL) {
      if (display->cls == NULL || display->cls(display->data, display->bg) != -1) {
        r = script_push_boolean(pe, 1);
      }
      ptr_unlock(ptr, TAG_DISPLAY);
    }
  }

  return r;
}

static int libdisplay_line_style(int pe, int style) {
  script_arg_t arg[5];
  libdisplay_t *display;
  int r, ptr, x1, y1, x2, y2;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg[0]) == 0 &&
      script_get_value(pe, 1, SCRIPT_ARG_INTEGER, &arg[1]) == 0 &&
      script_get_value(pe, 2, SCRIPT_ARG_INTEGER, &arg[2]) == 0 &&
      script_get_value(pe, 3, SCRIPT_ARG_INTEGER, &arg[3]) == 0 &&
      script_get_value(pe, 4, SCRIPT_ARG_INTEGER, &arg[4]) == 0) {

    ptr = arg[0].value.i;
    x1 = arg[1].value.i;
    y1 = arg[2].value.i;
    x2 = arg[3].value.i;
    y2 = arg[4].value.i;

    if ((display = ptr_lock(ptr, TAG_DISPLAY)) != NULL) {
      if (display->line == NULL || display->line(display->data, x1, y1, x2, y2, display->fg, style) != -1) {
        r = script_push_boolean(pe, 1);
      }
      ptr_unlock(ptr, TAG_DISPLAY);
    }
  }

  return r;
}

static int libdisplay_line(int pe) {
  return libdisplay_line_style(pe, DISPLAY_LINE_LINE);
}

static int libdisplay_rect(int pe) {
  return libdisplay_line_style(pe, DISPLAY_LINE_RECT);
}

static int libdisplay_box(int pe) {
  return libdisplay_line_style(pe, DISPLAY_LINE_FILLED);
}

static int libdisplay_circle_style(int pe, int style) {
  libdisplay_t *display;
  script_int_t ptr, x, y, rx, ry;
  int r = -1;

  if (script_get_integer(pe, 0, &ptr) == 0 &&
      script_get_integer(pe, 1, &x) == 0 &&
      script_get_integer(pe, 2, &y) == 0 &&
      script_get_integer(pe, 3, &rx) == 0 &&
      script_get_integer(pe, 4, &ry) == 0) {

    if ((display = ptr_lock(ptr, TAG_DISPLAY)) != NULL) {
      if (display->ellipse == NULL || display->ellipse(display->data, x, y, rx, ry, display->fg, style) != -1) {
        r = script_push_boolean(pe, 1);
      }
      ptr_unlock(ptr, TAG_DISPLAY);
    }
  }

  return r;
}

static int libdisplay_circle(int pe) {
  return libdisplay_circle_style(pe, DISPLAY_ELLIPSE_LINE);
}

static int libdisplay_disc(int pe) {
  return libdisplay_circle_style(pe, DISPLAY_ELLIPSE_FILLED);
}

static int libdisplay_draw(int pe) {
  script_arg_t arg[4];
  image_t *img;
  libdisplay_t *display;
  image_provider_t *image;
  int x, y, ptr1, ptr2, r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg[0]) == 0 &&
      script_get_value(pe, 1, SCRIPT_ARG_INTEGER, &arg[1]) == 0 &&
      script_get_value(pe, 2, SCRIPT_ARG_INTEGER, &arg[2]) == 0 &&
      script_get_value(pe, 3, SCRIPT_ARG_INTEGER, &arg[3]) == 0) {

    ptr1 = arg[0].value.i;
    ptr2 = arg[1].value.i;
    x = arg[2].value.i;
    y = arg[3].value.i;

    if ((image = script_get_pointer(pe, IMAGE_PROVIDER)) != NULL) {
      if ((img = (image_t *)ptr_lock(ptr2, TAG_IMAGE)) != NULL) {
        if ((display = ptr_lock(ptr1, TAG_DISPLAY)) != NULL) {
          r = display->draw ? display->draw(display->data, x, y, image, img) : 0;
          ptr_unlock(ptr1, TAG_DISPLAY);
        }
        ptr_unlock(ptr2, TAG_IMAGE);
      }
    } else {
      debug(DEBUG_ERROR, "DISPLAY", "image provider not found");
    }
  }

  return r == 0 ? script_push_boolean(pe, 1) : -1;
}

static int libdisplay_width(int pe) {
  script_arg_t arg;
  libdisplay_t *display;
  int r, ptr;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg) == 0) {
    ptr = arg.value.i;

    if ((display = ptr_lock(ptr, TAG_DISPLAY)) != NULL) {
      r = script_push_integer(pe, display->width);
      ptr_unlock(ptr, TAG_DISPLAY);
    }
  }

  return r;
}

static int libdisplay_height(int pe) {
  script_arg_t arg;
  libdisplay_t *display;
  int r, ptr;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg) == 0) {
    ptr = arg.value.i;

    if ((display = ptr_lock(ptr, TAG_DISPLAY)) != NULL) {
      r = script_push_integer(pe, display->height);
      ptr_unlock(ptr, TAG_DISPLAY);
    }
  }

  return r;
}

static int libdisplay_rgb(int pe) {
  script_arg_t arg[4];
  libdisplay_t *display;
  int red, green, blue;
  int r, ptr;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg[0]) == 0 &&
      script_get_value(pe, 1, SCRIPT_ARG_INTEGER, &arg[1]) == 0 &&
      script_get_value(pe, 2, SCRIPT_ARG_INTEGER, &arg[2]) == 0 &&
      script_get_value(pe, 3, SCRIPT_ARG_INTEGER, &arg[3]) == 0) {

    ptr = arg[0].value.i;
    red = arg[1].value.i;
    green = arg[2].value.i;
    blue = arg[3].value.i;

    if ((display = ptr_lock(ptr, TAG_DISPLAY)) != NULL) {
      if (display->rgb) {
        r = script_push_integer(pe, display->rgb(display->data, red, green, blue));
      }
      ptr_unlock(ptr, TAG_DISPLAY);
    }
  }

  return r;
}

static int libdisplay_color(int pe) {
  script_arg_t arg[3];
  libdisplay_t *display;
  int r, fg, bg, ptr;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg[0]) == 0 &&
      script_get_value(pe, 1, SCRIPT_ARG_INTEGER, &arg[1]) == 0 &&
      script_get_value(pe, 2, SCRIPT_ARG_INTEGER, &arg[2]) == 0) {

    ptr = arg[0].value.i;
    fg = arg[1].value.i;
    bg = arg[2].value.i;

    if ((display = ptr_lock(ptr, TAG_DISPLAY)) != NULL) {
      display->fg = fg;
      display->bg = bg;
      r = script_push_boolean(pe, 1);
      ptr_unlock(ptr, TAG_DISPLAY);
    }
  }

  return r;
}

static int libdisplay_font(int pe) {
  script_arg_t arg[2];
  libdisplay_t *display;
  int r, font, ptr;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg[0]) == 0 &&
      script_get_value(pe, 1, SCRIPT_ARG_INTEGER, &arg[1]) == 0) {

    ptr = arg[0].value.i;
    font = arg[1].value.i;

    if ((display = ptr_lock(ptr, TAG_DISPLAY)) != NULL) {
      r = script_push_boolean(pe, 1);

      if (!display->fixedfont) {
        switch (font) {
          case 0:
            display->f = &font5x8;
            break;
          case 1:
            display->f = &font6x8coco;
            break;
          case 2:
            display->f = &font6x8apple;
            break;
          case 3:
            display->f = &font8x8;
            break;
          case 4:
            display->f = &font10x16;
            break;
          case 5:
            display->f = &font12x16coco;
            break;
          case 6:
            display->f = &font16x16;
            break;
          case 7:
            display->f = &font8x8zx81;
            break;
          default:
            r = -1;
        }
      }
      ptr_unlock(ptr, TAG_DISPLAY);
    }
  }

  return r;
}

static int libdisplay_font_width(int pe) {
  script_arg_t arg;
  libdisplay_t *display;
  int r, ptr;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg) == 0) {
    ptr = arg.value.i;

    if ((display = ptr_lock(ptr, TAG_DISPLAY)) != NULL) {
      r = script_push_integer(pe, display->f->width);
      ptr_unlock(ptr, TAG_DISPLAY);
    }
  }

  return r;
}

static int libdisplay_font_height(int pe) {
  script_arg_t arg;
  libdisplay_t *display;
  int r, ptr;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg) == 0) {
    ptr = arg.value.i;

    if ((display = ptr_lock(ptr, TAG_DISPLAY)) != NULL) {
      r = script_push_integer(pe, display->f->height);
      ptr_unlock(ptr, TAG_DISPLAY);
    }
  }

  return r;
}

static int libdisplay_node_process(media_frame_t *frame, void *_data) {
  libdisplay_video_t *data;
  libdisplay_t *display;
  int r = -1;

  if (frame->len == 0) {
    return 0;
  }

  if (frame->meta.type != FRAME_TYPE_VIDEO) {
    debug(DEBUG_ERROR, "DISPLAY", "not a video frame");
    return -1;
  }

  data = (libdisplay_video_t *)_data;

  if ((display = ptr_lock(data->ptr, TAG_DISPLAY)) != NULL) {
    if (display->drawf) {
      r = display->drawf(display->data, data->x, data->y, frame->meta.av.v.encoding, frame->meta.av.v.width, frame->meta.av.v.height, frame->frame);
    }
    ptr_unlock(data->ptr, TAG_DISPLAY);
  }

  return r == 0 ? 1 : -1;
}

static int libdisplay_node_destroy(void *_data) {
  libdisplay_video_t *data;

  data = (libdisplay_video_t *)_data;
  if (data->frame) xfree(data->frame);
  xfree(data);

  return 0;
}

static int libdisplay_video(int pe) {
  script_arg_t arg[3];
  libdisplay_video_t *data;
  int ptr, node, r;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg[0]) == 0 &&
      script_get_value(pe, 1, SCRIPT_ARG_INTEGER, &arg[1]) == 0 &&
      script_get_value(pe, 2, SCRIPT_ARG_INTEGER, &arg[2]) == 0) {

    ptr = arg[0].value.i;

    if ((data = xcalloc(1, sizeof(libdisplay_video_t))) != NULL) {
      data->ptr = ptr;
      data->x = arg[1].value.i;
      data->y = arg[2].value.i;

      if ((node = node_create("DISPLAY", &display_dispatch, data)) != -1) {
        r = script_push_integer(pe, node);
      } else {
        xfree(data);
      }
    }
  }

  return r;
}

int libdisplay_init(int pe, script_ref_t obj) {
  script_add_iconst(pe, obj, "num_fonts", 7);

  script_add_function(pe, obj, "destroy",   libdisplay_destroy);
  script_add_function(pe, obj, "enable",    libdisplay_enable);
  script_add_function(pe, obj, "contrast",  libdisplay_contrast);
  script_add_function(pe, obj, "backlight", libdisplay_backlight);
  script_add_function(pe, obj, "print",     libdisplay_print);
  script_add_function(pe, obj, "cls",       libdisplay_cls);
  script_add_function(pe, obj, "line",      libdisplay_line);
  script_add_function(pe, obj, "rect",      libdisplay_rect);
  script_add_function(pe, obj, "box",       libdisplay_box);
  script_add_function(pe, obj, "circle",    libdisplay_circle);
  script_add_function(pe, obj, "disc",      libdisplay_disc);
  script_add_function(pe, obj, "width",     libdisplay_width);
  script_add_function(pe, obj, "height",    libdisplay_height);
  script_add_function(pe, obj, "rgb",       libdisplay_rgb);
  script_add_function(pe, obj, "color",     libdisplay_color);
  script_add_function(pe, obj, "font",      libdisplay_font);
  script_add_function(pe, obj, "fwidth",    libdisplay_font_width);
  script_add_function(pe, obj, "fheight",   libdisplay_font_height);
  script_add_function(pe, obj, "video",     libdisplay_video);
  script_add_function(pe, obj, "draw",      libdisplay_draw);

  return 0;
}
