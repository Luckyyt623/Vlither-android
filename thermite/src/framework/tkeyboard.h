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
#define GLFW_MOUSE_BUTTON_LEFT   0
#define GLFW_MOUSE_BUTTON_RIGHT  1
#define GLFW_MOUSE_BUTTON_MIDDLE 2
#endif

#endif
