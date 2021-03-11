#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "script.h"
#include "media.h"
#include "debug.h"
#include "xalloc.h"

#include "mono.h"

typedef struct {
  int first;
  int format;
  int frame_size;
} libmedia_mono_t;

static int libmedia_mono_process(media_frame_t *frame, void *data);
static int libmedia_mono_destroy(void *data);

static node_dispatch_t mono_dispatch = {
  libmedia_mono_process,
  NULL,
  NULL,
  libmedia_mono_destroy
};

static int libmedia_mono_process(media_frame_t *frame, void *_data) {
  libmedia_mono_t *data;
  uint8_t *u8;
  int8_t *s8;
  uint16_t *u16;
  int16_t *s16;
  uint32_t *u32;
  int32_t *s32;
  float *flt;
  double *dbl;
  int i, j, n;

  if (frame->len == 0) {
    return 0;
  }

  if (frame->meta.type != FRAME_TYPE_AUDIO) {
    return 1;
  }

  if (frame->meta.av.a.encoding != ENC_PCM) {
    debug(DEBUG_ERROR, "MONO", "invalid encoding %s", audio_encoding_name(frame->meta.av.a.encoding));
    return -1;
  }

  data = (libmedia_mono_t *)_data;

  if (frame->meta.av.a.channels == 1) {
    // nothing to do
    return 1;
  }

  if (data->first) {
    data->first = 0;
    data->format = frame->meta.av.a.pcm;

    switch (frame->meta.av.a.pcm) {
      case PCM_S8:
      case PCM_U8:
        data->frame_size = frame->meta.av.a.channels;
        break;
      case PCM_S16:
      case PCM_U16:
        data->frame_size = frame->meta.av.a.channels * 2;
        break;
      case PCM_S32:
      case PCM_U32:
      case PCM_FLT:
        data->frame_size = frame->meta.av.a.channels * 4;
        break;
      case PCM_DBL:
        data->frame_size = frame->meta.av.a.channels * 8;
        break;
      default:
        return -1;
    }

    debug(DEBUG_INFO, "MONO", "using audio: %s, %d channels, %d Hz", pcm_name(frame->meta.av.a.pcm), frame->meta.av.a.channels, frame->meta.av.a.rate);
  }

  n = frame->len / data->frame_size;

  for (i = 0, j = 0; j < n; j++, i += 2) {
    switch (data->format) {
      case PCM_S8:
        s8 = (int8_t *)frame->frame;
        s8[j] = ((int16_t)s8[i] + (int16_t)s8[i+1]) / 2;
        break;
      case PCM_U8:
        u8 = (uint8_t *)frame->frame;
        u8[j] = ((uint16_t)u8[i] + (uint16_t)u8[i+1]) / 2;
        break;
      case PCM_S16:
        s16 = (int16_t *)frame->frame;
        s16[j] = ((int32_t)s16[i] + (int32_t)s16[i+1]) / 2;
        break;
      case PCM_U16:
        u16 = (uint16_t *)frame->frame;
        u16[j] = ((uint32_t)u16[i] + (uint32_t)u16[i+1]) / 2;
        break;
      case PCM_S32:
        s32 = (int32_t *)frame->frame;
        s32[j] = ((int64_t)s32[i] + (int64_t)s32[i+1]) / 2;
        break;
      case PCM_U32:
        u32 = (uint32_t *)frame->frame;
        u32[j] = ((uint64_t)u32[i] + (uint64_t)u32[i+1]) / 2;
        break;
      case PCM_FLT:
        flt = (float *)frame->frame;
        flt[j] = (flt[i] + flt[i+1]) / 2.0;
        break;
      case PCM_DBL:
        dbl = (double *)frame->frame;
        dbl[j] = (dbl[i] + dbl[i+1]) / 2.0;
        break;
    }
  }

  frame->meta.av.a.channels = 1;
  frame->len /= 2;

  return 1;
}

static int libmedia_mono_destroy(void *_data) {
  libmedia_mono_t *data;

  data = (libmedia_mono_t *)_data;
  xfree(data);

  return 0;
}

int libmedia_node_mono(int pe) {
  libmedia_mono_t *data;
  int node, r = -1;

  if ((data = xcalloc(1, sizeof(libmedia_mono_t))) != NULL) {
    data->first = 1;

    if ((node = node_create("MEDIA_MONO", &mono_dispatch, data)) != -1) {
      r = script_push_integer(pe, node);
    } else {
      xfree(data);
    }
  }

  return r;
}
