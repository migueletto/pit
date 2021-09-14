#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "script.h"
#include "thread.h"
#include "media.h"
#include "ptr.h"
#include "mutex.h"
#include "pit_io.h"
#include "monitor.h"
#include "sync.h"
#include "convert.h"
#include "stream.h"
#include "save.h"
#include "null.h"
#include "mono.h"
#include "pcm.h"
#include "rtpnode.h"
#include "isolate.h"
#include "debug.h"
#include "xalloc.h"

static int libmedia_video_process(media_frame_t *frame, void *data);
static int libmedia_audio_process(media_frame_t *frame, void *data);
static int libmedia_av_destroy(void *data);

static node_dispatch_t video_dispatch = {
  libmedia_video_process,
  NULL,
  NULL,
  libmedia_av_destroy
};

static node_dispatch_t audio_dispatch = {
  libmedia_audio_process,
  NULL,
  NULL,
  libmedia_av_destroy
};

static int libmedia_connect_node(int pe) {
  script_int_t ptr1, ptr2;

  if (script_get_integer(pe, 0, &ptr1) == 0 &&
      script_get_integer(pe, 1, &ptr2) == 0) {

    return script_push_boolean(pe, node_connect(ptr1, ptr2) == 0);
  }

  return -1;
}

static int libmedia_disconnect_node(int pe) {
  script_int_t ptr1, ptr2;

  if (script_get_integer(pe, 0, &ptr1) == 0 &&
      script_get_integer(pe, 1, &ptr2) == 0) {

    return script_push_boolean(pe, node_disconnect(ptr1, ptr2) == 0);
  }

  return -1;
}

static int libmedia_show_node(int pe) {
  script_int_t ptr;
  int show;

  if (script_get_integer(pe, 0, &ptr) == 0 &&
      script_get_boolean(pe, 1, &show) == 0) {

    return script_push_boolean(pe, node_show(ptr, show) == 0);
  }

  return -1;
}

static int libmedia_option_node(int pe) {
  char *name = NULL, *value = NULL;
  script_int_t ptr;
  int r = -1;

  if (script_get_integer(pe, 0, &ptr) == 0 &&
      script_get_string(pe, 1, &name) == 0) {

    if (script_get_string(pe, 2, &value) != 0) {
      value = NULL;
    }

    r = node_option(ptr, name, value);
  }

  if (name) xfree(name);
  if (value) xfree(value);

  return script_push_boolean(pe, r == 0);
}

static int libmedia_destroy_node(int pe) {
  script_int_t ptr;

  if (script_get_integer(pe, 0, &ptr) == 0) {
    return script_push_boolean(pe, node_destroy(ptr) == 0);
  }

  return -1;
}

static int libmedia_av_process(media_frame_t *frame, int type) {
  if (frame->len == 0) {
    return 0;
  }

  if (frame->meta.type != type) {
    return media_frame_put(frame, NULL, 0) == 0 ? 1 : -1;
  }

  return 1;
}

static int libmedia_video_process(media_frame_t *frame, void *data) {
  return libmedia_av_process(frame, FRAME_TYPE_VIDEO);
}

static int libmedia_audio_process(media_frame_t *frame, void *data) {
  return libmedia_av_process(frame, FRAME_TYPE_AUDIO);
}

static int libmedia_av_destroy(void *data) {
  return 0;
}

int libmedia_node_video(int pe) {
  int node, r = -1;

  if ((node = node_create("MEDIA_VIDEO", &video_dispatch, NULL)) != -1) {
    r = script_push_integer(pe, node);
  }

  return r;
}

int libmedia_node_audio(int pe) {
  int node, r = -1;

  if ((node = node_create("MEDIA_AUDIO", &audio_dispatch, NULL)) != -1) { 
    r = script_push_integer(pe, node);
  } 

  return r;
}

static int libmedia_play2(int pe) {
  script_ref_t ref, obj;
  script_arg_t key, value;
  script_int_t handle;
  script_arg_t ret;
  int i, node = 0, err, first, r = -1;

  if (script_get_function(pe, 0, &ref) == 0 &&
      script_get_object(pe, 1, &obj) == 0) {

    for (i = 1, err = 0, first = 0;; i++) {
      key.type = SCRIPT_ARG_INTEGER;
      key.value.i = i;
      if (script_object_get(pe, obj, &key, &value) != 0) break;
      if (value.type == SCRIPT_ARG_NULL) break;

      if (value.type != SCRIPT_ARG_INTEGER) {
        debug(DEBUG_ERROR, "MEDIA", "invalid node argument %d", i);
        err = 1;
        continue;
      }
      if (value.value.i <= 0) {
        debug(DEBUG_ERROR, "MEDIA", "invalid node argument %d", i);
        err = 1;
        continue;
      }

      if (first == 0) {
        first = value.value.i;
      } else {
        node_connect(node, value.value.i);
      }
      node = value.value.i;
    }
    script_remove_ref(pe, obj);

    if (err != 0 || first == 0) {
      if (first != 0) node_destroy_chain(first);
      debug(DEBUG_ERROR, "MEDIA", "invalid node chain");
      script_call(pe, ref, &ret, "I", 0);
    } else {
      if ((handle = media_play(pe, ref, first, 1)) != -1) {
        r = script_push_integer(pe, handle);
      }
    }
  }

  return r;
}

static int libmedia_play(int pe) {
  script_int_t ptr_node, handle;
  script_ref_t ref;
  int r = -1;

  if (script_get_function(pe, 0, &ref) == 0 &&
      script_get_integer(pe, 1, &ptr_node) == 0) {

    if ((handle = media_play(pe, ref, ptr_node, 0)) != -1) {
      r = script_push_integer(pe, handle);
    }
  }

  return r;
}

static int libmedia_stop(int pe) {
  script_int_t handle;
  int r = -1;

  if (script_get_integer(pe, 0, &handle) == 0) {
    if (media_stop(handle) != -1) {
      r = script_push_boolean(pe, 1);
    }
  }

  return r;
}

int libmedia_init(int pe, script_ref_t obj) {
  script_add_function(pe, obj, "connect",    libmedia_connect_node);
  script_add_function(pe, obj, "disconnect", libmedia_disconnect_node);
  script_add_function(pe, obj, "show",       libmedia_show_node);
  script_add_function(pe, obj, "option",     libmedia_option_node);
  script_add_function(pe, obj, "destroy",    libmedia_destroy_node);
  script_add_function(pe, obj, "play",       libmedia_play);
  script_add_function(pe, obj, "play2",      libmedia_play2);
  script_add_function(pe, obj, "stop",       libmedia_stop);

  script_add_function(pe, obj, "monitor",    libmedia_monitor_node);
  script_add_function(pe, obj, "i420",       libmedia_node_i420);
  script_add_function(pe, obj, "yuyv",       libmedia_node_yuyv);
  script_add_function(pe, obj, "gray",       libmedia_node_gray);
  script_add_function(pe, obj, "rgb",        libmedia_node_rgb);
  script_add_function(pe, obj, "rgba",       libmedia_node_rgba);
  script_add_function(pe, obj, "jpeg",       libmedia_node_jpeg);
  script_add_function(pe, obj, "desaturate", libmedia_node_desaturate);
  script_add_function(pe, obj, "stream",     libmedia_node_stream);
  script_add_function(pe, obj, "save",       libmedia_node_save);
  script_add_function(pe, obj, "stdout",     libmedia_node_stdout);
  script_add_function(pe, obj, "send",       libmedia_node_send);
  script_add_function(pe, obj, "isolate",    libmedia_node_isolate);
  script_add_function(pe, obj, "sync",       libmedia_node_sync);
  script_add_function(pe, obj, "rtp",        libmedia_node_rtp);
  script_add_function(pe, obj, "null",       libmedia_node_null);

  script_add_function(pe, obj, "mono",       libmedia_node_mono);
  script_add_function(pe, obj, "pcm_s8",     libmedia_node_pcm_s8);
  script_add_function(pe, obj, "pcm_u8",     libmedia_node_pcm_u8);
  script_add_function(pe, obj, "pcm_s16",    libmedia_node_pcm_s16);
  script_add_function(pe, obj, "pcm_u16",    libmedia_node_pcm_u16);
  script_add_function(pe, obj, "pcm_s32",    libmedia_node_pcm_s32);
  script_add_function(pe, obj, "pcm_u32",    libmedia_node_pcm_u32);
  script_add_function(pe, obj, "pcm_flt",    libmedia_node_pcm_flt);
  script_add_function(pe, obj, "pcm_dbl",    libmedia_node_pcm_dbl);

  script_add_function(pe, obj, "audio",      libmedia_node_audio);
  script_add_function(pe, obj, "video",      libmedia_node_video);

  return 0;
}
