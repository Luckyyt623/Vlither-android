#ifdef ANDROID

#include "twindow.h"
#include "../core/tenv.h"

#include <android/log.h>
#include <android_native_app_glue.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "vlither"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern struct android_app* g_android_app;

/* Forward declarations from cimgui */
typedef struct ImGuiIO ImGuiIO;
extern ImGuiIO* igGetIO_Nil(void);
extern void ImGuiIO_AddInputCharacter(ImGuiIO* self, unsigned int c);
extern void ImGuiIO_AddKeyEvent(ImGuiIO* self, int key, bool down);

/* Show/hide the Android soft keyboard via JNI */
static void _android_set_keyboard(bool show) {
    JNIEnv* env = NULL;
    JavaVM* vm = g_android_app->activity->vm;
    (*vm)->AttachCurrentThread(vm, &env, NULL);

    jobject activity = g_android_app->activity->clazz;
    jclass cls = (*env)->GetObjectClass(env, activity);

    /* Get the InputMethodManager via getSystemService */
    jmethodID getSystemService = (*env)->GetMethodID(env, cls,
        "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring ime_str = (*env)->NewStringUTF(env, "input_method");
    jobject imm = (*env)->CallObjectMethod(env, activity, getSystemService, ime_str);
    (*env)->DeleteLocalRef(env, ime_str);

    jclass imm_cls = (*env)->GetObjectClass(env, imm);

    /* Get the window token */
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

/* FIX: ANativeWindow_getWidth/Height return pre-rotation (portrait) dimensions
   on many Android devices even when the app is locked to landscape.
   Since this app is landscape-only, width must always be the larger value. */
static void set_window_size(twindow* wnd, ANativeWindow* win) {
    int w = ANativeWindow_getWidth(win);
    int h = ANativeWindow_getHeight(win);
    LOGI("Raw window dims: %dx%d", w, h);
    /* Swap if portrait — landscape means width > height */
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

    /* Forward key events from soft keyboard to ImGui */
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
        int32_t action   = AKeyEvent_getAction(event);
        int32_t keycode  = AKeyEvent_getKeyCode(event);
        int32_t meta     = AKeyEvent_getMetaState(event);

        if (action == AKEY_EVENT_ACTION_DOWN || action == AKEY_EVENT_ACTION_MULTIPLE) {
            /* Get unicode character */
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

            /* Handle backspace */
            if (keycode == AKEYCODE_DEL) {
ImGuiIO* _io = igGetIO_Nil();
                if (_io) {
                    ImGuiIO_AddKeyEvent(_io, 523 /* ImGuiKey_Backspace */, true);
                    ImGuiIO_AddKeyEvent(_io, 523, false);
                }
            }
        }
        return 1;
    }

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event);
        int32_t action_masked = action & AMOTION_EVENT_ACTION_MASK;

        float x = AMotionEvent_getX(event, 0);
        float y = AMotionEvent_getY(event, 0);

        wnd->touch.x = x;
        wnd->touch.y = y;

        switch (action_masked) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                wnd->touch.down      = true;
                wnd->touch.just_down = true;
                wnd->touch.boost_down = (x > wnd->size[0] * 0.8f);
                break;

            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_POINTER_UP:
            case AMOTION_EVENT_ACTION_CANCEL:
                wnd->touch.down       = false;
                wnd->touch.just_down  = false;
                wnd->touch.boost_down = false;
                break;

            case AMOTION_EVENT_ACTION_MOVE:
                wnd->touch.just_down = false;
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
    window->touch.just_down = false;

    int events;
    struct android_poll_source* source;
    while (ALooper_pollAll(0, NULL, &events, (void**)&source) >= 0) {
        if (source) source->process(g_android_app, source);
        if (g_android_app->destroyRequested) {
            if (window->env) window->env->config.running = false;
            return;
        }
    }

    /* Show/hide soft keyboard based on ImGui WantTextInput.
       Use extern declaration to avoid including cimgui headers here. */
    extern bool g_imgui_wants_keyboard;
    if (g_imgui_wants_keyboard != g_keyboard_shown) {
        g_keyboard_shown = g_imgui_wants_keyboard;
        _android_set_keyboard(g_imgui_wants_keyboard);
    }
}

void twindow_wait_input(twindow* window) {
    window->touch.just_down = false;

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

#endif /* ANDROID */
