unsigned char *encode_jpeg(unsigned char *buf, int width, int height, int fwidth, int fheight, int quality, int *encoded_len);
unsigned char *decode_jpeg(unsigned char *buf, int len, int *decoded_len, int *width, int *height);
unsigned char *check_huffman_table(unsigned char *buf, int len, int *outlen);
