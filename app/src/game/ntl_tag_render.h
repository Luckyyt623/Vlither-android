#ifndef NTL_TAG_RENDER_H
#define NTL_TAG_RENDER_H

#include "../user.h"        // cimgui types (ImDrawList, ImVec2, ImTextureID)
#include "ntl_tags_main_data.h"  // ntl_tag_def

// Draw the NTL tag banner + its string along a screen-space cord polyline.
//   pls/np : cord points in SCREEN coords (neck -> dangle end); tag sits at end
//   wide   : base string thickness (px)
//   ang    : tag rotation (radians)
//   sw, sh : tag banner size (px)
//   a      : alpha
// Shared by the in-game renderer (redraw.c) and the skin-editor preview so the
// tag looks identical in both.
void ntl_tag_render(ImDrawList* dl, ImTextureID tex, const ntl_tag_def* td,
                    const ImVec2* pls, int np, float wide, float ang, float sw,
                    float sh, float a);

#endif
