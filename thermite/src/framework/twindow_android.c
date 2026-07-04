#ifdef ANDROID

#include "twindow.h"
#include "../core/tenv.h"

#include <android/log.h>
#include <android_native_app_glue.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define LOG_TAG "vlither"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

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
bool  g_panel_open       = false;

extern struct android_app* g_android_app;

typedef struct ImGuiIO ImGuiIO;
extern ImGuiIO* igGetIO_Nil(void);
extern void ImGuiIO_AddInputCharacter(ImGuiIO* self, unsigned int c);
extern void ImGuiIO_AddKeyEvent(ImGuiIO* self, int key, bool down);

static void _android_set_keyboard(bool show) {
    JNIEnv* env = NULL;
    JavaVM* vm = g_android_app->activity->vm;
    (*vm)->AttachCurrentThread(vm, &env, NULL);

    jobject activity = g_android_app->activity->clazz;
    jclass cls = (*env)->GetObjectClass(env, activity);

    jmethodID getSystemService = (*env)->GetMethodID(env, cls,
        "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring ime_str = (*env)->NewStringUTF(env, "input_method");
    jobject imm = (*env)->CallObjectMethod(env, activity, getSystemService, ime_str);
    (*env)->DeleteLocalRef(env, ime_str);

    jclass imm_cls = (*env)->GetObjectClass(env, imm);

    jmethodID getWindow = (*env)->GetMethodID(env, cls,
        "getWindow", "()Landroid/view/Window;");
    jobject window = (*env)->CallObjectMethod(env, activity, getWindow);
    jclass window_cls = (*env)->GetObjectClass(env, window);
    jmethodID getDecorView = (*env)->GetMethodID(env, window_cls,
        "getDecorView", "()Landroid/view/View;");
    jobject decor_view = (*env)->CallObjectMethod(env, window, getDecorView);

    if (show) {
        jmethodID showSoftInput = (*env)->GetMethodID(env, imm_cls,
            "showSoftInput", "(Landroid/view/View;I)Z");
        (*env)->CallBooleanMethod(env, imm, showSoftInput, decor_view, 0);
    } else {
        jclass view_cls = (*env)->GetObjectClass(env, decor_view);
        jmethodID getWindowToken = (*env)->GetMethodID(env, view_cls,
            "getWindowToken", "()Landroid/os/IBinder;");
        jobject token = (*env)->CallObjectMethod(env, decor_view, getWindowToken);
        jmethodID hideSoftInput = (*env)->GetMethodID(env, imm_cls,
            "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z");
        (*env)->CallBooleanMethod(env, imm, hideSoftInput, token, 0);
    }

    (*vm)->DetachCurrentThread(vm);
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
                wnd->native_window = app->window;
                set_window_size(wnd, app->window);
                g_state.surface_ready = true;

                _android_set_immersive_fullscreen();
            }
            break;

        case APP_CMD_TERM_WINDOW:
            g_state.surface_ready = false;
            wnd->native_window    = NULL;
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
            break;

        case APP_CMD_DESTROY:
            if (wnd->env)
                wnd->env->config.running = false;
            break;
    }
}

static int32_t handle_input(struct android_app* app, AInputEvent* event) {
    twindow* wnd = (twindow*)app->userData;
    if (!wnd) return 0;

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
        int32_t action   = AKeyEvent_getAction(event);
        int32_t keycode  = AKeyEvent_getKeyCode(event);
        int32_t meta     = AKeyEvent_getMetaState(event);

        if (action == AKEY_EVENT_ACTION_DOWN || action == AKEY_EVENT_ACTION_MULTIPLE) {

            JNIEnv* jenv = NULL;
            (*g_android_app->activity->vm)->AttachCurrentThread(
                g_android_app->activity->vm, &jenv, NULL);
            jclass kc = (*jenv)->FindClass(jenv, "android/view/KeyEvent");
            jmethodID ctor = (*jenv)->GetMethodID(jenv, kc, "<init>", "(II)V");
            jobject ke = (*jenv)->NewObject(jenv, kc, ctor, action, keycode);
            jmethodID getUnicodeChar = (*jenv)->GetMethodID(jenv, kc,
                "getUnicodeChar", "(I)I");
            int unicode = (*jenv)->CallIntMethod(jenv, ke, getUnicodeChar, meta);
            (*jenv)->DeleteLocalRef(jenv, ke);
            (*g_android_app->activity->vm)->DetachCurrentThread(
                g_android_app->activity->vm);

            if (unicode > 0) {
ImGuiIO* _io = igGetIO_Nil();
                if (_io) ImGuiIO_AddInputCharacter(_io, (unsigned int)unicode);
            }

            if (keycode == AKEYCODE_DEL) {
ImGuiIO* _io = igGetIO_Nil();
                if (_io) {
                    ImGuiIO_AddKeyEvent(_io, 523 , true);
                    ImGuiIO_AddKeyEvent(_io, 523, false);
                }
            }
        }
        return 1;
    }

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action        = AMotionEvent_getAction(event);
        int32_t action_masked = action & AMOTION_EVENT_ACTION_MASK;

        int32_t ptr_idx = (action >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT) &
                          AMOTION_EVENT_ACTION_POINTER_INDEX_MASK;

        extern bool g_ctrl_swap_sides;
        float sw = (float)wnd->size[0];

        switch (action_masked) {

            case AMOTION_EVENT_ACTION_DOWN: {
                float x   = AMotionEvent_getX(event, 0);
                float y   = AMotionEvent_getY(event, 0);
                int   pid = (int)AMotionEvent_getPointerId(event, 0);

                bool in_zslider = g_overlay_was_active &&
                                  (g_zslider_right > g_zslider_left) &&
                                  x >= g_zslider_left && x <= g_zslider_right &&
                                  y >= g_zslider_top  && y <= g_zslider_bottom;

                float dbx = x - g_boost_cx, dby = y - g_boost_cy;
                bool in_boost_circle;
                if (g_boost_r > 0) {
                    float det_r = g_boost_r * 1.4f;
                    in_boost_circle = (dbx*dbx + dby*dby) <= (det_r * det_r);
                } else {

                    in_boost_circle = g_ctrl_swap_sides
                                      ? (x < sw * 0.22f)
                                      : (x > sw * 0.78f);
                }

                float djx = x - g_joy_cx, djy = y - g_joy_cy;
                bool in_joy_ring = g_joy_r > 0 &&
                                   (djx*djx + djy*djy) <= (g_joy_r * g_joy_r);
                bool can_move = !g_panel_open &&
                                (g_is_trackpad_mode || in_joy_ring);

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
                } else if (!wnd->touch.down) {

                    wnd->touch.x         = x;
                    wnd->touch.y         = y;
                    wnd->touch.down      = true;
                    wnd->touch.just_down = true;
                    wnd->touch.move_ptr_id = pid;
                }
                break;
            }

            case AMOTION_EVENT_ACTION_POINTER_DOWN: {

                int32_t cnt2  = (int32_t)AMotionEvent_getPointerCount(event);
                int     pid   = -1;
                float   x = 0, y = 0;
                for (int32_t si = 0; si < cnt2; si++) {
                    int cpid = (int)AMotionEvent_getPointerId(event, si);
                    if (cpid != wnd->touch.move_ptr_id  &&
                        cpid != wnd->touch.boost_ptr_id &&
                        cpid != wnd->touch.zslider_ptr_id) {
                        pid = cpid;
                        x   = AMotionEvent_getX(event, si);
                        y   = AMotionEvent_getY(event, si);
                        break;
                    }
                }
                if (pid == -1) break;

                if (wnd->touch.pending_reconcile) {
                    wnd->touch.pending_reconcile = false;
                    bool move_alive2 = false, boost_alive2 = false;
                    for (int32_t ri = 0; ri < cnt2; ri++) {
                        int rpid = (int)AMotionEvent_getPointerId(event, ri);

                        if (rpid == pid) continue;
                        if (rpid == wnd->touch.move_ptr_id)  move_alive2  = true;
                        if (rpid == wnd->touch.boost_ptr_id) boost_alive2 = true;
                    }
                    if (!move_alive2 && wnd->touch.move_ptr_id != -1) {
                        wnd->touch.down        = false;
                        wnd->touch.just_down   = false;
                        wnd->touch.move_ptr_id = -1;
                    }
                    if (!boost_alive2 && wnd->touch.boost_ptr_id != -1) {
                        wnd->touch.boost_down      = false;
                        wnd->touch.boost_just_down = false;
                        wnd->touch.boost_ptr_id    = -1;
                    }
                }

                bool in_zslider2 = g_overlay_was_active &&
                                   (g_zslider_right > g_zslider_left) &&
                                   x >= g_zslider_left && x <= g_zslider_right &&
                                   y >= g_zslider_top  && y <= g_zslider_bottom;
                if (in_zslider2 && wnd->touch.zslider_ptr_id == -1) {
                    wnd->touch.zslider_ptr_id = pid;
                    wnd->touch.zslider_y      = g_zslider_horizontal ? x : y;
                    wnd->touch.zslider_offset = 0.0f;
                } else if (wnd->touch.down && !wnd->touch.boost_down) {

                    wnd->touch.boost_x         = x;
                    wnd->touch.boost_y         = y;
                    wnd->touch.boost_down      = true;
                    wnd->touch.boost_just_down = true;
                    wnd->touch.boost_ptr_id    = pid;
                } else if (!wnd->touch.down && wnd->touch.boost_down && !g_panel_open) {

                    float djx2 = x - g_joy_cx, djy2 = y - g_joy_cy;
                    bool joy_ok = g_is_trackpad_mode ||
                                  (g_joy_r > 0 && (djx2*djx2+djy2*djy2) <= g_joy_r*g_joy_r);
                    if (joy_ok) {
                        wnd->touch.x           = x;
                        wnd->touch.y           = y;
                        wnd->touch.down        = true;
                        wnd->touch.just_down   = true;
                        wnd->touch.move_ptr_id = pid;
                    }
                } else if (!wnd->touch.down && !wnd->touch.boost_down && !g_panel_open) {

                    float dbx2 = x - g_boost_cx, dby2 = y - g_boost_cy;
                    bool boost_hit = g_boost_r > 0 &&
                                     (dbx2*dbx2 + dby2*dby2) <= (g_boost_r * g_boost_r);
                    if (boost_hit) {
                        wnd->touch.boost_x         = x;
                        wnd->touch.boost_y         = y;
                        wnd->touch.boost_down      = true;
                        wnd->touch.boost_just_down = true;
                        wnd->touch.boost_ptr_id    = pid;
                    } else {
                        float djx2 = x - g_joy_cx, djy2 = y - g_joy_cy;
                        bool joy_ok = g_is_trackpad_mode ||
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

                if (wnd->touch.pending_reconcile) {
                    wnd->touch.pending_reconcile = false;
                    bool move_alive = false, boost_alive = false, zoom_alive = false;
                    for (int32_t i = 0; i < count; i++) {
                        int pid = (int)AMotionEvent_getPointerId(event, i);
                        if (pid == wnd->touch.move_ptr_id)    move_alive  = true;
                        if (pid == wnd->touch.boost_ptr_id)   boost_alive = true;
                        if (pid == wnd->touch.zslider_ptr_id) zoom_alive  = true;
                    }
                    if (!move_alive && wnd->touch.move_ptr_id != -1) {
                        wnd->touch.down        = false;
                        wnd->touch.just_down   = false;
                        wnd->touch.move_ptr_id = -1;
                    }
                    if (!boost_alive && wnd->touch.boost_ptr_id != -1) {
                        wnd->touch.boost_down      = false;
                        wnd->touch.boost_just_down = false;
                        wnd->touch.boost_ptr_id    = -1;
                    }
                    if (!zoom_alive && wnd->touch.zslider_ptr_id != -1) {
                        wnd->touch.zslider_ptr_id = -1;
                        wnd->touch.zslider_offset = 0.0f;
                    }
                }

                for (int32_t i = 0; i < count; i++) {
                    int   pid = (int)AMotionEvent_getPointerId(event, i);
                    float x   = AMotionEvent_getX(event, i);
                    float y   = AMotionEvent_getY(event, i);
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

                wnd->touch.down              = false;
                wnd->touch.just_down         = false;
                wnd->touch.boost_down        = false;
                wnd->touch.boost_just_down   = false;
                wnd->touch.move_ptr_id       = -1;
                wnd->touch.boost_ptr_id      = -1;
                wnd->touch.zslider_ptr_id    = -1;
                wnd->touch.zslider_offset    = 0.0f;
                wnd->touch.pending_reconcile = false;
                break;
            }

            case AMOTION_EVENT_ACTION_POINTER_UP: {

                wnd->touch.pending_reconcile = true;
                break;
            }

            case AMOTION_EVENT_ACTION_CANCEL:
                wnd->touch.down              = false;
                wnd->touch.just_down         = false;
                wnd->touch.boost_down        = false;
                wnd->touch.boost_just_down   = false;
                wnd->touch.move_ptr_id       = -1;
                wnd->touch.boost_ptr_id      = -1;
                wnd->touch.zslider_ptr_id    = -1;
                wnd->touch.zslider_offset    = 0.0f;
                wnd->touch.pending_reconcile = false;
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
    (void)window; (void)key;
    return false;
}

bool twindow_button_down(twindow* window, int button) {
    (void)button;
    return window ? window->touch.down : false;
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
