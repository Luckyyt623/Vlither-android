#ifndef NTL_TAG_RENDER_H
#define NTL_TAG_RENDER_H

#include "../user.h"
#include "ntl_tags_main_data.h"

void ntl_tag_render(ImDrawList* dl, ImTextureID tex, const ntl_tag_def* td,
                    const ImVec2* pls, int np, float wide, float ang, float sw,
                    float sh, float a);

#endif
