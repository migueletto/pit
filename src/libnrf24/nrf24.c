#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "script.h"
#include "gpio.h"
#include "spi.h"
#include "nrf24.h"
#include "nRF24L01.h"
#include "sys.h"
#include "debug.h"
#include "xalloc.h"

#define max(a,b) ((a)>(b)?(a):(b))
#define min(a,b) ((a)<(b)?(a):(b))
#define _BV(x) (1<<(x))

#define ADDR_WIDTH 5

static const char *const rf24_datarate[]  = { "1MBPS", "2MBPS", "250KBPS" };
static const char *const rf24_crclength[] = { "Disabled", "8 bits", "16 bits" };
static const char *const rf24_pa_dbm[]    = { "PA_MIN", "PA_LOW", "PA_HIGH", "PA_MAX" };
static const char *const rf24_csn[]       = { "CE0 (PI Hardware Driven)", "CE1 (PI Hardware Driven)", "CE2 (PI Hardware Driven)", "Custom GPIO Software Driven" };

static const uint8_t child_pipe[] = {
  RX_ADDR_P0, RX_ADDR_P1, RX_ADDR_P2, RX_ADDR_P3, RX_ADDR_P4, RX_ADDR_P5
};

static const uint8_t child_payload_size[] = {
  RX_PW_P0, RX_PW_P1, RX_PW_P2, RX_PW_P3, RX_PW_P4, RX_PW_P5
};

static const uint8_t child_pipe_enable[] = {
  ERX_P0, ERX_P1, ERX_P2, ERX_P3, ERX_P4, ERX_P5
};

static void nrf24_setup_ce(nrf24_t *rf) {
  rf->gpio->setup(rf->ce_pin, GPIO_OUT);
}

static void nrf24_ce(nrf24_t *rf, int on) {
  rf->gpio->output(rf->ce_pin, on);
}

static void nrf24_transfer(nrf24_t *rf, int n) {
  if (rf->spip->transfer(rf->spi, rf->spi_txbuff, rf->spi_rxbuff, n) == -1) {
    rf->error = 1;
  }
}

int nrf24_error(nrf24_t *rf) {
  return rf->error;
}

nrf24_t *nrf24_create(gpio_provider_t *gpio, spi_provider_t *spip, int ce_pin, int csn_pin, int spi_speed) {
  nrf24_t *rf;

  if ((rf = xcalloc(1, sizeof(nrf24_t))) == NULL) {
    return NULL;
  }

  if ((rf->spi = spip->open(csn_pin, spi_speed)) == NULL) {
    xfree(rf);
    return NULL;
  }

  rf->gpio = gpio;
  rf->spip = spip;
  rf->ce_pin = ce_pin;
  rf->csn_pin = csn_pin;
  rf->spi_speed = spi_speed;
  rf->payload_size = 32;
  rf->dynamic_payloads_enabled = 0;
  rf->addr_width = ADDR_WIDTH;

  return rf;
}

int nrf24_destroy(nrf24_t *rf) {
  int r = -1;

  if (rf) {
    rf->spip->close(rf->spi);
    xfree(rf);
    r = 0;
  }

  return r;
}

static uint8_t nrf24_read_register_buf(nrf24_t *rf, uint8_t reg, uint8_t *buf, uint8_t len) {
  uint8_t status;
  uint8_t *prx = rf->spi_rxbuff;
  uint8_t *ptx = rf->spi_txbuff;
  uint8_t size = len + 1; // add register value to transmit buffer

  *ptx++ = R_REGISTER | (REGISTER_MASK & reg);
  while (len--) {
    *ptx++ = NOP; // dummy operation, just for reading
  }
  nrf24_transfer(rf, size);

  status = *prx++; // status is 1st byte of receive buffer

  // decrement before to skip status byte
  while (--size) {
    *buf++ = *prx++;
  }

  return status;
} 

static uint8_t nrf24_read_register(nrf24_t *rf, uint8_t reg) {
  uint8_t result;
  uint8_t *prx = rf->spi_rxbuff;
  uint8_t *ptx = rf->spi_txbuff;
  
  *ptx++ = R_REGISTER | (REGISTER_MASK & reg);
  *ptx++ = NOP; // dummy operation, just for reading

  nrf24_transfer(rf, 2);

  result = *(++prx); // result is 2nd byte of receive buffer

  return result;
}

static uint8_t nrf24_write_register(nrf24_t *rf, uint8_t reg, uint8_t value) {
  uint8_t status;
  uint8_t *prx = rf->spi_rxbuff;
  uint8_t *ptx = rf->spi_txbuff;

  *ptx++ = W_REGISTER | (REGISTER_MASK & reg);
  *ptx = value;

  nrf24_transfer(rf, 2);

  status = *prx++; // status is 1st byte of receive buffer

  return status;
}

static uint8_t nrf24_write_register_buf(nrf24_t *rf, uint8_t reg, const uint8_t *buf, uint8_t len) {
  uint8_t status;
  uint8_t *prx = rf->spi_rxbuff;
  uint8_t *ptx = rf->spi_txbuff;
  uint8_t size = len + 1; // add register value to transmit buffer

  *ptx++ = W_REGISTER | (REGISTER_MASK & reg);
  while (len--) *ptx++ = *buf++;

  nrf24_transfer(rf, size);
  status = *prx; // status is 1st byte of receive buffer

  return status;
}

static uint8_t nrf24_write_payload(nrf24_t *rf, const void *buf, uint8_t len, const uint8_t writeType) {
  uint8_t status;
  uint8_t *prx = rf->spi_rxbuff;
  uint8_t *ptx = rf->spi_txbuff;
  uint8_t size;

  uint8_t *current = (uint8_t *)buf;
  uint8_t data_len = min(len, rf->payload_size);
  uint8_t blank_len = rf->dynamic_payloads_enabled ? 0 : rf->payload_size - data_len;

  size = data_len + blank_len + 1; // add register value to transmit buffer

  *ptx++ =  W_TX_PAYLOAD;
  while (data_len--) *ptx++ = *current++;
  while (blank_len--) *ptx++ = 0;

  nrf24_transfer(rf, size);
  status = *prx; // status is 1st byte of receive buffer

  return status;
}

static uint8_t nrf24_read_payload(nrf24_t *rf, void *buf, uint8_t len) {
  uint8_t status;
  uint8_t *prx = rf->spi_rxbuff;
  uint8_t *ptx = rf->spi_txbuff;
  uint8_t size;

  uint8_t *current = (uint8_t *)buf;
  uint8_t data_len = min(len, rf->payload_size);
  uint8_t blank_len = rf->dynamic_payloads_enabled ? 0 : rf->payload_size - data_len;

  size = data_len + blank_len + 1; // add register value to transmit buffer

  *ptx++ =  R_RX_PAYLOAD;
  while (size--) *ptx++ = NOP;

  // size has been lost during while, re affect
  size = data_len + blank_len + 1; // add register value to transmit buffer

  nrf24_transfer(rf, size);

  // 1st byte is status
  status = *prx++;

  // decrement before to skip 1st status byte
  while (--size) *current++ = *prx++;

  return status;
}

void nrf24_flush_rx(nrf24_t *rf) {
  rf->spi_txbuff[0] = FLUSH_RX;
  nrf24_transfer(rf, 1);
}

void nrf24_flush_tx(nrf24_t *rf) {
  rf->spi_txbuff[0] = FLUSH_TX;
  nrf24_transfer(rf, 1);
}

static uint8_t nrf24_get_status(nrf24_t *rf) {
  rf->spi_txbuff[0] = NOP;
  nrf24_transfer(rf, 1);
  return rf->spi_rxbuff[0];
}

void nrf24_setChannel(nrf24_t *rf, uint8_t channel) {
  const uint8_t max_channel = 127;
  nrf24_write_register(rf, RF_CH, min(channel, max_channel));
}

void nrf24_setPayloadSize(nrf24_t *rf, uint8_t size) {
  const uint8_t max_payload_size = 32;
  rf->payload_size = min(size, max_payload_size);
}

uint8_t nrf24_getPayloadSize(nrf24_t *rf) {
  return rf->payload_size;
}

void nrf24_stopListening(nrf24_t *rf) {
  rf->error = 0;
  nrf24_ce(rf, 0);
  nrf24_flush_tx(rf);
  nrf24_flush_rx(rf);
  usleep(150);
  nrf24_write_register(rf, CONFIG, nrf24_read_register(rf, CONFIG) & ~_BV(PRIM_RX));
  usleep(150);
}

void nrf24_powerDown(nrf24_t *rf) {
  nrf24_ce(rf, 0);
  nrf24_write_register(rf, CONFIG, nrf24_read_register(rf, CONFIG) & ~_BV(PWR_UP));
}

void nrf24_powerUp(nrf24_t *rf) {
  int up = nrf24_read_register(rf, CONFIG) & _BV(PWR_UP);

  if (!up) {
    nrf24_write_register(rf, CONFIG, nrf24_read_register(rf, CONFIG | _BV(PWR_UP)));
    usleep(5000);
  }
}

void nrf24_startListening(nrf24_t *rf) {
  rf->error = 0;
  nrf24_powerUp(rf);
  nrf24_write_register(rf, CONFIG, nrf24_read_register(rf, CONFIG) | _BV(PRIM_RX));
  nrf24_write_register(rf, STATUS, _BV(RX_DR) | _BV(TX_DS) | _BV(MAX_RT));

  // restore the pipe0 adddress, if exists
  if (rf->pipe0_reading_address[0] > 0) {
    nrf24_write_register_buf(rf, RX_ADDR_P0, rf->pipe0_reading_address, rf->addr_width);  
  }
  // flush buffers
  nrf24_flush_rx(rf);
  nrf24_flush_tx(rf);

  nrf24_ce(rf, 1);

  // wait for the radio to come up
  usleep(150);
}

void nrf24_reUseTX(nrf24_t *rf) {
  nrf24_write_register(rf, STATUS, _BV(MAX_RT)); // clear max retry flag
  rf->spi_txbuff[0] = REUSE_TX_PL;
  nrf24_transfer(rf, 1);
  nrf24_ce(rf, 0); // re-Transfer packet
  nrf24_ce(rf, 1);
}

// Per the documentation, we want to set PTX Mode when not listening. Then all we do is write data and set CE high
// In this mode, if we can keep the FIFO buffers loaded, packets will transmit immediately (no 130us delay)
// Otherwise we enter Standby-II mode, which is still faster than standby mode
// Also, we remove the need to keep writing the config register over and over and delaying for 150 us each time if sending a stream of data

void nrf24_startFastWrite(nrf24_t *rf, const void *buf, uint8_t len, const int multicast) {
  nrf24_write_payload(rf, buf, len, multicast ? W_TX_PAYLOAD_NO_ACK : W_TX_PAYLOAD);
  nrf24_ce(rf, 1);
}

void nrf24_startWrite(nrf24_t *rf, const void *buf, uint8_t len, const int multicast) {
  nrf24_write_payload(rf, buf, len, multicast ? W_TX_PAYLOAD_NO_ACK : W_TX_PAYLOAD);
  nrf24_ce(rf, 1);
  usleep(10);
  nrf24_ce(rf, 0);
}

int nrf24_writeFast(nrf24_t *rf, const void *buf, uint8_t len, const int multicast) {
  //Block until the FIFO is NOT full.
  //Keep track of the MAX retries and set auto-retry if seeing failures
  //Return 0 so the user can control the retrys and set a timer or failure counter if required
  //The radio will auto-clear everything in the FIFO as long as CE remains high

  // Blocking only if FIFO is full. This will loop and block until TX is successful or fail
  while ((nrf24_get_status(rf) & (_BV(TX_FULL))) && !rf->error) {
    if (nrf24_get_status(rf) & _BV(MAX_RT)) {
      //reUseTX();                         // set re-transmit
      nrf24_write_register(rf, STATUS, _BV(MAX_RT));  // clear max retry flag
      return 0; // return 0. The previous payload has been retransmitted
                // from the user perspective, if you get a 0, just keep trying to send the same payload
    }
  }

  // start Writing
  nrf24_startFastWrite(rf, buf, len, multicast);

  return rf->error ? 0 : 1;
}

int nrf24_write(nrf24_t *rf, const void *buf, uint8_t len, const int multicast) {
  nrf24_startFastWrite(rf, buf, len, multicast);

  while ((!(nrf24_get_status(rf) & (_BV(TX_DS) | _BV(MAX_RT)))) && !rf->error);
  nrf24_ce(rf, 0);
  uint8_t status = nrf24_write_register(rf, STATUS, _BV(RX_DR) | _BV(TX_DS) | _BV(MAX_RT));

  // max retries exceeded
  if (status & _BV(MAX_RT)) {
    nrf24_flush_tx(rf); // only going to be 1 packet int the FIFO at a time using this method, so just flush
    return 0;
  }
  // TX OK 1 or 0
  return rf->error ? 0 : 1;
}

static uint32_t millis(void) {
  return sys_get_clock() / 1000;
}

// For general use, the interrupt flags are not important to clear
int nrf24_writeBlocking(nrf24_t *rf, const void *buf, uint8_t len, uint32_t timeout) {
  // Block until the FIFO is NOT full.
  // Keep track of the MAX retries and set auto-retry if seeing failures
  // This way the FIFO will fill up and allow blocking until packets go through
  // The radio will auto-clear everything in the FIFO as long as CE remains high

  uint32_t timer = millis();  // Get the time that the payload transmission started

  // blocking only if FIFO is full. This will loop and block until TX is successful or timeout
  while (((nrf24_get_status(rf) & ( _BV(TX_FULL)))) && !rf->error) {
    if (nrf24_get_status(rf) & _BV(MAX_RT)) { // If MAX Retries have been reached
      nrf24_reUseTX(rf);                      // Set re-transmit and clear the MAX_RT interrupt flag
      if (millis() - timer > timeout) return 0;  // If this payload has exceeded the user-defined timeout, exit and return 0
    }
  }

  // start Writing
  nrf24_startFastWrite(rf, buf, len, 0); // write the payload if a buffer is clear

  return rf->error ? 0 : 1;
}

int nrf24_txStandBy(nrf24_t *rf) {
  while ((!(nrf24_read_register(rf, FIFO_STATUS) & _BV(TX_EMPTY))) && !rf->error) {
    if (nrf24_get_status(rf) & _BV(MAX_RT)) {
      nrf24_write_register(rf, STATUS, _BV(MAX_RT));
      nrf24_ce(rf, 0);
      nrf24_flush_tx(rf); // Non blocking, flush the data
      return 0;
    }
  }

  nrf24_ce(rf, 0); // set STANDBY-I mode
  return rf->error ? 0 : 1;
}

int nrf24_txStandBy_timeout(nrf24_t *rf, uint32_t timeout) {
  uint32_t start = millis();

  while ((!(nrf24_read_register(rf, FIFO_STATUS) & _BV(TX_EMPTY))) && !rf->error) {
    if (nrf24_get_status(rf) & _BV(MAX_RT)) {
      nrf24_write_register(rf, STATUS, _BV(MAX_RT));
      nrf24_ce(rf, 0); // set re-transmit
      nrf24_ce(rf, 1);
      if (millis() - start >= timeout) {
        nrf24_ce(rf, 0);
        nrf24_flush_tx(rf);
        return 0;
      }
    }
  }
  nrf24_ce(rf, 0); // set STANDBY-I mode
  return rf->error ? 0 : 1;
}

void nrf24_maskIRQ(nrf24_t *rf, int tx, int fail, int rx) {
  nrf24_write_register(rf, CONFIG, nrf24_read_register(rf, CONFIG) | fail << MASK_MAX_RT | tx << MASK_TX_DS | rx << MASK_RX_DR);
}

uint8_t nrf24_getDynamicPayloadSize(nrf24_t *rf) {
  rf->spi_txbuff[0] = R_RX_PL_WID;
  rf->spi_rxbuff[1] = 0xff;

  nrf24_transfer(rf, 2);

  if (rf->spi_rxbuff[1] > 32) {
    nrf24_flush_rx(rf);
    return 0;
  }

  return rf->spi_rxbuff[1];
}

int nrf24_available(nrf24_t *rf, uint8_t *pipe_num) {
  // check the FIFO buffer to see if data is waitng to be read
  if (!(nrf24_read_register(rf, FIFO_STATUS) & _BV(RX_EMPTY))) {
    // if the caller wants the pipe number, include that
    if (pipe_num) {
      uint8_t status = nrf24_get_status(rf);
      *pipe_num = (status >> RX_P_NO) & 0b111;
    }
    return 1;
  }

  return 0;
}

void nrf24_read(nrf24_t *rf, void *buf, uint8_t len) {
  // fetch the payload
  nrf24_read_payload(rf, buf, len);

  // clear the two possible interrupt flags with one command
  nrf24_write_register(rf, STATUS, _BV(RX_DR) | _BV(MAX_RT) | _BV(TX_DS));
}

void nrf24_whatHappened(nrf24_t *rf, int *tx_ok, int *tx_fail, int *rx_ready) {
  // read the status & reset the status in one easy call
  // or is that such a good idea?
  //uint8_t status = nrf24_write_register(rf, STATUS, _BV(RX_DR) | _BV(TX_DS) | _BV(MAX_RT));

  uint8_t status = nrf24_get_status(rf);

  // TX_DS: Data Sent TX FIFO interrupt. Set high when packet sent on TX.
  // If AUTO_ACK is activated, this bit will be set high only when ACK is received.
  *tx_ok = (status & _BV(TX_DS)) ? 1 : 0;

  // Maximum number of TX retries interrupt.
  *tx_fail = (status & _BV(MAX_RT)) ? 1 : 0;

  // Data Ready RX FIFO interrupt. Set high when new data arrives.
  *rx_ready = (status & _BV(RX_DR)) ? 1 : 0;
}

void nrf24_setAddressWidth(nrf24_t *rf, uint8_t a_width) {
  if (a_width -= 2) {
    nrf24_write_register(rf, SETUP_AW, a_width % 4);
    rf->addr_width = (a_width % 4) + 2;
  }
}

void nrf24_openWritingPipe(nrf24_t *rf, uint8_t *address) {
  //nrf24_write_register_buf(rf, RX_ADDR_P0, address, rf->addr_width);
  nrf24_write_register_buf(rf, TX_ADDR, address, rf->addr_width);
  nrf24_write_register(rf, RX_PW_P0, rf->payload_size);
}

void nrf24_openReadingPipe(nrf24_t *rf, uint8_t child, uint8_t *address) {
  // if this is pipe 0, cache the address.  This is needed because
  // openWritingPipe() will overwrite the pipe 0 address, so
  // startListening() will have to restore it.
  if (child == 0) {
    memcpy(rf->pipe0_reading_address, address, rf->addr_width);
  }

  if (child <= 6) {
    // for pipes 2-5, only write the LSB
    if (child < 2) {
      nrf24_write_register_buf(rf, child_pipe[child], address, rf->addr_width);
    } else {
      nrf24_write_register_buf(rf, child_pipe[child], address, 1);
    }

    nrf24_write_register(rf, child_payload_size[child], rf->payload_size);

    // note it would be more efficient to set all of the bits for all open
    // pipes at once.  However, I thought it would make the calling code
    // more simple to do it this way.
    nrf24_write_register(rf, EN_RXADDR, nrf24_read_register(rf, EN_RXADDR) | _BV(child_pipe_enable[child]));
  }
}

static void nrf24_toggle_features(nrf24_t *rf) {
  rf->spi_txbuff[0] = ACTIVATE;
  rf->spi_txbuff[1] = 0x73;
  nrf24_transfer(rf, 2);
}

void nrf24_enableDynamicPayloads(nrf24_t *rf) {
  nrf24_toggle_features(rf);
  nrf24_write_register(rf, FEATURE, nrf24_read_register(rf, FEATURE) | _BV(EN_DPL));

  // enable dynamic payload on all pipes
  // not sure the use case of only having dynamic payload on certain
  // pipes, so the library does not support it.
  nrf24_write_register(rf, DYNPD, nrf24_read_register(rf, DYNPD) | _BV(DPL_P5) | _BV(DPL_P4) | _BV(DPL_P3) | _BV(DPL_P2) | _BV(DPL_P1) | _BV(DPL_P0));

  rf->dynamic_payloads_enabled = 1;
}

void nrf24_enableAckPayload(nrf24_t *rf) {
  nrf24_toggle_features(rf);
  nrf24_write_register(rf, FEATURE, nrf24_read_register(rf, FEATURE) | _BV(EN_ACK_PAY) | _BV(EN_DPL));

  // enable dynamic payload on pipes 0 & 1
  rf->dynamic_payloads_enabled = 1;
  nrf24_write_register(rf, DYNPD, nrf24_read_register(rf, DYNPD) | _BV(DPL_P1) | _BV(DPL_P0));
}

void nrf24_enableDynamicAck(nrf24_t *rf) {
  // enable dynamic ack features
  nrf24_toggle_features(rf);
  nrf24_write_register(rf, FEATURE, nrf24_read_register(rf, FEATURE) | _BV(EN_DYN_ACK));
}

void nrf24_writeAckPayload(nrf24_t *rf, uint8_t pipe, const void *buf, uint8_t len) {
  uint8_t *ptx = rf->spi_txbuff;
  uint8_t size;

  uint8_t *current = (uint8_t *)buf;
  uint8_t data_len = min(len, rf->payload_size);
  size = data_len + 1; // add register value to transmit buffer

  *ptx++ = W_ACK_PAYLOAD | (pipe & 0b111);
  while (data_len--) {
    *ptx++ = *current++;
  }

  nrf24_transfer(rf, size);
}

int nrf24_isAckPayloadAvailable(nrf24_t *rf) {
  return !nrf24_read_register(rf, FIFO_STATUS) & _BV(RX_EMPTY);
}

void nrf24_setAutoAck(nrf24_t *rf, int enable) {
  if (enable) {
    nrf24_write_register(rf, EN_AA, 0b111111);
  } else {
    nrf24_write_register(rf, EN_AA, 0);
  }
}

void nrf24_setAutoAck_pipe(nrf24_t *rf, uint8_t pipe, int enable) {
  if (pipe <= 6) {
    uint8_t en_aa = nrf24_read_register(rf, EN_AA);
    if (enable) {
      en_aa |= _BV(pipe);
    } else {
      en_aa &= ~_BV(pipe);
    }
    nrf24_write_register(rf, EN_AA, en_aa);
  }
}

int nrf24_testCarrier(nrf24_t *rf) {
  return (nrf24_read_register(rf, CD) & 1) ? 1 : 0;
}

int nrf24_testRPD(nrf24_t *rf) {
  return (nrf24_read_register(rf, RPD) & 1) ? 1 : 0;
}

void nrf24_setPALevel(nrf24_t *rf, uint8_t level) {
  uint8_t setup = nrf24_read_register(rf, RF_SETUP) & 0b11111000;

  if (level > 3) {                   // if invalid level, go to max PA
    level = (RF24_PA_MAX << 1) + 1;  // +1 to support the SI24R1 chip extra bit
  } else {
    level = (level << 1) + 1;        // else set level as requested
  }

  nrf24_write_register(rf, RF_SETUP, setup |= level);  // write it to the chip
}

uint8_t nrf24_getPALevel(nrf24_t *rf) {
  return (nrf24_read_register(rf, RF_SETUP) & (_BV(RF_PWR_LOW) | _BV(RF_PWR_HIGH))) >> 1;
}

int nrf24_setDataRate(nrf24_t *rf, int rate) {
  int result = 0;
  uint8_t setup = nrf24_read_register(rf, RF_SETUP);

  // HIGH and LOW '00' is 1Mbs - our default
  setup &= ~(_BV(RF_DR_LOW) | _BV(RF_DR_HIGH));
  if (rate == RF24_250KBPS) {
    // Must set the RF_DR_LOW to 1; RF_DR_HIGH (used to be RF_DR) is already 0
    // Making it '10'.
    setup |= _BV(RF_DR_LOW);

  } else {
    // Set 2Mbs, RF_DR (RF_DR_HIGH) is set 1
    // Making it '01'
    if (rate == RF24_2MBPS) {
      setup |= _BV(RF_DR_HIGH);
    }
  }
  nrf24_write_register(rf, RF_SETUP, setup);

  // Verify our result
  if (nrf24_read_register(rf, RF_SETUP) == setup) {
    result = 1;
  }

  return result;
}

int nrf24_getDataRate(nrf24_t *rf) {
  int result;
  uint8_t dr = nrf24_read_register(rf, RF_SETUP) & (_BV(RF_DR_LOW) | _BV(RF_DR_HIGH));

  // Order matters in our case below
  if (dr == _BV(RF_DR_LOW)) {
    // '10' = 250KBPS
    result = RF24_250KBPS;
  } else if (dr == _BV(RF_DR_HIGH)) {
    // '01' = 2MBPS
    result = RF24_2MBPS;
  } else {
    // '00' = 1MBPS
    result = RF24_1MBPS;
  }
  return result;
}

void nrf24_setCRCLength(nrf24_t *rf, int length) {
  uint8_t config = nrf24_read_register(rf, CONFIG) & ~( _BV(CRCO) | _BV(EN_CRC));

  if (length == RF24_CRC_DISABLED) {
    // do nothing, we turned it off above.
  } else if (length == RF24_CRC_8) {
    config |= _BV(EN_CRC);
  } else {
    config |= _BV(EN_CRC);
    config |= _BV( CRCO );
  }
  nrf24_write_register(rf, CONFIG, config);
}

int nrf24_getCRCLength(nrf24_t *rf) {
  int result = RF24_CRC_DISABLED;
  uint8_t config = nrf24_read_register(rf, CONFIG) & ( _BV(CRCO) | _BV(EN_CRC));

  if (config & _BV(EN_CRC )) {
    if (config & _BV(CRCO)) {
      result = RF24_CRC_16;
    } else {
      result = RF24_CRC_8;
    }
  }

  return result;
}

void nrf24_disableCRC(nrf24_t *rf) {
  uint8_t disable = nrf24_read_register(rf, CONFIG) & ~_BV(EN_CRC);
  nrf24_write_register(rf, CONFIG, disable);
}

void nrf24_setRetries(nrf24_t *rf, uint8_t delay, uint8_t count) {
  // ARD (bits 7-4): Auto Re-transmit Delay (Delay defined from end of transmission to start of next transmission)
  //   0000 : Wait  250 + 86uS
  //   0001 : Wait  500 + 86uS
  //   ....
  //   1111 : Wait 4000 + 86uS
  // ARC (bits 3-0): Auto Retransmit Count on fail of AA (0000 to disable retransmit)
  nrf24_write_register(rf, SETUP_RETR, (delay & 0xf) << ARD | (count & 0xf) << ARC);
}

int nrf24_begin(nrf24_t *rf) {
  // NRF24 wants mode 0, MSB first and default to 1 Mbps

  // Initialise the CE pin of NRF24 (chip enable) after the CSN pin, so that
  // The input mode is not changed if using one of the hardware CE pins
  nrf24_setup_ce(rf);
  nrf24_ce(rf, 0);

  // wait 100ms
  usleep(100000);

  nrf24_write_register(rf, FEATURE, 0);

  // Set 1500uS (minimum for 32B payload in ESB@250KBPS) timeouts, to make testing a little easier
  // WARNING: If this is ever lowered, either 250KBS mode with AA is broken or maximum packet
  // sizes must never be used. See documentation for a more complete explanation.
  nrf24_setRetries(rf, 5, 15);

  if (!nrf24_setDataRate(rf, RF24_250KBPS)) {
    debug(DEBUG_ERROR, "NRF24", "could not set data rate");
    return -1;
  }
  nrf24_setCRCLength(rf, RF24_CRC_16);

  nrf24_toggle_features(rf);
  nrf24_write_register(rf, FEATURE, 0);
  nrf24_write_register(rf, DYNPD, 0);

  nrf24_write_register(rf, STATUS, _BV(RX_DR) | _BV(TX_DS) | _BV(MAX_RT));
  nrf24_setChannel(rf, 76);

  nrf24_flush_rx(rf);
  nrf24_flush_tx(rf);

  nrf24_powerUp(rf);

  // Enable PTX, do not write CE high so radio will remain in standby I mode
  // (130us max to transition to RX or TX instead of 1500us from powerUp)
  // PTX should use only 22uA of power
  nrf24_write_register(rf, CONFIG, nrf24_read_register(rf, CONFIG) & ~_BV(PRIM_RX));

  return 0;
}

int nrf24_end(nrf24_t *rf) {
  nrf24_powerDown(rf);
  return 0;
}

void nrf24_clearStatus(nrf24_t *rf) {
  nrf24_write_register(rf, STATUS, _BV(RX_DR) | _BV(MAX_RT) | _BV(TX_DS));
}

static void nrf24_print_status(uint8_t status) {
  debug(DEBUG_INFO, "NRF24", "status = 0x%02x RX_DR=%x TX_DS=%x MAX_RT=%x RX_P_NO=%x TX_FULL=%x",
    status,
    (status & _BV(RX_DR))  ? 1 : 0,
    (status & _BV(TX_DS))  ? 1 : 0,
    (status & _BV(MAX_RT)) ? 1 : 0,
    (status >> RX_P_NO) & 0b111,
    (status & _BV(TX_FULL)) ? 1 : 0
  );
}

static void nrf24_print_byte_register(nrf24_t *rf, const char *name, uint8_t reg, int p) {
  uint8_t b = nrf24_read_register(rf, reg);
  debug(DEBUG_INFO, "NRF24", "%s = %s%02X", name, p ? "0x" : "", b);
}

static void nrf24_print_address_register(nrf24_t *rf, const char *name, uint8_t reg) {
  uint8_t addr[ADDR_WIDTH];

  nrf24_read_register_buf(rf, reg, addr, ADDR_WIDTH);
  debug(DEBUG_INFO, "NRF24", "%s = %02X:%02X:%02X:%02X:%02X", name, addr[0], addr[1], addr[2], addr[3], addr[4]);
}

void nrf24_printDetails(nrf24_t *rf) {
  debug(DEBUG_INFO, "NRF24", "SPI configuration");
  debug_indent(2);
  debug(DEBUG_INFO, "NRF24", "CSN pin = %s", rf24_csn[rf->csn_pin]);
  debug(DEBUG_INFO, "NRF24", "CE pin = Custom GPIO%d", rf->ce_pin);
  debug(DEBUG_INFO, "NRF24", "Clock speed = %d Hz", rf->spi_speed);
  debug_indent(-2);

  debug(DEBUG_INFO, "NRF24", "NRF configuration");
  debug_indent(2);
  nrf24_print_status(nrf24_get_status(rf));

  nrf24_print_address_register(rf, "RX_ADDR_P0", RX_ADDR_P0);
  nrf24_print_address_register(rf, "RX_ADDR_P1", RX_ADDR_P1);
  nrf24_print_byte_register(rf, "RX_ADDR_P2", RX_ADDR_P2, 0);
  nrf24_print_byte_register(rf, "RX_ADDR_P3", RX_ADDR_P3, 0);
  nrf24_print_byte_register(rf, "RX_ADDR_P4", RX_ADDR_P4, 0);
  nrf24_print_byte_register(rf, "RX_ADDR_P5", RX_ADDR_P5, 0);
  nrf24_print_address_register(rf, "TX_ADDR", TX_ADDR);

  nrf24_print_byte_register(rf, "RX_PW_P0",  RX_PW_P0, 1);
  nrf24_print_byte_register(rf, "RX_PW_P1",  RX_PW_P1, 1);
  nrf24_print_byte_register(rf, "RX_PW_P2",  RX_PW_P2, 1);
  nrf24_print_byte_register(rf, "RX_PW_P3",  RX_PW_P3, 1);
  nrf24_print_byte_register(rf, "RX_PW_P4",  RX_PW_P4, 1);
  nrf24_print_byte_register(rf, "RX_PW_P5",  RX_PW_P5, 1);
  nrf24_print_byte_register(rf, "EN_AA",     EN_AA, 1);
  nrf24_print_byte_register(rf, "EN_RXADDR", EN_RXADDR, 1);
  nrf24_print_byte_register(rf, "RF_CH",     RF_CH, 1);
  nrf24_print_byte_register(rf, "RF_SETUP",  RF_SETUP, 1);
  nrf24_print_byte_register(rf, "CONFIG",    CONFIG, 1);
  nrf24_print_byte_register(rf, "DYNPD",     DYNPD, 1);
  nrf24_print_byte_register(rf, "FEATURE",   FEATURE, 1);

  debug(DEBUG_INFO, "NRF24", "Data rate = %s", rf24_datarate[nrf24_getDataRate(rf)]);
  debug(DEBUG_INFO, "NRF24", "CRC length = %s", rf24_crclength[nrf24_getCRCLength(rf)]);
  debug(DEBUG_INFO, "NRF24", "PA power = %s", rf24_pa_dbm[nrf24_getPALevel(rf)]);
  debug_indent(-2);
}
