#ifdef ANDROID

#include "../core/tenv.h"
#include "../framework/twindow.h"
#include "../framework/tkeyboard.h"
#include "android_jni.h"

/* ------------------------------------------------------------------ */
/*  Debug logger — sends log lines to ntfy.sh so you can read them    */
/*  in any browser at: https://ntfy.sh/vlither-debug-4821             */
/* ------------------------------------------------------------------ */
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define NTFY_TOPIC "vlither-debug-4821"

static void ntfy_log(const char* msg) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo("ntfy.sh", "80", &hints, &res) != 0) return;
    int fd = socket(res->ai_family, res->ai_socktype, 0);
    if (fd < 0) { freeaddrinfo(res); return; }
    struct timeval tv = {3, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (connect(fd, res->ai_addr, res->ai_addrlen) == 0) {
        char req[2048];
        int n = snprintf(req, sizeof(req),
            "POST /" NTFY_TOPIC " HTTP/1.1\r\n"
            "Host: ntfy.sh\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n%s",
            (int)strlen(msg), msg);
        write(fd, req, n);
    }
    close(fd);
    freeaddrinfo(res);
}

#define DLOG(fmt, ...) do { \
    char _dbuf[256]; \
    snprintf(_dbuf, sizeof(_dbuf), fmt, ##__VA_ARGS__); \
    __android_log_print(ANDROID_LOG_ERROR, "vlither", "%s", _dbuf); \
    ntfy_log(_dbuf); \
} while(0)
#include "../framework/tmouse.h"
#include "../graphics/tcontext.h"
#include <android_native_app_glue.h>
#include <android/log.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

/* App user data type */
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

/* FIX: g_android_app defined ONCE here.
   twindow_android.c uses extern to reference it. */
struct android_app* g_android_app = NULL;

/* Forward declarations of app callbacks (defined in main.c) */
void tlaunch(tenv* env);
void tinit(tenv* env);
void tinput(tenv* env);
void trender(tenv* env);
void tresize(tenv* env);
void tdestroy(tenv* env);

/* ------------------------------------------------------------------ */
/*  glfwGetTime / glfwSetTime                                           */
/* ------------------------------------------------------------------ */

/* FIX: g_time_base declared BEFORE glfwGetTime so it can be read.
   Previously it was declared after, so glfwGetTime always returned
   raw monotonic time (seconds since boot) instead of time since
   glfwSetTime(0), breaking all game timing. */
static double g_time_base = -1.0;

double glfwGetTime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    /* Subtract base so time starts from 0 when glfwSetTime(0) was called */
    return (g_time_base >= 0.0) ? (now - g_time_base) : now;
}

void glfwSetTime(double t) {
    if (t == 0.0) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        g_time_base = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    }
}

/* ------------------------------------------------------------------ */
/*  Android keyboard/mouse stubs                                        */
/* ------------------------------------------------------------------ */

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
    /* Zoom is applied directly to ms_zoom in ui_overlay.c — no dwheel needed */
    /* If the overlay was NOT drawn this frame (not in gameplay), clear the
       zoom-slider rect so it cannot steal taps on the settings screen etc. */
    {
        extern bool  g_overlay_drawn_this_frame;
        extern bool  g_overlay_was_active;
        extern float g_zslider_left, g_zslider_top;
        extern float g_zslider_right, g_zslider_bottom;
        /* Pass this frame's status to next frame's input routing */
        g_overlay_was_active = g_overlay_drawn_this_frame;
        if (!g_overlay_drawn_this_frame) {
            g_zslider_left = g_zslider_top = 0.0f;
            g_zslider_right = g_zslider_bottom = 0.0f;
            /* Also release zoom-slider touch slot so it doesn't persist */
            if (ms->window) {
                ms->window->touch.zslider_ptr_id = -1;
                ms->window->touch.zslider_offset = 0.0f;
            }
        }
        g_overlay_drawn_this_frame = false; /* reset for next frame */
    }
}
int  tmouse_button_pressed(tmouse* ms, int button)  { (void)button; return ms->window ? ms->window->touch.just_down : 0; }
int  tmouse_button_released(tmouse* ms, int button) { (void)ms; (void)button; return 0; }
void tmouse_destroy(tmouse* ms) { free(ms); }

/* ------------------------------------------------------------------ */
/*  android_main — NDK entry point (replaces int main())                */
/* ------------------------------------------------------------------ */

void android_main(struct android_app* app) {
    g_android_app = app;

    /* Open log file on SD card so we can read it from Termux */
    g_log_file = fopen("/sdcard/vlither_log.txt", "w");
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGBUS,  crash_handler);
    DLOG("android_main started");

    /* Store writable path so user_settings.c can use it */
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
            /* Swapchain is out of date — poll events then rebuild it */
            twindow_poll_input(env.wnd);
            if (g_android_app->destroyRequested) break;
            if (env.wnd->size[0] > 0 && env.wnd->size[1] > 0) {
                tcontext_resize(env.ctx, env.wnd->size, env.config.vsync);
                tresize(&env);
                DLOG("swapchain rebuilt: %dx%d",
                     env.wnd->size[0], env.wnd->size[1]);
            }
            continue;
        }

        twindow_poll_input(env.wnd);

        if (!env.config.running) break;

        tinput(&env);
        trender(&env);

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

#endif /* ANDROID */
