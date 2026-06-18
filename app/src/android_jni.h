#ifndef ANDROID_JNI_H
#define ANDROID_JNI_H

#ifdef ANDROID

/**
 * Returns ms remaining (>0) if unlocked, -1 if locked/expired.
 * Reads the unlock file written by MainActivity.kt — NO JNI needed.
 */
long android_jni_get_unlock_remaining_ms(void);

/**
 * Brings MainActivity to the foreground to show the rewarded ad.
 * Uses g_android_app (set by thermite's android_main) for JNI.
 */
void android_jni_request_ad(void);

/**
 * Reads the system clipboard's current text via JNI (Android has no
 * physical Ctrl+V, and ImGui draws its own widgets rather than native
 * EditTexts, so there's no built-in paste path without this bridge).
 * Returns a pointer to an internal static buffer — copy out if you need
 * it to outlive the next call. Returns "" (never NULL) on any failure.
 */
const char* android_jni_get_clipboard_text(void);

/**
 * Writes text to the system clipboard via JNI.
 */
void android_jni_set_clipboard_text(const char* text);

#endif /* ANDROID */
#endif /* ANDROID_JNI_H */
