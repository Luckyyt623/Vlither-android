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

/* Zoom-slider screen rect — set every frame by ui_overlay.c */
float g_zslider_left    = 0;
float g_zslider_top     = 0;
float g_zslider_right   = 0;
float g_zslider_bottom  = 0;
float g_zslider_half_h  = 1;     /* half-height of the slider in pixels */
float g_zoom_sensitivity = 1.0f;
/* Set true by ui_overlay.c each PLAYING frame; cleared in tmouse_update.
   Guards the zoom-slot routing so it never steals taps on other screens. */
bool  g_overlay_drawn_this_frame = false;
bool  g_overlay_was_active    = false; /* previous-frame overlay status   */

/* Boost/joy button positions — set by ui_overlay.c each gameplay frame.
   Used for circle-only hit detection instead of broad screen zones.    */
float g_boost_cx = -9999, g_boost_cy = -9999, g_boost_r = 0;
float g_joy_cx   = -9999, g_joy_cy   = -9999, g_joy_r   = 0;
bool  g_is_trackpad_mode = true;
bool  g_panel_open       = false; /* true when settings panel is open */

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

/* -----------------------------------------------------------------------
 * Sticky immersive fullscreen — hides nav bar + status bar and keeps
 * them hidden after accidental edge swipes.
 * Safe to call from any point after android_main starts.
 * ----------------------------------------------------------------------- */
static void _android_set_immersive_fullscreen(void) {
    ANativeActivity_setWindowFlags(
        g_android_app->activity,
        0x00000400 /* AWINDOW_FLAG_FULLSCREEN */,
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

    /* getWindow() */
    jmethodID getWindow = (*env)->GetMethodID(env, act_class,
        "getWindow", "()Landroid/view/Window;");
    if (!getWindow || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); (*vm)->DetachCurrentThread(vm); return; }
    jobject window = (*env)->CallObjectMethod(env, activity, getWindow);
    if (!window || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); (*vm)->DetachCurrentThread(vm); return; }

    /* getDecorView() */
    jclass win_class = (*env)->GetObjectClass(env, window);
    if (!win_class) { (*env)->ExceptionClear(env); (*vm)->DetachCurrentThread(vm); return; }
    jmethodID getDecorView = (*env)->GetMethodID(env, win_class,
        "getDecorView", "()Landroid/view/View;");
    if (!getDecorView || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); (*vm)->DetachCurrentThread(vm); return; }
    jobject decor_view = (*env)->CallObjectMethod(env, window, getDecorView);
    if (!decor_view || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); (*vm)->DetachCurrentThread(vm); return; }

    /* setSystemUiVisibility(flags) */
    jclass view_class = (*env)->GetObjectClass(env, decor_view);
    if (!view_class) { (*env)->ExceptionClear(env); (*vm)->DetachCurrentThread(vm); return; }
    jmethodID setUiVis = (*env)->GetMethodID(env, view_class,
        "setSystemUiVisibility", "(I)V");
    if (!setUiVis || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); (*vm)->DetachCurrentThread(vm); return; }

    /* LAYOUT_STABLE | LAYOUT_HIDE_NAV | LAYOUT_FULLSCREEN |
       HIDE_NAVIGATION | FULLSCREEN | IMMERSIVE_STICKY        */
    jint flags = 0x100 | 0x200 | 0x400 | 0x002 | 0x004 | 0x1000;
    (*env)->CallVoidMethod(env, decor_view, setUiVis, flags);
    (*env)->ExceptionClear(env);   /* deprecated on API30+ but harmless */

    (*vm)->DetachCurrentThread(vm);
}

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
                /* Apply sticky immersive fullscreen once the window exists */
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
            /* Re-apply immersive fullscreen (system bars may have come back) */
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
        int32_t action        = AMotionEvent_getAction(event);
        int32_t action_masked = action & AMOTION_EVENT_ACTION_MASK;
        /* Index of the pointer that triggered this event (for UP/DOWN) */
        int32_t ptr_idx = (action >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT) &
                          AMOTION_EVENT_ACTION_POINTER_INDEX_MASK;

        /* ---- classify a touch as "boost" based on screen-side threshold ----
           g_ctrl_swap_sides is set by the UI swap button via an extern.
           Default (false): boost zone = right 20 %.
           Swapped  (true):  boost zone = left  20 %.                        */
        extern bool g_ctrl_swap_sides;
        float sw = (float)wnd->size[0];

        switch (action_masked) {

            case AMOTION_EVENT_ACTION_DOWN: {
                float x   = AMotionEvent_getX(event, 0);
                float y   = AMotionEvent_getY(event, 0);
                int   pid = (int)AMotionEvent_getPointerId(event, 0);

                /* ── Priority 1: Zoom slider (circle rect) ── */
                bool in_zslider = g_overlay_was_active &&
                                  (g_zslider_right > g_zslider_left) &&
                                  x >= g_zslider_left && x <= g_zslider_right &&
                                  y >= g_zslider_top  && y <= g_zslider_bottom;

                /* ── Priority 2: Boost button ──────────────────────────────────
                   Use the actual circle (enlarged 40% for tap forgiveness) when
                   the overlay has drawn at least once (g_boost_r > 0).
                   Fall back to right/left zone on the very first frame so the
                   first-frame case never causes an ID swap.                  */
                float dbx = x - g_boost_cx, dby = y - g_boost_cy;
                bool in_boost_circle;
                if (g_boost_r > 0) {
                    float det_r = g_boost_r * 1.4f;
                    in_boost_circle = (dbx*dbx + dby*dby) <= (det_r * det_r);
                } else {
                    /* Overlay not yet drawn — fall back to screen-zone */
                    in_boost_circle = g_ctrl_swap_sides
                                      ? (x < sw * 0.22f)
                                      : (x > sw * 0.78f);
                }

                /* ── Priority 3: Movement / trackpad ──
                   Trackpad mode: any touch not taken above.
                   Joystick mode: only within the joystick ring. */
                float djx = x - g_joy_cx, djy = y - g_joy_cy;
                bool in_joy_ring = g_joy_r > 0 &&
                                   (djx*djx + djy*djy) <= (g_joy_r * g_joy_r);
                bool can_move = !g_panel_open &&
                                (g_is_trackpad_mode || in_joy_ring);

                if (in_zslider && wnd->touch.zslider_ptr_id == -1) {
                    wnd->touch.zslider_ptr_id = pid;
                    wnd->touch.zslider_y      = y;
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
                    /* Touch outside all game zones (e.g. UI tap when panel closed).
                       Still register as ImGui mouse so buttons work. */
                    wnd->touch.x         = x;
                    wnd->touch.y         = y;
                    wnd->touch.down      = true;
                    wnd->touch.just_down = true;
                    wnd->touch.move_ptr_id = pid;
                }
                break;
            }

            case AMOTION_EVENT_ACTION_POINTER_DOWN: {
                /* ptr_idx is unreliable on many devices — find the NEW finger
                   by scanning all pointers and excluding already-tracked IDs.  */
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
                if (pid == -1) break; /* couldn't identify new finger */

                /* ── Early reconciliation ─────────────────────────────────────
                   A POINTER_UP sets pending_reconcile and defers clearing slots
                   until the next MOVE event.  But if a new POINTER_DOWN arrives
                   first (e.g. player re-touches the trackpad while still holding
                   boost), touch.down is still stale-true and none of the routing
                   branches below match — the new finger gets silently dropped.
                   Fix: run reconciliation here too, using this event's pointer
                   list, which contains exactly the fingers genuinely still down. */
                if (wnd->touch.pending_reconcile) {
                    wnd->touch.pending_reconcile = false;
                    bool move_alive2 = false, boost_alive2 = false;
                    for (int32_t ri = 0; ri < cnt2; ri++) {
                        int rpid = (int)AMotionEvent_getPointerId(event, ri);
                        /* Skip the NEW finger (pid) — Android can reuse pointer IDs
                           immediately after a POINTER_UP, which would make the old
                           slot appear "alive" even though it was just lifted.
                           We only count IDs that belong to fingers already held. */
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

                /* Zoom slider — highest priority */
                bool in_zslider2 = g_overlay_was_active &&
                                   (g_zslider_right > g_zslider_left) &&
                                   x >= g_zslider_left && x <= g_zslider_right &&
                                   y >= g_zslider_top  && y <= g_zslider_bottom;
                if (in_zslider2 && wnd->touch.zslider_ptr_id == -1) {
                    wnd->touch.zslider_ptr_id = pid;
                    wnd->touch.zslider_y      = y;
                    wnd->touch.zslider_offset = 0.0f;
                } else if (wnd->touch.down && !wnd->touch.boost_down) {
                    /* Move active, boost free — new finger = boost */
                    wnd->touch.boost_x         = x;
                    wnd->touch.boost_y         = y;
                    wnd->touch.boost_down      = true;
                    wnd->touch.boost_just_down = true;
                    wnd->touch.boost_ptr_id    = pid;
                } else if (!wnd->touch.down && wnd->touch.boost_down && !g_panel_open) {
                    /* Boost active, move free — new finger = move */
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
                    /* Neither taken — use circle detection */
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

                /* ── Deferred reconciliation ──────────────────────────────────
                   After a POINTER_UP we didn't know which finger left (ptr_idx
                   is unreliable).  This MOVE comes only from fingers still down,
                   so we can now safely clear any slot whose ID is absent.      */
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

                /* ── Normal per-pointer position update (by ID) ─────────────── */
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
                        float dy = y - wnd->touch.zslider_y;
                        wnd->touch.zslider_y       = y;
                        wnd->touch.zslider_offset += dy;
                        if (wnd->touch.zslider_offset >  g_zslider_half_h)
                            wnd->touch.zslider_offset =  g_zslider_half_h;
                        if (wnd->touch.zslider_offset < -g_zslider_half_h)
                            wnd->touch.zslider_offset = -g_zslider_half_h;
                    }
                }
                break;
            }

            case AMOTION_EVENT_ACTION_UP: {
                /* Last finger is gone — clear everything immediately */
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
                /* Don't clear slots here — ptr_idx is unreliable on many devices.
                   Instead, set a flag and reconcile on the very next MOVE event,
                   which only contains pointers that are genuinely still down. */
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

    /* Show/hide soft keyboard based on ImGui WantTextInput.
       Use extern declaration to avoid including cimgui headers here. */
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

#endif /* ANDROID */
