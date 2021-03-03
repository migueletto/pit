typedef struct i2c_t i2c_t;

#define I2C_PROVIDER "i2c_provider"

typedef struct {
  i2c_t *(*open)(int bus, int addr);
  int (*close)(i2c_t *i2c);

  int (*write)(i2c_t *i2c, uint8_t *b, int len);
  int (*read)(i2c_t *i2c, uint8_t *b, int len);

  int32_t (*read_byte_data)(i2c_t *i2c, uint8_t command);
  int32_t (*read_word_data)(i2c_t *i2c, uint8_t command);

  int32_t (*write_byte_data)(i2c_t *i2c, uint8_t command, uint8_t value);
  int32_t (*write_word_data)(i2c_t *i2c, uint8_t command, uint16_t value);
} i2c_provider_t;
