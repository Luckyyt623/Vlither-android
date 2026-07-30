
#pragma once

#ifdef ANDROID

#include <time.h>
#include <stdbool.h>

double glfwGetTime(void);
void   glfwSetTime(double t);

/* Rectangles registered by Android HUD widgets during rendering are retained
   until the next frame. The input thread uses them to keep UI-owned touches
   out of gameplay controls from the very first ACTION_DOWN event. */
void android_ui_capture_begin_frame(void);
void android_ui_capture_rect(float left, float top, float right, float bottom);
bool android_ui_capture_contains(float x, float y);

#endif
