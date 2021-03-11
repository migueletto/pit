#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "script.h"
#include "media.h"
#include "debug.h"
#include "xalloc.h"

#include "pcm.h"

typedef struct {
  int first;
  int src_format, dst_format;
  int src_bits, dst_bits;
  unsigned char *dst;
  int dst_len;
} libmedia_pcm_t;

static int libmedia_pcm_process(media_frame_t *frame, void *data);
static int libmedia_pcm_destroy(void *data);

static node_dispatch_t pcm_dispatch = {
  libmedia_pcm_process,
  NULL,
  NULL,
  libmedia_pcm_destroy
};

static int libmedia_pcm_process(media_frame_t *frame, void *_data) {
  libmedia_pcm_t *data;
  unsigned char *dst;
  uint8_t *u8;
  int8_t *s8;
  uint16_t *u16;
  int16_t *s16;
  uint32_t *u32;
  int32_t *s32;
  float *flt;
  double *dbl;
  int i, n, len;

  if (frame->len == 0) {
    return 0;
  }

  if (frame->meta.type != FRAME_TYPE_AUDIO) {
    return 1;
  }

  if (frame->meta.av.a.encoding != ENC_PCM) {
    debug(DEBUG_ERROR, "PCM", "invalid encoding %s", audio_encoding_name(frame->meta.av.a.encoding));
    return -1;
  }

  data = (libmedia_pcm_t *)_data;

  if (frame->meta.av.a.pcm == data->dst_format) {
    // nothing to do
    return 1;
  }

  if (data->first) {
    data->first = 0;
    data->src_format = frame->meta.av.a.pcm;

    switch (frame->meta.av.a.pcm) {
      case PCM_S8:
      case PCM_U8:
        data->src_bits = 8;
        break;
      case PCM_S16:
      case PCM_U16:
        data->src_bits = 16;
        break;
      case PCM_S32:
      case PCM_U32:
      case PCM_FLT:
        data->src_bits = 32;
        break;
      case PCM_DBL:
        data->src_bits = 64;
        break;
      default:
        debug(DEBUG_ERROR, "PCM", "PCM %s is not supported", pcm_name(frame->meta.av.a.pcm));
        return -1;
    }

    debug(DEBUG_INFO, "PCM", "converting from %s to %s", pcm_name(data->src_format), pcm_name(data->dst_format));
  }

  if (data->dst_bits > data->src_bits) {
    len = frame->len * (data->dst_bits / data->src_bits);
    if (data->dst == NULL) {
      data->dst = xcalloc(1, len);
      data->dst_len = len;
    } else if (len > data->dst_len) {
      data->dst = xrealloc(data->dst, len);
      data->dst_len = len;
    }
    dst = data->dst;
  } else {
    dst = frame->frame;
  }

  n = frame->len / (data->src_bits / 8);

  for (i = 0; i < n; i += frame->meta.av.a.channels) {
    switch (data->src_format) {
      case PCM_S8:
        s8 = (int8_t *)frame->frame;
        switch (data->dst_format) {
          case PCM_U8:
            u8 = (uint8_t *)dst;
            u8[i] = (uint8_t)(((int16_t)s8[i]) + 128);
            if (frame->meta.av.a.channels == 2) u8[i+1] = (uint8_t)(((int16_t)s8[i+1]) + 128);
            break;
          case PCM_S16:
            s16 = (int16_t *)dst;
            s16[i] = ((int16_t)s8[i]) << 8;
            if (frame->meta.av.a.channels == 2) s16[i+1] = ((int16_t)s8[i+1]) << 8;
            break;
          case PCM_U16:
            u16 = (uint16_t *)dst;
            u16[i] = ((uint16_t)(((int16_t)s8[i]) + 128)) << 8;
            if (frame->meta.av.a.channels == 2) u16[i+1] = ((uint16_t)(((int16_t)s8[i+1]) + 128)) << 8;
            break;
          case PCM_S32:
            s32 = (int32_t *)dst;
            s32[i] = ((int32_t)s8[i]) << 24;
            if (frame->meta.av.a.channels == 2) s32[i+1] = ((int32_t)s8[i+1]) << 24;
            break;
          case PCM_U32:
            u32 = (uint32_t *)dst;
            u32[i] = ((uint32_t)(((int32_t)s8[i]) + 128)) << 24;
            if (frame->meta.av.a.channels == 2) u32[i+1] = ((uint32_t)(((int32_t)s8[i+1]) + 128)) << 24;
            break;
          case PCM_FLT:
            flt = (float *)dst;
            flt[i] = ((float)s8[i]) / 128.0;
            if (frame->meta.av.a.channels == 2) flt[i+1] = ((float)s8[i+1]) / 128.0;
            break;
          case PCM_DBL:
            dbl = (double *)dst;
            dbl[i] = ((double)s8[i]) / 128.0;
            if (frame->meta.av.a.channels == 2) dbl[i+1] = ((double)s8[i+1]) / 128.0;
            break;
          default:
            return -1;
        }
        break;
      case PCM_U8:
        u8 = (uint8_t *)frame->frame;
        switch (data->dst_format) {
          case PCM_S8:
            s8 = (int8_t *)dst;
            s8[i] = (int8_t)(((int16_t)u8[i]) - 128);
            if (frame->meta.av.a.channels == 2) s8[i+1] = (int8_t)(((int16_t)u8[i+1]) - 128);
            break;
          case PCM_S16:
            s16 = (int16_t *)dst;
            s16[i] = (((int16_t)u8[i]) - 128) << 8;
            if (frame->meta.av.a.channels == 2) s16[i+1] = (((int16_t)u8[i+1]) - 128) << 8;
            break;
          case PCM_U16:
            u16 = (uint16_t *)dst;
            u16[i] = ((uint16_t)u8[i]) << 8;
            if (frame->meta.av.a.channels == 2) u16[i+1] = ((uint16_t)u8[i+1]) << 8;
            break;
          case PCM_S32:
            s32 = (int32_t *)dst;
            s32[i] = (((int32_t)u8[i]) - 128) << 24;
            if (frame->meta.av.a.channels == 2) s32[i+1] = (((int32_t)u8[i+1]) - 128) << 24;
            break;
          case PCM_U32:
            u32 = (uint32_t *)dst;
            u32[i] = ((uint32_t)u8[i]) << 24;
            if (frame->meta.av.a.channels == 2) u32[i+1] = ((uint32_t)u8[i+1]) << 24;
            break;
          case PCM_FLT:
            flt = (float *)dst;
            flt[i] = (((float)u8[i]) - 128.0) / 128.0;
            if (frame->meta.av.a.channels == 2) flt[i+1] = (((float)u8[i+1]) - 128.0) / 128.0;
            break;
          case PCM_DBL:
            dbl = (double *)dst;
            dbl[i] = (((double)u8[i]) - 128.0) / 128.0;
            if (frame->meta.av.a.channels == 2) dbl[i+1] = (((double)u8[i+1]) - 128.0) / 128.0;
            break;
          default:
            return -1;
        }
        break;
      case PCM_S16:
        s16 = (int16_t *)frame->frame;
        switch (data->dst_format) {
          case PCM_S32:
            s32 = (int32_t *)dst;
            s32[i] = ((int32_t)s16[i]) << 16;
            if (frame->meta.av.a.channels == 2) s32[i+1] = ((int32_t)s16[i+1]) << 16;
            break;
          case PCM_FLT:
            flt = (float *)dst;
            flt[i] = ((float)s16[i]) / 32768.0;
            if (frame->meta.av.a.channels == 2) flt[i+1] = ((float)s16[i+1]) / 32768.0;
            break;
          default:
            return -1;
        }
        break;
      case PCM_S32:
        s32 = (int32_t *)frame->frame;
        switch (data->dst_format) {
          case PCM_S16:
            s16 = (int16_t *)dst;
            s16[i] = s32[i] >> 16;
            if (frame->meta.av.a.channels == 2) s16[i+1] = s32[i+1] >> 16;
            break;
          default:
            return -1;
        }
        break;
      case PCM_FLT:
        flt = (float *)frame->frame;
        switch (data->dst_format) {
          case PCM_S16:
            s16 = (int16_t *)dst;
            s16[i] = (int16_t)(flt[i] * 32768.0);
            if (frame->meta.av.a.channels == 2) s16[i+1] = (int16_t)(flt[i+1] * 32768.0);
            break;
          default:
            return -1;
        }
        break;
    }
  }

  frame->meta.av.a.pcm = data->dst_format;

  if (dst == frame->frame) {
    frame->len = n * (data->dst_bits / 8);
  } else {
    if (media_frame_put(frame, dst, n * (data->dst_bits / 8)) != 0) {
      return -1;
    }
  }

  return 1;
}

static int libmedia_pcm_destroy(void *_data) {
  libmedia_pcm_t *data;

  data = (libmedia_pcm_t *)_data;
  if (data->dst) free(data->dst);
  xfree(data);

  return 0;
}

static int libmedia_node_pcm(int pe, int format, int bits) {
  libmedia_pcm_t *data;
  int node, r = -1;

  if ((data = xcalloc(1, sizeof(libmedia_pcm_t))) != NULL) {
    data->first = 1;
    data->dst_format = format;
    data->dst_bits = bits;

    if ((node = node_create("MEDIA_PCM", &pcm_dispatch, data)) != -1) {
      r = script_push_integer(pe, node);
    } else {
      xfree(data);
    }
  }

  return r;
}

int libmedia_node_pcm_s8(int pe) {
  return libmedia_node_pcm(pe, PCM_S8, 8);
}

int libmedia_node_pcm_u8(int pe) {
  return libmedia_node_pcm(pe, PCM_U8, 8);
}

int libmedia_node_pcm_s16(int pe) {
  return libmedia_node_pcm(pe, PCM_S16, 16);
}

int libmedia_node_pcm_u16(int pe) {
  return libmedia_node_pcm(pe, PCM_U16, 16);
}

int libmedia_node_pcm_s32(int pe) {
  return libmedia_node_pcm(pe, PCM_S32, 32);
}

int libmedia_node_pcm_u32(int pe) {
  return libmedia_node_pcm(pe, PCM_U32, 32);
}

int libmedia_node_pcm_flt(int pe) {
  return libmedia_node_pcm(pe, PCM_FLT, 32);
}

int libmedia_node_pcm_dbl(int pe) {
  return libmedia_node_pcm(pe, PCM_DBL, 64);
}
