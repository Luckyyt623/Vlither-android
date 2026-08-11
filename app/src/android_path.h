#ifndef ANDROID_PATH_H
#define ANDROID_PATH_H

#ifdef ANDROID

#include <string.h>
#include <stdio.h>

void android_set_files_dir(const char* path);

const char* android_get_files_dir(void);

static inline void android_build_path(char* out, int out_size, const char* filename) {
    snprintf(out, (size_t)out_size, "%s/%s", android_get_files_dir(), filename);
}

#endif

#endif
