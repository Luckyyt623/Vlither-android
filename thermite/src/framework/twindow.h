#ifndef TWINDOW_H
#define TWINDOW_H

#include <cglm/struct.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct tkeyboard tkeyboard;
typedef struct tmouse    tmouse;
typedef struct twindow   twindow;
typedef struct tenv      tenv;

typedef void (*trender_func)(tenv* env);
typedef void (*tresize_func)(tenv* env);

typedef struct {

    float x, y;
    bool  down;
    bool  just_down;
    bool  just_up;

    float boost_x, boost_y;
    bool  boost_down;
    bool  boost_just_down;

    int   move_ptr_id;
    int   boost_ptr_id;

    int   zslider_ptr_id;
    float zslider_y;
    float zslider_offset;

    /* Double-tap-and-hold-to-boost: a fallback for when the round boost
       button isn't shown (portrait — see ui_overlay.c). Tracks the most
       recent primary-finger touch-down so the next one can be recognized as
       a double tap; only used/populated on Android. */
    int64_t last_tap_ms;
    float   last_tap_x, last_tap_y;

} touch_state;

#ifdef ANDROID
#include <android_native_app_glue.h>

typedef struct twindow {
    ANativeWindow*  native_window;
    ivec2           size;
    ivec2           lsize;
    ivec2           lpos;
    trender_func    _render_func;
    tresize_func    _resize_func;
    tenv*           env;
    bool            _refresh;
    bool            focused;
    touch_state     touch;
    touch_state     ui_touch;

    /* Real USB/Bluetooth mouse + physical-keyboard state on Android.
       Android reports these devices through AInputEvent instead of GLFW, so
       keep a small GLFW-compatible state cache for the game layer. */
    bool            hw_mouse_present;
    float           hw_mouse_x;
    float           hw_mouse_y;
    uint32_t        hw_mouse_buttons;
    uint32_t        hw_mouse_pressed;
    uint32_t        hw_mouse_released;
    float           hw_mouse_wheel;
    bool            hw_key_down[512];
    bool            hw_key_pressed[512];
    bool            hw_key_released[512];
} twindow;

extern struct android_app* g_android_app;

#else
#include <GLFW/glfw3.h>

typedef struct twindow {
    GLFWwindow*  handle;
    ivec2        size;
    ivec2        lsize;
    ivec2        lpos;
    trender_func _render_func;
    tresize_func _resize_func;
    tenv*        env;
    bool         _refresh;
    touch_state  touch;
} twindow;
#endif

void     twindow_request_refresh(twindow* window);
twindow* twindow_create(tenv* env, trender_func render_func, tresize_func resize_func);
void     twindow_poll_input(twindow* window);
void     twindow_wait_input(twindow* window);
void     twindow_toggle_fullscreen(twindow* window);
bool     twindow_key_down(twindow* window, int key);
bool     twindow_button_down(twindow* window, int button);
bool     twindow_closed(twindow* window);
void     twindow_destroy(twindow* window);

#endif
