#ifndef PIT_SPI_H
#define PIT_SPI_H

typedef struct spi_t spi_t;

#define SPI_PROVIDER "spi_provider"

typedef struct {
  spi_t *(*open)(int cs, int speed);
  int (*close)(spi_t *spi);
  int (*transfer)(spi_t *spi, uint8_t *txbuf, uint8_t *rxbuf, int len);
} spi_provider_t;

#endif
