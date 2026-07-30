#ifdef ANDROID

#include "../core/tenv.h"
#include "../framework/twindow.h"
#include "../framework/tkeyboard.h"
#include "android_jni.h"

#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define DLOG(fmt, ...) do { \
    char _dbuf[256]; \
    snprintf(_dbuf, sizeof(_dbuf), fmt, ##__VA_ARGS__); \
    __android_log_print(ANDROID_LOG_ERROR, "vlither", "%s", _dbuf); \
} while(0)
#include "../framework/tmouse.h"
#include "../graphics/tcontext.h"
#include <android_native_app_glue.h>
#include <android/log.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "user.h"
#include "android_path.h"

#include <stdio.h>
#include <signal.h>
#include <stdarg.h>

#define LOG_TAG "vlither"

static FILE* g_log_file = NULL;

static void vlog_write(const char* level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    __android_log_vprint(
        level[0]=='E' ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO,
        LOG_TAG, fmt, args);
    va_end(args);
    if (g_log_file) {
        va_start(args, fmt);
        fprintf(g_log_file, "[%s] ", level);
        vfprintf(g_log_file, fmt, args);
        fprintf(g_log_file, "\n");
        fflush(g_log_file);
        va_end(args);
    }
}
#define LOGI(...) vlog_write("I", __VA_ARGS__)
#define LOGE(...) vlog_write("E", __VA_ARGS__)

static void crash_handler(int sig) {
    if (g_log_file) {
        fprintf(g_log_file, "[CRASH] signal %d\n", sig);
        fflush(g_log_file);
        fclose(g_log_file);
        g_log_file = NULL;
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

struct android_app* g_android_app = NULL;

void tlaunch(tenv* env);
void tinit(tenv* env);
void tinput(tenv* env);
void trender(tenv* env);
void tresize(tenv* env);
void tdestroy(tenv* env);

static double g_time_base = -1.0;

double glfwGetTime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;

    return (g_time_base >= 0.0) ? (now - g_time_base) : now;
}

void glfwSetTime(double t) {
    if (t == 0.0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        g_time_base = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    }
}

tkeyboard* tkeyboard_create(twindow* window) {
    (void)window;
    return calloc(1, sizeof(tkeyboard));
}
void tkeyboard_update(tkeyboard* kb) { (void)kb; }
int  tkeyboard_key_pressed(tkeyboard* kb, int key) { (void)kb; (void)key; return 0; }
int  tkeyboard_key_released(tkeyboard* kb, int key) { (void)kb; (void)key; return 0; }
void tkeyboard_destroy(tkeyboard* kb) { free(kb); }

tmouse* tmouse_create(twindow* window) {
    (void)window;
    return calloc(1, sizeof(tmouse));
}
void tmouse_update(tmouse* ms) {
    if (ms->window) {
        ms->pos[0] = ms->window->touch.x;
        ms->pos[1] = ms->window->touch.y;
    }
    ms->dwheel = 0;

    {
        extern bool  g_overlay_drawn_this_frame;
        extern bool  g_overlay_was_active;
        extern float g_zslider_left, g_zslider_top;
        extern float g_zslider_right, g_zslider_bottom;

        g_overlay_was_active = g_overlay_drawn_this_frame;
        if (!g_overlay_drawn_this_frame) {
            g_zslider_left = g_zslider_top = 0.0f;
            g_zslider_right = g_zslider_bottom = 0.0f;

            if (ms->window) {
                ms->window->touch.zslider_ptr_id = -1;
                ms->window->touch.zslider_offset = 0.0f;
            }
        }
        g_overlay_drawn_this_frame = false;
    }
}
int  tmouse_button_pressed(tmouse* ms, int button)  { (void)button; return ms->window ? ms->window->touch.just_down : 0; }
int  tmouse_button_released(tmouse* ms, int button) { (void)ms; (void)button; return 0; }
void tmouse_destroy(tmouse* ms) { free(ms); }

void android_main(struct android_app* app) {
    g_android_app = app;

    g_log_file = fopen("/sdcard/vlither_log.txt", "w");
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGBUS,  crash_handler);
    DLOG("android_main started");

    android_set_files_dir(app->activity->internalDataPath);
    DLOG("files_dir: %s", app->activity->internalDataPath);

    tenv env;
    memset(&env, 0, sizeof(tenv));

    env.config.vsync        = true;
    env.config.running      = true;
    env.config.fullscreen   = false;
    env.config.resizable    = false;
    env.config.aspect_ratio = 16 / 9.0f;
    env.config.fif          = 3;
    env.config.title        = "Vlither";
    env.usr = malloc(sizeof(tuser_data));

    DLOG("calling tlaunch");
    tlaunch(&env);

    DLOG("creating window");
    env.wnd = twindow_create(&env, trender, tresize);
    DLOG("window created");

    env.kb  = tkeyboard_create(env.wnd);
    env.ms  = tmouse_create(env.wnd);

    DLOG("creating Vulkan context");
    env.ctx = tcontext_create(env.wnd, env.config.vsync, env.config.fif);
    if (env.ctx == NULL) {
        DLOG("FATAL: Failed to create Vulkan context");
        if (g_log_file) fclose(g_log_file);
        return;
    }
    DLOG("Vulkan context created");

    DLOG("calling tinit");
    tinit(&env);
    DLOG("tinit done, running=%d", env.config.running);

    DLOG("entering game loop");
    bool g_first_frame_done = false;
    while (env.config.running) {
        if (!env.ctx->swapchain_ok) {

            twindow_poll_input(env.wnd);
            if (g_android_app->destroyRequested) break;
            /* Re-check swapchain_ok: if we just resumed from background,
             * handle_app_cmd() already rebuilt the surface + swapchain
             * from inside twindow_poll_input() above (APP_CMD_INIT_WINDOW).
             * Doing it again here would be a redundant, wasted rebuild. */
            if (!env.ctx->swapchain_ok &&
                env.wnd->size[0] > 0 && env.wnd->size[1] > 0) {
                tcontext_resize(env.ctx, env.wnd->size, env.config.vsync);
                tresize(&env);
                DLOG("swapchain rebuilt: %dx%d",
                     env.wnd->size[0], env.wnd->size[1]);
            }
            continue;
        }

        twindow_poll_input(env.wnd);

        if (!env.config.running) break;

        struct timespec frame_start;
        clock_gettime(CLOCK_MONOTONIC, &frame_start);

        tinput(&env);
        trender(&env);

        int fps_limit = env.usr ? env.usr->usrs.fps_limit : 0;
        if (fps_limit > 0) {
            struct timespec frame_end;
            clock_gettime(CLOCK_MONOTONIC, &frame_end);
            double elapsed = (double)(frame_end.tv_sec - frame_start.tv_sec) +
                (double)(frame_end.tv_nsec - frame_start.tv_nsec) * 1e-9;
            double remaining = 1.0 / (double)fps_limit - elapsed;
            if (remaining > 0.0005) {
                struct timespec sleep_for = {
                    .tv_sec = (time_t)remaining,
                    .tv_nsec = (long)((remaining - (time_t)remaining) * 1e9)
                };
                nanosleep(&sleep_for, NULL);
            }
        }

        if (!g_first_frame_done) {
            g_first_frame_done = true;
            android_jni_notify_game_ready();
        }
        tkeyboard_update(env.kb);
        tmouse_update(env.ms);
    }

    tdestroy(&env);
    tcontext_destroy(env.ctx);
    tmouse_destroy(env.ms);
    tkeyboard_destroy(env.kb);
    twindow_destroy(env.wnd);
    free(env.usr);
}

#endif
