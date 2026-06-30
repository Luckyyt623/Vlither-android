#ifdef ANDROID

#include <time.h>
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui/cimgui.h"
#include "imgui_setup.h"
#include "user.h"

#include <android/asset_manager.h>
#include <android_native_app_glue.h>
extern struct android_app* g_android_app;

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

    static const ImWchar icon_ranges[] = {0xe900, 0xeaea, 0};

    for (int i = 0; i < NUM_FONT_SIZES; i++) {
        float size = 20.0f + i * 4.0f;

        ImFontConfig icons_cfg = {
            .FontDataOwnedByAtlas = true,
            .OversampleH          = 0,
            .OversampleV          = 0,
            .GlyphMaxAdvanceX     = FLT_MAX,
            .RasterizerDensity    = 1,
            .RasterizerMultiply   = 1,
            .EllipsisChar         = 0,
            .MergeMode            = true,
            .GlyphOffset          = (ImVec2){0, 2.0f + i},
            .GlyphMinAdvanceX     = 26.0f + i * 6.0f,
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

    float scale = (float)env->ctx->size[0] / 1080.0f;
    if (scale < 1.0f) scale = 1.0f;
    if (scale > 1.2f) scale = 1.2f;
    ImGuiStyle_ScaleAllSizes(igGetStyle(), scale);

    ImGuiStyle* style = igGetStyle();
    style->DockingNodeHasCloseButton        = false;
    style->WindowMenuButtonPosition         = ImGuiDir_None;
    style->TabCloseButtonMinWidthUnselected = -1;
    style->WindowBorderSize = style->FrameBorderSize =
    style->ChildBorderSize  = style->PopupBorderSize =
    style->TabBorderSize    = 1;
    style->FramePadding     = (ImVec2){8, 8};
    style->ItemSpacing      = (ImVec2){4, 4};
    style->ItemInnerSpacing = (ImVec2){4, 4};
    style->WindowPadding    = (ImVec2){4, 4};
    style->GrabMinSize      = 18;
    style->FrameRounding = style->TabRounding = style->ChildRounding =
    style->GrabRounding  = style->PopupRounding =
    style->ScrollbarRounding = style->WindowRounding =
    style->TreeLinesRounding = 3;
    style->ScrollbarSize    = 18;
    style->DockingSeparatorSize = 1;
    style->ScrollbarPadding = 1;
    style->CellPadding.x    = 2;

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
        io->MousePos     = (ImVec2){wnd->touch.x, wnd->touch.y};
        io->MouseDown[0] = wnd->touch.down;
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
