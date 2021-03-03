i2c_t *i2c_open(int bus, int addr);
int i2c_close(i2c_t *i2c);

int i2c_write(i2c_t *i2c, uint8_t *b, int len);
int i2c_read(i2c_t *i2c, uint8_t *b, int len);

int32_t i2c_smbus_read_byte_data(i2c_t *i2c, uint8_t command);
int32_t i2c_smbus_read_word_data(i2c_t *i2c, uint8_t command);

int32_t i2c_smbus_write_byte_data(i2c_t *i2c, uint8_t command, uint8_t value);
int32_t i2c_smbus_write_word_data(i2c_t *i2c, uint8_t command, uint16_t value);
