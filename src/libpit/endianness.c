int little_endian(void) {
  int i = 1;
  return *((char *)&i);
}
