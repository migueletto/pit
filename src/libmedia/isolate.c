#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "script.h"
#include "sys.h"
#include "media.h"
#include "debug.h"
#include "xalloc.h"

#include "isolate.h"

#define THRESHOLD     512
#define WBUF_LEN      2048
#define MAX_FILENAME  256

typedef struct {
  int pe;
  script_ref_t ref;
  char dir[MAX_FILENAME];
  int state;
  int threshold;
  int16_t *buf;
  int64_t pos, last_saved;
  int32_t len;
  uint32_t nsound, nsilence;
  uint32_t seq;
  int16_t wbuf[WBUF_LEN];
  uint32_t iwbuf, samples;
  char filename[MAX_FILENAME+32];
  int fd;
} libmedia_isolate_t;

static int libmedia_isolate_process(media_frame_t *frame, void *data);
static int libmedia_isolate_option(char *name, char *value, void *data);
static int libmedia_isolate_destroy(void *data);

static node_dispatch_t isolate_dispatch = {
  libmedia_isolate_process,
  libmedia_isolate_option,
  NULL,
  libmedia_isolate_destroy
};

static int64_t POS(libmedia_isolate_t *data, int64_t i) {
  i += data->pos;
  if (i < 0) i += data->len;
  return i % data->len;
}

static int write_sample(libmedia_isolate_t *data, int16_t s) {
  data->wbuf[data->iwbuf++] = s;
  if (data->iwbuf == WBUF_LEN) {
    if (sys_write(data->fd, (uint8_t *)data->wbuf, WBUF_LEN * sizeof(int16_t)) != WBUF_LEN * sizeof(int16_t)) {
      debug_errno("ISOLATE", "write");
      return -1;
    }
    data->samples += data->iwbuf;
    data->iwbuf = 0;
  }

  return 0;
}

static int flush_buffer(libmedia_isolate_t *data) {
  script_arg_t ret;

  if (data->fd) {
    if (data->iwbuf) {
      if (sys_write(data->fd, (uint8_t *)data->wbuf, data->iwbuf * sizeof(int16_t)) != data->iwbuf * sizeof(int16_t)) {
        debug_errno("ISOLATE", "write");
        sys_close(data->fd); 
        data->fd = 0;
        data->iwbuf = 0;
        data->filename[0] = 0;
        data->samples = 0;
        return -1;
      }
      data->samples += data->iwbuf;
      data->iwbuf = 0;
    }

    sys_close(data->fd); 
    data->fd = 0;

    if (script_call(data->pe, data->ref, &ret, "SI", data->filename, mkint(data->samples)) == -1) {
      data->filename[0] = 0;
      data->samples = 0;
      return -1;
    }
    data->filename[0] = 0;
    data->samples = 0;
    if (script_returned_value(&ret)) {
      return -1;
    }
  }

  return 0;
}

static int libmedia_isolate_process(media_frame_t *frame, void *_data) {
  libmedia_isolate_t *data;
  int16_t *buf, s;
  int i, j, samples;

  data = (libmedia_isolate_t *)_data;

  if (frame->len == 0) {
    return flush_buffer(data);
  }

  if (frame->meta.type != FRAME_TYPE_AUDIO) {
    return 1;
  }

  if (frame->meta.av.a.encoding != ENC_PCM) {
    debug(DEBUG_ERROR, "ISOLATE", "invalid encoding %s", audio_encoding_name(frame->meta.av.a.encoding));
    return -1;
  }

  if (frame->meta.av.a.pcm != PCM_S16) {
    debug(DEBUG_ERROR, "ISOLATE", "sample must be %s (got %s)", pcm_name(PCM_S16), pcm_name(frame->meta.av.a.pcm));
    return -1;
  }

  if (frame->meta.av.a.channels != 1) {
    debug(DEBUG_ERROR, "ISOLATE", "channels must be 1 (got %d)", frame->meta.av.a.channels);
    return -1;
  }

  if (data->buf == NULL) {
    data->len = frame->meta.av.a.rate / 4;
    if ((data->buf = xcalloc(data->len, sizeof(int16_t))) == NULL) return -1;
    data->nsilence = data->len;
  }

  samples = frame->len / sizeof(int16_t);
  buf = (int16_t *)frame->frame;
  debug(DEBUG_TRACE, "ISOLATE", "samples %d, sound %d, silence %d", samples, data->nsound, data->nsilence);

  for (i = 0; i < samples; i++, data->pos++) {

    data->buf[POS(data, 0)] = buf[i];
    if (buf[i] <= -data->threshold || buf[i] >= data->threshold) {
      data->nsound++;
    } else {
      data->nsilence++;
    }
    s = data->buf[POS(data, -data->len+1)];
    if (s <= -data->threshold || s >= data->threshold) {
      data->nsound--;
    } else {
      data->nsilence--;
    }

    switch (data->state) {
      case 1:  // looking for sound
        if (data->nsound >= data->nsilence) {
          debug(DEBUG_TRACE, "ISOLATE", "sound start");
          snprintf(data->filename, MAX_FILENAME+32-1, "%s/%06u.raw", data->dir, data->seq++);
          if ((data->fd = sys_create(data->filename, SYS_WRITE | SYS_TRUNC, 0644)) != -1) {
            for (j = data->len-1; j >= 0; j--) {
              if ((data->pos-j) > data->last_saved) {
                if (write_sample(data, data->buf[POS(data, -j)]) == -1) return -1;
              }
            }
            data->last_saved = data->pos;
          } else {
            debug_errno("ISOLATE", "create \"%s\"", data->filename);
            return -1;
          }
          data->state = 2;
        }
        break;
      case 2:  // looking for silence
        if (write_sample(data, buf[i]) == -1) return -1;
        data->last_saved = data->pos;
        if (data->nsound < data->nsilence) {
          debug(DEBUG_TRACE, "ISOLATE", "sound end");
          if (flush_buffer(data) == -1) return -1;
          data->state = 1;
        }
        break;
    }
  }

  return 1;
}

static int libmedia_isolate_option(char *name, char *value, void *_data) {
  libmedia_isolate_t *data;
  int r = -1;

  data = (libmedia_isolate_t *)_data;

  if (!strcmp(name, "threshold")) {
    data->threshold = atoi(value);
    r = 0;
  }

  return r;
}

static int libmedia_isolate_destroy(void *_data) {
  libmedia_isolate_t *data;

  data = (libmedia_isolate_t *)_data;
  flush_buffer(data);
  if (data->buf) xfree(data->buf);
  script_remove_ref(data->pe, data->ref);
  xfree(data);

  return 0;
}

int libmedia_node_isolate(int pe) {
  libmedia_isolate_t *data;
  script_ref_t ref;
  char *dir = NULL;
  int node, r = -1;

  if (script_get_function(pe, 0, &ref) == 0 &&
      script_get_string(pe, 1, &dir) == 0) {

    if ((data = xcalloc(1, sizeof(libmedia_isolate_t))) != NULL) {
      strncpy(data->dir, dir, MAX_FILENAME-1);
      data->pe = pe;
      data->ref = ref;
      data->threshold = THRESHOLD;
      data->state = 1;

      if ((node = node_create("MEDIA_ISOLATE", &isolate_dispatch, data)) != -1) {
        r = script_push_integer(pe, node);
      } else {
        xfree(data);
      }
    }
  }

  if (dir) xfree(dir);

  return r;
}
