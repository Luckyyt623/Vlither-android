#include "game/loop.h"
#include "ui/skin_editor.h"
#include "ui/title_screen.h"
#include "ui/settings.h"
#include "ui/viewport.h"
#include "user.h"

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

  /* F11 fullscreen is PC-only — no keyboard on Android */
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
  strcpy(usrs->ipv4, "15.204.212.200:444");
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
  DLOG("tinit: game_data_init done");
  DLOG("tinit: complete");
}

void tdestroy(tenv* env) {
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
    /* 1. Offscreen pass — must finish (including its pipeline barrier) BEFORE
          the swapchain render pass opens.  Opening tcontext_clear first and
          then starting a second render pass inside renderer_render violated
          Vulkan render-pass nesting rules and left the swapchain framebuffer
          in its cleared-black state on mobile drivers (Adreno / Mali). */
    renderer_render(usr->r, ctx, (vec4){0.086f, 0.109f, 0.133f, 1});
    /* Clear instances AFTER rendering them, so the UI can push new ones below */
    renderer_clear_instances(usr->r);

    /* 2. Now open the swapchain render pass.  The offscreen texture is already
          in SHADER_READ_ONLY_OPTIMAL (barrier emitted by renderer_render), so
          ImGui can safely sample it in the same command buffer. */
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

    /* Cursor must be submitted BEFORE igRender()/imgui_render() so it stays
       inside the swapchain render pass opened by tcontext_clear.
       Calling it after imgui_render() put it outside the render pass -> crash. */
    renderer_render_cursor(usr->r, ctx);

    igRender();
    imgui_render(ctx->frames[ctx->current_frame].cmd);
  }

  tcontext_end(ctx);
}

void tresize(tenv* env) { ui_viewport_resize(env); }

/* FIX: TDEF_ENTRY() expands to int main() which calls tentry().
   On Android, android_main() in tentry_android.c is the entry point —
   tentry() does not exist. Guard this so it only compiles on PC. */
#ifndef ANDROID
TDEF_ENTRY();
#endif
