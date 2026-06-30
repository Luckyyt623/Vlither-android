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

extern struct android_app* g_android_app;

long android_jni_get_unlock_remaining_ms(void) {
    const char* files_dir = android_get_files_dir();
    if (!files_dir || files_dir[0] == '\0') {
        AJNI_LOG("get_unlock: files_dir not set yet");
        return -1L;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", files_dir, UNLOCK_FILENAME);

    FILE* f = fopen(path, "r");
    if (!f) return -1L;

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

void android_jni_notify_game_ready(void) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        AJNI_LOG("notify_game_ready: g_android_app not ready");
        return;
    }

    JavaVM*  vm  = g_android_app->activity->vm;
    jobject  obj = g_android_app->activity->clazz;
    JNIEnv*  env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
            AJNI_LOG("notify_game_ready: AttachCurrentThread failed");
            return;
        }
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        AJNI_LOG("notify_game_ready: GetEnv failed status=%d", status);
        return;
    }

    jclass cls = (*env)->FindClass(env, "com/vlither/GameActivity");
    if (!cls) {
        AJNI_LOG("notify_game_ready: GameActivity class not found");
        (*env)->ExceptionClear(env);
        goto cleanup2;
    }

    {
        jmethodID mid = (*env)->GetStaticMethodID(
            env, cls, "notifyGameReady", "(Landroid/app/Activity;)V");
        if (!mid) {
            AJNI_LOG("notify_game_ready: method not found");
            (*env)->ExceptionClear(env);
            goto cleanup2;
        }
        (*env)->CallStaticVoidMethod(env, cls, mid, obj);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    }

cleanup2:
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

void android_jni_open_url(const char* url) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        AJNI_LOG("open_url: g_android_app not ready");
        return;
    }

    JavaVM*  vm  = g_android_app->activity->vm;
    jobject  act = g_android_app->activity->clazz;
    JNIEnv*  env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
            AJNI_LOG("open_url: AttachCurrentThread failed");
            return;
        }
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        AJNI_LOG("open_url: GetEnv failed status=%d", status);
        return;
    }

    jclass  uri_cls    = (*env)->FindClass(env, "android/net/Uri");
    jclass  intent_cls = (*env)->FindClass(env, "android/content/Intent");
    if (!uri_cls || !intent_cls) {
        AJNI_LOG("open_url: class lookup failed");
        (*env)->ExceptionClear(env);
        goto ou_cleanup;
    }

    {

        jmethodID uri_parse = (*env)->GetStaticMethodID(
            env, uri_cls, "parse",
            "(Ljava/lang/String;)Landroid/net/Uri;");
        jstring url_jstr = (*env)->NewStringUTF(env, url);
        jobject uri = (*env)->CallStaticObjectMethod(
            env, uri_cls, uri_parse, url_jstr);
        (*env)->DeleteLocalRef(env, url_jstr);
        if (!uri || (*env)->ExceptionCheck(env)) {
            AJNI_LOG("open_url: Uri.parse failed");
            (*env)->ExceptionClear(env);
            goto ou_cleanup;
        }

        jfieldID av_fid = (*env)->GetStaticFieldID(
            env, intent_cls, "ACTION_VIEW", "Ljava/lang/String;");
        jstring action_view = (jstring)(*env)->GetStaticObjectField(
            env, intent_cls, av_fid);

        jmethodID ctor = (*env)->GetMethodID(
            env, intent_cls, "<init>",
            "(Ljava/lang/String;Landroid/net/Uri;)V");
        jobject intent = (*env)->NewObject(
            env, intent_cls, ctor, action_view, uri);
        if (!intent || (*env)->ExceptionCheck(env)) {
            AJNI_LOG("open_url: Intent ctor failed");
            (*env)->ExceptionClear(env);
            goto ou_cleanup;
        }

        jclass act_cls = (*env)->GetObjectClass(env, act);
        jmethodID sa   = (*env)->GetMethodID(
            env, act_cls, "startActivity",
            "(Landroid/content/Intent;)V");
        (*env)->CallVoidMethod(env, act, sa, intent);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

        (*env)->DeleteLocalRef(env, intent);
        (*env)->DeleteLocalRef(env, uri);
    }

ou_cleanup:
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

#endif
