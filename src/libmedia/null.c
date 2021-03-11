#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include "script.h"
#include "thread.h"
#include "media.h"
#include "null.h"
#include "debug.h"

static int libmedia_null_process(media_frame_t *frame, void *data);

static node_dispatch_t null_dispatch = {
  libmedia_null_process,
  NULL,
  NULL,
  NULL
};

static int libmedia_null_process(media_frame_t *frame, void *data) {
  return frame->len > 0 ? 1 : 0;
}

int libmedia_node_null(int pe) {
  int node, r = -1;

  if ((node = node_create("MEDIA_NULL", &null_dispatch, NULL)) != -1) {
    r = script_push_integer(pe, node);
  }

  return r;
}
