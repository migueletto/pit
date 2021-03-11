#ifndef PIT_DELTA_H
#define PIT_DELTA_H

typedef struct {
  int width, height;
  int xprof, yprof;
  int *profile0, *profile1;
  int *prof0, *prof1, *prof;
} delta_t;

delta_t *delta_init(int width, int height);
void delta_close(delta_t *delta);
float delta_image(delta_t *delta, unsigned char *gray, float factor);
void delta_profile(delta_t *delta, unsigned char *gray);

#endif
