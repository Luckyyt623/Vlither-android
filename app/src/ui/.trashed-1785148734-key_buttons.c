/**
 * key_buttons.c
 *
 * Custom on-screen key buttons — place any keyboard key anywhere on screen.
 * Each button:
 *   - Fires GLFW keydown on press, keyup on release
 *   - Draggable and resizable in edit mode
 *   - Position/size saved in user_settings (persisted to user.dat)
 *
 * UI flow:
 *   - Small "KB" edit button always visible (bottom-right, above zoom slider)
 *   - Tap "KB" → enter edit mode → shows all placed buttons + keyboard picker
 *   - Keyboard picker: full key grid, tap any key → places a new button
 *   - In edit mode: drag buttons to move, drag resize handle to resize
 *   - Tap "×" on a button to delete it
 *   - Tap "Done" to exit edit mode and save
 */

#include "key_buttons.h"
#include "../user.h"
#include "../game/game_data.h"

/* IM_COL32 is defined in imgui.h (C++ only) — define it here for C */
#ifndef IM_COL32
#define IM_COL32(R,G,B,A)     (((ImU32)(A)<<24)|((ImU32)(B)<<16)|((ImU32)(G)<<8)|((ImU32)(R)<<0))
#endif
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifdef ANDROID
#include <android/log.h>
#define KBLOG(...) __android_log_print(ANDROID_LOG_DEBUG, "vlither_kb", __VA_ARGS__)
#else
#define KBLOG(...) (void)0
#endif

/* ── State ────────────────────────────────────────────────────────────── */
static struct {
    bool edit_mode;
    bool picker_open;
    /* which slot is being dragged */
    int  drag_idx;        /* -1 = none */
    float drag_off_x;
    float drag_off_y;
    /* which slot is being resized */
    int  resize_idx;
    float resize_start_y;
    float resize_start_size;
    /* which buttons are currently pressed (for keyup) */
    bool pressed[MAX_KEY_BTNS];
} s_kb = {
    .edit_mode   = false,
    .picker_open = false,
    .drag_idx    = -1,
    .resize_idx  = -1,
};

/* ── Key table ────────────────────────────────────────────────────────── */
typedef struct { int glfw; const char* label; } key_entry;

static const key_entry KEY_TABLE[] = {
    /* Letters */
    {GLFW_KEY_A,"A"},{GLFW_KEY_B,"B"},{GLFW_KEY_C,"C"},{GLFW_KEY_D,"D"},
    {GLFW_KEY_E,"E"},{GLFW_KEY_F,"F"},{GLFW_KEY_G,"G"},{GLFW_KEY_H,"H"},
    {GLFW_KEY_I,"I"},{GLFW_KEY_J,"J"},{GLFW_KEY_K,"K"},{GLFW_KEY_L,"L"},
    {GLFW_KEY_M,"M"},{GLFW_KEY_N,"N"},{GLFW_KEY_O,"O"},{GLFW_KEY_P,"P"},
    {GLFW_KEY_Q,"Q"},{GLFW_KEY_R,"R"},{GLFW_KEY_S,"S"},{GLFW_KEY_T,"T"},
    {GLFW_KEY_U,"U"},{GLFW_KEY_V,"V"},{GLFW_KEY_W,"W"},{GLFW_KEY_X,"X"},
    {GLFW_KEY_Y,"Y"},{GLFW_KEY_Z,"Z"},
    /* Digits */
    {GLFW_KEY_0,"0"},{GLFW_KEY_1,"1"},{GLFW_KEY_2,"2"},{GLFW_KEY_3,"3"},
    {GLFW_KEY_4,"4"},{GLFW_KEY_5,"5"},{GLFW_KEY_6,"6"},{GLFW_KEY_7,"7"},
    {GLFW_KEY_8,"8"},{GLFW_KEY_9,"9"},
    /* Function keys */
    {GLFW_KEY_F1,"F1"},{GLFW_KEY_F2,"F2"},{GLFW_KEY_F3,"F3"},
    {GLFW_KEY_F4,"F4"},{GLFW_KEY_F5,"F5"},{GLFW_KEY_F6,"F6"},
    {GLFW_KEY_F7,"F7"},{GLFW_KEY_F8,"F8"},{GLFW_KEY_F9,"F9"},
    {GLFW_KEY_F10,"F10"},{GLFW_KEY_F11,"F11"},{GLFW_KEY_F12,"F12"},
    /* Special */
    {GLFW_KEY_SPACE,"SPC"},{GLFW_KEY_ENTER,"Ent"},
    {GLFW_KEY_ESCAPE,"Esc"},{GLFW_KEY_BACKSPACE,"BS"},
    {GLFW_KEY_TAB,"Tab"},{GLFW_KEY_CAPS_LOCK,"Caps"},
    {GLFW_KEY_LEFT_SHIFT,"Shft"},{GLFW_KEY_LEFT_CONTROL,"Ctrl"},
    {GLFW_KEY_LEFT_ALT,"Alt"},
    {GLFW_KEY_UP,"↑"},{GLFW_KEY_DOWN,"↓"},
    {GLFW_KEY_LEFT,"←"},{GLFW_KEY_RIGHT,"→"},
};
static const int KEY_TABLE_COUNT = sizeof(KEY_TABLE) / sizeof(KEY_TABLE[0]);

/* ── Find free slot ───────────────────────────────────────────────────── */
static int find_free_slot(user_settings* usrs) {
    for (int i = 0; i < MAX_KEY_BTNS; i++)
        if (!usrs->key_btns[i].active) return i;
    return -1;
}

/* ── Add a button ─────────────────────────────────────────────────────── */
static void add_key_btn(user_settings* usrs, int glfw_key, const char* label) {
    int idx = find_free_slot(usrs);
    if (idx < 0) return;
    custom_key_btn* b = &usrs->key_btns[idx];
    b->active   = true;
    b->glfw_key = glfw_key;
    strncpy(b->label, label, 7);
    b->label[7] = '\0';
    /* Place in centre by default */
    b->rel_x    = 0.3f + (idx % 4) * 0.12f;
    b->rel_y    = 0.5f + (idx / 4) * 0.12f;
    b->rel_size = 0.08f;
    b->opacity  = 0.85f;
    KBLOG("added key btn idx=%d key=%d label=%s", idx, glfw_key, label);
}

/* ── Lifecycle ────────────────────────────────────────────────────────── */
void ui_key_buttons_init(tenv* env) {
    (void)env;
    memset(&s_kb, 0, sizeof(s_kb));
    s_kb.drag_idx   = -1;
    s_kb.resize_idx = -1;
}

void ui_key_buttons_destroy(tenv* env) { (void)env; }

/* ── Main render ──────────────────────────────────────────────────────── */
void ui_key_buttons(tenv* env) {
    tuser_data*    usr  = env->usr;
    tcontext*      ctx  = env->ctx;
    user_settings* usrs = &usr->usrs;
    ImGuiIO*       io   = igGetIO_Nil();
    ImGuiStyle*    st   = igGetStyle();

    float sw = (float)ctx->size[0];
    float sh = (float)ctx->size[1];

    igPushFont(usr->imgui_data.regular_font[FONT_SIZE_SMALL],
               usr->imgui_data.regular_font[FONT_SIZE_SMALL]->LegacySize);

    bool mouse_down    = io && igIsMouseDown_Nil(0);
    bool mouse_clicked = io && igIsMouseClicked_Bool(0, false);
    bool mouse_released= io && igIsMouseReleased_Nil(0);
    float mx = io ? io->MousePos.x : 0;
    float my = io ? io->MousePos.y : 0;

    /* ── "KB" edit toggle button — bottom right ─────────────────────── */
    {
        ImDrawList* fdl = igGetForegroundDrawList_ViewportPtr(igGetMainViewport());
        float br  = sh * 0.028f;
        float bx  = br * 1.8f;   /* left side */
        float by  = sh - br * 1.8f;

        ImU32 bg  = s_kb.edit_mode
                      ? IM_COL32(200, 100, 30, 200)
                      : IM_COL32(255,255,255, 25);
        ImU32 brd = IM_COL32(255,255,255, s_kb.edit_mode ? 180 : 55);

        ImDrawList_AddCircleFilled(fdl, (ImVec2){bx,by}, br, bg, 24);
        ImDrawList_AddCircle(fdl, (ImVec2){bx,by}, br, brd, 24, 1.5f);

        const char* lbl = s_kb.edit_mode ? "Done" : "KB";
        ImVec2 tsz; igCalcTextSize(&tsz, lbl, NULL, false, -1);
        ImDrawList_AddText_Vec2(fdl,
            (ImVec2){bx - tsz.x*0.5f, by - tsz.y*0.5f},
            IM_COL32(255,255,255,200), lbl, NULL);

        if (mouse_clicked) {
            float dx = mx - bx, dy = my - by;
            if (sqrtf(dx*dx+dy*dy) <= br) {
                s_kb.edit_mode = !s_kb.edit_mode;
                if (!s_kb.edit_mode) {
                    /* Save on exit */
                    save_user_settings(usrs);
                    s_kb.picker_open = false;
                    s_kb.drag_idx    = -1;
                    s_kb.resize_idx  = -1;
                }
            }
        }
    }

    /* ── Placed key buttons ──────────────────────────────────────────── */
    for (int i = 0; i < MAX_KEY_BTNS; i++) {
        custom_key_btn* b = &usrs->key_btns[i];
        if (!b->active) continue;

        float bsz  = b->rel_size * sh;   /* button radius */
        float bx   = b->rel_x * sw;
        float by_  = b->rel_y * sh;

        ImDrawList* fdl = igGetForegroundDrawList_ViewportPtr(igGetMainViewport());

        ImU32 bg_col, brd_col, txt_col;
        if (s_kb.edit_mode) {
            bg_col  = IM_COL32(60, 100, 160, 200);
            brd_col = IM_COL32(100,180,255, 200);
            txt_col = IM_COL32(255,255,255, 230);
        } else if (s_kb.pressed[i]) {
            bg_col  = IM_COL32(80, 200, 120, (int)(b->opacity * 230));
            brd_col = IM_COL32(120,255,160, (int)(b->opacity * 200));
            txt_col = IM_COL32(255,255,255, 255);
        } else {
            bg_col  = IM_COL32(40, 40, 40,  (int)(b->opacity * 180));
            brd_col = IM_COL32(200,200,200, (int)(b->opacity * 140));
            txt_col = IM_COL32(255,255,255, (int)(b->opacity * 220));
        }

        ImDrawList_AddCircleFilled(fdl, (ImVec2){bx,by_}, bsz, bg_col, 32);
        ImDrawList_AddCircle(fdl, (ImVec2){bx,by_}, bsz, brd_col, 32, 1.8f);

        /* Label */
        ImVec2 tsz; igCalcTextSize(&tsz, b->label, NULL, false, -1);
        ImDrawList_AddText_Vec2(fdl,
            (ImVec2){bx - tsz.x*0.5f, by_ - tsz.y*0.5f},
            txt_col, b->label, NULL);

        if (s_kb.edit_mode) {
            /* Delete × button — top-right of circle */
            float dx_b = bsz * 0.65f, dy_b = bsz * 0.65f;
            float del_x = bx + dx_b, del_y = by_ - dy_b;
            float del_r = bsz * 0.32f;
            ImDrawList_AddCircleFilled(fdl, (ImVec2){del_x,del_y},
                                       del_r, IM_COL32(180,40,40,220), 16);
            const char* x = "x";
            ImVec2 xsz; igCalcTextSize(&xsz, x, NULL, false, -1);
            ImDrawList_AddText_Vec2(fdl,
                (ImVec2){del_x-xsz.x*0.5f, del_y-xsz.y*0.5f},
                IM_COL32(255,255,255,255), x, NULL);

            /* Resize handle — bottom-right */
            float res_x = bx + bsz * 0.7f, res_y = by_ + bsz * 0.7f;
            float res_r = bsz * 0.28f;
            ImDrawList_AddCircleFilled(fdl, (ImVec2){res_x,res_y},
                                       res_r, IM_COL32(30,140,200,210), 16);
            ImDrawList_AddText_Vec2(fdl,
                (ImVec2){res_x-4.0f, res_y-6.0f},
                IM_COL32(255,255,255,255), "⊞", NULL);

            /* Hit tests in edit mode */
            if (mouse_clicked) {
                /* Delete? */
                float dxd = mx-del_x, dyd = my-del_y;
                if (sqrtf(dxd*dxd+dyd*dyd) <= del_r) {
                    b->active = false;
                    continue;
                }
                /* Resize start? */
                float dxr = mx-res_x, dyr = my-res_y;
                if (sqrtf(dxr*dxr+dyr*dyr) <= res_r) {
                    s_kb.resize_idx         = i;
                    s_kb.resize_start_y     = my;
                    s_kb.resize_start_size  = b->rel_size;
                }
                /* Drag start? */
                float dxb = mx-bx, dyb = my-by_;
                if (sqrtf(dxb*dxb+dyb*dyb) <= bsz &&
                    s_kb.resize_idx != i) {
                    s_kb.drag_idx   = i;
                    s_kb.drag_off_x = mx - bx;
                    s_kb.drag_off_y = my - by_;
                }
            }
        } else {
            /* Normal mode: press/release to fire key events */
            float dxb = mx - bx, dyb = my - by_;
            bool over = sqrtf(dxb*dxb + dyb*dyb) <= bsz;

            if (mouse_clicked && over && !s_kb.pressed[i]) {
                s_kb.pressed[i] = true;
                /* Inject fake key press — input.c picks this up next frame */
                int key = b->glfw_key;
                if (key > 0 && key < 512) {
                    usr->gdata.data.fake_key_pressed[key] = true;
                    usr->gdata.data.fake_key_down[key]    = true;
                }
            }
            /* Keep fake_key_down true while finger is held */
            if (s_kb.pressed[i]) {
                int key = b->glfw_key;
                if (key > 0 && key < 512)
                    usr->gdata.data.fake_key_down[key] = true;
            }
            if (mouse_released && s_kb.pressed[i]) {
                s_kb.pressed[i] = false;
                /* Release: clear fake_key_down */
                int key = b->glfw_key;
                if (key > 0 && key < 512)
                    usr->gdata.data.fake_key_down[key] = false;
            }
        }
    }

    /* ── Drag / resize update ────────────────────────────────────────── */
    if (s_kb.drag_idx >= 0 && mouse_down) {
        custom_key_btn* b = &usrs->key_btns[s_kb.drag_idx];
        b->rel_x = (mx - s_kb.drag_off_x) / sw;
        b->rel_y = (my - s_kb.drag_off_y) / sh;
        /* Clamp */
        float sz = b->rel_size;
        if (b->rel_x < sz)        b->rel_x = sz;
        if (b->rel_x > 1.0f - sz) b->rel_x = 1.0f - sz;
        if (b->rel_y < sz)        b->rel_y = sz;
        if (b->rel_y > 1.0f - sz) b->rel_y = 1.0f - sz;
    }
    if (mouse_released) {
        s_kb.drag_idx = -1;
    }

    if (s_kb.resize_idx >= 0 && mouse_down) {
        custom_key_btn* b = &usrs->key_btns[s_kb.resize_idx];
        float delta = (s_kb.resize_start_y - my) / sh;
        b->rel_size = s_kb.resize_start_size + delta;
        if (b->rel_size < 0.03f) b->rel_size = 0.03f;
        if (b->rel_size > 0.18f) b->rel_size = 0.18f;
    }
    if (mouse_released) {
        s_kb.resize_idx = -1;
    }

    /* ── Key picker panel (edit mode) ────────────────────────────────── */
    if (s_kb.edit_mode) {
        /* "Add Key" button */
        float add_bx = sw * 0.5f;
        float add_by = sh * 0.04f;
        float add_r  = sh * 0.032f;
        ImDrawList* fdl = igGetForegroundDrawList_ViewportPtr(igGetMainViewport());
        ImU32 add_bg  = s_kb.picker_open
                          ? IM_COL32(30, 160, 80, 220)
                          : IM_COL32(30, 120, 60, 180);
        ImDrawList_AddCircleFilled(fdl, (ImVec2){add_bx,add_by}, add_r, add_bg, 24);
        ImDrawList_AddCircle(fdl,(ImVec2){add_bx,add_by},add_r,
                             IM_COL32(80,220,120,200),24,1.5f);
        const char* plus = s_kb.picker_open ? "▲" : "+ Key";
        ImVec2 psz; igCalcTextSize(&psz, plus, NULL, false, -1);
        ImDrawList_AddText_Vec2(fdl,
            (ImVec2){add_bx-psz.x*0.5f, add_by-psz.y*0.5f},
            IM_COL32(255,255,255,220), plus, NULL);

        if (mouse_clicked) {
            float dx2 = mx-add_bx, dy2 = my-add_by;
            if (sqrtf(dx2*dx2+dy2*dy2) <= add_r)
                s_kb.picker_open = !s_kb.picker_open;
        }
    }

    if (s_kb.edit_mode && s_kb.picker_open &&
        find_free_slot(usrs) >= 0) {

        float pw = sw * 0.92f;
        float ph = sh * 0.40f;
        float px = (sw - pw) * 0.5f;
        float py = sh * 0.08f;

        igSetNextWindowPos((ImVec2){px, py}, ImGuiCond_Always, (ImVec2){});
        igSetNextWindowSize((ImVec2){pw, ph}, ImGuiCond_Always);
        igSetNextWindowBgAlpha(0.96f);

        if (igBegin("##key_picker",
                    NULL,
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoNav    |
                    ImGuiWindowFlags_HorizontalScrollbar)) {

            igTextColored((ImVec4){0.4f,0.85f,0.55f,1.0f},
                          "Tap a key to add it:");
            igSameLine(0,-1);
            igSetCursorPosX(pw - igGetFrameHeight() - igGetStyle()->WindowPadding.x * 2);
            if (igButton("×##closepicker", (ImVec2){igGetFrameHeight(),0}))
                s_kb.picker_open = false;
            igSeparator();
            igSpacing();

            float btn_sz = sh * 0.055f;
            int cols = (int)(pw / (btn_sz + 6));
            if (cols < 4) cols = 4;

            if (igBeginTable("##keytable", cols,
                             ImGuiTableFlags_None,
                             (ImVec2){}, 0)) {
                for (int k = 0; k < KEY_TABLE_COUNT; k++) {
                    igTableNextColumn();
                    igPushID_Int(1000 + k);
                    if (igButton(KEY_TABLE[k].label, (ImVec2){btn_sz, btn_sz})) {
                        add_key_btn(usrs, KEY_TABLE[k].glfw,
                                    KEY_TABLE[k].label);
                        s_kb.picker_open = false;
                    }
                    igPopID();
                }
                igEndTable();
            }
        }
        igEnd();
    }

    igPopFont();
}
