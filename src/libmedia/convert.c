#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "script.h"
#include "thread.h"
#include "media.h"
#include "yuv.h"
#include "debug.h"
#include "xalloc.h"
#include "convert.h"
#include "jpeg.h"

typedef struct {
  int first, encoding, quality, desaturate;
  unsigned char *yuyv, *rgb, *jpeg;
} convert_node_t;

static int libmedia_convert_process(media_frame_t *frame, void *data);
static int libmedia_convert_destroy(void *data);

static node_dispatch_t convert_dispatch = {
  libmedia_convert_process,
  NULL,
  NULL,
  libmedia_convert_destroy
};

static int libmedia_convert_process(media_frame_t *frame, void *_data) {
  convert_node_t *data;
  int i420_len, yuyv_len, gray_len, rgb_len, rgba_len, rgb565_len;
  unsigned char *buf;
  int len, r = -1;

  if (frame->len == 0) {
    return 0;
  }

  if (frame->meta.type != FRAME_TYPE_VIDEO) {
    return 1;
  }

  data = (convert_node_t *)_data;

  if (data->encoding == 0) {
    // handle the case of desaturate
    data->encoding = frame->meta.av.v.encoding;
  }

  i420_len = (frame->meta.av.v.fwidth * frame->meta.av.v.fheight * 3) / 2;
  yuyv_len = frame->meta.av.v.fwidth * frame->meta.av.v.fheight * 2;
  gray_len = frame->meta.av.v.fwidth * frame->meta.av.v.fheight;
  rgb_len = frame->meta.av.v.fwidth * frame->meta.av.v.fheight * 3;
  rgba_len = frame->meta.av.v.fwidth * frame->meta.av.v.fheight * 4;
  rgb565_len = frame->meta.av.v.fwidth * frame->meta.av.v.fheight * 2;

  if (frame->meta.av.v.encoding != data->encoding) {
    if (data->first) {
      data->yuyv = xmalloc(yuyv_len);
      data->rgb = xmalloc(rgba_len);
      data->first = 0;
    }
    buf = NULL;
    len = -1;

    switch (frame->meta.av.v.encoding) {
        case ENC_I420:
          switch (data->encoding) {
            case ENC_YUYV:
              buf = data->yuyv;
              i420_yuyv(frame->frame, frame->len, buf, frame->meta.av.v.fwidth);
              len = yuyv_len;
              len = frame->meta.av.v.width * frame->meta.av.v.height * 2;
              break;
            case ENC_GRAY:
              buf = data->rgb;
              i420_gray(frame->frame, frame->len, buf);
              len = gray_len;
              break;
            case ENC_RGB:
              i420_yuyv(frame->frame, frame->len, data->yuyv, frame->meta.av.v.fwidth);
              buf = data->rgb;
              len = rgb_len;
              yuyv_rgb(data->yuyv, yuyv_len, buf);
              break;
            case ENC_RGBA:
              i420_yuyv(frame->frame, frame->len, data->yuyv, frame->meta.av.v.fwidth);
              buf = data->rgb;
              len = rgba_len;
              yuyv_rgba(data->yuyv, yuyv_len, buf);
              break;
            case ENC_RGB565:
              i420_yuyv(frame->frame, frame->len, data->yuyv, frame->meta.av.v.fwidth);
              buf = data->rgb;
              len = rgb565_len;
              yuyv_rgb565(data->yuyv, yuyv_len, buf);
              break;
            default:
              buf = NULL;
              len = -1;
          }
          break;

        case ENC_YUYV:
          switch (data->encoding) {
            case ENC_I420:
              buf = data->yuyv;
              yuyv_i420(frame->frame, frame->len, buf, frame->meta.av.v.fwidth);
              len = i420_len;
              break;
            case ENC_GRAY:
              buf = data->rgb;
              yuyv_gray(frame->frame, frame->len, buf);
              len = gray_len;
              break;
            case ENC_RGB:
              buf = data->rgb;
              yuyv_rgb(frame->frame, frame->len, buf);
              len = rgb_len;
              break;
            case ENC_RGBA:
              buf = data->rgb;
              yuyv_rgba(frame->frame, frame->len, buf);
              len = rgba_len;
              break;
            case ENC_RGB565:
              buf = data->rgb;
              yuyv_rgb565(frame->frame, frame->len, buf);
              len = rgb565_len;
              break;
            default:
              buf = NULL;
              len = -1;
          }
          break;

        case ENC_UYVY:
          switch (data->encoding) {
            case ENC_YUYV:
              buf = data->yuyv;
              uyvy_yuyv(frame->frame, frame->len, buf);
              len = frame->len;
              break;
            default:
              buf = NULL;
              len = -1;
          }
          break;

        case ENC_GRAY:
          switch (data->encoding) {
            case ENC_YUYV:
              buf = data->yuyv;
              gray_yuyv(frame->frame, frame->len, buf);
              len = yuyv_len;
              break;
            case ENC_I420:
              gray_yuyv(frame->frame, frame->len, data->yuyv);
              buf = data->rgb;
              len = i420_len;
              yuyv_i420(data->yuyv, yuyv_len, buf, frame->meta.av.v.fwidth);
              break;
            case ENC_RGB:
              buf = data->rgb;
              gray_rgb(frame->frame, frame->len, buf);
              len = rgb_len;
              break;
            case ENC_RGBA:
              buf = data->rgb;
              gray_rgba(frame->frame, frame->len, buf);
              len = rgba_len;
              break;
            default:
              buf = NULL;
              len = -1;
          }
          break;

        case ENC_RGB:
          switch (data->encoding) {
            case ENC_YUYV:
              buf = data->yuyv;
              rgb_yuyv(frame->frame, frame->len, buf);
              len = yuyv_len;
            case ENC_I420:
              rgb_yuyv(frame->frame, frame->len, data->yuyv);
              buf = data->rgb;
              len = i420_len;
              yuyv_i420(data->yuyv, yuyv_len, buf, frame->meta.av.v.fwidth);
              break;
            case ENC_GRAY:
              buf = data->rgb;
              rgb_gray(frame->frame, frame->len, buf);
              len = gray_len;
              break;
            case ENC_RGBA:
              buf = data->rgb;
              rgb_rgba(frame->frame, frame->len, buf);
              len = rgba_len;
              break;
            case ENC_JPEG:
              if (data->jpeg) {
                xfree(data->jpeg);
                data->jpeg = NULL;
              }
              data->jpeg = encode_jpeg(frame->frame, frame->meta.av.v.width, frame->meta.av.v.height, frame->meta.av.v.fwidth, frame->meta.av.v.fheight, data->quality, &len);
              buf = data->jpeg;
              if (buf) {
                frame->meta.av.v.fwidth = frame->meta.av.v.width;  // jpeglib compression always delivers (width x height) pixels
                frame->meta.av.v.fheight = frame->meta.av.v.height;
              } else {
                len = -1;
              }
              break;
            default:
              buf = NULL;
              len = -1;
          }
          break;

        case ENC_RGBA:
          switch (data->encoding) {
            case ENC_YUYV:
              buf = data->yuyv;
              rgba_yuyv(frame->frame, frame->len, buf);
              len = yuyv_len;
              break;
            case ENC_I420:
              rgba_yuyv(frame->frame, frame->len, data->yuyv);
              buf = data->rgb;
              len = i420_len;
              yuyv_i420(data->yuyv, yuyv_len, buf, frame->meta.av.v.fwidth);
              break;
            case ENC_GRAY:
              buf = data->rgb;
              rgba_gray(frame->frame, frame->len, buf);
              len = gray_len;
              break;
            case ENC_RGB:
              buf = data->rgb;
              rgba_rgb(frame->frame, frame->len, buf);
              len = rgb_len;
              break;
            default:
              buf = NULL;
              len = -1;
          }
          break;

        case ENC_BGRA:
          switch (data->encoding) {
            case ENC_RGB:
              buf = data->rgb;
              bgra_rgb(frame->frame, frame->len, buf);
              len = rgb_len;
              break;
            case ENC_RGBA:
              buf = data->rgb;
              bgra_rgba(frame->frame, frame->len, buf);
              len = rgba_len;
              break;
            default:
              buf = NULL;
              len = -1;
          }
          break;

        case ENC_JPEG:
          if (data->encoding == ENC_RGB) {
            unsigned char *jpeg;
            int jpeg_len;
            jpeg = check_huffman_table(frame->frame, frame->len, &jpeg_len);
            buf = decode_jpeg(jpeg, jpeg_len, &len, &frame->meta.av.v.width, &frame->meta.av.v.height);
            if (buf == NULL || len <= 0) {
              buf = NULL;
              len = -1;
            }
            if (jpeg != frame->frame) xfree(jpeg);
          } else {
            buf = NULL;
            len = -1;
          }
          break;

        default:
          buf = NULL;
          len = -1;
    }

    if (buf && len > 0) {
      frame->meta.av.v.encoding = data->encoding;
      r = media_frame_put(frame, buf, len);
    } else {
      debug(DEBUG_ERROR, "CONVERT", "invalid source encoding %s", video_encoding_name(frame->meta.av.v.encoding));
    }

  } else {
    if (data->desaturate) {
      switch (data->encoding) {
        case ENC_I420:
          desaturate_i420(frame->frame, i420_len);
          break;
        case ENC_YUYV:
          desaturate_yuyv(frame->frame, yuyv_len);
      }
      frame->meta.av.v.encoding = data->encoding;
      r = media_frame_put(frame, frame->frame, frame->len);

    } else {
      r = 0;
    }
  }

  return r == 0 ? 1 : -1;
}

static int libmedia_convert_destroy(void *_data) {
  convert_node_t *data;

  data = (convert_node_t *)_data;

  if (data->yuyv) xfree(data->yuyv);
  if (data->rgb) xfree(data->rgb);
  if (data->jpeg) xfree(data->jpeg);
  xfree(data);

  return 0;
}

static int libmedia_node_convert(int pe, char *name, int encoding, int desaturate, int aquality) {
  convert_node_t *data;
  script_int_t node, quality;
  char buf[256];
  int r = -1;

  if (!aquality || script_opt_integer(pe, 0, &quality) != 0) {
    quality = DEFAULT_JPEG_QUALITY;
  }

  if ((data = xcalloc(1, sizeof(convert_node_t))) != NULL) {
    data->encoding = encoding;
    data->desaturate = desaturate;
    data->quality = quality;
    data->first = 1;

    strcpy(buf, "MEDIA_");
    strncat(buf, name, sizeof(buf)-(strlen(buf)+1));
    if ((node = node_create(buf, &convert_dispatch, data)) != -1) {
      r = script_push_integer(pe, node);
    }
  }

  return r;
}

int libmedia_node_i420(int pe) {
  return libmedia_node_convert(pe, video_encoding_name(ENC_I420), ENC_I420, 0, 0);
}

int libmedia_node_yuyv(int pe) {
  return libmedia_node_convert(pe, video_encoding_name(ENC_YUYV), ENC_YUYV, 0, 0);
}

int libmedia_node_gray(int pe) {
  return libmedia_node_convert(pe, video_encoding_name(ENC_GRAY), ENC_GRAY, 0, 0);
}

int libmedia_node_rgb(int pe) {
  return libmedia_node_convert(pe, video_encoding_name(ENC_RGB), ENC_RGB, 0, 0);
}

int libmedia_node_rgba(int pe) {
  return libmedia_node_convert(pe, video_encoding_name(ENC_RGBA), ENC_RGBA, 0, 0);
}

int libmedia_node_rgb565(int pe) {
  return libmedia_node_convert(pe, video_encoding_name(ENC_RGB565), ENC_RGB565, 0, 0);
}

int libmedia_node_jpeg(int pe) {
  return libmedia_node_convert(pe, video_encoding_name(ENC_JPEG), ENC_JPEG, 0, 1);
}

int libmedia_node_desaturate(int pe) {
  return libmedia_node_convert(pe, "DESAT", 0, 1, 0);
}
