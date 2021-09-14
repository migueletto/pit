#ifndef PIT_GPIO_H
#define PIT_GPIO_H

#define GPIO_BOARD     0
#define GPIO_BCM       1

#define GPIO_IN        0
#define GPIO_OUT       1
#define GPIO_LOW_OUT   2
#define GPIO_HIGH_OUT  3
#define GPIO_ALT0      4

#define GPIO_PROVIDER "gpio_provider"

typedef struct {
  int (*setmode)(int mode);
  int (*setup)(int pin, int direction);
  int (*output)(int pin, int value);
  int (*input)(int pin, int *value);
  int (*create_monitor)(int pe, int pin, int (*callback)(int pe, int pin, int value, script_ref_t ref, void *data), script_ref_t ref, void *data);
} gpio_provider_t;

#endif
