#ifdef ANDROID

#include <time.h>
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui/cimgui.h"
#include "imgui_setup.h"
#include "android_jni.h"
#include "user.h"

#include <android/asset_manager.h>
#include <android/keycodes.h>
#include <android/input.h>
#include <android_native_app_glue.h>
extern struct android_app* g_android_app;

static float g_android_imgui_scale = 1.0f;

static const char* android_imgui_get_clipboard(ImGuiContext* ctx) {
    (void)ctx;
    return android_jni_get_clipboard_text();
}

static void android_imgui_set_clipboard(ImGuiContext* ctx, const char* text) {
    (void)ctx;
    android_jni_set_clipboard_text(text ? text : "");
}

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}


static ImGuiKey android_ime_key_to_imgui(int keycode) {
    switch (keycode) {
        case AKEYCODE_TAB:         return ImGuiKey_Tab;
        case AKEYCODE_DPAD_LEFT:   return ImGuiKey_LeftArrow;
        case AKEYCODE_DPAD_RIGHT:  return ImGuiKey_RightArrow;
        case AKEYCODE_DPAD_UP:     return ImGuiKey_UpArrow;
        case AKEYCODE_DPAD_DOWN:   return ImGuiKey_DownArrow;
        case AKEYCODE_MOVE_HOME:   return ImGuiKey_Home;
        case AKEYCODE_MOVE_END:    return ImGuiKey_End;
        case AKEYCODE_FORWARD_DEL: return ImGuiKey_Delete;
        case AKEYCODE_DEL:         return ImGuiKey_Backspace;
        case AKEYCODE_ENTER:       return ImGuiKey_Enter;
        case AKEYCODE_ESCAPE:      return ImGuiKey_Escape;
        case AKEYCODE_CTRL_LEFT:   return ImGuiKey_LeftCtrl;
        case AKEYCODE_CTRL_RIGHT:  return ImGuiKey_RightCtrl;
        case AKEYCODE_SHIFT_LEFT:  return ImGuiKey_LeftShift;
        case AKEYCODE_SHIFT_RIGHT: return ImGuiKey_RightShift;
        case AKEYCODE_ALT_LEFT:    return ImGuiKey_LeftAlt;
        case AKEYCODE_ALT_RIGHT:   return ImGuiKey_RightAlt;
        case AKEYCODE_A:           return ImGuiKey_A;
        case AKEYCODE_C:           return ImGuiKey_C;
        case AKEYCODE_V:           return ImGuiKey_V;
        case AKEYCODE_X:           return ImGuiKey_X;
        case AKEYCODE_Y:           return ImGuiKey_Y;
        case AKEYCODE_Z:           return ImGuiKey_Z;
        default:                   return ImGuiKey_None;
    }
}

static void android_drain_ime_events(ImGuiIO* io) {
    android_ime_event event;
    while (android_jni_poll_ime_event(&event)) {
        if (event.type == ANDROID_IME_EVENT_TEXT) {
            if (event.text && event.text[0] != '\0')
                ImGuiIO_AddInputCharactersUTF8(io, event.text);
        } else if (event.type == ANDROID_IME_EVENT_COMPOSITION) {
            for (int i = 0; i < event.replace_codepoints; ++i) {
                ImGuiIO_AddKeyEvent(io, ImGuiKey_Backspace, true);
                ImGuiIO_AddKeyEvent(io, ImGuiKey_Backspace, false);
            }
            if (event.text && event.text[0] != '\0')
                ImGuiIO_AddInputCharactersUTF8(io, event.text);
        } else if (event.type == ANDROID_IME_EVENT_KEY) {
            const bool down = event.action == AKEY_EVENT_ACTION_DOWN ||
                              event.action == AKEY_EVENT_ACTION_MULTIPLE;
            const bool ctrl = (event.meta_state & AMETA_CTRL_ON) != 0 ||
                ((event.keycode == AKEYCODE_CTRL_LEFT ||
                  event.keycode == AKEYCODE_CTRL_RIGHT) && down);
            const bool shift = (event.meta_state & AMETA_SHIFT_ON) != 0 ||
                ((event.keycode == AKEYCODE_SHIFT_LEFT ||
                  event.keycode == AKEYCODE_SHIFT_RIGHT) && down);
            const bool alt = (event.meta_state & AMETA_ALT_ON) != 0 ||
                ((event.keycode == AKEYCODE_ALT_LEFT ||
                  event.keycode == AKEYCODE_ALT_RIGHT) && down);

            ImGuiIO_AddKeyEvent(io, ImGuiMod_Ctrl, ctrl);
            ImGuiIO_AddKeyEvent(io, ImGuiMod_Shift, shift);
            ImGuiIO_AddKeyEvent(io, ImGuiMod_Alt, alt);

            ImGuiKey key = android_ime_key_to_imgui(event.keycode);
            if (key != ImGuiKey_None)
                ImGuiIO_AddKeyEvent(io, key, down);
        }
        android_jni_release_ime_event(&event);
    }
}

void imgui_init(tenv* env) {
    tuser_data*   usr  = env->usr;
    user_settings* usrs = &usr->usrs;

    igCreateContext(NULL);
    igImplVulkan_Init(&(ImGui_ImplVulkan_InitInfo){
        .ApiVersion       = VK_API_VERSION_1_0,
        .Instance         = env->ctx->instance,
        .PhysicalDevice   = env->ctx->ph_device,
        .Device           = env->ctx->device,
        .QueueFamily      = env->ctx->queue_family,
        .Queue            = env->ctx->queue,
        .DescriptorPool   = env->ctx->descriptor_pool,
        .MinImageCount    = env->ctx->min_image_count,
        .ImageCount       = env->ctx->fif,
        .PipelineInfoMain = {
            .RenderPass  = env->ctx->renderpass,
            .Subpass     = 0,
            .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
        },
        .UseDynamicRendering = false,
    });

    ImGuiIO* io = igGetIO_Nil();
    io->DisplaySize = (ImVec2){(float)env->ctx->size[0],
                               (float)env->ctx->size[1]};

    AAssetManager* am = g_android_app->activity->assetManager;

    /* Resolution-aware UI density. Use the shorter edge so ultrawide phones
       do not receive oversized controls, while low-resolution devices still
       get a compact layout that fits without clipping. */
    float short_edge = (float)(env->ctx->size[0] < env->ctx->size[1]
                                   ? env->ctx->size[0] : env->ctx->size[1]);
    float ui_scale = short_edge / 720.0f;
    if (ui_scale < 0.75f) ui_scale = 0.75f;
    if (ui_scale > 1.30f) ui_scale = 1.30f;
    g_android_imgui_scale = ui_scale;

    static const ImWchar icon_ranges[] = {0xe900, 0xeaea, 0};

    for (int i = 0; i < NUM_FONT_SIZES; i++) {
        float size = (20.0f + i * 4.0f) * ui_scale;

        ImFontConfig icons_cfg = {
            .FontDataOwnedByAtlas = true,
            .OversampleH          = 0,
            .OversampleV          = 0,
            .GlyphMaxAdvanceX     = FLT_MAX,
            .RasterizerDensity    = 1,
            .RasterizerMultiply   = 1,
            .EllipsisChar         = 0,
            .MergeMode            = true,
            .GlyphOffset          = (ImVec2){0, (2.0f + i) * ui_scale},
            .GlyphMinAdvanceX     = (26.0f + i * 6.0f) * ui_scale,
        };

#define LOAD_FONT(path, sz, cfg, ranges) do { \
    AAsset* _a = AAssetManager_open(am, path, AASSET_MODE_BUFFER); \
    if (_a) { \
        off_t _len = AAsset_getLength(_a); \
        void* _buf = malloc(_len); \
        AAsset_read(_a, _buf, _len); \
        AAsset_close(_a); \
        ImFontAtlas_AddFontFromMemoryTTF(io->Fonts, _buf, (int)_len, sz, cfg, ranges); \
    } \
} while(0)

#define LOAD_FONT_RET(out, path, sz, cfg, ranges) do { \
    AAsset* _a = AAssetManager_open(am, path, AASSET_MODE_BUFFER); \
    (out) = NULL; \
    if (_a) { \
        off_t _len = AAsset_getLength(_a); \
        void* _buf = malloc(_len); \
        AAsset_read(_a, _buf, _len); \
        AAsset_close(_a); \
        (out) = ImFontAtlas_AddFontFromMemoryTTF(io->Fonts, _buf, (int)_len, sz, cfg, ranges); \
    } \
} while(0)

        LOAD_FONT_RET(usr->imgui_data.mono_font[i],
            "fonts/mono_regular.ttf", size, NULL, NULL);
        LOAD_FONT("fonts/iconfont.ttf", size, &icons_cfg, icon_ranges);

        LOAD_FONT_RET(usr->imgui_data.regular_font[i],
            "fonts/regular_regular.ttf", size, NULL, NULL);
        LOAD_FONT("fonts/iconfont.ttf", size, &icons_cfg, icon_ranges);

        LOAD_FONT_RET(usr->imgui_data.mono_font_bold[i],
            "fonts/mono_bold.ttf", size, NULL, NULL);
        LOAD_FONT("fonts/iconfont.ttf", size, &icons_cfg, icon_ranges);

        LOAD_FONT_RET(usr->imgui_data.regular_font_bold[i],
            "fonts/regular_bold.ttf", size, NULL, NULL);
        LOAD_FONT("fonts/iconfont.ttf", size, &icons_cfg, icon_ranges);

#undef LOAD_FONT
#undef LOAD_FONT_RET
    }

    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io->IniFilename  = NULL;

    /* Use Android's real system clipboard instead of Dear ImGui's private
       fallback clipboard. This enables copy/paste in every InputText widget. */
    ImGuiPlatformIO* platform_io = igGetPlatformIO_Nil();
    platform_io->Platform_GetClipboardTextFn = android_imgui_get_clipboard;
    platform_io->Platform_SetClipboardTextFn = android_imgui_set_clipboard;

    ImGuiStyle* style = igGetStyle();
    style->DockingNodeHasCloseButton        = false;
    style->WindowMenuButtonPosition         = ImGuiDir_None;
    style->TabCloseButtonMinWidthUnselected = -1;
    style->WindowBorderSize = style->FrameBorderSize =
    style->ChildBorderSize  = style->PopupBorderSize =
    style->TabBorderSize    = 1;
    style->FramePadding     = (ImVec2){8.0f * ui_scale, 8.0f * ui_scale};
    style->ItemSpacing      = (ImVec2){4.0f * ui_scale, 4.0f * ui_scale};
    style->ItemInnerSpacing = (ImVec2){4.0f * ui_scale, 4.0f * ui_scale};
    style->WindowPadding    = (ImVec2){4.0f * ui_scale, 4.0f * ui_scale};
    style->GrabMinSize      = 18.0f * ui_scale;
    style->FrameRounding = style->TabRounding = style->ChildRounding =
    style->GrabRounding  = style->PopupRounding =
    style->ScrollbarRounding = style->WindowRounding =
    style->TreeLinesRounding = 3.0f * ui_scale;
    style->ScrollbarSize    = 18.0f * ui_scale;
    style->DockingSeparatorSize = 1;
    style->ScrollbarPadding = 1.0f * ui_scale;
    style->CellPadding.x    = 2.0f * ui_scale;

    igStyleColorsDark(style);
    style->Colors[ImGuiCol_Text]       = (ImVec4){0.89f, 0.89f, 0.89f, 1.00f};
    style->Colors[ImGuiCol_WindowBg]   =
    style->Colors[ImGuiCol_PopupBg]    = (ImVec4){0.20f, 0.20f, 0.20f, 1.00f};
    style->Colors[ImGuiCol_Border]     = (ImVec4){0.00f, 0.00f, 0.00f, 1.00f};
    style->Colors[ImGuiCol_FrameBg]    = (ImVec4){0.16f, 0.16f, 0.16f, 1.00f};
    style->Colors[ImGuiCol_TitleBgActive] = (ImVec4){0.12f, 0.12f, 0.12f, 1.00f};
    style->Colors[ImGuiCol_Button]     = (ImVec4){0.25f, 0.25f, 0.25f, 1.00f};
    style->Colors[ImGuiCol_ButtonHovered] = (ImVec4){0.31f, 0.31f, 0.31f, 1.00f};
    style->Colors[ImGuiCol_Header]     = (ImVec4){0.25f, 0.25f, 0.25f, 1.00f};
    style->Colors[ImGuiCol_Tab]        = (ImVec4){0.25f, 0.25f, 0.25f, 1.00f};
    style->Colors[ImGuiCol_TabSelected] = (ImVec4){0.31f, 0.31f, 0.31f, 1.00f};
    style->Colors[ImGuiCol_ModalWindowDimBg] = (ImVec4){0, 0, 0, 0.7f};
    style->Colors[ImGuiCol_FrameBgHovered]   = (ImVec4){0.23f, 0.23f, 0.23f, 1.00f};
    style->Colors[ImGuiCol_FrameBgActive]    = (ImVec4){0.12f, 0.12f, 0.12f, 1.00f};
    style->Colors[ImGuiCol_SliderGrab]       = (ImVec4){0.31f, 0.31f, 0.31f, 1.00f};
    style->Colors[ImGuiCol_ButtonActive]     = (ImVec4){0.14f, 0.14f, 0.14f, 1.00f};
}

bool g_imgui_wants_keyboard = false;

void imgui_prerender(void) {
    igImplVulkan_NewFrame();

    ImGuiIO* io = igGetIO_Nil();
    android_drain_ime_events(io);

    static struct timespec _last_time = {0, 0};
    struct timespec _now;
    clock_gettime(CLOCK_MONOTONIC, &_now);
    if (_last_time.tv_sec == 0 && _last_time.tv_nsec == 0) {
        io->DeltaTime = 1.0f / 60.0f;
    } else {
        double dt = (_now.tv_sec  - _last_time.tv_sec)
                  + (_now.tv_nsec - _last_time.tv_nsec) * 1e-9;
        io->DeltaTime = (dt > 0.0) ? (float)dt : 1.0f / 60.0f;
    }
    _last_time = _now;

    if (g_android_app->userData) {
        twindow* wnd = (twindow*)g_android_app->userData;
        io->MousePos     = (ImVec2){wnd->ui_touch.x, wnd->ui_touch.y};
        io->MouseDown[0] = wnd->ui_touch.down;
        if (wnd->size[0] > 0 && wnd->size[1] > 0)
            io->DisplaySize = (ImVec2){(float)wnd->size[0], (float)wnd->size[1]};

        extern bool g_panel_open;
        static float s_scroll_last_y   = 0.0f;
        static bool  s_scroll_was_down = false;
        if (g_panel_open) {
            bool down_now = io->MouseDown[0];
            if (down_now && s_scroll_was_down) {
                float dy = io->MousePos.y - s_scroll_last_y;

                if (dy < -2.0f || dy > 2.0f)
                    io->MouseWheel += dy / 30.0f;
            }
            s_scroll_was_down = down_now;
            s_scroll_last_y   = down_now ? io->MousePos.y : 0.0f;
        } else {
            s_scroll_was_down = false;
        }

        /* Text, keyboard clipboard suggestions, composition and edit keys
           arrive through the native Android InputConnection. A stationary
           long press remains available as a direct paste fallback. */
        static bool s_paste_tracking = false;
        static bool s_paste_fired = false;
        static ImVec2 s_paste_start = {0.0f, 0.0f};
        static double s_paste_start_time = 0.0;

        if (!io->MouseDown[0] || !io->WantTextInput) {
            s_paste_tracking = false;
            s_paste_fired = false;
        } else if (!s_paste_tracking) {
            s_paste_tracking = true;
            s_paste_fired = false;
            s_paste_start = io->MousePos;
            s_paste_start_time = monotonic_seconds();
        } else {
            float dx = io->MousePos.x - s_paste_start.x;
            float dy = io->MousePos.y - s_paste_start.y;
            float cancel_distance = 18.0f * g_android_imgui_scale;
            if (dx * dx + dy * dy > cancel_distance * cancel_distance) {
                s_paste_tracking = false;
                s_paste_fired = false;
            } else if (!s_paste_fired &&
                       monotonic_seconds() - s_paste_start_time >= 0.55) {
                android_jni_enqueue_clipboard_paste();
                s_paste_fired = true;
            }
        }

        g_imgui_wants_keyboard = io->WantTextInput;
    }

    igNewFrame();
}

void imgui_render(VkCommandBuffer cmd) {
    igImplVulkan_RenderDrawData(igGetDrawData(), cmd, NULL);
}

void imgui_destroy(void) {
    igImplVulkan_Shutdown();
    igDestroyContext(NULL);
}

#endif
