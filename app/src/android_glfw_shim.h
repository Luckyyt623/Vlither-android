
#pragma once

#ifdef ANDROID

#include <time.h>
#include <stdbool.h>

double glfwGetTime(void);
void   glfwSetTime(double t);

void android_ui_capture_begin_frame(void);
void android_ui_capture_rect(float left, float top, float right, float bottom);
bool android_ui_capture_contains(float x, float y);

#endif
