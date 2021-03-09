#include <RF24/RF24.h>

#include "wrapper.h"
#include "debug.h"
#include "xalloc.h"

struct nrf24_wrapper_t {
  RF24 *radio;
};

nrf24_wrapper_t *nrf24_init(int ce, int csn) {
  nrf24_wrapper_t *wrapper;

  if ((wrapper = (nrf24_wrapper_t *)xcalloc(1, sizeof(nrf24_wrapper_t))) != NULL) {
    wrapper->radio = new RF24(ce, csn);
  }

  return wrapper;
}

int nrf24_deinit(nrf24_wrapper_t *wrapper) {
  if (wrapper) {
    delete wrapper->radio;
    xfree(wrapper);
  }

  return 0;
}

int nrf24_begin(nrf24_wrapper_t *wrapper) {
  return wrapper->radio->begin();
}

int nrf24_isChipConnected(nrf24_wrapper_t *wrapper) {
  return wrapper->radio->isChipConnected();
}

void nrf24_startListening(nrf24_wrapper_t *wrapper) {
  wrapper->radio->startListening();
}

void nrf24_stopListening(nrf24_wrapper_t *wrapper) {
  wrapper->radio->stopListening();
}

int nrf24_available(nrf24_wrapper_t *wrapper) {
  return wrapper->radio->available();
}

int nrf24_available_pipe(nrf24_wrapper_t *wrapper, uint8_t* pipe_num) {
  return wrapper->radio->available(pipe_num);
}

void nrf24_read(nrf24_wrapper_t *wrapper, void* buf, uint8_t len) {
  wrapper->radio->read(buf, len);
}

int nrf24_write(nrf24_wrapper_t *wrapper, const void* buf, uint8_t len, const int multicast) {
  return wrapper->radio->write(buf, len, multicast);
}

void nrf24_openWritingPipe(nrf24_wrapper_t *wrapper, const uint8_t* address) {
  wrapper->radio->openWritingPipe(address);
}

void nrf24_openReadingPipe(nrf24_wrapper_t *wrapper, uint8_t number, const uint8_t* address) {
  wrapper->radio->openReadingPipe(number, address);
}

void nrf24_printDetails(nrf24_wrapper_t *wrapper) {
}

void nrf24_powerDown(nrf24_wrapper_t *wrapper) {
  wrapper->radio->powerDown();
}

void nrf24_powerUp(nrf24_wrapper_t *wrapper) {
  wrapper->radio->powerUp();
}

int nrf24_writeAckPayload(nrf24_wrapper_t *wrapper, uint8_t pipe, const void* buf, uint8_t len) {
  return wrapper->radio->writeAckPayload(pipe, buf, len);
}

void nrf24_whatHappened(nrf24_wrapper_t *wrapper, int *tx_ok, int *tx_fail, int *rx_ready) {
  bool tok, tfail, rready;
  wrapper->radio->whatHappened(tok, tfail, rready);
  *tx_ok = tok;
  *tx_fail = tfail;
  *rx_ready = rready;
}

int nrf24_testCarrier(nrf24_wrapper_t *wrapper) {
  return wrapper->radio->testCarrier();
}

int nrf24_isValid(nrf24_wrapper_t *wrapper) {
  return wrapper->radio->isValid();
}

void nrf24_closeReadingPipe(nrf24_wrapper_t *wrapper, uint8_t pipe) {
  wrapper->radio->closeReadingPipe(pipe);
}

void nrf24_setAddressWidth(nrf24_wrapper_t *wrapper, uint8_t a_width) {
  wrapper->radio->setAddressWidth(a_width);
}

void nrf24_setRetries(nrf24_wrapper_t *wrapper, uint8_t delay, uint8_t count) {
  wrapper->radio->setRetries(delay, count);
}

void nrf24_setChannel(nrf24_wrapper_t *wrapper, uint8_t channel) {
  wrapper->radio->setChannel(channel);
}

void nrf24_setPayloadSize(nrf24_wrapper_t *wrapper, uint8_t size) {
  wrapper->radio->setPayloadSize(size);
}

uint8_t nrf24_getDynamicPayloadSize(nrf24_wrapper_t *wrapper) {
  return wrapper->radio->getDynamicPayloadSize();
}

void nrf24_enableAckPayload(nrf24_wrapper_t *wrapper) {
  wrapper->radio->enableAckPayload();
}

void nrf24_disableAckPayload(nrf24_wrapper_t *wrapper) {
  wrapper->radio->disableAckPayload();
}

void nrf24_enableDynamicPayloads(nrf24_wrapper_t *wrapper) {
  wrapper->radio->enableDynamicPayloads();
}

void nrf24_disableDynamicPayloads(nrf24_wrapper_t *wrapper) {
  wrapper->radio->disableDynamicPayloads();
}

void nrf24_enableDynamicAck(nrf24_wrapper_t *wrapper) {
  wrapper->radio->enableDynamicAck();
}

int nrf24_isPVariant(nrf24_wrapper_t *wrapper) {
  return wrapper->radio->isPVariant();
}

void nrf24_setAutoAck(nrf24_wrapper_t *wrapper, int enable) {
  wrapper->radio->setAutoAck(enable);
}

void nrf24_setAutoAck_pipe(nrf24_wrapper_t *wrapper, uint8_t pipe, int enable) {
  wrapper->radio->setAutoAck(pipe, enable);
}

void nrf24_setPALevel(nrf24_wrapper_t *wrapper, uint8_t level, int lnaEnable) {
  wrapper->radio->setPALevel(level, lnaEnable);
}

int nrf24_setDataRate(nrf24_wrapper_t *wrapper, uint32_t speed) {
  return wrapper->radio->setDataRate((rf24_datarate_e)speed);
}

void nrf24_setCRCLength(nrf24_wrapper_t *wrapper, uint8_t length) {
  wrapper->radio->setCRCLength((rf24_crclength_e)length);
}

void nrf24_disableCRC(nrf24_wrapper_t *wrapper) {
  wrapper->radio->disableCRC();
}

int nrf24_isAckPayloadAvailable(nrf24_wrapper_t *wrapper) {
  return wrapper->radio->isAckPayloadAvailable();
}
