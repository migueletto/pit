#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "thread.h"
#include "script.h"
#include "main.h"
#include "sys.h"
#include "vfs.h"
#include "ptr.h"
#include "sig.h"
#include "endianness.h"
#include "debug.h"
#include "xalloc.h"

static void idle_loop(void) {
  unsigned char *buf;
  unsigned int len;

  for (; !thread_must_end();) {
    if (thread_server_read_timeout(100000, &buf, &len) == -1) break;
    if (buf) xfree(buf);
  }
}

int pit_main(int argc, char *argv[]) {
  char *script_engine, *debugfile;
  char *match_function;
  int pe, background, dlevel, err, i;
  int script_argc, status;
  char **script_argv, *d, *s;

  script_engine = NULL;
  script_argc = 0;
  script_argv = NULL;
  background = 0;
  debugfile = NULL;
  match_function = NULL;
  err = 0;

  for (i = 1; i < argc && !err; i++) {
    if (argv[i][0] == '-') {
      if (i < argc-1) {
        switch (argv[i][1]) {
          case 'b':
            background = 1;
            break;
          case 'f':
            debugfile = argv[++i];
            break;
          case 'd':
            d = argv[++i];
            dlevel = atoi(d);
            s = strchr(d, ':');
            debug_setsyslevel(s ? s+1 : NULL, dlevel);
            break;
          case 's':
            if (script_engine == NULL) {
              script_engine = argv[++i];
            } else {
              err = 1;
            }
            break;
          case 't':
            debug_scope(1);
            break;
          case 'm':
            match_function = argv[++i];
            break;
          default:
            err = 1;
        }
      } else {
        err = 1;
      }
    } else {
      script_argc = argc - i;
      script_argv = &argv[i];
      break;
    }
  }

  if (err || script_engine == NULL || script_argv == NULL) {
    fprintf(stderr, "%s\n", SYSTEM_NAME);
    fprintf(stderr, "usage: %s [ -b ] [ -f <debugfile> ] [ -d level ] -s <libname.so> [ <script> <arg> ... ]\n", argv[0]);
    return STATUS_ERROR;
  }

  sys_init();
  debug_init(debugfile);
  thread_init();
  ptr_init();

  debug(DEBUG_INFO, "MAIN", "%s starting on %s (%s endian)", SYSTEM_NAME, SYSTEM_OS, little_endian() ? "little" : "big");

  if (background && sys_daemonize() != 0) {
    debug_close();
    return STATUS_ERROR;
  }
  thread_setmain();

  sys_unblock_signals();
  signal_install_handlers();

  if (script_load_engine(script_engine) == -1) {
    debug(DEBUG_ERROR, "MAIN", "error exit");
    thread_close();
    debug_close();
    return STATUS_ERROR;
  }

  if (script_init() == -1) {
    debug(DEBUG_ERROR, "MAIN", "error exit");
    thread_close();
    debug_close();
    return STATUS_ERROR;
  }

  if ((pe = script_create()) == -1) {
    debug(DEBUG_ERROR, "MAIN", "error exit");
    thread_close();
    debug_close();
    return STATUS_ERROR;
  }

  vfs_init();

  if (script_run(pe, script_argv[0], script_argc-1, &script_argv[1]) == 0) {
    if (match_function) {
      script_match(pe, match_function);
    } else {
      debug(DEBUG_INFO, "MAIN", "idle loop begin");
      idle_loop();
      debug(DEBUG_INFO, "MAIN", "idle loop end");
    }
  } else {
    sys_set_finish(STATUS_ERROR);
  }

  sys_usleep(1000); // give threads a chance to increment thread count
  thread_wait_all();
  script_destroy(pe);
  script_finish();
  vfs_finish();
  status = thread_get_status();
  thread_close();
  debug(DEBUG_INFO, "MAIN", "%s stopping", SYSTEM_NAME);
  debug_close();

  return status;
}

int main(int argc, char *argv[]) {
  exit(pit_main(argc, argv));
}
