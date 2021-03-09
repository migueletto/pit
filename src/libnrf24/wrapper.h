#ifdef __cplusplus
extern "C" {
#endif

#define NRF24_PA_MIN   0
#define NRF24_PA_LOW   1
#define NRF24_PA_HIGH  2
#define NRF24_PA_MAX   3

#define NRF24_1MBPS    0
#define NRF24_2MBPS    1
#define NRF24_250KBPS  2

#define NRF24_CRC_DISABLED  0
#define NRF24_CRC_8         1
#define NRF24_CRC_16        2

typedef struct nrf24_wrapper_t nrf24_wrapper_t;

nrf24_wrapper_t *nrf24_init(int ce, int csn);
int nrf24_deinit(nrf24_wrapper_t *wrapper);

int nrf24_begin(nrf24_wrapper_t *wrapper);
int nrf24_isChipConnected(nrf24_wrapper_t *wrapper);
void nrf24_startListening(nrf24_wrapper_t *wrapper);
void nrf24_stopListening(nrf24_wrapper_t *wrapper);
int nrf24_available(nrf24_wrapper_t *wrapper);
int nrf24_available_pipe(nrf24_wrapper_t *wrapper, uint8_t* pipe_num);
void nrf24_read(nrf24_wrapper_t *wrapper, void* buf, uint8_t len);
int nrf24_write(nrf24_wrapper_t *wrapper, const void* buf, uint8_t len, const int multicast);
void nrf24_openWritingPipe(nrf24_wrapper_t *wrapper, const uint8_t* address);
void nrf24_openReadingPipe(nrf24_wrapper_t *wrapper, uint8_t number, const uint8_t* address);
void nrf24_printDetails(nrf24_wrapper_t *wrapper);
void nrf24_powerDown(nrf24_wrapper_t *wrapper);
void nrf24_powerUp(nrf24_wrapper_t *wrapper);
int nrf24_writeAckPayload(nrf24_wrapper_t *wrapper, uint8_t pipe, const void* buf, uint8_t len);
void nrf24_whatHappened(nrf24_wrapper_t *wrapper, int *tx_ok, int *tx_fail, int *rx_ready);
int nrf24_testCarrier(nrf24_wrapper_t *wrapper);
int nrf24_isValid(nrf24_wrapper_t *wrapper);
void nrf24_closeReadingPipe(nrf24_wrapper_t *wrapper, uint8_t pipe);
void nrf24_setAddressWidth(nrf24_wrapper_t *wrapper, uint8_t a_width);
void nrf24_setRetries(nrf24_wrapper_t *wrapper, uint8_t delay, uint8_t count);
void nrf24_setChannel(nrf24_wrapper_t *wrapper, uint8_t channel);
void nrf24_setPayloadSize(nrf24_wrapper_t *wrapper, uint8_t size);
uint8_t nrf24_getDynamicPayloadSize(nrf24_wrapper_t *wrapper);
void nrf24_enableAckPayload(nrf24_wrapper_t *wrapper);
void nrf24_disableAckPayload(nrf24_wrapper_t *wrapper);
void nrf24_enableDynamicPayloads(nrf24_wrapper_t *wrapper);
void nrf24_disableDynamicPayloads(nrf24_wrapper_t *wrapper);
void nrf24_enableDynamicAck(nrf24_wrapper_t *wrapper);
int nrf24_isPVariant(nrf24_wrapper_t *wrapper);
void nrf24_setAutoAck(nrf24_wrapper_t *wrapper, int enable);
void nrf24_setAutoAck_pipe(nrf24_wrapper_t *wrapper, uint8_t pipe, int enable);
void nrf24_setPALevel(nrf24_wrapper_t *wrapper, uint8_t level, int lnaEnable);
int nrf24_setDataRate(nrf24_wrapper_t *wrapper, uint32_t speed);
void nrf24_setCRCLength(nrf24_wrapper_t *wrapper, uint8_t length);
void nrf24_disableCRC(nrf24_wrapper_t *wrapper);
int nrf24_isAckPayloadAvailable(nrf24_wrapper_t *wrapper);

#ifdef __cplusplus
}
#endif
