void lcdc_init(i2c_provider_t *i2cp, i2c_t *i2c, int cols, int rows);
void lcdc_finish(i2c_provider_t *i2cp, i2c_t *i2c);
void lcdc_clear(i2c_provider_t *i2cp, i2c_t *i2c, int bl);
void lcdc_home(i2c_provider_t *i2cp, i2c_t *i2c, int bl);
void lcdc_displaycontrol(i2c_provider_t *i2cp, i2c_t *i2c, int on, int cursor, int blink, int bl);
void lcdc_entrymode(i2c_provider_t *i2cp, i2c_t *i2c, int left, int increment, int bl);
void lcdc_scrollleft(i2c_provider_t *i2cp, i2c_t *i2c, int bl);
void lcdc_scrollright(i2c_provider_t *i2cp, i2c_t *i2c, int bl);
void lcdc_createchar(i2c_provider_t *i2cp, i2c_t *i2c, int location, unsigned char[], int bl);
void lcdc_setcursor(i2c_provider_t *i2cp, i2c_t *i2c, int col, int row, int bl); 
void lcdc_write(i2c_provider_t *i2cp, i2c_t *i2c, char *s, int n, int bl);