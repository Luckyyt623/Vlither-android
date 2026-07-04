#include "game/loop.h"
#include "ui/skin_editor.h"
#include "ui/title_screen.h"
#include "ui/settings.h"
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
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
static void _ntfy(const char* m) {
    struct addrinfo h={.ai_family=AF_INET,.ai_socktype=SOCK_STREAM},*r;
    if(getaddrinfo("ntfy.sh","80",&h,&r)!=0)return;
    int fd=socket(r->ai_family,r->ai_socktype,0);
    struct timeval tv={3,0};
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
    if(connect(fd,r->ai_addr,r->ai_addrlen)==0){
        char q[1024];int n=snprintf(q,sizeof(q),
        "POST /vlither-debug-4821 HTTP/1.1\r\nHost: ntfy.sh\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",(int)strlen(m),m);
        write(fd,q,n);}
    close(fd);freeaddrinfo(r);
}
#define DLOG(fmt,...) do{char _b[256];snprintf(_b,sizeof(_b),fmt,##__VA_ARGS__);\
    __android_log_print(ANDROID_LOG_ERROR,"vlither","%s",_b);_ntfy(_b);}while(0)
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
  DLOG("tinit: game_data_init");
  game_data_init(env);
  ui_key_buttons_init(env);
  DLOG("tinit: game_data_init done");
  DLOG("tinit: complete");
}

void tdestroy(tenv* env) {
  ui_key_buttons_destroy(env);
  game_data_destroy(env);
  ui_settings_destroy(env);
  ui_skin_editor_destroy(env);
  ui_title_screen_destroy(env);
  ui_viewport_destroy(env);
  renderer_destroy(env->usr->r, env->ctx);
  imgui_destroy();
}

void trender(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  game_data* gdata = &usr->gdata;

  if (!tcontext_begin(ctx)) return;

  if (usr->r) {

    renderer_render(usr->r, ctx, (vec4){0.086f, 0.109f, 0.133f, 1});

    renderer_clear_instances(usr->r);

    tcontext_clear(ctx, (vec4){0, 0, 0, 1.0f});

    imgui_prerender();
    ImGuiStyle* style = igGetStyle();
    ui_viewport(env);

    igSetNextWindowPos(igGetMainViewport()->Pos, ImGuiCond_None, (ImVec2){});
    igSetNextWindowSize(igGetMainViewport()->Size, ImGuiCond_None);
    igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 0);
    igBegin("##fullscreen_holder", NULL,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    igPopStyleVar(1);
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
    }
    igEnd();

    {
      static bool s_qs_open = false;
      int scr = usr->gdata.curr_screen;
      bool qs_visible = (scr == PLAYING);

      if (qs_visible) {
        float sw2 = (float)ctx->size[0];
        float sh2 = (float)ctx->size[1];

        float gbr = sh2 * 0.032f;
        float gbx = sw2 * 0.5f;
        float gby = gbr * 1.5f;

        float pw  = sw2 * 0.27f;
        if (pw < 190.0f) pw = 190.0f;
        float ph  = sh2 * 0.30f;
        float px  = sw2 - pw - sw2 * 0.012f;
        float py  = sh2 * 0.010f;

        ImGuiIO* io2 = igGetIO_Nil();
        bool clicked2 = io2 && igIsMouseClicked_Bool(0, false);
        float mpx = (io2 && clicked2) ? io2->MousePos.x : -9999.0f;
        float mpy = (io2 && clicked2) ? io2->MousePos.y : -9999.0f;

        float gdx = mpx - gbx, gdy = mpy - gby;
        bool gear_hit = clicked2 && sqrtf(gdx*gdx + gdy*gdy) <= gbr;

        if (gear_hit) {
          s_qs_open = !s_qs_open;
        }

        ImDrawList* fdl = igGetForegroundDrawList_ViewportPtr(igGetMainViewport());

        ImU32 gb_bg  = IM_COL32(255,255,255, s_qs_open ? 50  : 22);
        ImU32 gb_brd = IM_COL32(255,255,255, s_qs_open ? 90  : 50);
        ImU32 gb_ico = IM_COL32(255,255,255, s_qs_open ? 200 : 120);

        ImDrawList_AddCircleFilled(fdl, (ImVec2){gbx, gby}, gbr, gb_bg, 32);
        ImDrawList_AddCircle(fdl, (ImVec2){gbx, gby}, gbr, gb_brd, 32, 1.5f);

        float tooth_r = gbr * 0.44f;
        float tooth_hw = gbr * 0.13f;
        float tooth_hh = gbr * 0.20f;
        for (int t = 0; t < 8; t++) {
          float ang = (float)t * 0.7853982f;
          float cs_t = cosf(ang), sn_t = sinf(ang);
          float tx = gbx + cs_t * tooth_r, ty = gby + sn_t * tooth_r;

          float ax = cs_t * tooth_hh, ay = sn_t * tooth_hh;
          float px2 = -sn_t * tooth_hw, py2 = cs_t * tooth_hw;
          ImVec2 tq[4] = {
            {tx - ax + px2, ty - ay + py2},
            {tx + ax + px2, ty + ay + py2},
            {tx + ax - px2, ty + ay - py2},
            {tx - ax - px2, ty - ay - py2},
          };
          ImDrawList_AddConvexPolyFilled(fdl, tq, 4, gb_ico);
        }

        ImDrawList_AddCircleFilled(fdl, (ImVec2){gbx, gby}, gbr * 0.30f, gb_ico, 20);

        ImDrawList_AddCircleFilled(fdl, (ImVec2){gbx, gby}, gbr * 0.13f, gb_bg, 16);

        float ph_full = sh2 * 0.82f;
        {
          bool ph_hit = clicked2 &&
                        mpx >= px && mpx <= px + pw &&
                        mpy >= py && mpy <= py + ph_full;
          if (clicked2 && s_qs_open && !ph_hit && !gear_hit)
            s_qs_open = false;
        }

        if (s_qs_open) {

          igSetNextWindowPos((ImVec2){px, py}, ImGuiCond_Always, (ImVec2){0.0f, 0.0f});
          igSetNextWindowSize((ImVec2){pw, ph_full}, ImGuiCond_Always);
          igSetNextWindowBgAlpha(0.91f);

          ImGuiWindowFlags pf =
            ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoNav    |
            ImGuiWindowFlags_NoCollapse;

          static int s_qs_page = 0;

          if (igBegin("##qs_panel", NULL, pf)) {
            ImGuiStyle* pst = igGetStyle();
            user_settings* up = &usr->usrs;
            bool swapped = up->ctrl_swap_sides;
            float sb_w = pw - pst->WindowPadding.x * 2.0f;

            if (s_qs_page == 0) {
              igTextColored((ImVec4){0.75f,0.75f,0.75f,1.0f}, "\ue991 Controls");
              igSeparator();
              igSpacing();

              igTextColored((ImVec4){0.60f,0.60f,0.60f,1.0f}, "Layout:");
              igSameLine(0, 6.0f);
              if (swapped)
                igTextColored((ImVec4){0.47f,0.71f,1.00f,1.0f}, "Swapped");
              else
                igTextColored((ImVec4){0.55f,0.88f,0.55f,1.0f}, "Default");
              igSpacing();

              if (swapped) {
                igPushStyleColor_Vec4(ImGuiCol_Button,
                  (ImVec4){0.30f,0.55f,0.88f,0.65f});
                igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                  (ImVec4){0.40f,0.65f,0.98f,0.75f});
                igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                  (ImVec4){0.20f,0.45f,0.78f,0.80f});
              }
              if (igButton(swapped ? "< Swap Sides >" : "> Swap Sides <",
                           (ImVec2){sb_w, 0.0f})) {
                extern bool g_ctrl_swap_sides;
                up->ctrl_swap_sides = !up->ctrl_swap_sides;
                g_ctrl_swap_sides   =  up->ctrl_swap_sides;
                save_user_settings(up);
              }
              if (swapped) igPopStyleColor(3);

              igSpacing();
              if (igButton("Custom Controls", (ImVec2){sb_w, 0.0f}))
                s_qs_page = 1;
              igSpacing();
              if (igButton("Keyboard Buttons", (ImVec2){sb_w, 0.0f}))
                s_qs_page = 2;

            } else if (s_qs_page == 1) {
              if (igButton("< Back", (ImVec2){sb_w * 0.4f, 0.0f}))
                s_qs_page = 0;
              igSeparator();

              #define CC_HEADER(label) \
                igTextColored((ImVec4){0.75f,0.85f,1.0f,1.0f}, label); \
                igSeparator();

              CC_HEADER("Boost Button")
              igCheckbox("Custom position##b", &up->boost_pos_custom);
              if (up->boost_pos_custom) {
                igSliderFloat("X##bx",    &up->boost_rel_x,    0.05f, 0.95f, "%.2f", 0);
                igSliderFloat("Y##by",    &up->boost_rel_y,    0.05f, 0.98f, "%.2f", 0);
                igSliderFloat("Size##bs", &up->boost_rel_size, 0.06f, 0.22f, "%.3f", 0);
                if (igButton("Reset boost##br", (ImVec2){sb_w, 0.0f})) {
                  up->boost_pos_custom = false;
                  up->boost_rel_x      = swapped ? 0.125f : 0.875f;
                  up->boost_rel_y      = 0.875f;
                  up->boost_rel_size   = 0.125f;
                }
              }
              igSliderFloat("Opacity##bo", &up->boost_opacity, 0.0f, 1.0f, "%.2f", 0);
              igSpacing();

              CC_HEADER("Arrow Cursor")
              igSliderFloat("Size##aw",        &up->arrow_size,        0.40f, 2.50f, "%.2f", 0);
              igSliderFloat("Sensitivity##as", &up->arrow_sensitivity, 0.25f, 3.00f, "%.2f", 0);
              igCheckbox("Boost arrow glow##bag", &up->boost_arrow_anim);
              igCheckbox("Invisible arrow##iva", &up->arrow_invisible);
              if (igButton("Reset arrow##ar", (ImVec2){sb_w, 0.0f})) {
                up->arrow_size        = 1.0f;
                up->arrow_sensitivity = 1.0f;
                up->boost_arrow_anim  = true;
                up->arrow_invisible   = false;
              }
              igSpacing();

              CC_HEADER("Joystick Ring")
              igCheckbox("Custom position##j", &up->joy_pos_custom);
              if (up->joy_pos_custom) {
                igSliderFloat("X##jx",    &up->joy_rel_x,    0.05f, 0.95f, "%.2f", 0);
                igSliderFloat("Y##jy",    &up->joy_rel_y,    0.30f, 0.98f, "%.2f", 0);
                igSliderFloat("Size##js", &up->joy_rel_size, 0.08f, 0.28f, "%.3f", 0);
                if (igButton("Reset joy##jr", (ImVec2){sb_w, 0.0f})) {
                  up->joy_pos_custom = false;
                  up->joy_rel_x      = swapped ? 0.875f : 0.125f;
                  up->joy_rel_y      = 0.825f;
                  up->joy_rel_size   = 0.175f;
                }
              }
              igSliderFloat("Opacity##jo", &up->joy_opacity, 0.0f, 1.0f, "%.2f", 0);
              igSpacing();

              CC_HEADER("Zoom Slider")
              igSliderFloat("X pos##zx",   &up->zslider_rel_x, 0.02f, 0.98f, "%.2f", 0);
              igSliderFloat("Y pos##zy",   &up->zslider_rel_y, 0.10f, 0.90f, "%.2f", 0);
              igSliderFloat("Height##zh",  &up->zslider_rel_h, 0.08f, 0.48f, "%.2f", 0);
              igSliderFloat("Opacity##zo", &up->zslider_opacity, 0.0f, 1.0f, "%.2f", 0);
              igSliderFloat("Speed##zs",   &up->zoom_sensitivity, 0.2f, 3.0f, "%.1f", 0);
              igCheckbox("Horizontal##zhz", &up->zslider_horizontal);
              igCheckbox("Hide zoom bar##zhd", &up->zslider_hidden);
              if (igButton("Reset zoom slider##zr", (ImVec2){sb_w, 0.0f})) {
                up->zoom_sensitivity   = 1.0f;
                up->zslider_rel_x      = 0.968f;
                up->zslider_rel_y      = 0.500f;
                up->zslider_rel_h      = 0.280f;
                up->zslider_opacity    = 1.0f;
                up->zslider_horizontal = false;
                up->zslider_hidden     = false;
              }
              igSpacing();

              #undef CC_HEADER

              save_user_settings(up);

            } else if (s_qs_page == 2) {
              if (igButton("< Back##hk", (ImVec2){sb_w * 0.4f, 0.0f}))
                s_qs_page = 0;
              igSeparator();
              igTextColored((ImVec4){0.75f,0.85f,1.0f,1.0f}, "On-Screen Buttons");
              igTextColored((ImVec4){0.55f,0.55f,0.55f,1.0f},
                "Toggle to show a tap button in-game.");
              igSpacing();

              static const char* hk_labels[NUM_HOTKEYS] = {
                "HUD", "Show Names", "Big Food",
                "Assist", "Bot", "Menu", "Restart", "Quit"
              };
              for (int hi = 0; hi < NUM_HOTKEYS; hi++) {
                char lbl[80];
                bool shown = up->hk_show_btn[hi];
                snprintf(lbl, sizeof(lbl), "[%s] %s##hk%d",
                         shown ? "ON " : "OFF", hk_labels[hi], hi);
                if (shown) {
                  igPushStyleColor_Vec4(ImGuiCol_Button,
                    (ImVec4){0.20f,0.55f,0.30f,0.70f});
                  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                    (ImVec4){0.30f,0.65f,0.40f,0.80f});
                }
                if (igButton(lbl, (ImVec2){sb_w, 0.0f})) {
                  up->hk_show_btn[hi] = !up->hk_show_btn[hi];
                  save_user_settings(up);
                }
                if (shown) igPopStyleColor(2);
              }
            }
          }
          igEnd();
        }
      }

      { extern bool g_panel_open; g_panel_open = s_qs_open; }
    }

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
