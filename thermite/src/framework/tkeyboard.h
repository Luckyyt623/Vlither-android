#ifndef TKEYBOARD_H
#define TKEYBOARD_H

#ifndef ANDROID
#include <GLFW/glfw3.h>
#endif

typedef struct twindow twindow;

typedef struct tkeyboard {
  int* keys_pressed;
  int* keys_released;
  char char_pressed;
} tkeyboard;

tkeyboard* tkeyboard_create(twindow* window);
void       tkeyboard_update(tkeyboard* keyboard);
int        tkeyboard_key_pressed(tkeyboard* keyboard, int key);
int        tkeyboard_key_released(tkeyboard* keyboard, int key);
void       tkeyboard_destroy(tkeyboard* keyboard);

/* Android: no physical keyboard — define GLFW key codes as stubs */
#ifdef ANDROID
#define GLFW_KEY_LEFT    263
#define GLFW_KEY_RIGHT   262
#define GLFW_KEY_UP      265
#define GLFW_KEY_DOWN    264
#define GLFW_KEY_SPACE    32
#define GLFW_KEY_F11     300
#define GLFW_KEY_N        78
#define GLFW_KEY_M        77
#define GLFW_KEY_H        72
#define GLFW_KEY_P        80
#define GLFW_KEY_F        70
#define GLFW_KEY_K        75
#define GLFW_KEY_T        84
#define GLFW_KEY_Z        90
#define GLFW_KEY_R        82
#define GLFW_KEY_Q        81
/* Letters */
#define GLFW_KEY_A        65
#define GLFW_KEY_B        66
#define GLFW_KEY_C        67
#define GLFW_KEY_D        68
#define GLFW_KEY_E        69
#define GLFW_KEY_G        71
#define GLFW_KEY_I        73
#define GLFW_KEY_J        74
#define GLFW_KEY_L        76
#define GLFW_KEY_O        79
#define GLFW_KEY_S        83
#define GLFW_KEY_U        85
#define GLFW_KEY_V        86
#define GLFW_KEY_W        87
#define GLFW_KEY_X        88
#define GLFW_KEY_Y        89
/* Digits */
#define GLFW_KEY_0        48
#define GLFW_KEY_1        49
#define GLFW_KEY_2        50
#define GLFW_KEY_3        51
#define GLFW_KEY_4        52
#define GLFW_KEY_5        53
#define GLFW_KEY_6        54
#define GLFW_KEY_7        55
#define GLFW_KEY_8        56
#define GLFW_KEY_9        57
/* Function keys */
#define GLFW_KEY_F1       290
#define GLFW_KEY_F2       291
#define GLFW_KEY_F3       292
#define GLFW_KEY_F4       293
#define GLFW_KEY_F5       294
#define GLFW_KEY_F6       295
#define GLFW_KEY_F7       296
#define GLFW_KEY_F8       297
#define GLFW_KEY_F9       298
#define GLFW_KEY_F10      299
#define GLFW_KEY_F12      301
/* Special */
#define GLFW_KEY_ESCAPE         256
#define GLFW_KEY_ENTER          257
#define GLFW_KEY_TAB            258
#define GLFW_KEY_BACKSPACE      259
#define GLFW_KEY_CAPS_LOCK      280
#define GLFW_KEY_LEFT_SHIFT     340
#define GLFW_KEY_LEFT_CONTROL   341
#define GLFW_KEY_LEFT_ALT       342
#define GLFW_MOUSE_BUTTON_LEFT   0
#define GLFW_MOUSE_BUTTON_RIGHT  1
#define GLFW_MOUSE_BUTTON_MIDDLE 2
#endif

#endif
