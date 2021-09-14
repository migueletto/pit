#define RF24_PA_MIN   0
#define RF24_PA_LOW   1
#define RF24_PA_HIGH  2
#define RF24_PA_MAX   3
#define RF24_PA_ERROR 4

#define RF24_1MBPS    0
#define RF24_2MBPS    1
#define RF24_250KBPS  2

#define RF24_CRC_DISABLED 0
#define RF24_CRC_8        1
#define RF24_CRC_16       2

typedef struct {
  gpio_provider_t *gpio;
  spi_provider_t *spip;
  spi_t *spi;
  uint8_t ce_pin;       // Chip Enable pin, activates the RX or TX role
  uint8_t csn_pin;      // SPI Chip select
  uint32_t spi_speed;   // SPI Bus Speed
  uint8_t payload_size; // fixed size of payloads

  int dynamic_payloads_enabled;     // whether dynamic payloads are enabled
  uint8_t pipe0_reading_address[5]; // last address set on pipe 0 for reading.
  uint8_t addr_width;

  uint8_t spi_rxbuff[32+1]; // SPI receive buffer  (payload max 32 bytes)
  uint8_t spi_txbuff[32+1]; // SPI transmit buffer (payload max 32 bytes + 1 byte for the command)

  int error;
} nrf24_t;

nrf24_t *nrf24_create(gpio_provider_t *gpio, spi_provider_t *spip, int ce_pin, int csn_pin, int spi_speed);
int nrf24_error(nrf24_t *rf);
int nrf24_destroy(nrf24_t *rf);
int nrf24_begin(nrf24_t *rf);
int nrf24_end(nrf24_t *rf);
void nrf24_startListening(nrf24_t *rf);
void nrf24_stopListening(nrf24_t *rf);
int nrf24_write(nrf24_t *rf, const void* buf, uint8_t len, const int multicast);
int nrf24_available(nrf24_t *rf, uint8_t *pipe_num);
void nrf24_read(nrf24_t *rf, void *buf, uint8_t len);
void nrf24_openWritingPipe(nrf24_t *rf, uint8_t *address);
void nrf24_openReadingPipe(nrf24_t *rf, uint8_t child, uint8_t *address);
void nrf24_flush_rx(nrf24_t *rf);
void nrf24_flush_tx(nrf24_t *rf);
void nrf24_setRetries(nrf24_t *rf, uint8_t delay, uint8_t count);
void nrf24_setChannel(nrf24_t *rf, uint8_t channel);
void nrf24_setPayloadSize(nrf24_t *rf, uint8_t size);
uint8_t nrf24_getPayloadSize(nrf24_t *rf);
uint8_t nrf24_getDynamicPayloadSize(nrf24_t *rf);
void nrf24_enableAckPayload(nrf24_t *rf);
void nrf24_enableDynamicPayloads(nrf24_t *rf);
void nrf24_enableDynamicAck(nrf24_t *rf);
void nrf24_setAutoAck(nrf24_t *rf, int enable);
void nrf24_setAutoAck_pipe(nrf24_t *rf, uint8_t pipe, int enable);
void nrf24_setPALevel(nrf24_t *rf, uint8_t level);
uint8_t nrf24_getPALevel(nrf24_t *rf);
int nrf24_setDataRate(nrf24_t *rf, int rate);
int nrf24_getDataRate(nrf24_t *rf);
void nrf24_setCRCLength(nrf24_t *rf, int length);
int nrf24_getCRCLength(nrf24_t *rf);
void nrf24_disableCRC(nrf24_t *rf);
void nrf24_printDetails(nrf24_t *rf);
void nrf24_powerDown(nrf24_t *rf);
void nrf24_powerUp(nrf24_t *rf);
int nrf24_writeFast(nrf24_t *rf, const void* buf, uint8_t len, const int multicast);
int nrf24_writeBlocking(nrf24_t *rf, const void* buf, uint8_t len, uint32_t timeout);
int nrf24_txStandBy(nrf24_t *rf);
int nrf24_txStandBy_timeout(nrf24_t *rf, uint32_t timeout);
void nrf24_startFastWrite(nrf24_t *rf, const void *buf, uint8_t len, const int multicast);
void nrf24_startWrite(nrf24_t *rf, const void* buf, uint8_t len, const int multicast);
void nrf24_reUseTX(nrf24_t *rf);
void nrf24_writeAckPayload(nrf24_t *rf, uint8_t pipe, const void* buf, uint8_t len);
int nrf24_isAckPayloadAvailable(nrf24_t *rf);
void nrf24_whatHappened(nrf24_t *rf, int *tx_ok, int *tx_fail, int *rx_ready);
int nrf24_testCarrier(nrf24_t *rf);
int nrf24_testRPD(nrf24_t *rf);
void nrf24_maskIRQ(nrf24_t *rf, int tx, int fail, int rx);
void nrf24_setAddressWidth(nrf24_t *rf, uint8_t a_width);
void nrf24_clearStatus(nrf24_t *rf);
