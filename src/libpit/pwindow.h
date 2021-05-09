#ifndef PIT_WINDOW_H
#define PIT_WINDOW_H

#ifdef __cplusplus
extern "C" {
#endif

#define WINDOW_PROVIDER "window_provider"

#define WINDOW_KEY         1
#define WINDOW_BUTTON      2
#define WINDOW_MOTION      3
#define WINDOW_KEYDOWN     4
#define WINDOW_KEYUP       5
#define WINDOW_BUTTONDOWN  6
#define WINDOW_BUTTONUP    7
#define WINDOW_CUSTOM      0xFF

#define MOD_SHIFT      0x01
#define MOD_CTRL       0x02
#define MOD_LALT       0x04
#define MOD_RALT       0x08

#define KEY_UP         0x81
#define KEY_DOWN       0x82
#define KEY_LEFT       0x83
#define KEY_RIGHT      0x84
#define KEY_PGUP       0x85
#define KEY_PGDOWN     0x86
#define KEY_HOME       0x87
#define KEY_END        0x88

#define KEY_F1         0x91
#define KEY_F2         0x92
#define KEY_F3         0x93
#define KEY_F4         0x94
#define KEY_F5         0x95
#define KEY_F6         0x96
#define KEY_F7         0x97
#define KEY_F8         0x98
#define KEY_F9         0x99
#define KEY_F10        0x9A

#define KEY_SHIFT      0xA1
#define KEY_CTRL       0xA2
#define KEY_LALT       0xA3
#define KEY_RALT       0xA4

typedef void *window_t;
typedef struct texture_t texture_t;

typedef struct {
  window_t *(*create)(int encoding, int *width, int *height, int rotate, int fullscreen, int software);

  int (*draw)(window_t *window, uint32_t *raw, int width, int height);

  int (*event)(window_t *window, int wait, int remove, int *key, int *mods, int *buttons);

  int (*destroy)(window_t *window);

  int (*draw2)(window_t *window, uint32_t *raw, int x, int y, int width, int height, int pitch);

  int (*erase)(window_t *window, uint32_t bg);

  int (*render)(window_t *_window);

  int (*background)(window_t *window, uint32_t *raw, int width, int height);

  texture_t *(*create_texture)(window_t *window, int width, int height);

  int (*destroy_texture)(window_t *_window, texture_t *texture);

  int (*update_texture)(window_t *_window, texture_t *texture, uint8_t *raw);

  int (*draw_texture)(window_t *_window, texture_t *texture, int x, int y);

  void (*status)(window_t *window, int *x, int *y, int *buttons);

  void (*title)(window_t *window, char *title);

  char *(*clipboard)(window_t *window, char *clipboard, int len);

  int (*event2)(window_t *window, int wait, int *arg1, int *arg2);

  int (*mixer_init)(void);
  
  int (*mixer_play)(uint8_t *buf, uint32_t len, int volume);

  int (*mixer_stop)(void);

} window_provider_t;

#ifdef __cplusplus
}
#endif

#endif
