#ifdef ANDROID
#include "../android_glfw_shim.h"
#endif
#include "chat.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../user.h"
#include "../network/ntl_client.h"

#define MAX_CHAT_MESSAGES 30

typedef struct chat_message {
  char sender[MAX_NICKNAME_LEN + 1];
  char text[128];
  double timestamp;
} chat_message;

static chat_message chat_history[MAX_CHAT_MESSAGES];
static int chat_count = 0;
static int chat_start_index = 0;
static bool chat_is_active = false;
static char input_buf[128] = {0};
static bool focus_input = false;

void ui_chat_init(tenv* env) {
  chat_count = 0;
  chat_start_index = 0;
  chat_is_active = false;
  memset(input_buf, 0, sizeof(input_buf));

  // Add an initial welcome system message
#ifdef ANDROID
  ui_chat_add_message("System", "Welcome! Tap the chat box to talk to your team.");
#else
  ui_chat_add_message("System", "Welcome! Press [Enter] to chat with your team.");
#endif
}

void ui_chat_add_message(const char* sender, const char* text) {
  int target_idx = 0;
  if (chat_count < MAX_CHAT_MESSAGES) {
    target_idx = chat_count;
    chat_count++;
  } else {
    target_idx = chat_start_index;
    chat_start_index = (chat_start_index + 1) % MAX_CHAT_MESSAGES;
  }

  strncpy(chat_history[target_idx].sender, sender, MAX_NICKNAME_LEN);
  chat_history[target_idx].sender[MAX_NICKNAME_LEN] = '\0';

  strncpy(chat_history[target_idx].text, text, 127);
  chat_history[target_idx].text[127] = '\0';

  chat_history[target_idx].timestamp = glfwGetTime();
}

void ui_chat(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  user_settings* usrs = &usr->usrs;
  game_data* gdata = &usr->gdata;

  if (!usrs->show_chat_hud) return;

  static float last_scr_w = 0;
  static float last_scr_h = 0;
  static float ratio_x = -1;
  static float ratio_y = -1;

  float scr_w = ctx->size[0];
  float scr_h = ctx->size[1];

  if (last_scr_w > 0 && last_scr_h > 0 && (scr_w != last_scr_w || scr_h != last_scr_h)) {
    if (ratio_x >= 0 && ratio_y >= 0) {
      ImVec2 new_pos = { ratio_x * scr_w, ratio_y * scr_h };
      igSetWindowPos_Str("Chat##ntl_hud", new_pos, ImGuiCond_Always);
    }
  }

  // Poll teammate NTL chat messages and insert them into chat history
  char incoming_sender[32];
  char incoming_text[128];
  while (ntl_client_poll_chat(incoming_sender, incoming_text)) {
    ui_chat_add_message(incoming_sender, incoming_text);
  }

  // Toggle chat active on Enter press (desktop) — Android has no physical
  // Enter key, so it instead taps directly into the input box below.
  if (tkeyboard_key_pressed(env->kb, GLFW_KEY_ENTER)) {
    if (chat_is_active) {
      if (strlen(input_buf) > 0) {
        char name[MAX_NICKNAME_LEN + 1] = {0};
        if (strlen(usrs->nickname) > 0) {
          strcpy(name, usrs->nickname);
        } else {
          strcpy(name, "Player");
        }
        ui_chat_add_message(name, input_buf);
        ntl_client_send_msg(input_buf);
        memset(input_buf, 0, sizeof(input_buf));
      }
      chat_is_active = false;
    } else {
      chat_is_active = true;
      focus_input = true;
    }
  }

  // Vlither already has a separate full-screen SETTINGS state; there's no
  // "in-game settings overlay while still playing" concept here, so chat
  // simply stays interactive on every other screen.
  bool settings_open = gdata->curr_screen == SETTINGS;
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
  if (!settings_open) {
    flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize;
    if (!chat_is_active) {
#ifdef ANDROID
      /* Keep inputs enabled while inactive so a tap on the hint box can
         open chat — there is no Enter key to do it on Android. */
      flags |= ImGuiWindowFlags_NoBackground;
#else
      flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground;
#endif
    }
  }

  // Set default window pos/size on first use
  igSetNextWindowPos((ImVec2){20.0f, ctx->size[1] - 220.0f}, ImGuiCond_FirstUseEver, (ImVec2){0, 0});
  igSetNextWindowSize((ImVec2){360.0f, 180.0f}, ImGuiCond_FirstUseEver);
  igSetNextWindowSizeConstraints((ImVec2){200.0f, 100.0f}, (ImVec2){800.0f, 600.0f}, NULL, NULL);

  igPushStyleColor_Vec4(ImGuiCol_WindowBg, (ImVec4){0.08f, 0.08f, 0.10f, chat_is_active ? 0.65f : 0.0f});
  igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 8.0f);
  igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){8, 8});

  if (igBegin("Chat##ntl_hud", &usrs->show_chat_hud, flags)) {
    ImVec2 win_sz;
    igGetWindowSize(&win_sz);

    // Render messages
    float message_box_h = win_sz.y - 36.0f - 16.0f;
    igBeginChild_Str("##messages_list_box", (ImVec2){win_sz.x - 16.0f, message_box_h}, false, ImGuiWindowFlags_None);

    igPushFont(usr->imgui_data.regular_font[FONT_SIZE_SMALL],
               usr->imgui_data.regular_font[FONT_SIZE_SMALL]->LegacySize);

    igPushTextWrapPos(win_sz.x - 24.0f);
    for (int i = 0; i < chat_count; i++) {
      int idx = (chat_start_index + i) % MAX_CHAT_MESSAGES;
      chat_message* msg = &chat_history[idx];

      // System message vs user message colors
      ImVec4 sender_col = (strcmp(msg->sender, "System") == 0)
                          ? (ImVec4){0.90f, 0.70f, 0.20f, 1.0f}
                          : (ImVec4){0.20f, 0.70f, 0.90f, 1.0f};
      ImVec4 text_col = (strcmp(msg->sender, "System") == 0)
                        ? (ImVec4){0.90f, 0.85f, 0.60f, 1.0f}
                        : (ImVec4){0.90f, 0.90f, 0.92f, 1.0f};

      igTextColored(sender_col, "[%s]: ", msg->sender);
      igSameLine(0, 0);
      igTextColored(text_col, "%s", msg->text);
    }
    igPopTextWrapPos();

    // Auto-scroll to bottom
    if (igGetScrollY() < igGetScrollMaxY()) {
      igSetScrollHereY(1.0f);
    }

    igPopFont();
    igEndChild(); // end messages_list_box

    // Render input field or helper
    igSetCursorPosY(win_sz.y - 32.0f);
    if (chat_is_active) {
      if (focus_input) {
        igSetKeyboardFocusHere(0);
        focus_input = false;
      }

#ifdef ANDROID
      igPushItemWidth(win_sz.x - 16.0f - 50.0f);
#else
      igPushItemWidth(win_sz.x - 16.0f);
#endif
      bool submitted = igInputTextWithHint("##chat_box_input", "Press Enter to send...",
                                           input_buf, sizeof(input_buf),
                                           ImGuiInputTextFlags_EnterReturnsTrue, NULL, NULL);
      igPopItemWidth();

#ifdef ANDROID
      igSameLine(0, 4);
      /* Guaranteed fallback: some Android keyboards don't map their
         enter/done key the way ImGui's EnterReturnsTrue expects, so don't
         rely on that alone for sending. */
      if (igButton("Send##send_chat", (ImVec2){-1, 0})) {
        submitted = true;
      }
#endif

      if (submitted) {
        if (strlen(input_buf) > 0) {
          char name[MAX_NICKNAME_LEN + 1] = {0};
          if (usrs->nickname[0] != 0) {
            strcpy(name, usrs->nickname);
          } else {
            strcpy(name, "Player");
          }
          ui_chat_add_message(name, input_buf);
          ntl_client_send_msg(input_buf);
          memset(input_buf, 0, sizeof(input_buf));
        }
        chat_is_active = false;
      }
    } else {
      // Inactive: render a read-only/disabled looking box as a hint.
      // On Android, tapping this box is the only way to open chat, so it
      // stays hit-testable (NoInputs is skipped above for that reason).
      igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding, (ImVec2){4, 4});
      igPushStyleColor_Vec4(ImGuiCol_FrameBg, (ImVec4){0.12f, 0.12f, 0.14f, 0.40f});
      igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.60f, 0.60f, 0.60f, 0.50f});

      char dummy_buf[1] = "";
#ifdef ANDROID
      igInputTextWithHint("##chat_box_input_inactive", "Tap to chat...",
                          dummy_buf, sizeof(dummy_buf),
                          ImGuiInputTextFlags_ReadOnly, NULL, NULL);
      if (igIsItemClicked(0)) {
        chat_is_active = true;
        focus_input = true;
      }
#else
      igInputTextWithHint("##chat_box_input_inactive", "Press [Enter] to chat...",
                          dummy_buf, sizeof(dummy_buf),
                          ImGuiInputTextFlags_ReadOnly, NULL, NULL);
#endif

      igPopStyleColor(2);
      igPopStyleVar(1);
    }
    ImVec2 pos;
    igGetWindowPos(&pos);
    ratio_x = pos.x / scr_w;
    ratio_y = pos.y / scr_h;
  }
  igEnd();

  igPopStyleVar(2);
  igPopStyleColor(1);

  last_scr_w = scr_w;
  last_scr_h = scr_h;
}

void ui_chat_destroy(tenv* env) {}

bool chat_is_typing(void) {
  return chat_is_active;
}
