#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>

//#include "io.h"
#include "sys.h"
#include "filter.h"
#include "xalloc.h"

typedef struct {
  int fd;
} sys_conn_filter_t;

static int conn_filter_peek(struct conn_filter_t *filter, uint32_t us) {
  sys_conn_filter_t *data = (sys_conn_filter_t *)filter->data;

  return sys_select(data->fd, us);
}

static int conn_filter_read(struct conn_filter_t *filter, uint8_t *b) {
  sys_conn_filter_t *data = (sys_conn_filter_t *)filter->data;

  return sys_read(data->fd, b, 1);
}

static int conn_filter_write(struct conn_filter_t *filter, uint8_t *b, int n) {
  sys_conn_filter_t *data = (sys_conn_filter_t *)filter->data;

  return sys_write(data->fd, b, n);
}

conn_filter_t *conn_filter(int fd) {
  conn_filter_t *filter;
  sys_conn_filter_t *data;

  if ((filter = xcalloc(1, sizeof(conn_filter_t))) != NULL) {
    if ((data = xcalloc(1, sizeof(sys_conn_filter_t))) != NULL) {
      data->fd = fd;
      filter->data = data;
      filter->peek = conn_filter_peek;
      filter->read = conn_filter_read;
      filter->write = conn_filter_write;
    } else {
      xfree(filter);
    }
  }

  return filter;
}
