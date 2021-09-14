int sd1306_start(void *wire);
int sd1306_stop(void *wire);
int sd1306_contrast(void *wire, uint8_t contrast);
int sd1306_xy(void *wire, uint8_t x, uint8_t y);
int sd1306_printchar(void *wire, int x, int y, uint8_t c, font_t *f, uint32_t fg, uint32_t bg);
int sd1306_cls(void *wire, int width, int height, uint32_t bg);
int sd1306_row(void *wire, int width, uint8_t *p);
void sd1306_display_size(int *width, int *height);

int sd1306_command(void *wire, uint8_t c);
int sd1306_data(void *wire, uint8_t *d, int len);
