#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>

#include "script.h"
#include "media.h"
#include "sys.h"
#include "debug.h"
#include "xalloc.h"

#include "sync.h"

typedef struct {
  int64_t t0;
  int64_t other_ts, my_ts;
  int ptr;
} libmedia_sync_t;

static int libmedia_sync_process(media_frame_t *frame, void *data);
static int libmedia_sync_destroy(void *data);

static node_dispatch_t sync_dispatch = {
  libmedia_sync_process,
  NULL,
  NULL,
  libmedia_sync_destroy
};

static int libmedia_sync_process(media_frame_t *frame, void *_data) {
  libmedia_sync_t *data;
  int64_t ts, dt;

  if (frame->len == 0) {
    return 0;
  }

  data = (libmedia_sync_t *)_data;

  if (data->ptr <= 0) {
    ts = sys_get_clock() - data->t0;
  } else {
    if (data->other_ts == 0) {
      data->other_ts = node_sync(data->ptr);
      data->my_ts = sys_get_clock();
    }
    if (data->other_ts == 0) {
      ts = frame->ts;
    } else {
      ts = data->other_ts + (sys_get_clock() - data->my_ts);
    }
  }

  dt = frame->ts - ts;
  if (dt > 150) {
    debug(DEBUG_TRACE, "SYNC", "usleep %llu", dt);
    sys_usleep(dt);
  }

  return 1;
}

static int libmedia_sync_destroy(void *_data) {
  libmedia_sync_t *data;

  data = (libmedia_sync_t *)_data;
  xfree(data);

  return 0;
}

int libmedia_node_sync(int pe) {
  libmedia_sync_t *data;
  script_int_t ptr;
  int node, r = -1;

  if (script_get_integer(pe, 0, &ptr) == 0) {
    if ((data = xcalloc(1, sizeof(libmedia_sync_t))) != NULL) {
      data->t0 = sys_get_clock();
      data->ptr = ptr;

      if ((node = node_create("MEDIA_SYNC", &sync_dispatch, data)) != -1) {
        r = script_push_integer(pe, node);
      } else {
        xfree(data);
      }
    }
  }

  return r;
}
