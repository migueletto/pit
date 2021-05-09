#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "script.h"
#include "thread.h"
#include "io.h"
#include "sys.h"
#include "sim.h"
#include "debug.h"
#include "xalloc.h"

#define MAX_SIM 16

typedef struct {
  char *service;
  int id;
  char *host;
  int port;
  bt_provider_t *bt;
} sim_t;

static sim_t simulator[MAX_SIM];
static int nsim = 0;

int sim_add(char *service, int id, char *host, int port, bt_provider_t *bt) {
  int r = -1;

  if (nsim < MAX_SIM) {
    debug(DEBUG_INFO, "SIM", "adding simulated %s / %d at host %s port %d", service, id, host, port);
    simulator[nsim].service = xstrdup(service);
    simulator[nsim].id = id;
    simulator[nsim].host = xstrdup(host);
    simulator[nsim].port = port;
    simulator[nsim].bt = bt;
    nsim++;
    r = 0;
  }

  return r;
}

// returns:
//  >0 : connected to simulator
//  <0 : error connecting to simulator
//  0  : not simulated
int sim_connect(char *service, int id) {
  int i;

  for (i = 0; i < nsim; i++) {
    if (!strcmp(simulator[i].service, service) && simulator[i].id == id) {
      debug(DEBUG_INFO, "SIM", "simulating service %s id %d at %s %d", service, id, simulator[i].host, simulator[i].port);
      return io_connect(simulator[i].host, simulator[i].port, simulator[i].bt);
    }
  }

  return 0;
}

int sim_read(int fd, uint8_t *b, int n) {
  int nread, len, i, r;

  if (b == NULL || n <= 0) {
    return -1;
  }

  for (; !thread_must_end();) {
    r = sys_read_timeout(fd, b, 1, &nread, 1000);
    if (r == -1) return -1;
    if (r == 0) continue;
    if (nread != 1) return -1;
    len = b[0];
    break;
  }

  if (n < len) {
    debug(DEBUG_ERROR, "SIM", "buffer (%d) is smaller than packet (%d)", n, len);
    return -1;
  }

  for (i = 0;;) {
    r = sys_read_timeout(fd, &b[i], len, &nread, 1000);
    if (r == -1) return -1;
    if (r == 0) continue;
    if (nread == 0) return -1;
    len -= nread;
    i += nread;
    if (len == 0) break;
  }

  return i;
}

int sim_write(int fd, uint8_t *b, int n) {
  uint8_t buf[256];
  int i, r = -1;

  if (b == NULL || n < 0) {
    return -1;
  }

  buf[0] = n;
  for (i = 0; i < n && i < 255; i++) buf[i+1] = b[i];

  if (sys_write(fd, buf, n+1) == n+1) {
    // read 1 byte as delivery confirmation
    if (sim_read(fd, b, 1) == 1) {
      r = n;
    }
  }

  return r;
}
