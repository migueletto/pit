#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <time.h>

#include "sys.h"
#include "io.h"
#include "i2c.h"
#include "li2c.h"
#include "sim.h"
#include "xalloc.h"
#include "debug.h"

#define SIM_I2C  "i2c%d"
#define I2C_MASK "/dev/i2c-%d"

#define I2C_SLAVE  0x0703  // Change slave address
#define I2C_SMBUS  0x0720  // SMBus-level access

struct i2c_t {
  int fd;
  int simulate;
};

union i2c_smbus_data {
  uint8_t byte;
  uint16_t word;
};

// smbus_access read or write markers
#define I2C_SMBUS_WRITE  0
#define I2C_SMBUS_READ   1

// SMBus transaction types
#define I2C_SMBUS_BYTE_DATA   2 
#define I2C_SMBUS_WORD_DATA   3

struct i2c_smbus_ioctl_data {
  char read_write;
  uint8_t command;
  int size;
  union i2c_smbus_data *data;
};

static int i2c_sim_ioctl(int fd, struct i2c_smbus_ioctl_data *args) {
  uint8_t buf[256];
  int r = -1;

  switch (args->read_write) {
    case I2C_SMBUS_WRITE:
      switch (args->size) {
        case I2C_SMBUS_BYTE_DATA:
          buf[0] = args->command;
          buf[1] = args->data->byte;
          r = sim_write(fd, buf, 2) == 2 ? 0 : -1;
          break;
        case I2C_SMBUS_WORD_DATA:
          buf[0] = args->command;
          buf[1] = args->data->word >> 8; 
          buf[2] = args->data->word & 0xFF;
          r = sim_write(fd, buf, 3) == 3 ? 0 : -1;
          break;
        default:
          debug(DEBUG_ERROR, "I2C", "simulated ioctl write size %d is not supported", args->size);
          break;
      }
      break;
    case I2C_SMBUS_READ:
      switch (args->size) {
        case I2C_SMBUS_BYTE_DATA:
          buf[0] = args->command;
          if (sim_write(fd, buf, 1) != 1) return -1;
          if (sim_read(fd, buf, 1) != 1) return -1;
          args->data->byte = buf[0];
          r = 0;
          break;
        case I2C_SMBUS_WORD_DATA:
          buf[0] = args->command;
          if (sim_write(fd, buf, 1) != 1) return -1;
          if (sim_read(fd, buf, 2) != 2) return -1;
          args->data->word = (((uint16_t)buf[0]) << 8) | buf[1];
          r = 0;
          break;
        default:
          debug(DEBUG_ERROR, "I2C", "simulated ioctl read size %d is not supported", args->size);
          break;
      }
      break;
  }

  return r;
}

static int32_t i2c_smbus_access(i2c_t *i2c, char read_write, uint8_t command, int size, union i2c_smbus_data *data) {
  struct i2c_smbus_ioctl_data args;

  args.read_write = read_write;
  args.command = command;
  args.size = size;
  args.data = data;

  if (i2c->simulate) {
    return i2c_sim_ioctl(i2c->fd, &args);
  }

  return ioctl(i2c->fd, I2C_SMBUS, &args);
}

int32_t i2c_smbus_read_byte_data(i2c_t *i2c, uint8_t command) {
  union i2c_smbus_data data;

  if (i2c_smbus_access(i2c, I2C_SMBUS_READ, command, I2C_SMBUS_BYTE_DATA, &data)) {
    return -1;
  } else {
    return data.byte;
  }
}

int32_t i2c_smbus_write_byte_data(i2c_t *i2c, uint8_t command, uint8_t value) {
  union i2c_smbus_data data;

  data.byte = value;
  return i2c_smbus_access(i2c, I2C_SMBUS_WRITE, command, I2C_SMBUS_BYTE_DATA, &data);
}

int32_t i2c_smbus_read_word_data(i2c_t *i2c, uint8_t command) {
  union i2c_smbus_data data;

  if (i2c_smbus_access(i2c, I2C_SMBUS_READ, command, I2C_SMBUS_WORD_DATA, &data)) {
    return -1;
  } else {
    return data.word;
  }
}

int32_t i2c_smbus_write_word_data(i2c_t *i2c, uint8_t command, uint16_t value) {
  union i2c_smbus_data data;

  data.word = value;
  return i2c_smbus_access(i2c, I2C_SMBUS_WRITE, command, I2C_SMBUS_WORD_DATA, &data);
}

i2c_t *i2c_open(int bus, int addr) {
  i2c_t *i2c;
  char buf[64];
  int r;

  if ((i2c = xcalloc(1, sizeof(i2c_t))) == NULL) {
    return NULL;
  }

  sprintf(buf, SIM_I2C, bus);
  if ((r = sim_connect(buf, addr)) == -1) {
    xfree(i2c);
    return NULL;
  }

  if (r > 0) {
    i2c->simulate = 1;
    return i2c;
  }

  sprintf(buf, I2C_MASK, bus);
  if ((i2c->fd = sys_open(buf, SYS_RDWR)) == -1) {
    debug_errno("I2C", "open \"%s\"", buf);
    xfree(i2c);
    return NULL;
  }

  if (ioctl(i2c->fd, I2C_SLAVE, addr) == -1) {
    debug_errno("I2C", "set slave addr 0x%02X", addr);
    sys_close(i2c->fd);
    xfree(i2c);
    return NULL;
  }

  return i2c;
}

int i2c_close(i2c_t *i2c) {
  if (i2c) {
    sys_close(i2c->fd);
    xfree(i2c);
    return 0;
  }

  return -1;
}

int i2c_write(i2c_t *i2c, uint8_t *b, int len) {
  if (i2c->simulate) {
    return sim_write(i2c->fd, b, len);
  }

  return sys_write(i2c->fd, b, len);
}

int i2c_read(i2c_t *i2c, uint8_t *b, int len) {
  if (i2c->simulate) {
    return sim_read(i2c->fd, b, len);
  }

  return sys_read(i2c->fd, b, len);
}
