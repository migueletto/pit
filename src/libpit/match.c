#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "script.h"
#include "sys.h"
#include "match.h"
#include "debug.h"

#define MAX_BUF 1024

int script_match_fd(int fd, int pe, script_ref_t ref) {
  script_arg_t args[3], ret;
  unsigned char buf[MAX_BUF];
  unsigned char line[MAX_BUF];
  int i, num, n, len, r = 0;

  debug(DEBUG_INFO, "MATCH", "begin");
  len = 0;
  num = 1;

  args[0].type = SCRIPT_ARG_INTEGER;
  args[1].type = SCRIPT_ARG_LSTRING;
  args[1].value.l.s = (char *)line;
  args[2].type = SCRIPT_ARG_BOOLEAN;

  for (;;) {
    n = sys_read(fd, buf, sizeof(buf));
    if (n == -1) {
      debug_errno("MATCH", "read");
      r = -1;
      break;
    }
    if (n == 0) {
      debug(DEBUG_INFO, "MATCH", "end");
      break;
    }

    for (i = 0; i < n; i++) {
      if (buf[i] == '\n') {
        args[0].value.i = num;
        args[1].value.l.n = len;
        args[2].value.i = 0;
        if (script_call_args(pe, ref, &ret, 2, args) == -1) {
          r = -1;
          break;
        }
        if (script_returned_value(&ret)) {
          debug(DEBUG_INFO, "MATCH", "break");
          break;
        }
        len = 0;
        num++;
      } else if (buf[i] == 9 || buf[i] >= 32) {
        if (len < MAX_BUF) {
          line[len] = buf[i];
          len++;
        }
      }
    }
  }

  if (r == 0) {
    args[0].value.i = num;
    args[1].value.l.n = len;
    args[2].value.i = 1;
    if (script_call_args(pe, ref, &ret, 3, args) == -1) {
      r = -1;
    }
  }

  return r;
}
