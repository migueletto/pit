int sd1306_start(i2c_provider_t *i2cp, i2c_t *i2c);
int sd1306_stop(i2c_provider_t *i2cp, i2c_t *i2c);
int sd1306_contrast(i2c_provider_t *i2cp, i2c_t *i2c, uint8_t contrast);
int sd1306_printchar(i2c_provider_t *i2cp, i2c_t *i2c, int x, int y, uint8_t c, font_t *f, int fg, int bg);
int sd1306_cls(i2c_provider_t *i2cp, i2c_t *i2c, int bg);
void sd1306_display_size(int *width, int *height);
