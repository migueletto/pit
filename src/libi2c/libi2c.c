#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "script.h"
#include "ptr.h"
#include "i2c.h"
#include "li2c.h"
#include "debug.h"
#include "xalloc.h"

#define TAG_I2C "i2c"

typedef struct {
  char *tag;
  i2c_t *i2c;
} libi2c_t;

static i2c_provider_t provider;

static void i2c_destructor(void *p) {
  libi2c_t *i2c;

  i2c = (libi2c_t *)p;
  if (i2c) {
    i2c_close(i2c->i2c);
    xfree(i2c);
  }
}

static int libi2c_open(int pe) {
  script_arg_t arg[2];
  libi2c_t *i2c;
  int bus, addr, ptr, r;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg[0]) == 0 &&
      script_get_value(pe, 1, SCRIPT_ARG_INTEGER, &arg[1]) == 0) {

    bus = arg[0].value.i;
    addr = arg[1].value.i;

    if ((i2c = xcalloc(1, sizeof(libi2c_t))) != NULL) {
      if ((i2c->i2c = i2c_open(bus, addr)) != NULL) {
        i2c->tag = TAG_I2C;

        if ((ptr = ptr_new(i2c, i2c_destructor)) != -1) {
          r = script_push_integer(pe, ptr);
        } else {
          i2c_close(i2c->i2c);
          xfree(i2c);
        }
      } else {
        xfree(i2c);
      }
    }
  }

  return r;
}

static int libi2c_close(int pe) {
  script_arg_t arg;
  int ptr, r;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg) == 0) {
    ptr = arg.value.i;
    if (ptr_free(ptr, TAG_I2C) == 0) {
      r = script_push_boolean(pe, 1);
    }
  }

  return r;
}

static int libi2c_read_reg8(int pe) {
  script_arg_t arg[2];
  libi2c_t *i2c;
  int r, ptr, reg, data;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg[0]) == 0 &&
      script_get_value(pe, 1, SCRIPT_ARG_INTEGER, &arg[1]) == 0) {

    ptr = arg[0].value.i;
    reg = arg[1].value.i;

    if ((i2c = ptr_lock(ptr, TAG_I2C)) != NULL) {
      if ((data = i2c_smbus_read_byte_data(i2c->i2c, reg)) != -1) {
        r = script_push_integer(pe, data);
      }
      ptr_unlock(ptr, TAG_I2C);
    }
  }

  return r;
}

static int libi2c_read_reg16(int pe) {
  script_arg_t arg[2];
  libi2c_t *i2c;
  int r, ptr, reg, data;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg[0]) == 0 &&
      script_get_value(pe, 1, SCRIPT_ARG_INTEGER, &arg[1]) == 0) {

    ptr = arg[0].value.i;
    reg = arg[1].value.i;

    if ((i2c = ptr_lock(ptr, TAG_I2C)) != NULL) {
      if ((data = i2c_smbus_read_word_data(i2c->i2c, reg)) != -1) {
        r = script_push_integer(pe, data);
      }
      ptr_unlock(ptr, TAG_I2C);
    }
  }

  return r;
}

static int libi2c_write_reg8(int pe) {
  script_arg_t arg[3];
  libi2c_t *i2c;
  int r, ptr, reg, data;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg[0]) == 0 &&
      script_get_value(pe, 1, SCRIPT_ARG_INTEGER, &arg[1]) == 0 &&
      script_get_value(pe, 2, SCRIPT_ARG_INTEGER, &arg[2]) == 0) {

    ptr = arg[0].value.i;
    reg = arg[1].value.i;
    data = arg[2].value.i;

    if ((i2c = ptr_lock(ptr, TAG_I2C)) != NULL) {
      if (i2c_smbus_write_byte_data(i2c->i2c, reg, data) != -1) {
        r = script_push_boolean(pe, 1);
      }
      ptr_unlock(ptr, TAG_I2C);
    }
  }

  return r;
}

static int libi2c_write_reg16(int pe) {
  script_arg_t arg[3];
  libi2c_t *i2c;
  int r, ptr, reg, data;

  r = -1;

  if (script_get_value(pe, 0, SCRIPT_ARG_INTEGER, &arg[0]) == 0 &&
      script_get_value(pe, 1, SCRIPT_ARG_INTEGER, &arg[1]) == 0 &&
      script_get_value(pe, 2, SCRIPT_ARG_INTEGER, &arg[2]) == 0) {

    ptr = arg[0].value.i;
    reg = arg[1].value.i;
    data = arg[2].value.i;

    if ((i2c = ptr_lock(ptr, TAG_I2C)) != NULL) {
      if (i2c_smbus_write_word_data(i2c->i2c, reg, data) != -1) {
        r = script_push_boolean(pe, 1);
      }
      ptr_unlock(ptr, TAG_I2C);
    }
  }

  return r;
}

int libi2c_load(void) {
  provider.open = i2c_open;
  provider.close = i2c_close;
  provider.write = i2c_write;
  provider.read = i2c_read;
  provider.read_byte_data = i2c_smbus_read_byte_data;
  provider.read_word_data = i2c_smbus_read_word_data;
  provider.write_byte_data = i2c_smbus_write_byte_data;
  provider.write_word_data = i2c_smbus_write_word_data;

  return 0;
}

int libi2c_init(int pe, script_ref_t obj) {
  debug(DEBUG_INFO, "I2C", "registering %s", I2C_PROVIDER);
  script_set_pointer(pe, I2C_PROVIDER, &provider);

  script_add_function(pe, obj, "open",        libi2c_open);
  script_add_function(pe, obj, "close",       libi2c_close);

  script_add_function(pe, obj, "read_reg8",   libi2c_read_reg8);
  script_add_function(pe, obj, "read_reg16",  libi2c_read_reg16);

  script_add_function(pe, obj, "write_reg8",  libi2c_write_reg8);
  script_add_function(pe, obj, "write_reg16", libi2c_write_reg16);

  return 0;
}
