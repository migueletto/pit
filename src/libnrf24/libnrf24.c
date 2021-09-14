#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "script.h"
#include "gpio.h"
#include "spi.h"
#include "nrf24.h"
#include "io.h"
#include "sys.h"
#include "thread.h"
#include "debug.h"
#include "xalloc.h"

#define MAX_BUF 32

#define TAG_NRF24  "NRF24"

#define NRF24_SEND 1
#define NRF24_RECV 2
#define NRF24_EXIT 3

#define OP_RECVADDR 1
#define OP_PAYLOAD  2
#define OP_SENDADDR 3
#define OP_WRITE    4
#define OP_POWER    5
#define OP_CHANNEL  6
#define OP_LISTEN   7
#define OP_DETAILS  8

typedef struct {
  nrf24_t *rf;
  uint8_t channel;
  int pe;
  script_ref_t ref;
  uint8_t payload[MAX_BUF];
  uint8_t payload_len;
  int first;
} libnrf24_t;

typedef struct {
  uint8_t op;
  uint8_t addr[5];
  uint8_t buf[MAX_BUF];
  uint8_t n;
} libnrf24_arg_t;

static int libnrf24_read(libnrf24_t *rf, int ev, uint8_t pipe) {
  script_arg_t ret;
  script_lstring_t lstr;
  uint8_t buf[MAX_BUF];
  uint32_t n;
  int r = 0;

  n = nrf24_getDynamicPayloadSize(rf->rf);
  if (n > 0) {
    debug(DEBUG_TRACE, "NRF24", "received %d byte(s) on pipe %d", n, pipe);
    if (n > MAX_BUF) n = MAX_BUF;
    nrf24_read(rf->rf, buf, n);
    lstr.s = (char *)buf;
    lstr.n = n;

    if (script_call(rf->pe, rf->ref, &ret, "IBIL", mkint(ev), 1, mkint(pipe), &lstr) == 0) {
      r = script_returned_value(&ret);
    }
  } else {
    debug(DEBUG_ERROR, "NRF24", "received empty packet ?");
  }

  return r;
}

static int libnrf24_loop(libnrf24_t *rf) {
  uint8_t pipe;
  int r = 0;

  while (nrf24_available(rf->rf, &pipe)) {
    if (rf->payload_len) {
      debug(DEBUG_TRACE, "NRF24", "writing ack payload to pipe %d", pipe);
      nrf24_writeAckPayload(rf->rf, pipe, rf->payload, rf->payload_len);
    }
    r = libnrf24_read(rf, NRF24_RECV, pipe);
  }

  return r;
}

static int libnrf24_action(void *arg) {
  libnrf24_t *rf;
  libnrf24_arg_t *targ;
  script_arg_t ret;
  uint8_t pipe;
  unsigned int n;
  
  rf = (libnrf24_t *)arg;

  if (nrf24_begin(rf->rf) == 0) {
    nrf24_setPALevel(rf->rf, RF24_PA_MIN);
    nrf24_setDataRate(rf->rf, RF24_250KBPS);
    nrf24_setAutoAck(rf->rf, 1);
    nrf24_setRetries(rf->rf, 5, 15);
    nrf24_enableDynamicPayloads(rf->rf);
    nrf24_setCRCLength(rf->rf, RF24_CRC_16);
    nrf24_setChannel(rf->rf, rf->channel);

    for (; !thread_must_end();) {
      if (thread_server_read((unsigned char **)&targ, &n) == -1) {
        break;
      }

      if (targ) {
        if (n == sizeof(libnrf24_arg_t)) {
          switch (targ->op) {
            case OP_RECVADDR:
              debug(DEBUG_INFO, "NRF24", "set recv address %02X:%02X:%02X:%02X:%02X pipe %d", targ->addr[4], targ->addr[3], targ->addr[2], targ->addr[1], targ->addr[0], targ->n);
              nrf24_openReadingPipe(rf->rf, targ->n, targ->addr);
              break;

            case OP_SENDADDR:
              debug(DEBUG_INFO, "NRF24", "set send address %02X:%02X:%02X:%02X:%02X", targ->addr[4], targ->addr[3], targ->addr[2], targ->addr[1], targ->addr[0]);
              nrf24_openWritingPipe(rf->rf, targ->addr);
              break;

            case OP_PAYLOAD:
              rf->payload_len = targ->n;
              if (targ->n) {
                memcpy(rf->payload, targ->buf, targ->n);
                if (rf->first) {
                  debug(DEBUG_INFO, "NRF24", "enabling ack payload");
                  nrf24_enableAckPayload(rf->rf);
                  rf->first = 0;
                }
              } else {
                debug(DEBUG_INFO, "NRF24", "disabling ack payload");
              }
              break;

            case OP_WRITE:
              debug(DEBUG_TRACE, "NRF24", "writing %d byte(s)", targ->n);
              if (!nrf24_write(rf->rf, targ->buf, targ->n, 0)) {
                debug(DEBUG_ERROR, "NRF24", "write failed");
                script_call(rf->pe, rf->ref, &ret, "IB", mkint(NRF24_SEND), 0);
              } else {
                if (!nrf24_available(rf->rf, &pipe)) {
                  debug(DEBUG_INFO, "NRF24", "ack without payload received");
                  script_call(rf->pe, rf->ref, &ret, "IB", mkint(NRF24_SEND), 1);
                } else {
                  debug(DEBUG_INFO, "NRF24", "ack with payload received");
                  libnrf24_read(rf, NRF24_SEND, pipe);
                }
              }
              break;

            case OP_POWER:
              debug(DEBUG_INFO, "NRF24", "power set to %d", targ->n);
              nrf24_setPALevel(rf->rf, targ->n);
              break;

            case OP_CHANNEL:
              debug(DEBUG_INFO, "NRF24", "channel set to %d", targ->n);
              rf->channel = targ->n;
              nrf24_setChannel(rf->rf, rf->channel);
              break;

            case OP_LISTEN:
              if (targ->n) {
                debug(DEBUG_INFO, "NRF24", "listen on");
                nrf24_startListening(rf->rf);
              } else {
                debug(DEBUG_INFO, "NRF24", "listen off");
                nrf24_stopListening(rf->rf);
              }
              break;

            case OP_DETAILS:
              nrf24_printDetails(rf->rf);
              break;
          }
        }
        xfree(targ);
      }

      if (libnrf24_loop(rf)) break;
      sys_usleep(100000);
    }
  }

  script_call(rf->pe, rf->ref, &ret, "I", mkint(NRF24_EXIT));
  script_remove_ref(rf->pe, rf->ref);
  nrf24_stopListening(rf->rf);
  nrf24_end(rf->rf);
  nrf24_destroy(rf->rf);
  xfree(rf);

  return 0;
}

static int libnrf24_create(int pe) {
  gpio_provider_t *gpio;
  spi_provider_t *spi;
  libnrf24_t *rf;
  script_int_t ce_pin, csn_pin, speed, channel, handle;
  script_ref_t ref;
  int r = -1;

  if ((gpio = script_get_pointer(pe, GPIO_PROVIDER)) == NULL) {
    debug(DEBUG_ERROR, "NRF24", "gpio provider not found");
  }

  if ((spi = script_get_pointer(pe, SPI_PROVIDER)) == NULL) {
    debug(DEBUG_ERROR, "NRF24", "spi provider not found");
  }

  if (gpio == NULL || spi == NULL) {
    return -1;
  }

  if (script_get_function(pe, 0, &ref) == 0 &&
      script_get_integer(pe, 1, &ce_pin) == 0 &&
      script_get_integer(pe, 2, &csn_pin) == 0 &&
      script_get_integer(pe, 3, &speed) == 0 &&
      script_get_integer(pe, 4, &channel) == 0) {

    if (channel >= 0 && channel < 125) {
      if ((rf = xcalloc(1, sizeof(libnrf24_t))) != NULL) {
        if ((rf->rf = nrf24_create(gpio, spi, ce_pin, csn_pin, speed)) != NULL) {
          rf->pe = pe;
          rf->ref = ref;
          rf->channel = channel;
          rf->first = 1;

          if ((handle = thread_begin(TAG_NRF24, libnrf24_action, rf)) != -1) {
            r = script_push_integer(pe, handle);
          } else {
            nrf24_destroy(rf->rf);
            xfree(rf);
          }
        } else {
          xfree(rf);
        }
      }
    } else {
      debug(DEBUG_ERROR, "NRF24", "invalid channel %d", channel);
    }
  }

  return r;
}

static int libnrf24_close(int pe) {
  script_int_t handle;
  int r;

  if (script_get_integer(pe, 0, &handle) == -1) return -1;
  r = thread_end(TAG_NRF24, handle);

  return script_push_boolean(pe, r == 0);
}

static int libnrf24_peer(int pe, int op) {
  libnrf24_arg_t targ;
  uint32_t a0, a1, a2, a3, a4;
  script_int_t handle, pipe = 0;
  char *s = NULL;
  int len, r = -1;

  if (script_get_integer(pe, 0, &handle) == 0 &&
      script_get_string(pe,  1, &s) == 0 &&
      (op == OP_SENDADDR || script_get_integer(pe, 2, &pipe) == 0)) {

    targ.op = op;
    targ.n = pipe; // only used for OP_RECVADDR

    if (s && strlen(s) == 14 && sscanf(s, "%02X:%02X:%02X:%02X:%02X", &a4, &a3, &a2, &a1, &a0) == 5) {
      targ.addr[0] = a0;
      targ.addr[1] = a1;
      targ.addr[2] = a2;
      targ.addr[3] = a3;
      targ.addr[4] = a4;
      len = sizeof(libnrf24_arg_t);
      r = thread_client_write(handle, (unsigned char *)&targ, len) == 0 ? 0 : -1;
    } else {
      debug(DEBUG_ERROR, "NRF24", "invalid address \"%s\"", s);
    }
  }

  if (s) xfree(s);

  return script_push_boolean(pe, r != -1);
}

static int libnrf24_recvaddr(int pe) {
  return  libnrf24_peer(pe, OP_RECVADDR);
}

static int libnrf24_sendaddr(int pe) {
  return  libnrf24_peer(pe, OP_SENDADDR);
}

static int libnrf24_payload(int pe) {
  libnrf24_arg_t targ;
  script_int_t handle;
  char *buf = NULL;
  int len, r = -1;

  if (script_get_integer(pe, 0, &handle) == 0 &&
      script_get_lstring(pe, 1, &buf, &len) == 0) {

    targ.op = OP_PAYLOAD;
    targ.n = len;

    if (buf) {
      if (targ.n > MAX_BUF) targ.n = MAX_BUF;
      if (targ.n) memcpy(targ.buf, buf, targ.n);
      len = sizeof(libnrf24_arg_t);
      r = thread_client_write(handle, (unsigned char *)&targ, len) == 0 ? 0 : -1;
    }
  }

  if (buf) xfree(buf);

  return script_push_boolean(pe, r != -1);
}

static int libnrf24_write(int pe) {
  libnrf24_arg_t targ;
  script_int_t handle;
  char *buf = NULL;
  int len, r = -1;

  if (script_get_integer(pe, 0, &handle) == 0 &&
      script_get_lstring(pe, 1, &buf, &len) == 0) {

    targ.op = OP_WRITE;
    targ.n = len;

    if (buf && targ.n) {
      if (targ.n > MAX_BUF) targ.n = MAX_BUF;
      memcpy(targ.buf, buf, targ.n);
      len = sizeof(libnrf24_arg_t);
      r = thread_client_write(handle, (unsigned char *)&targ, len) == 0 ? 0 : -1;
    }
  }

  if (buf) xfree(buf);

  return script_push_boolean(pe, r != -1);
}

static int libnrf24_power(int pe) {
  libnrf24_arg_t targ;
  script_int_t handle, power;
  int len, r = -1;

  if (script_get_integer(pe, 0, &handle) == 0 &&
      script_get_integer(pe, 1, &power) == 0) {

    switch (power) {
      case RF24_PA_MIN:
      case RF24_PA_LOW:
      case RF24_PA_HIGH:
      case RF24_PA_MAX:
        targ.op = OP_POWER;
        targ.n = power;
        len = sizeof(libnrf24_arg_t);
        r = thread_client_write(handle, (unsigned char *)&targ, len) == 0 ? 0 : -1;
        break;
      default:
        debug(DEBUG_ERROR, "NRF24", "invalid power setting %d", power);
        break;
    }
  }

  return script_push_boolean(pe, r != -1);
}

/*
  RF channel frequency is set according to the following formula:
  Freq = 2400 + CH
  Ex.:if you select 108 as your channel, the RF channel frequency would be 2508MHz (2400 + 108)
*/

static int libnrf24_channel(int pe) {
  libnrf24_arg_t targ;
  script_int_t handle, channel;
  int len, r = -1;

  if (script_get_integer(pe, 0, &handle) == 0 &&
      script_get_integer(pe, 1, &channel) == 0) {

    if (channel >= 0 && channel < 125) {
      targ.op = OP_CHANNEL;
      targ.n = channel;
      len = sizeof(libnrf24_arg_t);
      r = thread_client_write(handle, (unsigned char *)&targ, len) == 0 ? 0 : -1;
    } else {
      debug(DEBUG_ERROR, "NRF24", "invalid channel %d", channel);
    }
  }

  return script_push_boolean(pe, r != -1);
}

static int libnrf24_listen(int pe) {
  libnrf24_arg_t targ;
  script_int_t handle;
  int len, on, r = -1;

  if (script_get_integer(pe, 0, &handle) == 0 &&
      script_get_boolean(pe, 1, &on) == 0) {

    targ.op = OP_LISTEN;
    targ.n = on;
    len = sizeof(libnrf24_arg_t);
    r = thread_client_write(handle, (unsigned char *)&targ, len) == 0 ? 0 : -1;
  }

  return script_push_boolean(pe, r != -1);
}

static int libnrf24_details(int pe) {
  libnrf24_arg_t targ;
  script_int_t handle;
  int len, r = -1;

  if (script_get_integer(pe, 0, &handle) == 0) {
    targ.op = OP_DETAILS;
    len = sizeof(libnrf24_arg_t);
    r = thread_client_write(handle, (unsigned char *)&targ, len) == 0 ? 0 : -1;
  }

  return script_push_boolean(pe, r != -1);
}

int libnrf24_init(int pe, script_ref_t obj) {
  script_add_function(pe, obj, "create",   libnrf24_create);
  script_add_function(pe, obj, "close",    libnrf24_close);
  script_add_function(pe, obj, "recvaddr", libnrf24_recvaddr);
  script_add_function(pe, obj, "sendaddr", libnrf24_sendaddr);
  script_add_function(pe, obj, "payload",  libnrf24_payload);
  script_add_function(pe, obj, "listen",   libnrf24_listen);
  script_add_function(pe, obj, "write",    libnrf24_write);
  script_add_function(pe, obj, "power",    libnrf24_power);
  script_add_function(pe, obj, "channel",  libnrf24_channel);
  script_add_function(pe, obj, "details",  libnrf24_details);

  script_add_iconst(pe, obj, "SEND", NRF24_SEND);
  script_add_iconst(pe, obj, "RECV", NRF24_RECV);
  script_add_iconst(pe, obj, "EXIT", NRF24_EXIT);

  script_add_iconst(pe, obj, "POWER_MIN",  RF24_PA_MIN);
  script_add_iconst(pe, obj, "POWER_LOW",  RF24_PA_LOW);
  script_add_iconst(pe, obj, "POWER_HIGH", RF24_PA_HIGH);
  script_add_iconst(pe, obj, "POWER_MAX",  RF24_PA_MAX);

  return 0;
}
