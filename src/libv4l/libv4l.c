#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <errno.h>

#include "script.h"
#include "thread.h"
#include "mutex.h"
#include "sys.h"
#include "media.h"
#include "yuv.h"
#include "ptr.h"
#include "iterator.h"
#include "debug.h"
#include "xalloc.h"

#define NAME "V4L"

#define NUM_BUFFERS 4

#define IO_METHOD_READ    0
#define IO_METHOD_MMAP    1
#define IO_METHOD_USERPTR 2

#define V4L_NUM_OPTIONS (V4L2_CID_LASTP1 - V4L2_CID_BASE)
#define MAX_BUF 32

typedef struct cam_buffer {
  unsigned char *start;
  size_t length;
} cam_buffer;

typedef struct {
  int id;
  int min, max;
} v4l_option_t;

typedef struct {
  mutex_t *mutex;
  char *device;
  int first;
  int fd;
  int io;
  int encoding;
  int pixelformat;
  int fps;
  cam_buffer *buffers;
  unsigned int n_buffers;
  int width, height;
  int input;
  unsigned char *black;
  int blacklen;
  int started;
  int64_t t0;
  struct v4l2_capability cap;
  iterator_t iterator;
  int it;
  int current_option;
  int num_options;
  char option_value[MAX_BUF];
  v4l_option_t options[V4L_NUM_OPTIONS];
} libv4l_t;

static int v4l_node_process(media_frame_t *frame, void *data);
static int v4l_node_option(char *name, char *value, void *data);
static int v4l_node_destroy(void *data);

static node_dispatch_t v4l_dispatch = {
  v4l_node_process,
  v4l_node_option,
  NULL,
  v4l_node_destroy
};

static char *v4l_op_name[V4L_NUM_OPTIONS + 1];

static int xioctl(int fd, int request, void *arg, int print_err) {
  int r;

  do {
    r = ioctl(fd, request, arg);
  } while (r == -1 && errno == EINTR);

  if (r == -1 && print_err) {
    debug_errno("V4L", "ioctl");
  }

  return r;
}

// return: -1=error, 0=no input, 1=input
static int v4l_cam_select(libv4l_t *data) {
  struct timeval tv;
  fd_set fds, efds;
  int nfds, n, r;

  r = -1;

  if (data->fd != -1) {
    tv.tv_sec = 0;
    tv.tv_usec = 500000;

    nfds = data->fd + 1;
    FD_ZERO(&fds);
    FD_ZERO(&efds);
    FD_SET(data->fd, &fds);
    FD_SET(data->fd, &efds);
    n = select(nfds, &fds, NULL, &efds, &tv);

    if (n == 0) {
      debug(DEBUG_ERROR, "V4L", "select timeout");
      r = 0;
    } if (n > 0) {
      r = 1;
    } else {
      debug_errno("V4L", "select");
    }
  } else {
    debug(DEBUG_ERROR, "V4L", "select on fd -1");
  }

  return r;
}

static int v4l_cam_start(libv4l_t *data) {
  unsigned int i;
  enum v4l2_buf_type type;
  struct v4l2_buffer buf;
  int r = 0;

  switch (data->io) {
    case IO_METHOD_READ:
      break;

    case IO_METHOD_MMAP:
      for (i = 0; i < data->n_buffers; ++i) {
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
  
        if (xioctl(data->fd, VIDIOC_QBUF, &buf, 1) == -1) {
          debug(DEBUG_ERROR, "V4L", "VIDIOC_QBUF failed (%d / %d) on cam_start", i, data->n_buffers);
          r = -1;
          break;
        }
      }
      if (r == -1) break;
    
      type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      if (xioctl(data->fd, VIDIOC_STREAMON, &type, 1) == -1) {
        debug(DEBUG_ERROR, "V4L", "VIDIOC_STREAMON failed");
        r = -1;
      }
      break;

    case IO_METHOD_USERPTR:
      for (i = 0; i < data->n_buffers; ++i) {
        memset(&buf, 0, sizeof(buf));
        buf.type      = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory    = V4L2_MEMORY_USERPTR;
        buf.index     = i;
        buf.m.userptr = (unsigned long)data->buffers[i].start;
        buf.length    = data->buffers[i].length;

        if (xioctl(data->fd, VIDIOC_QBUF, &buf, 1) == -1) {
          debug(DEBUG_ERROR, "V4L", "VIDIOC_QBUF failed (%d / %d) on cam_start", i, data->n_buffers);
          r = -1;
          break;
        }
      }
      if (r == -1) break;

      type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      if (xioctl(data->fd, VIDIOC_STREAMON, &type, 1) == -1) {
        debug(DEBUG_ERROR, "V4L", "VIDIOC_STREAMON failed");
        r = -1;
      }
      break;
  }

  if (r == 0) {
    debug(DEBUG_INFO, "V4L", "camera start");
  }

  return r;
}

static int v4l_cam_stop(libv4l_t *data) {
  enum v4l2_buf_type type;
  int r = 0;

  switch (data->io) {
    case IO_METHOD_READ:
      break;

    case IO_METHOD_MMAP:
    case IO_METHOD_USERPTR:
      type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      if (xioctl(data->fd, VIDIOC_STREAMOFF, &type, 1) == -1) {
        debug(DEBUG_ERROR, "V4L", "VIDIOC_STREAMOFF failed");
        r = -1;
      }
      break;
  }

  if (r == 0) {
    debug(DEBUG_INFO, "V4L", "camera stop");
  }

  return r;
}

static int init_read(libv4l_t *data, unsigned int buffer_size) {
  data->n_buffers = 1;
  data->buffers = xcalloc(data->n_buffers, sizeof(cam_buffer));

  if (!data->buffers) {
    debug(DEBUG_ERROR, "V4L", "out of memory");
    return -1;
  }

  data->buffers[0].length = buffer_size;
  data->buffers[0].start = xmalloc(buffer_size);

  if (!data->buffers[0].start) {
    debug(DEBUG_ERROR, "V4L", "out of memory");
    return -1;
  }

  debug(DEBUG_ERROR, "V4L", "read using %d buffer(s)", data->n_buffers);

  return 0;
}

static int init_mmap(libv4l_t *data) {
  struct v4l2_requestbuffers req;
  struct v4l2_buffer buf;

  memset(&req, 0, sizeof(req));
  req.count  = NUM_BUFFERS;
  req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;

  if (xioctl(data->fd, VIDIOC_REQBUFS, &req, 1) == -1) {
    debug(DEBUG_ERROR, "V4L", "VIDIOC_REQBUFS failed");
    return -1;
  }

  if (req.count < 2) {
    debug(DEBUG_ERROR, "V4L", "insufficient buffer memory on device");
    return -1;
  }

  data->buffers = xcalloc(req.count, sizeof(cam_buffer));
  if (!data->buffers) {
    debug(DEBUG_ERROR, "V4L", "out of memory");
    return -1;
  }

  for (data->n_buffers = 0; data->n_buffers < req.count; data->n_buffers++) {
    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index  = data->n_buffers;

    if (xioctl(data->fd, VIDIOC_QUERYBUF, &buf, 1) == -1) {
      debug(DEBUG_ERROR, "V4L", "VIDIOC_QUERYBUF failed");
      return -1;
    }

    data->buffers[data->n_buffers].length = buf.length;
    data->buffers[data->n_buffers].start =
        mmap(NULL /* start anywhere */,
        buf.length,
        PROT_READ | PROT_WRITE /* required */,
        MAP_SHARED /* recommended */,
        data->fd, buf.m.offset);

    if (data->buffers[data->n_buffers].start == MAP_FAILED) {
      debug(DEBUG_ERROR, "V4L", "mmap failed");
      return -1;
    }
  }

  debug(DEBUG_INFO, "V4L", "mmap using %d buffer(s)", data->n_buffers);

  return 0;
}

static int init_userp(libv4l_t *data, unsigned int buffer_size) {
  struct v4l2_requestbuffers req;
  unsigned int page_size;

  page_size = getpagesize ();
  buffer_size = (buffer_size + page_size - 1) & ~(page_size - 1);

  memset(&req, 0, sizeof(req));
  req.count  = NUM_BUFFERS;
  req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_USERPTR;

  if (xioctl(data->fd, VIDIOC_REQBUFS, &req, 1) == -1) {
    debug(DEBUG_ERROR, "V4L", "VIDIOC_REQBUFS failed");
    return -1;
  }

  data->buffers = xcalloc(req.count, sizeof(cam_buffer));
  if (!data->buffers) {
    debug(DEBUG_ERROR, "V4L", "out of memory");
    return -1;
  }

  for (data->n_buffers = 0; data->n_buffers < req.count; data->n_buffers++) {
    data->buffers[data->n_buffers].length = buffer_size;
    data->buffers[data->n_buffers].start = memalign (/* boundary */ page_size, buffer_size);

    if (!data->buffers[data->n_buffers].start) {
      debug(DEBUG_ERROR, "V4L", "out of memory");
      return -1;
    }
  }

  debug(DEBUG_INFO, "V4L", "userptr using %d buffer(s)", data->n_buffers);

  return 0;
}

static void print_capabilities(int capabilities) {
  debug(DEBUG_INFO, "V4L", "capabilities:");
  if (capabilities & V4L2_CAP_VIDEO_CAPTURE) debug(DEBUG_INFO, "V4L", "  video capture device");
  if (capabilities & V4L2_CAP_VIDEO_OUTPUT) debug(DEBUG_INFO, "V4L", "  video output device");
  if (capabilities & V4L2_CAP_VIDEO_OVERLAY) debug(DEBUG_INFO, "V4L", "  video overlay");
  if (capabilities & V4L2_CAP_VBI_CAPTURE) debug(DEBUG_INFO, "V4L", "  raw VBI capture device");
  if (capabilities & V4L2_CAP_VBI_OUTPUT) debug(DEBUG_INFO, "V4L", "  raw VBI output device");
  if (capabilities & V4L2_CAP_SLICED_VBI_CAPTURE) debug(DEBUG_INFO, "V4L", "  sliced VBI capture device");
  if (capabilities & V4L2_CAP_SLICED_VBI_OUTPUT) debug(DEBUG_INFO, "V4L", "  sliced VBI output device");
  if (capabilities & V4L2_CAP_RDS_CAPTURE) debug(DEBUG_INFO, "V4L", "  RDS data capture");
  if (capabilities & V4L2_CAP_VIDEO_OUTPUT_OVERLAY) debug(DEBUG_INFO, "V4L", "  video output overlay");
  if (capabilities & V4L2_CAP_HW_FREQ_SEEK) debug(DEBUG_INFO, "V4L", "  hardware frequency seek");
  if (capabilities & V4L2_CAP_TUNER) debug(DEBUG_INFO, "V4L", "  tuner");
  if (capabilities & V4L2_CAP_AUDIO) debug(DEBUG_INFO, "V4L", "  audio support");
  if (capabilities & V4L2_CAP_RADIO) debug(DEBUG_INFO, "V4L", "  radio device");
  if (capabilities & V4L2_CAP_READWRITE) debug(DEBUG_INFO, "V4L", "  read/write systemcalls");
  if (capabilities & V4L2_CAP_ASYNCIO) debug(DEBUG_INFO, "V4L", "  async I/O");
  if (capabilities & V4L2_CAP_STREAMING) debug(DEBUG_INFO, "V4L", "  streaming I/O ioctls");
}

static int v4l_cam_option(libv4l_t *data, int id, int new_value, int *current_value) {
  struct v4l2_queryctrl queryctrl;
  struct v4l2_control control;
  int get, r = -1;

  if (id >= V4L2_CID_BASE && id < V4L2_CID_LASTP1) {
    memset(&queryctrl, 0, sizeof(queryctrl));
    queryctrl.id = id;

    if (xioctl(data->fd, VIDIOC_QUERYCTRL, &queryctrl, 1) != 0) {
      debug(DEBUG_ERROR, "V4L", "query option %s failed", v4l_op_name[id - V4L2_CID_BASE]);

    } else if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
      debug(DEBUG_ERROR, "V4L", "option %s is disabled", v4l_op_name[id - V4L2_CID_BASE]);

    } else {
      get = current_value != NULL;
      memset(&control, 0, sizeof(control));
      control.id = id;
      if (!get) control.value = new_value;

      if ((r = xioctl(data->fd, get ? VIDIOC_G_CTRL : VIDIOC_S_CTRL, &control, 1)) == 0) {
        if (get) {
          *current_value = control.value;
          debug(DEBUG_INFO, "V4L", "get option %s = %d", v4l_op_name[id - V4L2_CID_BASE], *current_value);
        } else {
          debug(DEBUG_INFO, "V4L", "set option %s = %d", v4l_op_name[id - V4L2_CID_BASE], new_value);
          usleep(10000);
        }
      } else {
        if (get) {
          debug(DEBUG_ERROR, "V4L", "error getting option %s", v4l_op_name[id - V4L2_CID_BASE]);
        } else {
          debug(DEBUG_ERROR, "V4L", "error setting option %s = %d", v4l_op_name[id - V4L2_CID_BASE], new_value);
        }
      }
    }
  } else {
    debug(DEBUG_ERROR, "V4L", "invalid option id %d", id);
  }

  return r;
}

static void options_menu(int fd, struct v4l2_queryctrl *queryctrl) {
  struct v4l2_querymenu querymenu;

  memset(&querymenu, 0, sizeof(querymenu));
  querymenu.id = queryctrl->id;

  for (querymenu.index = queryctrl->minimum; querymenu.index <= queryctrl->maximum; querymenu.index++) {
    if (xioctl(fd, VIDIOC_QUERYMENU, &querymenu, 0) == 0) {
      debug(DEBUG_INFO, "V4L", "     %2d: %s", querymenu.index, querymenu.name);
    }
  }
}

static void check_options(libv4l_t *data) {
  struct v4l2_queryctrl queryctrl;
  struct v4l2_control control;

  memset(&queryctrl, 0, sizeof(queryctrl));

  for (queryctrl.id = V4L2_CID_BASE; queryctrl.id < V4L2_CID_LASTP1; queryctrl.id++) {
    if (xioctl(data->fd, VIDIOC_QUERYCTRL, &queryctrl, 0) == 0) {
      if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) continue;

      memset(&control, 0, sizeof(control));
      control.id = queryctrl.id;
      if (xioctl(data->fd, VIDIOC_G_CTRL, &control, 0) != 0) continue;

      if (data->num_options == 0) debug(DEBUG_INFO, "V4L", "options:");
      debug(DEBUG_INFO, "V4L", "  %s = %d", v4l_op_name[queryctrl.id - V4L2_CID_BASE], control.value);
      data->options[data->num_options].id = queryctrl.id;

      if (queryctrl.type == V4L2_CTRL_TYPE_MENU) {
        data->options[data->num_options].min = queryctrl.minimum;
        data->options[data->num_options].max = queryctrl.maximum;
        options_menu(data->fd, &queryctrl);
      }
      data->num_options++;
    }
  }
}

static int v4l_option_count(iterator_t *it) {
  libv4l_t *data;

  data = (libv4l_t *)it->data;
  return data->num_options;
}

static int v4l_option_position(iterator_t *it, int index) {
  libv4l_t *data;

  data = (libv4l_t *)it->data;
  data->current_option = index-1;

  return 0;
}

static int v4l_option_next(iterator_t *it) {
  libv4l_t *data;
  int r;

  data = (libv4l_t *)it->data;
  data->current_option++;

  if (data->current_option < data->num_options) {
    r = 1;
  } else {
    r = 0;
  }

  return r;
}

static int v4l_option_num_properties(iterator_t *it) {
  return 4;
}

static char *v4l_option_property_name(iterator_t *it, int index) {
  char *name;

  switch (index) {
    case 1: name = "name"; break;
    case 2: name = "value"; break;
    case 3: name = "min"; break;
    case 4: name = "max"; break;
    default: name = NULL;
  }

  return name;
}

static char *v4l_option_get_property_value(iterator_t *it, int index) {
  libv4l_t *data;
  int current_value, r;
  char *value = NULL;

  data = (libv4l_t *)it->data;

  if (data->current_option >= 0 && data->current_option < data->num_options) {
    switch (index) {
      case 1:
        value = v4l_op_name[data->options[data->current_option].id - V4L2_CID_BASE];
        break;

      case 2:
        r = -1;

        if (mutex_lock(data->mutex) == 0) {
          r = v4l_cam_option(data, data->options[data->current_option].id, 0, &current_value);
          mutex_unlock(data->mutex);
        }

        if (r == 0) {
          snprintf(data->option_value, MAX_BUF-1, "%d", current_value);
          value = data->option_value;
        }
        break;

      case 3:
        snprintf(data->option_value, MAX_BUF-1, "%d", data->options[data->current_option].min);
        value = data->option_value;
        break;

      case 4:
        snprintf(data->option_value, MAX_BUF-1, "%d", data->options[data->current_option].max);
        value = data->option_value;
        break;
    }
  }

  return value;
}

static int v4l_option_set_property_value(iterator_t *it, int index, char *value) {
  libv4l_t *data;
  int new_value, r = -1;

  data = (libv4l_t *)it->data;

  if (data->current_option >= 0 && data->current_option < data->num_options) {
    if (index == 2) {
      new_value = atoi(value);

      if (mutex_lock(data->mutex) == 0) {
        r = v4l_cam_option(data, data->options[data->current_option].id, new_value, NULL);
        mutex_unlock(data->mutex);
      }
    }
  }

  return r;
}

static int init_device(libv4l_t *data) {
  struct v4l2_cropcap cropcap;
  struct v4l2_crop crop;
  struct v4l2_fmtdesc fmtdesc;
  struct v4l2_frmsizeenum frmsize;
  struct v4l2_format fmt;
  struct v4l2_input input;
  struct v4l2_streamparm sparam;  
  unsigned int min;
  int has_yuyv, has_uyvy, has_i420, has_jpeg, has_mjpeg;
  int i, r, size_ok;
  char *s;

  if (xioctl(data->fd, VIDIOC_QUERYCAP, &data->cap, 1) == -1) {
    debug(DEBUG_ERROR, "V4L", "VIDIOC_QUERYCAP failed");
    return -1;
  }

  debug(DEBUG_INFO, "V4L", "driver: %s", data->cap.driver);
  debug(DEBUG_INFO, "V4L", "card: %s", data->cap.card);
  debug(DEBUG_INFO, "V4L", "bus info: %s", data->cap.bus_info);
  debug(DEBUG_INFO, "V4L", "version: %08x", data->cap.version);
  print_capabilities(data->cap.capabilities);

  if (!(data->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    debug(DEBUG_ERROR, "V4L", "device is no video capture device");
    return -1;
  }

  if (data->cap.capabilities & V4L2_CAP_STREAMING) {
    data->io = IO_METHOD_MMAP;
    debug(DEBUG_INFO, "V4L", "using mmap method");

  } else if (data->cap.capabilities & V4L2_CAP_READWRITE) {
    data->io = IO_METHOD_READ;
    debug(DEBUG_INFO, "V4L", "using read method");

  } else {
    data->io = IO_METHOD_USERPTR;
    debug(DEBUG_INFO, "V4L", "using userptr method");
  }

  check_options(data);

  memset(&cropcap, 0, sizeof(cropcap));
  cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (xioctl(data->fd, VIDIOC_CROPCAP, &cropcap, 0) == 0) {
    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    crop.c = cropcap.defrect; // reset to default

    if (xioctl(data->fd, VIDIOC_S_CROP, &crop, 0) == -1) {
      debug(DEBUG_INFO, "V4L", "cropping not supported");
    }
  } else {  
    debug(DEBUG_INFO, "V4L", "cropping not supported");
  }

  has_i420 = 0;
  has_yuyv = 0;
  has_uyvy = 0;
  has_jpeg = 0;
  has_mjpeg = 0;

  debug(DEBUG_INFO, "V4L", "formats:");
  for (i = 0;; i++) {
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.index = i;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(data->fd, VIDIOC_ENUM_FMT, &fmtdesc, 0) != 0) break;

    switch (fmtdesc.pixelformat) {
      case V4L2_PIX_FMT_YUV420:
        has_i420 = 1;
        break;
      case V4L2_PIX_FMT_YUYV:
        has_yuyv = 1;
        break;
      case V4L2_PIX_FMT_UYVY:
        has_uyvy = 1;
        break;
      case V4L2_PIX_FMT_MJPEG:
        has_mjpeg = 1;
        break;
      case V4L2_PIX_FMT_JPEG:
        has_jpeg = 1;
        break;
    }

    debug(DEBUG_INFO, "V4L", "  %s, %s, fourcc=%c%c%c%c",
      fmtdesc.description,
      fmtdesc.flags & V4L2_FMT_FLAG_COMPRESSED ? "compressed" : "uncompressed",
      fmtdesc.pixelformat & 0xFF,
      (fmtdesc.pixelformat >> 8) & 0xFF,
      (fmtdesc.pixelformat >> 16) & 0xFF,
      (fmtdesc.pixelformat >> 24) & 0xFF);
  }

  data->pixelformat = 0;

  // handle suggestion
  if (data->encoding) {
    switch (data->encoding) {
      case ENC_I420:
        if (has_i420) data->pixelformat = V4L2_PIX_FMT_YUV420;
        break;
      case ENC_YUYV:
        if (has_yuyv) data->pixelformat = V4L2_PIX_FMT_YUYV;
        break;
      case ENC_UYVY:
        if (has_uyvy) data->pixelformat = V4L2_PIX_FMT_UYVY;
        break;
      case ENC_JPEG:
        if (has_mjpeg) data->pixelformat = V4L2_PIX_FMT_MJPEG;
        if (has_jpeg)  data->pixelformat = V4L2_PIX_FMT_JPEG;
        break;
    }

    if (data->pixelformat != 0) {
      debug(DEBUG_INFO, "V4L", "suggested format %s", video_encoding_name(data->encoding));
    } else {
      debug(DEBUG_INFO, "V4L", "dit not find suggested format %s", video_encoding_name(data->encoding));
    }
  }

  if (data->pixelformat == 0) {
    if (has_i420) {
      data->pixelformat = V4L2_PIX_FMT_YUV420;
      data->encoding = ENC_I420;
    } else if (has_yuyv) {
      data->pixelformat = V4L2_PIX_FMT_YUYV;
      data->encoding = ENC_YUYV;
    } else if (has_uyvy) {
      data->pixelformat = V4L2_PIX_FMT_UYVY;
      data->encoding = ENC_UYVY;
    } else if (has_jpeg) {
      data->pixelformat = V4L2_PIX_FMT_JPEG;
      data->encoding = ENC_JPEG;
    } else if (has_mjpeg) {
      data->pixelformat = V4L2_PIX_FMT_MJPEG;
      data->encoding = ENC_JPEG;
    } else {
      debug(DEBUG_ERROR, "V4L", "camera does not support compatible format");
      return -1;
    }
  }

  debug(DEBUG_INFO, "V4L", "frame sizes:");
  size_ok = 0;

  for (i = 0;; i++) {
    memset(&frmsize, 0, sizeof(frmsize));
    frmsize.index = i;
    frmsize.pixel_format = data->pixelformat;
    if (xioctl(data->fd, VIDIOC_ENUM_FRAMESIZES, &frmsize, 0) != 0) break;

    if (frmsize.discrete.width == data->width && frmsize.discrete.height == data->height) {
      size_ok = 1;
    }
    debug(DEBUG_INFO, "V4L", "  %d x %d", frmsize.discrete.width, frmsize.discrete.height);
  }

  if (!size_ok) {
    debug(DEBUG_ERROR, "V4L", "requested frame size not found");
    return -1;
  }

  memset(&fmt, 0, sizeof(fmt));
  fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width       = data->width; 
  fmt.fmt.pix.height      = data->height;
  fmt.fmt.pix.pixelformat = data->pixelformat;
  //fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
  fmt.fmt.pix.field       = V4L2_FIELD_NONE;
  fmt.fmt.pix.colorspace  = V4L2_COLORSPACE_SMPTE170M;

  if (xioctl(data->fd, VIDIOC_S_FMT, &fmt, 1) == -1) {
    debug(DEBUG_ERROR, "V4L", "VIDIO_S_FMT failed");
    return -1;
  }

  data->width = fmt.fmt.pix.width;
  data->height = fmt.fmt.pix.height;

  switch (data->encoding) {
    case ENC_I420:
      debug(DEBUG_INFO, "V4L", "selected I420 format");
      data->blacklen = (data->width * data->height * 3) / 2;
      data->black = xmalloc(data->blacklen);
      break;
    case ENC_YUYV:
      debug(DEBUG_INFO, "V4L", "selected YUYV format");
      data->blacklen = data->width * data->height * 2;
      data->black = xmalloc(data->blacklen);
      break;
    case ENC_UYVY:
      debug(DEBUG_INFO, "V4L", "selected UYVY format");
      data->blacklen = data->width * data->height * 2;
      data->black = xmalloc(data->blacklen);
      break;
    case ENC_JPEG:
      debug(DEBUG_INFO, "V4L", "selected JPEG format");
      data->blacklen = 0;
      data->black = NULL;
      break;
  }

  debug(DEBUG_INFO, "V4L", "selected frame size: %d x %d", data->width, data->height);

  // Note VIDIOC_S_FMT may change width and height

  // Buggy driver paranoia
  min = fmt.fmt.pix.width * 2;
  if (fmt.fmt.pix.bytesperline < min) fmt.fmt.pix.bytesperline = min;
  min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
  if (fmt.fmt.pix.sizeimage < min) fmt.fmt.pix.sizeimage = min;

  debug(DEBUG_INFO, "V4L", "bytes per line: %d", fmt.fmt.pix.bytesperline);
  debug(DEBUG_INFO, "V4L", "image size: %d", fmt.fmt.pix.sizeimage);

  debug(DEBUG_INFO, "V4L", "inputs:");
  for (i = 0;; i++) {
    memset(&input, 0, sizeof(input));
    input.index = i;
    if (xioctl(data->fd, VIDIOC_ENUMINPUT, &input, 0) != 0) break;

    if (input.type == V4L2_INPUT_TYPE_TUNER) s = "tuner";
    else if (input.type == V4L2_INPUT_TYPE_CAMERA) s = "camera";
    else s = "unknown";

    debug(DEBUG_INFO, "V4L", "  %d: %s (%s)", i, input.name, s);
  }

  if (data->input < 0 || data->input > i) {
    data->input = 0;
  }

  memset(&input, 0, sizeof(input));
  input.index = data->input;

  if (xioctl(data->fd, VIDIOC_ENUMINPUT, &input, 1) == 0) {
    if (xioctl(data->fd, VIDIOC_S_INPUT, &data->input, 1) == -1) {
      debug(DEBUG_ERROR, "V4L", "VIDIOC_S_INPUT failed");
      return -1;
    }
    debug(DEBUG_INFO, "V4L", "input %d selected", data->input);
  }

  memset(&sparam, 0, sizeof(sparam));
  sparam.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (xioctl(data->fd, VIDIOC_G_PARM, &sparam, 1) == 0) {
    if (sparam.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) {
      debug(DEBUG_INFO, "V4L", "stream supports time per frame setting");
      debug(DEBUG_INFO, "V4L", "current frame rate is %d/%d",
        sparam.parm.capture.timeperframe.denominator, sparam.parm.capture.timeperframe.numerator);

      sparam.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      sparam.parm.capture.timeperframe.denominator = data->fps;
      sparam.parm.capture.timeperframe.numerator = 1;
      debug(DEBUG_INFO, "V4L", "requested frame rate is %d/%d",
        sparam.parm.capture.timeperframe.denominator, sparam.parm.capture.timeperframe.numerator);

      if (xioctl(data->fd, VIDIOC_S_PARM, &sparam, 1) == 0) {
        memset(&sparam, 0, sizeof(sparam));
        sparam.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (xioctl(data->fd, VIDIOC_G_PARM, &sparam, 1) == 0) {
          debug(DEBUG_INFO, "V4L", "new frame rate is %d/%d",
            sparam.parm.capture.timeperframe.denominator, sparam.parm.capture.timeperframe.numerator);
        } else {
          debug(DEBUG_INFO, "V4L", "could not get stream parameters after setting");
        }
      } else {
        debug(DEBUG_INFO, "V4L", "could not set stream parameters");
      }
    } else {
      debug(DEBUG_INFO, "V4L", "stream does not support time per frame setting");
    }
  } else {
    debug(DEBUG_INFO, "V4L", "could not get stream parameters");
  }

  r = 0;
  switch (data->io) {
    case IO_METHOD_READ:
      r = init_read(data, fmt.fmt.pix.sizeimage);
      break;

    case IO_METHOD_MMAP:
      r = init_mmap(data);
      break;

    case IO_METHOD_USERPTR:
      r = init_userp(data, fmt.fmt.pix.sizeimage);
      break;
  }

  if (r != 0) {
    debug(DEBUG_ERROR, "V4L", "init read failed");
  }

  return r;
}

static int open_device(char *device) {
  struct stat st; 
  int fd;

  if (stat(device, &st) == -1) {
    debug_errno("V4L", "stat \"%s\"", device);
    return -1;
  }

  if (!S_ISCHR(st.st_mode)) {
    debug(DEBUG_ERROR, "V4L", "%s is not a character device", device);
    return -1;
  }

  fd = sys_open(device, SYS_RDWR | SYS_NONBLOCK);

  if (fd == -1) {
    debug_errno("V4L", "open \"%s\"", device);
  }

  return fd;
}

static int v4l_cam_open(libv4l_t *data) {
  int r = -1;

  if ((data->fd = open_device(data->device)) != -1) {
    if (init_device(data) == 0) {
      debug(DEBUG_INFO, "V4L", "camera open");
      r = 0;

    } else {
      sys_close(data->fd);
      data->fd = -1;
    }
  }

  return r;
}

static void uninit_device(libv4l_t *data) {
  unsigned int i;

  switch (data->io) {
    case IO_METHOD_READ:
      xfree(data->buffers[0].start);
      break;

    case IO_METHOD_MMAP:
      for (i = 0; i < data->n_buffers; ++i) {
        if (munmap(data->buffers[i].start, data->buffers[i].length) == -1) {
          debug(DEBUG_ERROR, "V4L", "munmap failed");
        }
      }
      break;

    case IO_METHOD_USERPTR:
      for (i = 0; i < data->n_buffers; ++i) {
        xfree(data->buffers[i].start);
      }
      break;
  }

  xfree(data->buffers);

  if (data->black) xfree(data->black);
}

static int v4l_cam_close(libv4l_t *data) {
  int r = -1;

  if (data->fd != -1) {
    uninit_device(data);
    sys_close(data->fd);
    data->fd = -1;
    r = 0;
  }

  debug(DEBUG_INFO, "V4L", "camera close");

  return r;
}

static int v4l_node_process(media_frame_t *frame, void *_data) {
  libv4l_t *data;
  struct v4l2_buffer vbuf;
  unsigned char *buf;
  int i, r = -1;

  data = (libv4l_t *)_data;

  if (mutex_lock(data->mutex) == 0) {
    if (data->first) {
      //data->t0 = sys_get_clock();
      data->first = 0;
      data->t0 = 0;
    }
    frame->meta.type = FRAME_TYPE_VIDEO;
    frame->meta.av.v.encoding = data->encoding;
    frame->meta.av.v.width = data->width;
    frame->meta.av.v.height = data->height;
    frame->meta.av.v.fwidth = data->width;
    frame->meta.av.v.fheight = data->height;
    frame->meta.av.v.ar_num = 1;
    frame->meta.av.v.ar_den = 1;
    frame->meta.av.v.tb_num = 1;
    frame->meta.av.v.tb_den = 1;
    //frame->ts = sys_get_clock() - data->t0;
    frame->ts = data->t0;
    data->t0 += 1000000 / data->fps;

    if (!data->started) {
      // camera is not on, return a black frame
      debug(DEBUG_TRACE, "V4L", "camera is not on (black frame)");
      r = media_frame_put(frame, data->black, data->blacklen);
    } else {

      for (;;) {
        r = -1;
        if (thread_must_end()) break;
        r = v4l_cam_select(data);
        if (r > 0) break;
      }

      if (r > 0) {
        debug(DEBUG_TRACE, "V4L", "read_frame begin");
        r = -1;

        switch (data->io) {
          case IO_METHOD_READ:
            if (sys_read(data->fd, data->buffers[0].start, data->buffers[0].length) == -1) {
              debug_errno("V4L", "read");
            } else {
              buf = data->buffers[0].start;
              r = data->buffers[0].length;
              if (media_frame_put(frame, buf, r) == -1) r = -1;
            }
            break;

          case IO_METHOD_MMAP:
            memset(&vbuf, 0, sizeof(vbuf));
            vbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            vbuf.memory = V4L2_MEMORY_MMAP;

            debug(DEBUG_TRACE, "V4L", "VIDIOC_DQBUF ...");
            if (xioctl(data->fd, VIDIOC_DQBUF, &vbuf, 1) != -1) {
              debug(DEBUG_TRACE, "V4L", "VIDIOC_DQBUF ok");

              if (vbuf.index < data->n_buffers) {
                buf = data->buffers[vbuf.index].start;
                r = vbuf.bytesused;
                if (data->encoding != ENC_JPEG && r != data->blacklen) {
                  debug(DEBUG_ERROR, "V4L", "got only %d/%d bytes from buffer", r, data->blacklen);
                  r = data->blacklen;
                }
                if (media_frame_put(frame, buf, r) == -1) r = -1;
              }

              if (xioctl(data->fd, VIDIOC_QBUF, &vbuf, 1) == -1) {
                debug(DEBUG_ERROR, "V4L", "VIDIOC_QBUF failed (%d / %d) on read_frame", vbuf.index, data->n_buffers);
                r = -1;
              }
            } else {
              debug(DEBUG_TRACE, "V4L", "VIDIOC_DQBUF failed");
            }
            break;

          case IO_METHOD_USERPTR:
            memset(&vbuf, 0, sizeof(vbuf));
            vbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            vbuf.memory = V4L2_MEMORY_USERPTR;

            debug(DEBUG_TRACE, "V4L", "VIDIOC_DQBUF ...");
            if (xioctl(data->fd, VIDIOC_DQBUF, &vbuf, 1) != -1) {
              debug(DEBUG_TRACE, "V4L", "VIDIOC_DQBUF ok");

              for (i = 0; i < data->n_buffers; i++) {
                if (vbuf.m.userptr == (unsigned long)data->buffers[i].start && vbuf.length == data->buffers[i].length) break;
              }

              if (i < data->n_buffers) {
                buf = (unsigned char *)vbuf.m.userptr;
                r = vbuf.length;
                if (media_frame_put(frame, buf, r) == -1) r = -1;
              }
    
              if (xioctl(data->fd, VIDIOC_QBUF, &vbuf, 1) == -1) {
                debug(DEBUG_ERROR, "V4L", "VIDIOC_QBUF failed (%d / %d) on read_frame", vbuf.index, data->n_buffers);
                r = -1;
              }
            }
        }

        debug(DEBUG_TRACE, "V4L", "read_frame end");
      }
    }
    mutex_unlock(data->mutex);
  }

  return r != -1 ? 1 : -1;
}

/*
Camera Venus:
V4L: standard options:
V4L:   brightness = 0
V4L:   contrast = 20
V4L:   saturation = 6
V4L:   white_balance = 1
V4L:   whiteness = 160
V4L:   gain = 32
V4L:   power_line_freq = 1
V4L:       0: Disabled
V4L:       1: 50 Hz
V4L:       2: 60 Hz
V4L:   white_balance_temp = 6500
V4L:   sharpness = 8
V4L:   backlight_comp = 0
*/

static int v4l_node_option(char *name, char *value, void *_data) {
  libv4l_t *data;
  int id, v, i, r = -1;

  data = (libv4l_t *)_data;

  if (mutex_lock(data->mutex) == 0) {
    if (!strcmp(name, "start")) {
      if (!data->started) {
        if ((r = v4l_cam_start(data)) == 0) {
          data->started = 1;
          r = 0;
        }
      } else {
        r = 0;
      }
    } else if (!strcmp(name, "stop")) {
      if (data->started) {
        if ((r = v4l_cam_stop(data)) == 0) {
          data->started = 0;
          r = 0;
        }
      } else {
        r = 0;
      }
    } else {
      for (i = 0; v4l_op_name[i]; i++) {
        if (!strcmp(v4l_op_name[i], name)) {
          id = i + V4L2_CID_BASE;
          v = value ? atoi(value) : 0;
          r = v4l_cam_option(data, id, v, NULL);
          break;
        }
      }
    }
    mutex_unlock(data->mutex);
  }

  return r;
}

static int v4l_node_destroy(void *_data) {
  libv4l_t *data;

  data = (libv4l_t *)_data;

  v4l_cam_stop(data);
  v4l_cam_close(data);
  ptr_free(data->it, TAG_ITERATOR);
  mutex_destroy(data->mutex);
  xfree(data->device);
  xfree(data);

  return 0;
}

static int libv4l_node(int pe) {
  libv4l_t *data;
  script_int_t width, height, input, fps, encoding;
  char *device;
  int node, r = -1;

  if (script_get_string(pe, 0, &device) == 0 &&
      script_get_integer(pe, 1, &width) == 0 &&
      script_get_integer(pe, 2, &height) == 0 &&
      script_get_integer(pe, 3, &input) == 0 &&
      script_get_integer(pe, 4, &fps) == 0) {

    if (script_opt_integer(pe, 5, &encoding) != 0) {
      encoding = 0;
    }

    if ((data = xcalloc(1, sizeof(libv4l_t))) != NULL) {
      data->mutex = mutex_create("v4l");
      data->fd = -1;
      data->device = device;
      data->encoding = encoding;
      data->width = width;
      data->height = height;
      data->input = input;
      data->fps = fps;

      if (v4l_cam_open(data) == 0) {
        debug(DEBUG_INFO, "V4L", "encoding %s", video_encoding_name(data->encoding));

        data->iterator.tag = TAG_ITERATOR;
        data->iterator.data = data;
        data->iterator.count = v4l_option_count;
        data->iterator.position = v4l_option_position;
        data->iterator.next = v4l_option_next;
        data->iterator.num_properties = v4l_option_num_properties;
        data->iterator.property_name = v4l_option_property_name;
        data->iterator.get_property_value = v4l_option_get_property_value;
        data->iterator.set_property_value = v4l_option_set_property_value;
        data->it = ptr_new(&data->iterator, NULL);

        if ((node = node_create(NAME, &v4l_dispatch, data)) != -1) {
          r = script_push_integer(pe, node);
        }
      } else {
        mutex_destroy(data->mutex);
        xfree(data->device);
        xfree(data);
      }
    }
  }

  return r;
}

static int libv4l_options_iterator_callback(void *_data, void *_arg) {
  libv4l_t *data;
  int r = -1;

  data = (libv4l_t *)_data;

  if (mutex_lock(data->mutex) == 0) {
    r = data->it;
    mutex_unlock(data->mutex);
  }

  return r;
}

static int libv4l_options_iterator(int pe) {
  script_int_t ptr;
  int it, r = -1;

  if (script_get_integer(pe, 0, &ptr) == 0) {
    if ((it = node_call(ptr, NAME, libv4l_options_iterator_callback, NULL)) != -1) {
      r = script_push_integer(pe, it);
    }
  }

  return r;
}

int libv4l_load(void) {
  memset(v4l_op_name, 0, sizeof(v4l_op_name));

  v4l_op_name[V4L2_CID_BRIGHTNESS - V4L2_CID_BASE] = "brightness";
  v4l_op_name[V4L2_CID_CONTRAST - V4L2_CID_BASE] = "contrast";
  v4l_op_name[V4L2_CID_SATURATION - V4L2_CID_BASE] = "saturation";
  v4l_op_name[V4L2_CID_HUE - V4L2_CID_BASE] = "hue";
  v4l_op_name[V4L2_CID_AUDIO_VOLUME - V4L2_CID_BASE] = "volume";
  v4l_op_name[V4L2_CID_AUDIO_BALANCE - V4L2_CID_BASE] = "balance";
  v4l_op_name[V4L2_CID_AUDIO_BASS - V4L2_CID_BASE] = "bass";
  v4l_op_name[V4L2_CID_AUDIO_TREBLE - V4L2_CID_BASE] = "treeble";
  v4l_op_name[V4L2_CID_AUDIO_MUTE - V4L2_CID_BASE] = "mute";
  v4l_op_name[V4L2_CID_AUDIO_LOUDNESS - V4L2_CID_BASE] = "loudness";
  v4l_op_name[V4L2_CID_BLACK_LEVEL - V4L2_CID_BASE] = "black_level";
  v4l_op_name[V4L2_CID_AUTO_WHITE_BALANCE - V4L2_CID_BASE] = "white_balance";
  v4l_op_name[V4L2_CID_DO_WHITE_BALANCE - V4L2_CID_BASE] = "do_white_balance";
  v4l_op_name[V4L2_CID_RED_BALANCE - V4L2_CID_BASE] = "red_balance";
  v4l_op_name[V4L2_CID_BLUE_BALANCE - V4L2_CID_BASE] = "blue_balance";
  v4l_op_name[V4L2_CID_GAMMA - V4L2_CID_BASE] = "gamma";
  v4l_op_name[V4L2_CID_WHITENESS - V4L2_CID_BASE] = "whiteness";
  v4l_op_name[V4L2_CID_EXPOSURE - V4L2_CID_BASE] = "exposure";
  v4l_op_name[V4L2_CID_AUTOGAIN - V4L2_CID_BASE] = "autogain";
  v4l_op_name[V4L2_CID_GAIN - V4L2_CID_BASE] = "gain";
  v4l_op_name[V4L2_CID_HFLIP - V4L2_CID_BASE] = "hflip";
  v4l_op_name[V4L2_CID_VFLIP - V4L2_CID_BASE] = "vflip";
  //v4l_op_name[V4L2_CID_HCENTER - V4L2_CID_BASE] = "hcenter";
  //v4l_op_name[V4L2_CID_VCENTER - V4L2_CID_BASE] = "vcenter";
  v4l_op_name[V4L2_CID_POWER_LINE_FREQUENCY - V4L2_CID_BASE] = "power_line_freq";
  v4l_op_name[V4L2_CID_HUE_AUTO - V4L2_CID_BASE] = "hue_auto";
  v4l_op_name[V4L2_CID_WHITE_BALANCE_TEMPERATURE - V4L2_CID_BASE] = "white_balance_temp";
  v4l_op_name[V4L2_CID_SHARPNESS - V4L2_CID_BASE] = "sharpness";
  v4l_op_name[V4L2_CID_BACKLIGHT_COMPENSATION  - V4L2_CID_BASE] = "backlight_comp";
  v4l_op_name[V4L2_CID_CHROMA_AGC - V4L2_CID_BASE] = "chorma_agc";
  v4l_op_name[V4L2_CID_COLOR_KILLER - V4L2_CID_BASE] = "color_killer";
  v4l_op_name[V4L2_CID_COLORFX - V4L2_CID_BASE] = "colorfx";
  v4l_op_name[V4L2_CID_AUTOBRIGHTNESS - V4L2_CID_BASE] = "auto_brightness";
  v4l_op_name[V4L2_CID_BAND_STOP_FILTER - V4L2_CID_BASE] = "band_stop_filter";
  v4l_op_name[V4L2_CID_ROTATE - V4L2_CID_BASE] = "rotate";
  v4l_op_name[V4L2_CID_BG_COLOR - V4L2_CID_BASE] = "bg_color";
  v4l_op_name[V4L2_CID_CHROMA_GAIN - V4L2_CID_BASE] = "chroma_gain";
  v4l_op_name[V4L2_CID_ILLUMINATORS_1 - V4L2_CID_BASE] = "illuminators_1";
  v4l_op_name[V4L2_CID_ILLUMINATORS_2 - V4L2_CID_BASE] = "illuminators_2";
  v4l_op_name[V4L2_CID_MIN_BUFFERS_FOR_CAPTURE - V4L2_CID_BASE] = "min_buffers_capture";
  v4l_op_name[V4L2_CID_MIN_BUFFERS_FOR_OUTPUT - V4L2_CID_BASE] = "min_buffers_output";

  return 0;
}

int libv4l_init(int pe, script_ref_t obj) {
  script_add_function(pe, obj, "camera",  libv4l_node);
  script_add_function(pe, obj, "options", libv4l_options_iterator);

  return 0;
}
