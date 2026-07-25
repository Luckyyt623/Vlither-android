#include "game/loop.h"
#include "game/bg_preview.h"
#include "ui/skin_editor.h"
#include "ui/title_screen.h"
#include "ui/settings.h"
#include "ui/controls.h"
#include "ui/key_buttons.h"
#include "ui/viewport.h"
#include "user.h"
#ifdef ANDROID
#include "android_jni.h"
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
  DLOG("tinit: game_data_init done");
  DLOG("tinit: complete");
}

void tdestroy(tenv* env) {
  ui_key_buttons_destroy(env);
  game_data_destroy(env);
  ui_controls_destroy(env);
  ui_settings_destroy(env);
  ui_skin_editor_destroy(env);
  ui_title_screen_destroy(env);
  ui_viewport_destroy(env);
  renderer_destroy(env->usr->r, env->ctx);
  imgui_destroy();
}

/* Draws the already-rendered game-world texture a few extra times, softly
   offset and scaled with low alpha, on top of the sharp copy ui_viewport()
   just drew — a cheap "poor man's blur" that needs no extra render passes
   or shaders. Then dims/tints it so foreground panels stay readable.
   `strength` of 1.0 = full blur+dim (Settings), lower values keep more of
   the scene crisp (Controls, where the player needs to actually see where
   things line up). */
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
  /* Route touches as normal UI interaction (and drag-to-scroll) instead
     of boost/joystick hit-testing while on Settings/Controls — see
     twindow_android.c and imgui_setup_android.c. Without this, a real
     tap on an adjustment slider that happens to fall inside the
     (currently invisible, since nothing is being previewed yet at this
     exact point) boost circle's hit region gets consumed as a boost
     press before ImGui ever sees it, and the slider never receives the
     click. */
  { extern bool g_panel_open;
    g_panel_open = (gdata->curr_screen == SETTINGS || gdata->curr_screen == CONTROLS); }
#endif

  if (usr->r) {

    renderer_render(usr->r, ctx, (vec4){0.086f, 0.109f, 0.133f, 1});

    renderer_clear_instances(usr->r);

    tcontext_clear(ctx, (vec4){0, 0, 0, 1.0f});

    imgui_prerender();
    ImGuiStyle* style = igGetStyle();
    ui_viewport(env);

    if (bg_preview_visible(env) && usr->gdata.curr_screen == SETTINGS) {
      /* Controls intentionally skips the multi-tap blur. The translucent
         Controls card provides readability by itself, while leaving the
         live snake/buttons sharp and avoiding an extra full-screen GPU
         composition pass every frame. */
      draw_bg_preview_blur(env, 1.0f);
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

    /* Must run inside an active ImGui frame *and* with a current window
       bound (same as game_loop()'s call to redraw() during real
       PLAYING, just below) — redraw()/ui_overlay() call ImGui functions
       like igGetWindowDrawList() that assert/crash if there's no
       current window. This used to run before imgui_prerender() and
       before this igBegin(), which worked fine until the game actually
       needed to draw something through that path (e.g. another
       player's name label) — rare on a quiet test server, routine on a
       populated official one. */
    bg_preview_update(env);

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
      case CONTROLS:
        /* Use the same owned background-preview path as Settings. Calling
           game_loop() while bg_preview_update() owns the preview connection
           polls and advances the same connection twice per frame, which can
           prevent the preview snake from completing its join. Only keep the
           normal game loop alive here when Controls was opened over a real
           player session that the preview system does not own. */
        if (!bg_preview_visible(env) &&
            (gdata->conn == CONNECTED || gdata->conn == CONNECTING)) {
          game_loop(env);
        }
        ui_controls(env);
        break;
    }
    igEnd();

    renderer_render_cursor(usr->r, ctx);

    if (usr->gdata.curr_screen == PLAYING ||
        usr->gdata.curr_screen == TITLE_SCREEN)
      ui_key_buttons(env);

    igRender();
    imgui_render(ctx->frames[ctx->current_frame].cmd);
  }

  tcontext_end(ctx);
}

void tresize(tenv* env) { ui_viewport_resize(env); }

#ifndef ANDROID
TDEF_ENTRY();
#endif
