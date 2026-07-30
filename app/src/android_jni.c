#ifdef ANDROID

#include "android_jni.h"
#include "android_path.h"
#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AJNI_TAG "vlither_jni"
#define AJNI_LOG(...) \
    __android_log_print(ANDROID_LOG_DEBUG, AJNI_TAG, __VA_ARGS__)

#define UNLOCK_FILENAME "vlither_unlock_expiry.txt"


extern struct android_app* g_android_app;

static int android_jni_read_i32_le(const unsigned char* bytes) {
    return (int)((unsigned int)bytes[0] |
                 ((unsigned int)bytes[1] << 8) |
                 ((unsigned int)bytes[2] << 16) |
                 ((unsigned int)bytes[3] << 24));
}

bool android_jni_poll_ime_event(android_ime_event* out_event) {
    if (!out_event) return false;
    memset(out_event, 0, sizeof(*out_event));

    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm || !g_android_app->activity->clazz) {
        return false;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;
    bool have_event = false;
    jclass cls = NULL;
    jbyteArray packet = NULL;
    jbyte* packet_bytes = NULL;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return false;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return false;
    }

    cls = (*env)->GetObjectClass(env, g_android_app->activity->clazz);
    if (!cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto ime_poll_cleanup;
    }

    jmethodID mid = (*env)->GetStaticMethodID(
        env, cls, "pollImeEvent", "(Landroid/app/Activity;)[B");
    if (!mid || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto ime_poll_cleanup;
    }

    packet = (jbyteArray)(*env)->CallStaticObjectMethod(
        env, cls, mid, g_android_app->activity->clazz);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        packet = NULL;
        goto ime_poll_cleanup;
    }
    if (!packet) goto ime_poll_cleanup;

    jsize len = (*env)->GetArrayLength(env, packet);
    if (len <= 0 || len > 65541) goto ime_poll_cleanup;

    packet_bytes = (*env)->GetByteArrayElements(env, packet, NULL);
    if (!packet_bytes || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        packet_bytes = NULL;
        goto ime_poll_cleanup;
    }

    const unsigned char* bytes = (const unsigned char*)packet_bytes;
    if (bytes[0] == ANDROID_IME_EVENT_TEXT && len > 1) {
        size_t text_len = (size_t)len - 1u;
        char* copy = (char*)malloc(text_len + 1u);
        if (copy) {
            memcpy(copy, bytes + 1, text_len);
            copy[text_len] = '\0';
            out_event->type = ANDROID_IME_EVENT_TEXT;
            out_event->text = copy;
            have_event = true;
        }
    } else if (bytes[0] == ANDROID_IME_EVENT_KEY && len == 13) {
        out_event->type = ANDROID_IME_EVENT_KEY;
        out_event->keycode = android_jni_read_i32_le(bytes + 1);
        out_event->action = android_jni_read_i32_le(bytes + 5);
        out_event->meta_state = android_jni_read_i32_le(bytes + 9);
        have_event = true;
    } else if (bytes[0] == ANDROID_IME_EVENT_COMPOSITION && len >= 5) {
        size_t text_len = (size_t)len - 5u;
        char* copy = (char*)malloc(text_len + 1u);
        if (copy) {
            if (text_len > 0) memcpy(copy, bytes + 5, text_len);
            copy[text_len] = '\0';
            out_event->type = ANDROID_IME_EVENT_COMPOSITION;
            out_event->text = copy;
            out_event->replace_codepoints =
                android_jni_read_i32_le(bytes + 1);
            if (out_event->replace_codepoints < 0)
                out_event->replace_codepoints = 0;
            if (out_event->replace_codepoints > 4096)
                out_event->replace_codepoints = 4096;
            have_event = true;
        }
    }

ime_poll_cleanup:
    if (packet_bytes) {
        (*env)->ReleaseByteArrayElements(env, packet, packet_bytes, JNI_ABORT);
    }
    if (packet) (*env)->DeleteLocalRef(env, packet);
    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
    return have_event;
}

void android_jni_release_ime_event(android_ime_event* event) {
    if (!event) return;
    free(event->text);
    memset(event, 0, sizeof(*event));
}

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


void android_jni_set_text_input_active(bool active) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return;
    }

    jclass cls = (*env)->GetObjectClass(
        env, g_android_app->activity->clazz);
    if (!cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto text_input_cleanup;
    }

    jmethodID mid = (*env)->GetStaticMethodID(
        env, cls, "setTextInputActive", "(Landroid/app/Activity;Z)V");
    if (!mid || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto text_input_cleanup;
    }

    (*env)->CallStaticVoidMethod(env, cls, mid,
        g_android_app->activity->clazz, active ? JNI_TRUE : JNI_FALSE);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

text_input_cleanup:
    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

bool android_jni_enqueue_clipboard_paste(void) {
    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm || !g_android_app->activity->clazz) {
        return false;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;
    bool queued = false;
    jclass cls = NULL;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return false;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return false;
    }

    cls = (*env)->GetObjectClass(env, g_android_app->activity->clazz);
    if (!cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto enqueue_paste_cleanup;
    }

    jmethodID mid = (*env)->GetStaticMethodID(
        env, cls, "enqueueClipboardPaste", "(Landroid/app/Activity;)Z");
    if (!mid || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto enqueue_paste_cleanup;
    }

    queued = (*env)->CallStaticBooleanMethod(
        env, cls, mid, g_android_app->activity->clazz) == JNI_TRUE;
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        queued = false;
    }

enqueue_paste_cleanup:
    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
    return queued;
}

const char* android_jni_get_clipboard_text(void) {
    static char* clipboard_cache = NULL;
    static size_t clipboard_capacity = 0;
    static const char empty[] = "";

    if (!g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return empty;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return empty;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return empty;
    }

    const char* result = empty;
    jclass cls = (*env)->GetObjectClass(
        env, g_android_app->activity->clazz);
    if (!cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto clipboard_get_cleanup;
    }

    jmethodID mid = (*env)->GetStaticMethodID(
        env, cls, "getClipboardUtf8", "(Landroid/app/Activity;)[B");
    if (!mid || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto clipboard_get_cleanup;
    }

    jbyteArray bytes = (jbyteArray)(*env)->CallStaticObjectMethod(
        env, cls, mid, g_android_app->activity->clazz);
    if (!bytes || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto clipboard_get_cleanup;
    }

    jsize len = (*env)->GetArrayLength(env, bytes);
    if (len < 0) len = 0;
    if (len > 1048576) len = 1048576;
    size_t needed = (size_t)len + 1u;
    if (needed > clipboard_capacity) {
        size_t new_capacity = needed < 256u ? 256u : needed;
        char* new_cache = (char*)realloc(clipboard_cache, new_capacity);
        if (new_cache) {
            clipboard_cache = new_cache;
            clipboard_capacity = new_capacity;
        }
    }

    if (clipboard_cache && needed <= clipboard_capacity) {
        if (len > 0) {
            (*env)->GetByteArrayRegion(
                env, bytes, 0, len, (jbyte*)clipboard_cache);
        }
        if (!(*env)->ExceptionCheck(env)) {
            clipboard_cache[len] = '\0';
            result = clipboard_cache;
        } else {
            (*env)->ExceptionClear(env);
        }
    }
    (*env)->DeleteLocalRef(env, bytes);

clipboard_get_cleanup:
    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
    return result;
}

void android_jni_set_clipboard_text(const char* text) {
    if (!text || !g_android_app || !g_android_app->activity ||
        !g_android_app->activity->vm) {
        return;
    }

    JavaVM* vm = g_android_app->activity->vm;
    JNIEnv* env = NULL;
    bool did_attach = false;

    int status = (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK)
            return;
        did_attach = true;
    } else if (status != JNI_OK || !env) {
        return;
    }

    jclass cls = (*env)->GetObjectClass(
        env, g_android_app->activity->clazz);
    if (!cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto clipboard_set_cleanup;
    }

    jmethodID mid = (*env)->GetStaticMethodID(
        env, cls, "setClipboardUtf8", "(Landroid/app/Activity;[B)V");
    if (!mid || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto clipboard_set_cleanup;
    }

    size_t length = strlen(text);
    if (length > 1048576u) length = 1048576u;
    jbyteArray bytes = (*env)->NewByteArray(env, (jsize)length);
    if (!bytes || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        goto clipboard_set_cleanup;
    }
    if (length > 0) {
        (*env)->SetByteArrayRegion(
            env, bytes, 0, (jsize)length, (const jbyte*)text);
    }
    if (!(*env)->ExceptionCheck(env)) {
        (*env)->CallStaticVoidMethod(
            env, cls, mid, g_android_app->activity->clazz, bytes);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, bytes);

clipboard_set_cleanup:
    if (cls) (*env)->DeleteLocalRef(env, cls);
    if (did_attach) (*vm)->DetachCurrentThread(vm);
}

#endif
