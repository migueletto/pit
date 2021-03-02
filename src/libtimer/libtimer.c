#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>

#include "script.h"
#include "sys.h"
#include "thread.h"
#include "debug.h"
#include "xalloc.h"

#define TAG_TIMER  "TIMER"

typedef int (*timer_callback_f)(int pe, int finish, script_ref_t ref);

typedef struct {
  int64_t ts, period, sleep;
  unsigned int count;
  unsigned int dt;
  timer_callback_f callback;
  int pe;
  script_ref_t ref;
} timer_def_t;

static int timer_callback(int pe, int finish, script_ref_t ref) {
  script_arg_t ret;
  int r = 0;

  if (finish) {
    script_remove_ref(pe, ref);
    return 1;
  }

  if (script_call(pe, ref, &ret, "") == 0) {
    r = script_returned_value(&ret);
  } else {
    r = 0; // do not end thread on script error
  }

  return r;
}

static int timer_action(void *arg) {
  timer_def_t *t;
  int64_t now, delta;

  t = (timer_def_t *)arg;

  for (; !thread_must_end();) {
    now = sys_get_clock();
    delta = t->ts - now;

    if (delta <= 0) {
      // -delta us past the target
      if (t->callback(t->pe, 0, t->ref)) break;
      if (delta < -t->period) delta = -t->period;
      t->ts = now + t->period + delta;

    } else if (delta > t->sleep) {
      // more than t->sleep us to target
      sys_usleep(t->sleep);

    } else {
      // only delta us to target
      sys_usleep(delta);
    }
  }

  t->callback(t->pe, 1, t->ref);
  xfree(t);

  return 0;
}

PIT_LIB_FUNCTION(timer,create)
  PIT_LIB_PARAM_F(callback)
  PIT_LIB_PARAM_I(period)
  script_int_t handle = -1;
PIT_LIB_CODE
  timer_def_t *t;

  if (period > 0) {
    if ((t = xcalloc(1, sizeof(timer_def_t))) != NULL) {
      t->period = period * 1000l;
      if (t->period <= 10000l) {
        t->sleep = 1000l;
      } else if (t->period <= 100000l) {
        t->sleep = 10000l;
      } else if (t->period <= 1000000l) {
        t->sleep = 100000l;
      } else {
        t->sleep = 1000000l;
      }
      t->ts = sys_get_clock() + t->period;
      t->callback = timer_callback;
      t->pe = PIT_LIB_PE;
      t->ref = callback;

      if ((handle = thread_begin(TAG_TIMER, timer_action, t)) == -1) {
        xfree(t);
      } else {
        r = 0;
      }
    }
  }
PIT_LIB_END_I(handle)

PIT_LIB_FUNCTION(timer,close)
  PIT_LIB_PARAM_I(handle)
PIT_LIB_CODE
  r = thread_end(TAG_TIMER, handle);
PIT_LIB_END_B

PIT_LIB_BEGIN(timer)
  PIT_LIB_EXPORT_F(create);
  PIT_LIB_EXPORT_F(close);
PIT_LIB_END
