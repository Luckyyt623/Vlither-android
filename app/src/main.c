#include "game/loop.h"
#include "game/bg_preview.h"
#include "game/ntl_team.h"
#include "game/ntl_tags.h"
#include "game/vlither_tags.h"
#include "game/vlither_ratings.h"
#include "game/snakey_rain.h"
#include "ui/skin_editor.h"
#include "ui/title_screen.h"
#include "ui/settings.h"
#include "ui/controls.h"
#include "ui/key_buttons.h"
#include "ui/viewport.h"
#include "ui/ratings.h"
#include "ui/leaderboard.h"
#include "user.h"
#ifdef ANDROID
#include "android_jni.h"
#include "android_glfw_shim.h"
#endif
#include <math.h>

#ifndef IM_COL32
#define IM_COL32(R,G,B,A) \
  (((ImU32)(A)<<24)|((ImU32)(B)<<16)|((ImU32)(G)<<8)|((ImU32)(R)<<0))
#endif

#ifdef ANDROID
#include <android/log.h>
#define DLOG(fmt,...) do{char _b[256];snprintf(_b,sizeof(_b),fmt,##__VA_ARGS__);\
    __android_log_print(ANDROID_LOG_ERROR,"vlither","%s",_b);}while(0)
#else
#define DLOG(fmt,...) do{}while(0)
#endif

void tinput(tenv* env) {
  tuser_data* usr = env->usr;
  user_settings* usrs = &usr->usrs;

  if (twindow_closed(env->wnd)) {
    env->config.running = false;
    save_user_settings(usrs);
  }

#ifndef ANDROID
  if (tkeyboard_key_pressed(env->kb, GLFW_KEY_F11)) {
    twindow_toggle_fullscreen(env->wnd);
  }
#endif
}

void tlaunch(tenv* env) {
  tuser_data* usr = env->usr;
  user_settings* usrs = &usr->usrs;
  srand(time(NULL));

  memset(usrs, 0, sizeof(user_settings));
  strcpy(usrs->ipv4, "148.113.20.151:444");
  strcpy(usrs->server_address, "148.113.20.151:444");
  strcpy(usrs->nickname, "");

  usrs->custom_skin = false;
  usrs->default_skin = rand() % 9;
  usrs->accessory = NO_ACCESSORY;

  read_user_settings(usrs);

  env->config.vsync = usrs->vsync;
  env->config.fullscreen = false;
  env->config.title = "Vlither";
}

void tinit(tenv* env) {
  tuser_data* usr = env->usr;

  DLOG("tinit: imgui_init starting");
  imgui_init(env);
  DLOG("tinit: imgui_init done");

  DLOG("tinit: renderer_create starting");
  env->usr->r = renderer_create(env);
  DLOG("tinit: renderer_create done r=%p", (void*)env->usr->r);

  if (!env->usr->r) {
    DLOG("tinit: renderer is NULL - bailing");
    env->config.running = false;
    return;
  }

  DLOG("tinit: ui_viewport_init");
  ui_viewport_init(env);
  DLOG("tinit: ui_title_screen_init");
  ui_title_screen_init(env);
  DLOG("tinit: ui_skin_editor_init");
  ui_skin_editor_init(env);
  DLOG("tinit: ui_settings_init");
  ui_settings_init(env);
  DLOG("tinit: ui_controls_init");
  ui_controls_init(env);
  DLOG("tinit: game_data_init");
  game_data_init(env);
  ui_key_buttons_init(env);
  ntl_team_init(env);
  ntl_tags_init(env);
  vlither_tags_init(env);
  vlither_ratings_init(env);
  snakey_rain_init(env);
  DLOG("tinit: game_data_init done");
  DLOG("tinit: complete");
}

void tdestroy(tenv* env) {
  ui_key_buttons_destroy(env);
  snakey_rain_destroy(env);
  vlither_ratings_destroy(env);
  vlither_tags_destroy(env);
  ntl_tags_destroy(env);
  ntl_team_destroy(env);
  game_data_destroy(env);
  ui_controls_destroy(env);
  ui_settings_destroy(env);
  ui_skin_editor_destroy(env);
  ui_title_screen_destroy(env);
  ui_viewport_destroy(env);
  renderer_destroy(env->usr->r, env->ctx);
  imgui_destroy();
}

static void draw_bg_preview_blur(tenv* env, float strength) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;

  ImDrawList* dl = igGetBackgroundDrawList(igGetMainViewport());
  ImTextureRef tex = (ImTextureRef){
      NULL, (ImTextureID)usr->viewport_widget.scene[ctx->current_frame]};
  ImVec2 p0 = {0, 0};
  ImVec2 p1 = {(float)ctx->size[0], (float)ctx->size[1]};

  static const struct { float ox, oy, scale, alpha; } TAPS[] = {
      {0.0025f, 0.0000f, 1.010f, 0.16f}, {-0.0025f, 0.0015f, 1.016f, 0.14f},
      {0.0015f, -0.0025f, 1.022f, 0.12f}, {-0.0015f, -0.0020f, 1.028f, 0.10f},
      {0.0030f, 0.0030f, 1.034f, 0.09f}, {-0.0030f, -0.0010f, 1.040f, 0.08f},
  };
  int num_taps = (int)(strength * (sizeof(TAPS) / sizeof(TAPS[0])) + 0.999f);
  if (num_taps > (int)(sizeof(TAPS) / sizeof(TAPS[0])))
    num_taps = (int)(sizeof(TAPS) / sizeof(TAPS[0]));

  for (int i = 0; i < num_taps; i++) {
    float sc = TAPS[i].scale;
    float uv_hw = 0.5f / sc;
    ImVec2 uv0 = {0.5f - uv_hw + TAPS[i].ox, 0.5f - uv_hw + TAPS[i].oy};
    ImVec2 uv1 = {0.5f + uv_hw + TAPS[i].ox, 0.5f + uv_hw + TAPS[i].oy};
    ImU32 col = igGetColorU32_Vec4((ImVec4){1, 1, 1, TAPS[i].alpha * strength});
    ImDrawList_AddImage(dl, tex, p0, p1, uv0, uv1, col);
  }

  ImU32 tint = igGetColorU32_Vec4((ImVec4){0.03f, 0.04f, 0.05f, 0.55f * strength});
  ImDrawList_AddRectFilled(dl, p0, p1, tint, 0, 0);
}

void trender(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  game_data* gdata = &usr->gdata;

  if (!tcontext_begin(ctx)) return;

#ifdef ANDROID
  { extern bool g_panel_open;
    g_panel_open = (gdata->curr_screen == TITLE_SCREEN ||
                    gdata->curr_screen == SETTINGS ||
                    gdata->curr_screen == CONTROLS ||
                    gdata->curr_screen == SKIN_EDITOR ||
                    gdata->curr_screen == NTL_PANEL ||
                    gdata->curr_screen == EVENTS_PANEL ||
                    gdata->curr_screen == RATINGS_PANEL ||
                    gdata->curr_screen == LEADERBOARD_PANEL ||
                    gdata->curr_screen == KEYBOARD_EDITOR ||
                    igGetIO_Nil()->WantTextInput);
    if (g_panel_open) {
      touch_state* t = &env->wnd->touch;
      t->down = t->just_down = false;
      t->boost_down = t->boost_just_down = false;
      t->move_ptr_id = t->boost_ptr_id = t->zslider_ptr_id = -1;
      t->zslider_offset = 0.0f;
    }
  }
#endif

  if (usr->r) {

    renderer_render(usr->r, ctx, (vec4){0.086f, 0.109f, 0.133f, 1});

    renderer_clear_instances(usr->r);

    tcontext_clear(ctx, (vec4){0, 0, 0, 1.0f});

    imgui_prerender();
#ifdef ANDROID
    android_ui_capture_begin_frame();
#endif
    ImGuiStyle* style = igGetStyle();
    ui_viewport(env);

    if (bg_preview_visible(env) && usr->gdata.curr_screen == SETTINGS) {
      draw_bg_preview_blur(env, usr->usrs.performance_mode ? 0.55f : 1.0f);
    }

    igSetNextWindowPos(igGetMainViewport()->Pos, ImGuiCond_None, (ImVec2){});
    igSetNextWindowSize(igGetMainViewport()->Size, ImGuiCond_None);
    igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 0);
    igBegin("##fullscreen_holder", NULL,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    igPopStyleVar(1);

    bg_preview_update(env);
    ntl_team_update(env);
    ntl_tags_update(env);
    vlither_tags_update(env);
    vlither_ratings_update(env);
    snakey_rain_update(env);

    if (usr->gdata.curr_screen == PLAYING)
      ui_key_buttons(env);
    if (usr->gdata.curr_screen == PLAYING)
      ntl_team_consume_ui_touch(env);

    leaderboard_update(env);

    switch (usr->gdata.curr_screen) {
      case TITLE_SCREEN:
        ui_title_screen(env);
        break;
      case SKIN_EDITOR:
        ui_skin_editor(env);
        break;
      case PLAYING:
        game_loop(env);
        break;
      case SETTINGS:
        ui_settings(env);
        break;
      case NTL_PANEL:
        ntl_team_panel(env);
        break;
      case EVENTS_PANEL:
        ui_events_panel(env);
        break;
      case RATINGS_PANEL:
        ui_ratings_panel(env);
        break;
      case LEADERBOARD_PANEL:
        ui_leaderboard_panel(env);
        break;
      case KEYBOARD_EDITOR:
        ui_key_buttons(env);
        break;
      case CONTROLS:
        if (!bg_preview_visible(env) &&
            (gdata->conn == CONNECTED || gdata->conn == CONNECTING)) {
          game_loop(env);
        }
        ui_controls(env);
        break;
    }

    if (gdata->curr_screen == PLAYING)
      snakey_rain_draw(env);

#ifdef ANDROID
    android_jni_capture_sync(
        usr->usrs.record_gameplay, usr->usrs.screenshot_on_kill,
        gdata->curr_screen == PLAYING && gdata->conn == CONNECTED &&
            !gdata->preview_active);
#endif

    if (gdata->curr_screen == PLAYING || gdata->curr_screen == SETTINGS)
      ntl_team_draw(env);
    igEnd();

    renderer_render_cursor(usr->r, ctx);

    igRender();
    imgui_render(ctx->frames[ctx->current_frame].cmd);
  }

  tcontext_end(ctx);
}

void tresize(tenv* env) { ui_viewport_resize(env); }

#ifndef ANDROID
TDEF_ENTRY();
#endif
