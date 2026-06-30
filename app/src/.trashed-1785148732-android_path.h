#ifndef ANDROID_PATH_H
#define ANDROID_PATH_H

/*
 * android_path.h
 *
 * Stores the app's internal writable directory path so any C file can
 * build safe absolute paths for reading/writing files on Android.
 *
 * On Android, fopen("user.dat", ...) fails because the current working
 * directory is not writable. The correct writable path comes from
 * app->activity->internalDataPath (set at startup in android_main).
 *
 * Usage:
 *   // In android_main (tentry_android.c):
 *   android_set_files_dir(app->activity->internalDataPath);
 *
 *   // In any .c file that needs to open a file:
 *   #include "android_path.h"
 *   char path[512];
 *   android_build_path(path, sizeof(path), "user.dat");
 *   FILE* f = fopen(path, "rb");
 */

#ifdef ANDROID

#include <string.h>
#include <stdio.h>

/* Call once at startup with app->activity->internalDataPath */
void android_set_files_dir(const char* path);

/* Returns the stored files dir (or "" if not set yet) */
const char* android_get_files_dir(void);

/* Fills 'out' with "<files_dir>/<filename>", safely null-terminated */
static inline void android_build_path(char* out, int out_size, const char* filename) {
    snprintf(out, (size_t)out_size, "%s/%s", android_get_files_dir(), filename);
}

#endif /* ANDROID */

#endif /* ANDROID_PATH_H */
