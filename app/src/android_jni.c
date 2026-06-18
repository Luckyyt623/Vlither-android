#ifdef ANDROID

#include "android_jni.h"
#include "android_path.h"
#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <time.h>
#include <stdio.h>

#define AJNI_TAG "vlither_jni"
#define AJNI_LOG(...) \
    __android_log_print(ANDROID_LOG_DEBUG, AJNI_TAG, __VA_ARGS__)

#define UNLOCK_FILENAME "vlither_unlock_expiry.txt"

/* g_android_app is defined in thermite/src/core/tentry_android.c */
extern struct android_app* g_android_app;

/* ── get_unlock_remaining_ms — reads file, zero JNI ─────────────────── */

long android_jni_get_unlock_remaining_ms(void) {
    const char* files_dir = android_get_files_dir();
    if (!files_dir || files_dir[0] == '\0') {
        AJNI_LOG("get_unlock: files_dir not set yet");
        return -1L;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", files_dir, UNLOCK_FILENAME);

    FILE* f = fopen(path, "r");
    if (!f) return -1L;  /* file missing = no ad watched yet */

    long long expiry_ms = 0LL;
    int n = fscanf(f, "%lld", &expiry_ms);
    fclose(f);

    if (n != 1 || expiry_ms <= 0LL) {
        AJNI_LOG("get_unlock: bad file content");
        return -1L;
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long long now_ms = (long long)ts.tv_sec * 1000LL +
                       (long long)(ts.tv_nsec / 1000000LL);

    long long remaining = expiry_ms - now_ms;
    return (remaining > 0LL) ? (long)remaining : -1L;
}

/* ── request_ad — JNI call via g_android_app ────────────────────────── */

void android_jni_request_ad(void) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        AJNI_LOG("request_ad: g_android_app not ready");
        return;
    }

    JavaVM*  vm  = g_android_app->activity->vm;
    jobject  obj = g_android_app->activity->clazz;
    JNIEnv*  env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
            AJNI_LOG("request_ad: AttachCurrentThread failed");
            return;
        }
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        AJNI_LOG("request_ad: GetEnv failed status=%d", status);
        return;
    }

    jclass cls = (*env)->FindClass(env, "com/vlither/GameActivity");
    if (!cls) {
        AJNI_LOG("request_ad: GameActivity class not found");
        (*env)->ExceptionClear(env);
        goto cleanup;
    }

    {
        jmethodID mid = (*env)->GetStaticMethodID(
            env, cls, "requestAdFromC", "(Landroid/app/Activity;)V");
        if (!mid) {
            AJNI_LOG("request_ad: method not found");
            (*env)->ExceptionClear(env);
            goto cleanup;
        }
        (*env)->CallStaticVoidMethod(env, cls, mid, obj);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    }

cleanup:
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

/* ── clipboard bridge — JNI calls via g_android_app ─────────────────── */

static char clipboard_buf[1024] = {0};

const char* android_jni_get_clipboard_text(void) {
    clipboard_buf[0] = '\0';

    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        AJNI_LOG("get_clipboard: g_android_app not ready");
        return clipboard_buf;
    }

    JavaVM*  vm  = g_android_app->activity->vm;
    jobject  obj = g_android_app->activity->clazz;
    JNIEnv*  env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
            AJNI_LOG("get_clipboard: AttachCurrentThread failed");
            return clipboard_buf;
        }
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        AJNI_LOG("get_clipboard: GetEnv failed status=%d", status);
        return clipboard_buf;
    }

    jclass cls = (*env)->FindClass(env, "com/vlither/GameActivity");
    if (!cls) {
        AJNI_LOG("get_clipboard: GameActivity class not found");
        (*env)->ExceptionClear(env);
        goto cleanup;
    }

    {
        jmethodID mid = (*env)->GetStaticMethodID(
            env, cls, "getClipboardTextFromC",
            "(Landroid/app/Activity;)Ljava/lang/String;");
        if (!mid) {
            AJNI_LOG("get_clipboard: method not found");
            (*env)->ExceptionClear(env);
            goto cleanup;
        }
        jstring jtext = (jstring)(*env)->CallStaticObjectMethod(env, cls, mid, obj);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
        } else if (jtext) {
            const char* utf = (*env)->GetStringUTFChars(env, jtext, NULL);
            if (utf) {
                strncpy(clipboard_buf, utf, sizeof(clipboard_buf) - 1);
                clipboard_buf[sizeof(clipboard_buf) - 1] = '\0';
                (*env)->ReleaseStringUTFChars(env, jtext, utf);
            }
            (*env)->DeleteLocalRef(env, jtext);
        }
    }

cleanup:
    if (did_attach) (*vm)->DetachCurrentThread(vm);
    return clipboard_buf;
}

void android_jni_set_clipboard_text(const char* text) {
    if (!text) return;
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        AJNI_LOG("set_clipboard: g_android_app not ready");
        return;
    }

    JavaVM*  vm  = g_android_app->activity->vm;
    jobject  obj = g_android_app->activity->clazz;
    JNIEnv*  env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
            AJNI_LOG("set_clipboard: AttachCurrentThread failed");
            return;
        }
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        AJNI_LOG("set_clipboard: GetEnv failed status=%d", status);
        return;
    }

    jclass cls = (*env)->FindClass(env, "com/vlither/GameActivity");
    if (!cls) {
        AJNI_LOG("set_clipboard: GameActivity class not found");
        (*env)->ExceptionClear(env);
        goto cleanup;
    }

    {
        jmethodID mid = (*env)->GetStaticMethodID(
            env, cls, "setClipboardTextFromC",
            "(Landroid/app/Activity;Ljava/lang/String;)V");
        if (!mid) {
            AJNI_LOG("set_clipboard: method not found");
            (*env)->ExceptionClear(env);
            goto cleanup;
        }
        jstring jtext = (*env)->NewStringUTF(env, text);
        (*env)->CallStaticVoidMethod(env, cls, mid, obj, jtext);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, jtext);
    }

cleanup:
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

#endif /* ANDROID */
