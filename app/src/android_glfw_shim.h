/*
 * android_glfw_shim.h
 *
 * Provides stubs for the few GLFW calls that remain in app-level source
 * files (loop.c, title_screen.c, game_data.c, oef.c) after the framework
 * layer is replaced.  Include this only on Android.
 */
#pragma once

#ifdef ANDROID

#include <time.h>

/* glfwGetTime / glfwSetTime are implemented in tentry_android.c */
double glfwGetTime(void);
void   glfwSetTime(double t);

#endif /* ANDROID */
