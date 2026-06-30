#ifdef ANDROID

#include "android_path.h"
#include <string.h>

static char g_files_dir[512] = "";

void android_set_files_dir(const char* path) {
    if (path) {
        strncpy(g_files_dir, path, sizeof(g_files_dir) - 1);
        g_files_dir[sizeof(g_files_dir) - 1] = '\0';
    }
}

const char* android_get_files_dir(void) {
    return g_files_dir;
}

#endif
