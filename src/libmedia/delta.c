#include <stdlib.h>
#include <stdarg.h>

#include "delta.h"
#include "debug.h"
#include "xalloc.h"

delta_t *delta_init(int width, int height) {
  delta_t *d;
  int len;

  if ((d = xcalloc(1, sizeof(delta_t))) == NULL) {
    return NULL;
  }

  d->width = width;
  d->height = height;
  d->xprof = (d->width + 15) >> 4;
  d->yprof = (d->height + 15) >> 4;

  len = d->xprof * d->yprof * sizeof(int);

  if ((d->profile0 = xmalloc(len)) == NULL) {
    xfree(d);
    return NULL;
  }

  if ((d->profile1 = xmalloc(len)) == NULL) {
    xfree(d->profile0);
    xfree(d);
    return NULL;
  }

  d->prof0 = d->profile0;
  d->prof1 = d->profile1;
  d->prof = d->prof1;

  return d;
}

void delta_close(delta_t *delta) {
  if (delta) {
    if (delta->profile0) xfree(delta->profile0);
    if (delta->profile1) xfree(delta->profile1);
    xfree(delta);
  }
}

// return: [0,1] 0: no delta, 1=max delta
float delta_image(delta_t *delta, unsigned char *gray, float factor) {
  int i, j, k, m, d, dif, *p;
  int tprof, min, max;

  if (!delta || !gray) return 0;
  tprof = delta->xprof * delta->yprof;

  k = 0;
  for (i = 0; i < delta->height; i++) {
    for (j = 0; j < delta->width; j++) {
      m = gray[k];
      m >>= 5;   // 8 bits -> 3 bits
      delta->prof1[(i >> 4) * delta->xprof + (j >> 4)] += m;   // acumula
      k++;
    }
  }

  dif = 0;
  for (i = 0; i < tprof; i++) {
    delta->prof1[i] >>= 8;   // normaliza: prof1[i] vale 0 a 7  (16x16 pixels -> 1 pixel)
    d = delta->prof1[i] - delta->prof0[i];   // diferenca com relacao ao profile anterior
    dif += d < 0 ? -d : d;   // acumula diferenca
  }

  // dif: de 0 a tprof*7
  max = tprof * 7;
  min = (int)(max * factor);
  delta->prof = delta->prof1;

  if (dif < min) {
    debug(DEBUG_TRACE, "MONITOR", "delta %d (min=%d, max=%d)", dif, min, max);
    return 0;
  }
  debug(DEBUG_TRACE, "MONITOR", "delta %d (min=%d, max=%d) : ok", dif, min, max);

  p = delta->prof0;
  delta->prof0 = delta->prof1;
  delta->prof1 = p;

  return (float)dif / max;
}

void delta_profile(delta_t *delta, unsigned char *gray) {
  int i, j, k;

  if (delta && gray) {
    k = 0;
    for (i = 0; i < delta->height; i++) {
      for (j = 0; j < delta->width; j++) {
        gray[k++] = delta->prof[(i >> 4) * delta->xprof + (j >> 4)] << 5;
      }
    }
  }
}
