#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>

#include "script.h"
#include "thread.h"
#include "media.h"
#include "delta.h"
#include "yuv.h"
#include "sys.h"
#include "monitor.h"
#include "debug.h"
#include "xalloc.h"

typedef struct {
  script_real_t factor;
  int pe;
  script_ref_t ref;
  delta_t *delta;
  unsigned char *i420;
  unsigned char *gray;
  int height, width;
  int i420_len, encoding;
  int first, recording;
  int start_rec, max_rec;
  int start_delay, max_delay;
  int64_t t0, dt;
} monitor_node_t;

static int libmedia_monitor_process(media_frame_t *frame, void *data);
static int libmedia_monitor_destroy(void *data);

static node_dispatch_t monitor_dispatch = {
  libmedia_monitor_process,
  NULL,
  NULL,
  libmedia_monitor_destroy
};

static int libmedia_monitor_process(media_frame_t *frame, void *_data) {
  script_arg_t ret;
  monitor_node_t *data;
  unsigned char *gray;
  int stop_max, stop_delay;
  int delta, now, r;

  if (frame->len == 0) {
    return 0;
  }

  if (frame->meta.type != FRAME_TYPE_VIDEO) {
    return 1;
  }

  data = (monitor_node_t *)_data;

  if (data->first) {
    data->first = 0;

    switch (frame->meta.av.v.encoding) {
      case ENC_YUYV:
      case ENC_I420:
      case ENC_GRAY:
      case ENC_RGB:
      case ENC_RGBA:
        data->encoding = frame->meta.av.v.encoding;
        break;
      default:
        debug(DEBUG_ERROR, "MONITOR", "invalid source encoding %s", video_encoding_name(frame->meta.av.v.encoding));
        return -1;
    }

    data->width = frame->meta.av.v.width;
    data->height = frame->meta.av.v.height;
    data->i420_len = (frame->meta.av.v.width * frame->meta.av.v.height * 3) / 2;

    data->i420 = xmalloc(data->i420_len);
    data->gray = xmalloc(frame->meta.av.v.fwidth * frame->meta.av.v.fheight);
    data->delta = delta_init(frame->meta.av.v.fwidth, frame->meta.av.v.fheight);

    if (data->i420 == NULL || data->gray == NULL || data->delta == NULL) {
      if (data->i420) xfree(data->i420);
      if (data->gray) xfree(data->gray);
      if (data->delta) delta_close(data->delta);
      data->i420 = NULL;
      data->gray = NULL;
      data->delta = NULL;
      debug(DEBUG_ERROR, "MONITOR", "initialization failed");
      return -1;
    }
  }

  if (frame->meta.av.v.encoding != data->encoding) {
    debug(DEBUG_ERROR, "MONITOR", "encoding changed from %s to %s", video_encoding_name(data->encoding), video_encoding_name(frame->meta.av.v.encoding));
    return -1;
  }

  gray = NULL;

  switch (data->encoding) {
    case ENC_YUYV:
      yuyv_i420(frame->frame, frame->len, data->i420, data->width);
      i420_gray(data->i420, data->i420_len, data->gray);
      gray = data->gray;
      break;
    case ENC_I420:
      i420_gray(frame->frame, frame->len, data->gray);
      gray = data->gray;
      break;
    case ENC_GRAY:
      gray = frame->frame;
      break;
    case ENC_RGB:
      rgb_gray(frame->frame, frame->len, data->gray);
      gray = data->gray;
      break;
    case ENC_RGBA:
      rgba_gray(frame->frame, frame->len, data->gray);
      gray = data->gray;
  }

  if (gray) {
    delta = delta_image(data->delta, gray, data->factor) > 0;

    if (!data->recording) {
      // not recording yet

      if (delta) {
        // frame changed, start recording
        data->recording = 1;
        data->start_rec = sys_get_clock() / 1000;
        data->start_delay = 0;
        if (data->t0) {
          data->dt += (frame->ts - data->t0);
          debug(1, "MONITOR", "dt = %lld", data->dt);
        }

        if (script_call(data->pe, data->ref, &ret, "B", 1) == 0) {
          r = 1;
        } else {
          r = -1;
        }
      } else {
        // nothing to show
        frame->len = 0;
        r = 0;
      }

    } else {
      // recording already in progress
      now = sys_get_clock() / 1000;

      stop_max = (now - data->start_rec) > data->max_rec;
      stop_delay = data->start_delay && (now - data->start_delay) > data->max_delay;

      if (stop_max || stop_delay) {
        // stop recording
        data->recording = 0;
        data->t0 = frame->ts;

        if (script_call(data->pe, data->ref, &ret, "B", 0) == 0) {
          // nothing to show
          frame->len = 0;
          r = 0;
        } else {
          r = -1;
        }
      } else {
        r = 1;
      }

      if (delta) {
        // frame changed
        data->start_delay = 0;

      } else {
        // frame did not change
        if (data->start_delay == 0) {
          data->start_delay = now;
        }
      }
    }

    // XXX to debug what delta is seeing
    //if (r > 0) delta_profile(data->delta, *buf);
  } else {
    r = -1;
  }

  if (r == 1) {
    frame->ts -= data->dt;
  }

  return r;
}

static int libmedia_monitor_destroy(void *_data) {
  monitor_node_t *data;

  data = (monitor_node_t *)_data;

  if (data->i420) xfree(data->i420);
  if (data->gray) xfree(data->gray);
  if (data->delta) delta_close(data->delta);
  script_remove_ref(data->pe, data->ref);
  xfree(data);

  return 0;
}

int libmedia_monitor_node(int pe) {
  monitor_node_t *data;
  script_int_t max_rec, max_delay, node;
  script_real_t factor;
  script_ref_t ref;
  int r = -1;

  if (script_get_function(pe, 0, &ref) == 0 &&
      script_get_real(pe, 1, &factor) == 0 &&
      script_get_integer(pe, 2, &max_rec) == 0 &&
      script_get_integer(pe, 3, &max_delay) == 0) {

    if ((data = xcalloc(1, sizeof(monitor_node_t))) != NULL) {
      data->first = 1;
      data->factor = factor;
      data->max_rec = max_rec;
      data->max_delay = max_delay;
      data->pe = pe;
      data->ref = ref;

      if ((node = node_create("MONIT", &monitor_dispatch, data)) != -1) {
        r = script_push_integer(pe, node);
      }
    }
  }

  return r;
}
