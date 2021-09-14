#ifndef PIT_INPUT_H
#define PIT_INPUT_H

#define INPUT_PROVIDER "input_provider"

typedef struct input_t input_t;

typedef struct {
  input_t *(*create)(int num, int width, int height);
  int (*event)(input_t *input, int *x, int *y, uint32_t us);
  int (*destroy)(input_t *input);
} input_provider_t;

#endif
