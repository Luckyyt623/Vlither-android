/*
 * chat.c  –  In-Game Chat Overlay for Vlither Android
 *
 * Displays a scrollable chat history and a text input field.
 * The chat window is always visible when show_chat_hud is true.
 * Tapping the on-screen CHAT button (rendered in ui_overlay.c) calls
 * ui_chat_toggle() which opens/closes the input field.
 *
 * On Android the soft keyboard appears automatically when the ImGui
 * InputText field is focused (g_imgui_wants_keyboard is set by
 * imgui_setup_android.c based on io->WantTextInput).
 */

#ifdef ANDROID
#include "../android_glfw_shim.h"
#ifndef IM_COL32
#define IM_COL32(R,G,B,A) \
  (((ImU32)(A)<<24)|((ImU32)(B)<<16)|((ImU32)(G)<<8)|((ImU32)(R)<<0))
#endif
#endif

#include "chat.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../user.h"
#include "../network/ntl_client.h"

#define MAX_CHAT_MESSAGES 30

typedef struct chat_message {
  char   sender[MAX_NICKNAME_LEN + 1];
  char   text[128];
  double timestamp;
} chat_message;

static chat_message chat_history[MAX_CHAT_MESSAGES];
static int          chat_count       = 0;
static int          chat_start_index = 0;
static bool         chat_is_active   = false;
static char         input_buf[128]   = {0};
static bool         focus_input      = false;
static bool         scroll_to_bottom = false;

/* ── Public API ─────────────────────────────────────────────────────── */

void ui_chat_init(tenv* env) {
  chat_count       = 0;
  chat_start_index = 0;
  chat_is_active   = false;
  memset(input_buf, 0, sizeof(input_buf));
  ui_chat_add_message("System", "Welcome to Vlither! Tap CHAT to talk to teammates.");
}

void ui_chat_add_message(const char* sender, const char* text) {
  int idx;
  if (chat_count < MAX_CHAT_MESSAGES) {
    idx = chat_count++;
  } else {
    idx = chat_start_index;
    chat_start_index = (chat_start_index + 1) % MAX_CHAT_MESSAGES;
  }
  strncpy(chat_history[idx].sender, sender, MAX_NICKNAME_LEN);
  chat_history[idx].sender[MAX_NICKNAME_LEN] = '\0';
  strncpy(chat_history[idx].text, text, 127);
  chat_history[idx].text[127] = '\0';
  chat_history[idx].timestamp = glfwGetTime();
  scroll_to_bottom = true;
}

void ui_chat_toggle(void) {
  chat_is_active = !chat_is_active;
  if (chat_is_active) {
    focus_input      = true;
    scroll_to_bottom = true;
  }
}

bool chat_is_typing(void) {
  return chat_is_active;
}

/* ── Per-frame render ───────────────────────────────────────────────── */

void ui_chat(tenv* env) {
  tuser_data*    usr  = env->usr;
  tcontext*      ctx  = env->ctx;
  user_settings* usrs = &usr->usrs;
  game_data*     gdata = &usr->gdata;

  if (!usrs->show_chat_hud) return;

  /* Pull incoming teammate messages */
  char inc_sender[32], inc_text[128];
  while (ntl_client_poll_chat(inc_sender, inc_text))
    ui_chat_add_message(inc_sender, inc_text);

  float scr_w = ctx->size[0];
  float scr_h = ctx->size[1];

  /* ── Window flags ─────────────────────────────────────────────────── */
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse
                         | ImGuiWindowFlags_NoTitleBar
                         | ImGuiWindowFlags_NoResize
                         | ImGuiWindowFlags_NoMove;

  if (!chat_is_active)
    flags |= ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs;

  /* Anchor bottom-left, above the joystick area */
  float win_w = scr_w * 0.45f;
  float win_h = scr_h * 0.28f;
  float win_x = scr_w * 0.008f;
  float win_y = scr_h - win_h - scr_h * 0.35f; /* stay above joystick */

  igSetNextWindowPos((ImVec2){win_x, win_y}, ImGuiCond_Always, (ImVec2){0,0});
  igSetNextWindowSize((ImVec2){win_w, win_h}, ImGuiCond_Always);

  float bg_alpha = chat_is_active ? 0.60f : 0.0f;
  igPushStyleColor_Vec4(ImGuiCol_WindowBg,
      (ImVec4){0.06f, 0.06f, 0.10f, bg_alpha});
  igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 10.0f);
  igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){8, 6});

  if (igBegin("Chat##ntl_hud", &usrs->show_chat_hud, flags)) {
    ImVec2 win_sz;
    igGetWindowSize(&win_sz);

    /* Input field height */
    float input_h = chat_is_active ? 34.0f : 0.0f;
    float msg_h   = win_sz.y - input_h - 12.0f;
    if (msg_h < 10.0f) msg_h = 10.0f;

    /* ── Message history ────────────────────────────────────────────── */
    igBeginChild_Str("##chat_msgs",
        (ImVec2){win_sz.x - 12.0f, msg_h},
        false, ImGuiWindowFlags_None);

    igPushFont(usr->imgui_data.regular_font[FONT_SIZE_SMALL],
               usr->imgui_data.regular_font[FONT_SIZE_SMALL]->LegacySize);
    igPushTextWrapPos(win_sz.x - 20.0f);

    for (int i = 0; i < chat_count; i++) {
      int idx = (chat_start_index + i) % MAX_CHAT_MESSAGES;
      chat_message* msg = &chat_history[idx];

      bool is_system  = (strcmp(msg->sender, "System") == 0);
      bool is_teammate = !is_system;

      ImVec4 sender_col = is_system
          ? (ImVec4){0.90f, 0.70f, 0.20f, 0.95f}
          : (ImVec4){0.20f, 0.90f, 0.70f, 1.0f};  /* vibrant cyan for teammates */
      ImVec4 text_col   = is_system
          ? (ImVec4){0.90f, 0.85f, 0.60f, 0.85f}
          : (ImVec4){0.95f, 0.95f, 0.98f, 0.90f};

      /* ★ star prefix for teammate messages */
      if (is_teammate) {
        igTextColored(sender_col, "\xe2\x98\x85 %s:", msg->sender); /* UTF-8 ★ */
      } else {
        igTextColored(sender_col, "[%s]:", msg->sender);
      }
      igSameLine(0, 4);
      igTextColored(text_col, "%s", msg->text);
    }

    igPopTextWrapPos();
    if (scroll_to_bottom) {
      igSetScrollHereY(1.0f);
      scroll_to_bottom = false;
    }
    igPopFont();
    igEndChild();

    /* ── Text input (only when active) ─────────────────────────────── */
    if (chat_is_active) {
      igSetCursorPosY(win_sz.y - input_h + 2.0f);
      igPushItemWidth(win_sz.x - 12.0f);
      igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding, (ImVec2){6, 6});

      if (focus_input) {
        igSetKeyboardFocusHere(0);
        focus_input = false;
      }

      bool submitted = igInputTextWithHint(
          "##chat_input",
          "Type message, tap Send...",
          input_buf, sizeof(input_buf),
          ImGuiInputTextFlags_EnterReturnsTrue, NULL, NULL);

      igPopStyleVar(1);
      igPopItemWidth();

      if (submitted && input_buf[0]) {
        const char* nick = usrs->nickname[0] ? usrs->nickname : "Player";
        ui_chat_add_message(nick, input_buf);
        ntl_client_send_msg(input_buf);
        memset(input_buf, 0, sizeof(input_buf));
        chat_is_active = false;
      }
    }
  }
  igEnd();

  igPopStyleVar(2);
  igPopStyleColor(1);
}

void ui_chat_destroy(tenv* env) {
  (void)env;
}
