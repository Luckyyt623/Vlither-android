#ifdef ANDROID

#include "twindow.h"
#include "tkeyboard.h"
#include "../core/tenv.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui/cimgui.h"
#include "android_jni.h"

#include <android/log.h>
#include <android_native_app_glue.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#define LOG_TAG "vlither"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* Compatibility fallbacks for older Android NDK headers. These constants are
   part of Android's stable native input ABI. */
#ifndef AINPUT_SOURCE_MOUSE
#define AINPUT_SOURCE_MOUSE 0x00002002
#endif
#ifndef AMOTION_EVENT_ACTION_HOVER_MOVE
#define AMOTION_EVENT_ACTION_HOVER_MOVE 7
#endif
#ifndef AMOTION_EVENT_ACTION_SCROLL
#define AMOTION_EVENT_ACTION_SCROLL 8
#endif
#ifndef AMOTION_EVENT_ACTION_BUTTON_PRESS
#define AMOTION_EVENT_ACTION_BUTTON_PRESS 11
#endif
#ifndef AMOTION_EVENT_ACTION_BUTTON_RELEASE
#define AMOTION_EVENT_ACTION_BUTTON_RELEASE 12
#endif
#ifndef AMOTION_EVENT_BUTTON_PRIMARY
#define AMOTION_EVENT_BUTTON_PRIMARY 1
#endif
#ifndef AMOTION_EVENT_BUTTON_SECONDARY
#define AMOTION_EVENT_BUTTON_SECONDARY 2
#endif
#ifndef AMOTION_EVENT_BUTTON_TERTIARY
#define AMOTION_EVENT_BUTTON_TERTIARY 4
#endif
#ifndef AMOTION_EVENT_AXIS_VSCROLL
#define AMOTION_EVENT_AXIS_VSCROLL 9
#endif

float g_zslider_left    = 0;
float g_zslider_top     = 0;
float g_zslider_right   = 0;
float g_zslider_bottom  = 0;
float g_zslider_half_h  = 1;
bool  g_zslider_horizontal = false;
float g_zoom_sensitivity = 1.0f;

bool  g_overlay_drawn_this_frame = false;
bool  g_overlay_was_active    = false;

float g_boost_cx = -9999, g_boost_cy = -9999, g_boost_r = 0;
float g_joy_cx   = -9999, g_joy_cy   = -9999, g_joy_r   = 0;
bool  g_is_trackpad_mode = true;
/* Slither joystick mode accepts a movement finger anywhere on its selected
   side of the screen, not only inside the artwork's circular base. */
bool  g_joystick_uses_side = false;
bool  g_joystick_left_side = true;
bool  g_panel_open       = false;

/* Implemented by app/src/ui/key_buttons.c. Custom gameplay buttons get a
   direct native touch path so top-edge taps cannot be stolen by another HUD
   rectangle or lost between render frames. */
extern bool ui_key_buttons_native_press(float x, float y, int pointer_id);
extern bool ui_key_buttons_native_release(int pointer_id);
extern void ui_key_buttons_native_cancel_all(void);

typedef struct { float l, t, r, b; } android_ui_rect;
#define ANDROID_UI_CAPTURE_MAX 64
static android_ui_rect g_ui_capture_rects[ANDROID_UI_CAPTURE_MAX];
static int g_ui_capture_count = 0;
static uint32_t g_ui_owned_pointer_mask = 0;

void android_ui_capture_begin_frame(void) { g_ui_capture_count = 0; }

void android_ui_capture_rect(float left, float top, float right, float bottom) {
    if (g_ui_capture_count >= ANDROID_UI_CAPTURE_MAX) return;
    if (right < left) { float v = left; left = right; right = v; }
    if (bottom < top) { float v = top; top = bottom; bottom = v; }
    g_ui_capture_rects[g_ui_capture_count++] =
        (android_ui_rect){left, top, right, bottom};
}

bool android_ui_capture_contains(float x, float y) {
    for (int i = g_ui_capture_count - 1; i >= 0; --i) {
        android_ui_rect r = g_ui_capture_rects[i];
        if (x >= r.l && x <= r.r && y >= r.t && y <= r.b) return true;
    }
    return false;
}

static void set_ui_pointer_owned(int pid, bool owned) {
    if (pid < 0 || pid >= 32) return;
    uint32_t bit = (uint32_t)1u << (uint32_t)pid;
    if (owned) g_ui_owned_pointer_mask |= bit;
    else g_ui_owned_pointer_mask &= ~bit;
}

static bool is_ui_pointer_owned(int pid) {
    if (pid < 0 || pid >= 32) return false;
    return (g_ui_owned_pointer_mask & ((uint32_t)1u << (uint32_t)pid)) != 0;
}

extern struct android_app* g_android_app;

static void _android_set_keyboard(bool show) {
    android_jni_set_text_input_active(show);
}

static bool g_keyboard_shown = false;

static void _android_set_immersive_fullscreen(void) {
    ANativeActivity_setWindowFlags(
        g_android_app->activity,
        0x00000400 ,
        0);

    JNIEnv* env = NULL;
    JavaVM* vm  = g_android_app->activity->vm;
    if (!vm) return;
    (*vm)->AttachCurrentThread(vm, &env, NULL);
    if (!env) return;

    jobject activity = g_android_app->activity->clazz;
    if (!activity) { (*vm)->DetachCurrentThread(vm); return; }
    jclass act_class = (*env)->GetObjectClass(env, activity);
    if (!act_class) { (*env)->ExceptionClear(env); (*vm)->DetachCurrentThread(vm); return; }

    jmethodID getWindow = (*env)->GetMethodID(env, act_class,
        "getWindow", "()Landroid/view/Window;");
    if (!getWindow || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); (*vm)->DetachCurrentThread(vm); return; }
    jobject window = (*env)->CallObjectMethod(env, activity, getWindow);
    if (!window || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); (*vm)->DetachCurrentThread(vm); return; }

    jclass win_class = (*env)->GetObjectClass(env, window);
    if (!win_class) { (*env)->ExceptionClear(env); (*vm)->DetachCurrentThread(vm); return; }
    jmethodID getDecorView = (*env)->GetMethodID(env, win_class,
        "getDecorView", "()Landroid/view/View;");
    if (!getDecorView || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); (*vm)->DetachCurrentThread(vm); return; }
    jobject decor_view = (*env)->CallObjectMethod(env, window, getDecorView);
    if (!decor_view || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); (*vm)->DetachCurrentThread(vm); return; }

    jclass view_class = (*env)->GetObjectClass(env, decor_view);
    if (!view_class) { (*env)->ExceptionClear(env); (*vm)->DetachCurrentThread(vm); return; }
    jmethodID setUiVis = (*env)->GetMethodID(env, view_class,
        "setSystemUiVisibility", "(I)V");
    if (!setUiVis || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); (*vm)->DetachCurrentThread(vm); return; }

    jint flags = 0x100 | 0x200 | 0x400 | 0x002 | 0x004 | 0x1000;
    (*env)->CallVoidMethod(env, decor_view, setUiVis, flags);
    (*env)->ExceptionClear(env);

    (*vm)->DetachCurrentThread(vm);
}

static void set_window_size(twindow* wnd, ANativeWindow* win) {
    int w = ANativeWindow_getWidth(win);
    int h = ANativeWindow_getHeight(win);
    LOGI("Raw window dims: %dx%d", w, h);

    wnd->size[0] = (w >= h) ? w : h;
    wnd->size[1] = (w >= h) ? h : w;
    LOGI("Using window size: %dx%d", wnd->size[0], wnd->size[1]);
}

typedef struct {
    twindow*  window;
    bool      initialized;
    bool      surface_ready;
} android_window_state;

static android_window_state g_state = {0};

static void handle_app_cmd(struct android_app* app, int32_t cmd) {
    twindow* wnd = (twindow*)app->userData;
    if (!wnd) return;

    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) {
                /* If a tcontext already exists, this ISN'T the very first
                 * window (that one is consumed by the poll loop in
                 * twindow_create(), before tcontext_create() ever runs).
                 * It means we just resumed from the background and Android
                 * has handed us a brand-new ANativeWindow -> the Vulkan
                 * surface tied to the old one is dead and must be rebuilt,
                 * not just resized. */
                bool resuming = (wnd->env && wnd->env->ctx != NULL);

                wnd->native_window = app->window;
                set_window_size(wnd, app->window);
                g_state.surface_ready = true;

                if (resuming) {
                    tcontext_recreate_surface(wnd->env->ctx, wnd,
                                              wnd->env->config.vsync);
                    if (wnd->_resize_func) wnd->_resize_func(wnd->env);
                }

                _android_set_immersive_fullscreen();
            }
            break;

        case APP_CMD_TERM_WINDOW:
            g_state.surface_ready = false;
            wnd->native_window    = NULL;
            /* Stop the render loop from touching Vulkan against a surface
             * whose window is already gone; it'll come back true inside
             * tcontext_recreate_surface() once APP_CMD_INIT_WINDOW fires. */
            if (wnd->env && wnd->env->ctx)
                wnd->env->ctx->swapchain_ok = false;
            break;

        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
            if (app->window) {
                int old_w = wnd->size[0];
                int old_h = wnd->size[1];
                set_window_size(wnd, app->window);
                if (wnd->size[0] != old_w || wnd->size[1] != old_h) {
                    if (wnd->env && wnd->env->ctx) {
                        tcontext_resize(wnd->env->ctx, wnd->size,
                                        wnd->env->config.vsync);
                        wnd->_resize_func(wnd->env);
                    }
                }
            }
            break;

        case APP_CMD_GAINED_FOCUS:
            wnd->focused = true;

            _android_set_immersive_fullscreen();
            break;

        case APP_CMD_LOST_FOCUS:
            wnd->focused = false;
            memset(wnd->hw_key_down, 0, sizeof(wnd->hw_key_down));
            memset(wnd->hw_key_pressed, 0, sizeof(wnd->hw_key_pressed));
            memset(wnd->hw_key_released, 0, sizeof(wnd->hw_key_released));
            wnd->hw_mouse_buttons = 0;
            wnd->hw_mouse_pressed = 0;
            wnd->hw_mouse_released = 0;
            break;

        case APP_CMD_DESTROY:
            if (wnd->env)
                wnd->env->config.running = false;
            break;
    }
}

static void begin_ui_touch(twindow* wnd, float x, float y, int pid) {
    if (wnd->ui_touch.down) return;
    wnd->ui_touch.x = x;
    wnd->ui_touch.y = y;
    wnd->ui_touch.down = true;
    wnd->ui_touch.just_down = true;
    wnd->ui_touch.just_up = false;
    wnd->ui_touch.move_ptr_id = pid;
}

static void end_ui_touch(twindow* wnd, int pid) {
    if (pid != wnd->ui_touch.move_ptr_id) return;
    /* Keep just_down latched until the render pass. A quick Android tap can
       deliver ACTION_DOWN and ACTION_UP in one input poll; clearing just_down
       here made small/top on-screen buttons miss the gesture completely. */
    wnd->ui_touch.down = false;
    wnd->ui_touch.just_up = true;
    wnd->ui_touch.move_ptr_id = -1;
}

static ImGuiKey android_key_to_imgui(int32_t keycode) {
    switch (keycode) {
        case AKEYCODE_TAB:        return ImGuiKey_Tab;
        case AKEYCODE_DPAD_LEFT:  return ImGuiKey_LeftArrow;
        case AKEYCODE_DPAD_RIGHT: return ImGuiKey_RightArrow;
        case AKEYCODE_DPAD_UP:    return ImGuiKey_UpArrow;
        case AKEYCODE_DPAD_DOWN:  return ImGuiKey_DownArrow;
        case AKEYCODE_MOVE_HOME:  return ImGuiKey_Home;
        case AKEYCODE_MOVE_END:   return ImGuiKey_End;
        case AKEYCODE_FORWARD_DEL:return ImGuiKey_Delete;
        case AKEYCODE_DEL:        return ImGuiKey_Backspace;
        case AKEYCODE_SPACE:      return ImGuiKey_Space;
        case AKEYCODE_ENTER:      return ImGuiKey_Enter;
        case AKEYCODE_ESCAPE:     return ImGuiKey_Escape;
        case AKEYCODE_CTRL_LEFT:  return ImGuiKey_LeftCtrl;
        case AKEYCODE_CTRL_RIGHT: return ImGuiKey_RightCtrl;
        case AKEYCODE_SHIFT_LEFT: return ImGuiKey_LeftShift;
        case AKEYCODE_SHIFT_RIGHT:return ImGuiKey_RightShift;
        case AKEYCODE_ALT_LEFT:   return ImGuiKey_LeftAlt;
        case AKEYCODE_ALT_RIGHT:  return ImGuiKey_RightAlt;
        case AKEYCODE_A:          return ImGuiKey_A;
        case AKEYCODE_C:          return ImGuiKey_C;
        case AKEYCODE_V:          return ImGuiKey_V;
        case AKEYCODE_X:          return ImGuiKey_X;
        case AKEYCODE_Y:          return ImGuiKey_Y;
        case AKEYCODE_Z:          return ImGuiKey_Z;
        default:                  return ImGuiKey_None;
    }
}

/* Convert Android hardware-key codes into the GLFW values used by Vlither's
   existing gameplay and hotkey code. */
static int android_key_to_glfw(int32_t keycode) {
    if (keycode >= AKEYCODE_A && keycode <= AKEYCODE_Z)
        return GLFW_KEY_A + (keycode - AKEYCODE_A);
    if (keycode >= AKEYCODE_0 && keycode <= AKEYCODE_9)
        return GLFW_KEY_0 + (keycode - AKEYCODE_0);
    if (keycode >= AKEYCODE_F1 && keycode <= AKEYCODE_F12)
        return GLFW_KEY_F1 + (keycode - AKEYCODE_F1);

    switch (keycode) {
        case AKEYCODE_DPAD_LEFT:    return GLFW_KEY_LEFT;
        case AKEYCODE_DPAD_RIGHT:   return GLFW_KEY_RIGHT;
        case AKEYCODE_DPAD_UP:      return GLFW_KEY_UP;
        case AKEYCODE_DPAD_DOWN:    return GLFW_KEY_DOWN;
        case AKEYCODE_SPACE:        return GLFW_KEY_SPACE;
        case AKEYCODE_ENTER:        return GLFW_KEY_ENTER;
        case AKEYCODE_TAB:          return GLFW_KEY_TAB;
        case AKEYCODE_DEL:          return GLFW_KEY_BACKSPACE;
        case AKEYCODE_ESCAPE:       return GLFW_KEY_ESCAPE;
        case AKEYCODE_CAPS_LOCK:    return GLFW_KEY_CAPS_LOCK;
        case AKEYCODE_SHIFT_LEFT:   return GLFW_KEY_LEFT_SHIFT;
        case AKEYCODE_SHIFT_RIGHT:  return GLFW_KEY_RIGHT_SHIFT;
        case AKEYCODE_CTRL_LEFT:    return GLFW_KEY_LEFT_CONTROL;
        case AKEYCODE_CTRL_RIGHT:   return GLFW_KEY_RIGHT_CONTROL;
        case AKEYCODE_ALT_LEFT:     return GLFW_KEY_LEFT_ALT;
        case AKEYCODE_ALT_RIGHT:    return GLFW_KEY_RIGHT_ALT;
        case AKEYCODE_APOSTROPHE:   return GLFW_KEY_APOSTROPHE;
        case AKEYCODE_COMMA:        return GLFW_KEY_COMMA;
        case AKEYCODE_MINUS:        return GLFW_KEY_MINUS;
        case AKEYCODE_PERIOD:       return GLFW_KEY_PERIOD;
        case AKEYCODE_SLASH:        return GLFW_KEY_SLASH;
        case AKEYCODE_SEMICOLON:    return GLFW_KEY_SEMICOLON;
        case AKEYCODE_EQUALS:       return GLFW_KEY_EQUAL;
        case AKEYCODE_LEFT_BRACKET: return GLFW_KEY_LEFT_BRACKET;
        case AKEYCODE_BACKSLASH:    return GLFW_KEY_BACKSLASH;
        case AKEYCODE_RIGHT_BRACKET:return GLFW_KEY_RIGHT_BRACKET;
        case AKEYCODE_GRAVE:        return GLFW_KEY_GRAVE_ACCENT;
        default:                    return -1;
    }
}

static uint32_t android_mouse_buttons_to_glfw(int32_t android_buttons) {
    uint32_t result = 0;
    if (android_buttons & AMOTION_EVENT_BUTTON_PRIMARY)
        result |= 1u << GLFW_MOUSE_BUTTON_LEFT;
    if (android_buttons & AMOTION_EVENT_BUTTON_SECONDARY)
        result |= 1u << GLFW_MOUSE_BUTTON_RIGHT;
    if (android_buttons & AMOTION_EVENT_BUTTON_TERTIARY)
        result |= 1u << GLFW_MOUSE_BUTTON_MIDDLE;
    return result;
}

#ifndef AKEYCODE_CUT
#define AKEYCODE_CUT 277
#endif
#ifndef AKEYCODE_COPY
#define AKEYCODE_COPY 278
#endif
#ifndef AKEYCODE_PASTE
#define AKEYCODE_PASTE 279
#endif

static int32_t handle_input(struct android_app* app, AInputEvent* event) {
    twindow* wnd = (twindow*)app->userData;
    if (!wnd) return 0;

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
        int32_t action  = AKeyEvent_getAction(event);
        int32_t keycode = AKeyEvent_getKeyCode(event);
        int32_t meta    = AKeyEvent_getMetaState(event);
        ImGuiIO* io     = igGetIO_Nil();
        if (!io) return 1;

        bool is_down = action == AKEY_EVENT_ACTION_DOWN ||
                       action == AKEY_EVENT_ACTION_MULTIPLE;
        bool is_up   = action == AKEY_EVENT_ACTION_UP;
        bool ctrl    = (meta & AMETA_CTRL_ON) != 0 ||
                       ((keycode == AKEYCODE_CTRL_LEFT ||
                         keycode == AKEYCODE_CTRL_RIGHT) && is_down);
        bool shift   = (meta & AMETA_SHIFT_ON) != 0 ||
                       ((keycode == AKEYCODE_SHIFT_LEFT ||
                         keycode == AKEYCODE_SHIFT_RIGHT) && is_down);
        bool alt     = (meta & AMETA_ALT_ON) != 0 ||
                       ((keycode == AKEYCODE_ALT_LEFT ||
                         keycode == AKEYCODE_ALT_RIGHT) && is_down);

        /* Physical keyboard state for the gameplay/hotkey layer. Text input
           and full-screen panels still receive the key through ImGui below,
           but do not also trigger gameplay actions. Key-up always clears the
           cached state so a focus/UI transition cannot leave a key stuck. */
        int glfw_key = android_key_to_glfw(keycode);
        if (glfw_key >= 0 && glfw_key < 512) {
            if (is_up) {
                if (wnd->hw_key_down[glfw_key])
                    wnd->hw_key_released[glfw_key] = true;
                wnd->hw_key_down[glfw_key] = false;
            } else if (action == AKEY_EVENT_ACTION_DOWN &&
                       !io->WantTextInput && !g_panel_open) {
                if (!wnd->hw_key_down[glfw_key])
                    wnd->hw_key_pressed[glfw_key] = true;
                wnd->hw_key_down[glfw_key] = true;
            }
        }

        ImGuiIO_AddKeyEvent(io, ImGuiMod_Ctrl,  ctrl);
        ImGuiIO_AddKeyEvent(io, ImGuiMod_Shift, shift);
        ImGuiIO_AddKeyEvent(io, ImGuiMod_Alt,   alt);

        if (keycode == AKEYCODE_PASTE && is_down) {
            const char* clipboard = android_jni_get_clipboard_text();
            if (clipboard && clipboard[0] != '\0')
                ImGuiIO_AddInputCharactersUTF8(io, clipboard);
            return 1;
        }

        /* Android exposes dedicated copy/cut keys on some hardware keyboards.
           Translate them to the shortcuts Dear ImGui already understands. */
        if ((keycode == AKEYCODE_COPY || keycode == AKEYCODE_CUT) && is_down) {
            ImGuiIO_AddKeyEvent(io, ImGuiMod_Ctrl, true);
            ImGuiKey command = keycode == AKEYCODE_COPY ? ImGuiKey_C : ImGuiKey_X;
            ImGuiIO_AddKeyEvent(io, command, true);
            ImGuiIO_AddKeyEvent(io, command, false);
            ImGuiIO_AddKeyEvent(io, ImGuiMod_Ctrl, false);
            return 1;
        }

        ImGuiKey imgui_key = android_key_to_imgui(keycode);
        if (imgui_key != ImGuiKey_None) {
            if (action == AKEY_EVENT_ACTION_MULTIPLE) {
                ImGuiIO_AddKeyEvent(io, imgui_key, true);
                ImGuiIO_AddKeyEvent(io, imgui_key, false);
            } else {
                ImGuiIO_AddKeyEvent(io, imgui_key, is_down && !is_up);
            }
        }

        if (is_down && !ctrl && keycode != AKEYCODE_DEL) {
            JNIEnv* jenv = NULL;
            JavaVM* vm = g_android_app->activity->vm;
            bool did_attach = false;
            int status = (*vm)->GetEnv(vm, (void**)&jenv, JNI_VERSION_1_6);
            if (status == JNI_EDETACHED) {
                if ((*vm)->AttachCurrentThread(vm, &jenv, NULL) == JNI_OK)
                    did_attach = true;
            }

            if (jenv) {
                jclass kc = (*jenv)->FindClass(jenv, "android/view/KeyEvent");
                if (kc) {
                    jmethodID ctor = (*jenv)->GetMethodID(jenv, kc, "<init>", "(II)V");
                    jmethodID get_unicode = (*jenv)->GetMethodID(
                        jenv, kc, "getUnicodeChar", "(I)I");
                    if (ctor && get_unicode) {
                        jobject ke = (*jenv)->NewObject(jenv, kc, ctor, action, keycode);
                        if (ke) {
                            int unicode = (*jenv)->CallIntMethod(
                                jenv, ke, get_unicode, meta);
                            if (unicode > 0)
                                ImGuiIO_AddInputCharacter(io, (unsigned int)unicode);
                            (*jenv)->DeleteLocalRef(jenv, ke);
                        }
                    }
                    (*jenv)->DeleteLocalRef(jenv, kc);
                }
                if ((*jenv)->ExceptionCheck(jenv)) (*jenv)->ExceptionClear(jenv);
            }
            if (did_attach) (*vm)->DetachCurrentThread(vm);
        }
        return 1;
    }

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action        = AMotionEvent_getAction(event);
        int32_t action_masked = action & AMOTION_EVENT_ACTION_MASK;

        /* A real USB/Bluetooth mouse arrives as an Android motion source too,
           but it must never enter the touchscreen joystick/trackpad path.
           Cache its absolute pointer and buttons for Mouse + Keyboard mode. */
        int32_t source = AInputEvent_getSource(event);
        if ((source & AINPUT_SOURCE_MOUSE) == AINPUT_SOURCE_MOUSE) {
            int32_t count = (int32_t)AMotionEvent_getPointerCount(event);
            if (count > 0) {
                float x = AMotionEvent_getX(event, 0);
                float y = AMotionEvent_getY(event, 0);
                wnd->hw_mouse_present = true;
                wnd->hw_mouse_x = x;
                wnd->hw_mouse_y = y;

                uint32_t old_buttons = wnd->hw_mouse_buttons;
                uint32_t new_buttons = android_mouse_buttons_to_glfw(
                    AMotionEvent_getButtonState(event));
                wnd->hw_mouse_buttons = new_buttons;
                wnd->hw_mouse_pressed  |= new_buttons & ~old_buttons;
                wnd->hw_mouse_released |= old_buttons & ~new_buttons;

                if (action_masked == AMOTION_EVENT_ACTION_SCROLL) {
                    wnd->hw_mouse_wheel += AMotionEvent_getAxisValue(
                        event, AMOTION_EVENT_AXIS_VSCROLL, 0);
                }

                /* ImGui should still be usable with the external mouse.
                   Primary/left click is routed to UI only when the pointer is
                   over an owned UI region; otherwise gameplay reads it as boost. */
                wnd->ui_touch.x = x;
                wnd->ui_touch.y = y;
                const int mouse_ui_pid = 31;
                bool left_pressed =
                    (new_buttons & (1u << GLFW_MOUSE_BUTTON_LEFT)) &&
                    !(old_buttons & (1u << GLFW_MOUSE_BUTTON_LEFT));
                bool left_released =
                    !(new_buttons & (1u << GLFW_MOUSE_BUTTON_LEFT)) &&
                    (old_buttons & (1u << GLFW_MOUSE_BUTTON_LEFT));

                if (left_pressed &&
                    (g_panel_open || android_ui_capture_contains(x, y))) {
                    set_ui_pointer_owned(mouse_ui_pid, true);
                    begin_ui_touch(wnd, x, y, mouse_ui_pid);
                }
                if (left_released) {
                    end_ui_touch(wnd, mouse_ui_pid);
                    set_ui_pointer_owned(mouse_ui_pid, false);
                }

                if (action_masked == AMOTION_EVENT_ACTION_CANCEL) {
                    wnd->hw_mouse_buttons = 0;
                    end_ui_touch(wnd, mouse_ui_pid);
                    set_ui_pointer_owned(mouse_ui_pid, false);
                }
            }
            return 1;
        }

        int32_t ptr_idx = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                          AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

        switch (action_masked) {

            case AMOTION_EVENT_ACTION_DOWN: {
                float x   = AMotionEvent_getX(event, 0);
                float y   = AMotionEvent_getY(event, 0);
                int   pid = (int)AMotionEvent_getPointerId(event, 0);

                if (ui_key_buttons_native_press(x, y, pid)) {
                    set_ui_pointer_owned(pid, true);
                    break;
                }

                if (g_panel_open || android_ui_capture_contains(x, y)) {
                    set_ui_pointer_owned(pid, true);
                    begin_ui_touch(wnd, x, y, pid);
                    break;
                }

                bool in_zslider = !g_panel_open && g_overlay_was_active &&
                                  (g_zslider_right > g_zslider_left) &&
                                  x >= g_zslider_left && x <= g_zslider_right &&
                                  y >= g_zslider_top  && y <= g_zslider_bottom;

                float dbx = x - g_boost_cx, dby = y - g_boost_cy;
                bool in_boost_circle = g_boost_r > 0 &&
                    (dbx*dbx + dby*dby) <= (g_boost_r * g_boost_r);

                float djx = x - g_joy_cx, djy = y - g_joy_cy;
                bool in_joy_ring = g_joy_r > 0 &&
                                   (djx*djx + djy*djy) <= (g_joy_r * g_joy_r);
                bool on_joy_side = g_joystick_uses_side &&
                    (g_joystick_left_side ? x < wnd->size[0] * 0.5f
                                          : x >= wnd->size[0] * 0.5f);
                bool can_move = !g_panel_open &&
                                (g_is_trackpad_mode || on_joy_side || in_joy_ring);

                if (in_zslider && wnd->touch.zslider_ptr_id == -1) {
                    wnd->touch.zslider_ptr_id = pid;
                    wnd->touch.zslider_y      = g_zslider_horizontal ? x : y;
                    wnd->touch.zslider_offset = 0.0f;
                } else if (in_boost_circle && !wnd->touch.boost_down) {
                    wnd->touch.boost_x         = x;
                    wnd->touch.boost_y         = y;
                    wnd->touch.boost_down      = true;
                    wnd->touch.boost_just_down = true;
                    wnd->touch.boost_ptr_id    = pid;
                } else if (can_move && !wnd->touch.down) {
                    wnd->touch.x           = x;
                    wnd->touch.y           = y;
                    wnd->touch.down        = true;
                    wnd->touch.just_down   = true;
                    wnd->touch.move_ptr_id = pid;
                }
                break;
            }

            case AMOTION_EVENT_ACTION_POINTER_DOWN: {

                int32_t cnt2 = (int32_t)AMotionEvent_getPointerCount(event);
                if (ptr_idx < 0 || ptr_idx >= cnt2) break;
                int pid = (int)AMotionEvent_getPointerId(event, ptr_idx);
                float x = AMotionEvent_getX(event, ptr_idx);
                float y = AMotionEvent_getY(event, ptr_idx);

                if (ui_key_buttons_native_press(x, y, pid)) {
                    set_ui_pointer_owned(pid, true);
                    break;
                }

                if (g_panel_open || android_ui_capture_contains(x, y)) {
                    set_ui_pointer_owned(pid, true);
                    begin_ui_touch(wnd, x, y, pid);
                    break;
                }

                bool in_zslider2 = !g_panel_open && g_overlay_was_active &&
                                   (g_zslider_right > g_zslider_left) &&
                                   x >= g_zslider_left && x <= g_zslider_right &&
                                   y >= g_zslider_top  && y <= g_zslider_bottom;
                if (in_zslider2 && wnd->touch.zslider_ptr_id == -1) {
                    wnd->touch.zslider_ptr_id = pid;
                    wnd->touch.zslider_y      = g_zslider_horizontal ? x : y;
                    wnd->touch.zslider_offset = 0.0f;
                } else {
                    float dbx2 = x - g_boost_cx, dby2 = y - g_boost_cy;
                    bool boost_hit = g_boost_r > 0 &&
                                     (dbx2*dbx2 + dby2*dby2) <=
                                         (g_boost_r * g_boost_r);
                    if (boost_hit && !wnd->touch.boost_down) {
                        wnd->touch.boost_x         = x;
                        wnd->touch.boost_y         = y;
                        wnd->touch.boost_down      = true;
                        wnd->touch.boost_just_down = true;
                        wnd->touch.boost_ptr_id    = pid;
                    } else if (!wnd->touch.down) {
                        float djx2 = x - g_joy_cx, djy2 = y - g_joy_cy;
                        bool on_joy_side2 = g_joystick_uses_side &&
                            (g_joystick_left_side ? x < wnd->size[0] * 0.5f
                                                  : x >= wnd->size[0] * 0.5f);
                        bool joy_ok = g_is_trackpad_mode || on_joy_side2 ||
                                      (g_joy_r > 0 && (djx2*djx2+djy2*djy2) <= g_joy_r*g_joy_r);
                        if (joy_ok) {
                            wnd->touch.x           = x;
                            wnd->touch.y           = y;
                            wnd->touch.down        = true;
                            wnd->touch.just_down   = true;
                            wnd->touch.move_ptr_id = pid;
                        }
                    }
                }
                break;
            }

            case AMOTION_EVENT_ACTION_MOVE: {
                int32_t count = (int32_t)AMotionEvent_getPointerCount(event);

                for (int32_t i = 0; i < count; i++) {
                    int   pid = (int)AMotionEvent_getPointerId(event, i);
                    float x   = AMotionEvent_getX(event, i);
                    float y   = AMotionEvent_getY(event, i);
                    if (pid == wnd->ui_touch.move_ptr_id) {
                        wnd->ui_touch.x = x;
                        wnd->ui_touch.y = y;
                    }
                    if (is_ui_pointer_owned(pid)) continue;
                    if (wnd->touch.down && pid == wnd->touch.move_ptr_id) {
                        wnd->touch.x = x;
                        wnd->touch.y = y;
                    } else if (wnd->touch.boost_down && pid == wnd->touch.boost_ptr_id) {
                        wnd->touch.boost_x = x;
                        wnd->touch.boost_y = y;
                    } else if (wnd->touch.zslider_ptr_id != -1 &&
                               pid == wnd->touch.zslider_ptr_id) {
                        float pos = g_zslider_horizontal ? x : y;
                        float delta = pos - wnd->touch.zslider_y;
                        wnd->touch.zslider_y       = pos;
                        wnd->touch.zslider_offset += delta;
                        if (wnd->touch.zslider_offset >  g_zslider_half_h)
                            wnd->touch.zslider_offset =  g_zslider_half_h;
                        if (wnd->touch.zslider_offset < -g_zslider_half_h)
                            wnd->touch.zslider_offset = -g_zslider_half_h;
                    }
                }
                break;
            }

            case AMOTION_EVENT_ACTION_UP: {

                int lifted_pid = (int)AMotionEvent_getPointerId(event, ptr_idx);
                ui_key_buttons_native_release(lifted_pid);
                end_ui_touch(wnd, lifted_pid);
                set_ui_pointer_owned(lifted_pid, false);
                wnd->touch.down              = false;
                wnd->touch.just_down         = false;
                wnd->touch.boost_down        = false;
                wnd->touch.boost_just_down   = false;
                wnd->touch.move_ptr_id       = -1;
                wnd->touch.boost_ptr_id      = -1;
                wnd->touch.zslider_ptr_id    = -1;
                wnd->touch.zslider_offset    = 0.0f;
                break;
            }

            case AMOTION_EVENT_ACTION_POINTER_UP: {

                int lifted_pid = (int)AMotionEvent_getPointerId(event, ptr_idx);
                ui_key_buttons_native_release(lifted_pid);
                end_ui_touch(wnd, lifted_pid);
                set_ui_pointer_owned(lifted_pid, false);

                if (lifted_pid == wnd->touch.move_ptr_id) {
                    wnd->touch.down        = false;
                    wnd->touch.just_down   = false;
                    wnd->touch.move_ptr_id = -1;
                }
                if (lifted_pid == wnd->touch.boost_ptr_id) {
                    wnd->touch.boost_down      = false;
                    wnd->touch.boost_just_down = false;
                    wnd->touch.boost_ptr_id    = -1;
                }
                if (lifted_pid == wnd->touch.zslider_ptr_id) {
                    wnd->touch.zslider_ptr_id = -1;
                    wnd->touch.zslider_offset = 0.0f;
                }

                break;
            }

            case AMOTION_EVENT_ACTION_CANCEL:
                ui_key_buttons_native_cancel_all();
                g_ui_owned_pointer_mask      = 0;
                wnd->ui_touch.just_up        = wnd->ui_touch.down ||
                                                  wnd->ui_touch.just_down;
                wnd->ui_touch.down           = false;
                wnd->ui_touch.move_ptr_id    = -1;
                wnd->touch.down              = false;
                wnd->touch.just_down         = false;
                wnd->touch.boost_down        = false;
                wnd->touch.boost_just_down   = false;
                wnd->touch.move_ptr_id       = -1;
                wnd->touch.boost_ptr_id      = -1;
                wnd->touch.zslider_ptr_id    = -1;
                wnd->touch.zslider_offset    = 0.0f;
                break;
        }
        return 1;
    }
    return 0;
}

twindow* twindow_create(tenv* env, trender_func render_func, tresize_func resize_func) {
    twindow* wnd = calloc(1, sizeof(twindow));
    if (!wnd) return NULL;

    wnd->_render_func = render_func;
    wnd->_resize_func = resize_func;
    wnd->env          = env;
    wnd->focused      = true;

    /* calloc() zero-fills the struct, but 0 is also a perfectly normal,
       commonly-assigned Android pointer id (usually the very first finger
       of a gesture). If these are left at 0 instead of a real "unset"
       sentinel, the first-ever touch gesture can be misread as already
       matching move/boost/zslider before any real assignment happens.
       Force them to -1 so they can never collide with a live pointer id. */
    wnd->touch.move_ptr_id    = -1;
    wnd->touch.boost_ptr_id   = -1;
    wnd->touch.zslider_ptr_id = -1;
    wnd->ui_touch.move_ptr_id    = -1;
    wnd->ui_touch.boost_ptr_id   = -1;
    wnd->ui_touch.zslider_ptr_id = -1;

    g_android_app->userData     = wnd;
    g_android_app->onAppCmd     = handle_app_cmd;
    g_android_app->onInputEvent = handle_input;

    while (!g_state.surface_ready) {
        int events;
        struct android_poll_source* source;
        ALooper_pollAll(0, NULL, &events, (void**)&source);
        if (source) source->process(g_android_app, source);
        if (g_android_app->destroyRequested) return wnd;
    }

    wnd->native_window = g_android_app->window;
    set_window_size(wnd, g_android_app->window);

    return wnd;
}

void twindow_poll_input(twindow* window) {
    window->touch.just_down       = false;
    window->touch.boost_just_down = false;
    window->ui_touch.just_down    = false;
    window->ui_touch.just_up      = false;

    int events;
    struct android_poll_source* source;
    while (ALooper_pollAll(0, NULL, &events, (void**)&source) >= 0) {
        if (source) source->process(g_android_app, source);
        if (g_android_app->destroyRequested) {
            if (window->env) window->env->config.running = false;
            return;
        }
    }

    extern bool g_imgui_wants_keyboard;
    if (g_imgui_wants_keyboard != g_keyboard_shown) {
        g_keyboard_shown = g_imgui_wants_keyboard;
        _android_set_keyboard(g_imgui_wants_keyboard);
    }
}

void twindow_wait_input(twindow* window) {
    window->touch.just_down       = false;
    window->touch.boost_just_down = false;
    window->ui_touch.just_down    = false;
    window->ui_touch.just_up      = false;

    int events;
    struct android_poll_source* source;
    if (ALooper_pollAll(-1, NULL, &events, (void**)&source) >= 0) {
        if (source) source->process(g_android_app, source);
        if (g_android_app->destroyRequested) {
            if (window->env) window->env->config.running = false;
        }
    }
}

void twindow_toggle_fullscreen(twindow* window) {
    (void)window;
}

bool twindow_key_down(twindow* window, int key) {
    if (!window || key < 0 || key >= 512) return false;
    return window->hw_key_down[key];
}

bool twindow_button_down(twindow* window, int button) {
    if (!window) return false;
    if (window->hw_mouse_present && button >= 0 && button <= 2)
        return (window->hw_mouse_buttons & (1u << (unsigned)button)) != 0;
    return button == GLFW_MOUSE_BUTTON_LEFT ? window->touch.down : false;
}

bool twindow_closed(twindow* window) {
    (void)window;
    return g_android_app->destroyRequested != 0;
}

void twindow_request_refresh(twindow* window) {
    if (window) window->_refresh = true;
}

void twindow_destroy(twindow* window) {
    free(window);
}

#endif
