#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "script.h"
#include "thread.h"
#include "io.h"
#include "sys.h"
#include "media.h"
#include "save.h"
#include "debug.h"
#include "xalloc.h"

#define MAX_FILENAME 256
#define MAX_SUFFIX   5

typedef struct {
  FILE *f;
  int fd;
  uint32_t frame;
  char filename[MAX_FILENAME];
  char suffix[MAX_SUFFIX];
} save_node_t;

static int libmedia_save_process(media_frame_t *frame, void *data);
static int libmedia_save_option(char *name, char *value, void *data);
static int libmedia_save_destroy(void *data);

static int libmedia_fd_process(media_frame_t *frame, void *data);

static node_dispatch_t save_dispatch = {
  libmedia_save_process,
  libmedia_save_option,
  NULL,
  libmedia_save_destroy
};

static node_dispatch_t fd_dispatch = {
  libmedia_fd_process,
  NULL,
  NULL,
  libmedia_save_destroy
};

static int libmedia_save_process(media_frame_t *frame, void *_data) {
  save_node_t *data;
  char filename[MAX_FILENAME + 16 + MAX_SUFFIX];
  int n, r = -1;

  if (frame->len == 0) {
    return 0;
  }

  data = (save_node_t *)_data;

  if (data->filename[0] != 0) {
    if (data->suffix[0] == 0 && data->frame == 0) {
      if ((data->f = fopen(data->filename, "w")) == NULL) {
        debug_errno("SAVE", "fopen \"%s\"", data->filename);
      }
    } else if (data->suffix[0] != 0) {
      snprintf(filename, sizeof(filename), "%s%010d.%s", data->filename, data->frame, data->suffix);
      if ((data->f = fopen(filename, "w")) == NULL) {
        debug_errno("SAVE", "fopen \"%s\"", filename);
      }
    }
    data->frame++;
  } else {
    r = 0;
  }

  if (data->f) {
    if ((n = fwrite(frame->frame, 1, frame->len, data->f)) == frame->len) {
      fflush(data->f);
      debug(DEBUG_TRACE, "SAVE", "wrote %d byte(s)", frame->len);
      r = 0;
    } else {
      debug(DEBUG_ERROR, "SAVE", "wrote only %d byte(s) of %d", n, frame->len);
    }

    if (data->suffix[0] != 0) {
      fclose(data->f);
      data->f = NULL;
    }
  } else {
    debug(DEBUG_INFO, "SAVE", "lost %d byte(s) because file is not open", frame->len);
  }

  return r == 0 ? 1 : -1;
}

static int libmedia_save_option(char *name, char *value, void *_data) {
  save_node_t *data;
  int r = -1;

  data = (save_node_t *)_data;

  if (data->filename[0] == 0) {
    if (!strcmp(name, "open") && value && value[0]) {
      if (data->f) {
        fclose(data->f);
      }
      if ((data->f = fopen(value, "w")) == NULL) {
        debug_errno("SAVE", "fopen \"%s\"", value);
      } else {
        r = 0;
      }
    } else if (!strcmp(name, "close")) {
      if (data->f) {
        fclose(data->f);
        data->f = NULL;
      }
      r = 0;
    }
  }

  return r;
}

static int libmedia_save_destroy(void *_data) {
  save_node_t *data;

  data = (save_node_t *)_data;

  if (data) {
    if (data->f) fclose(data->f);
    if (data->fd > 2) sys_close(data->fd);
    xfree(data);
  }

  return 0;
}

int libmedia_node_save(int pe) {
  save_node_t *data;
  char *filename = NULL, *suffix = NULL;
  int r, node;

  r = -1;

  if (script_get_string(pe, 0, &filename) != 0) {
    filename = NULL;
  }

  if (script_get_string(pe, 1, &suffix) != 0) {
    suffix = NULL;
  }

  if ((data = xcalloc(1, sizeof(save_node_t))) != NULL) {
    if (filename) strncpy(data->filename, filename, MAX_FILENAME-1);
    if (suffix) strncpy(data->suffix, suffix, MAX_SUFFIX-1);

    node = node_create("MEDIA_SAVE", &save_dispatch, data);
    if (node != -1) {
      r = script_push_integer(pe, node);
    }
  }

  if (filename) xfree(filename);
  if (suffix) xfree(suffix);

  return r;
}

static int libmedia_fd_process(media_frame_t *frame, void *_data) {
  save_node_t *data;
  int n, r = -1;

  data = (save_node_t *)_data;

  if (frame->len > 0) {
    if ((n = sys_write(data->fd, frame->frame, frame->len)) == frame->len) {
      r = 1;
    } else {
      debug(DEBUG_ERROR, "SAVE", "wrote only %d byte(s) of %d", n, frame->len);
      r = -1;
    }
  } else {
    r = 0;
  }

  return r;
}

int libmedia_node_stdout(int pe) {
  save_node_t *data;
  int node, r = -1;

  if ((data = xcalloc(1, sizeof(save_node_t))) != NULL) {
    data->fd = 1; // stdout
    node = node_create("MEDIA_STDOUT", &fd_dispatch, data);
    if (node != -1) {
      r = script_push_integer(pe, node);
    }
  }

  return r;
}

int libmedia_node_send(int pe) {
  save_node_t *data;
  bt_provider_t *bt;
  script_int_t port;
  char *host = NULL;
  int node, r = -1;

  if (script_get_string(pe, 0, &host) == 0 &&
      script_get_integer(pe, 1, &port) == 0) {

    if ((data = xcalloc(1, sizeof(save_node_t))) != NULL) {
      bt = script_get_pointer(pe, BT_PROVIDER);
      if ((data->fd = io_connect(host, port, bt)) != -1) {
        node = node_create("MEDIA_SEND", &fd_dispatch, data);
        if (node != -1) {
          r = script_push_integer(pe, node);
        }
      }
    }
  }

  if (host) xfree(host);

  return r;
}
