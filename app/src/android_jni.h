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
 * Opens the "RealTime Leaderboards" popup — same destination the JS
 * slither.io mod opens (Li()/the NTL RealTime Status iframe) — as an
 * embedded WebView overlay on top of the native game surface.
 * Uses g_android_app (set by thermite's android_main) for JNI.
 */
void android_jni_open_realtime_leaderboard(void);

#endif /* ANDROID */
#endif /* ANDROID_JNI_H */
