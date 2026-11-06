#include "ntl_tag_render.h"

#include <math.h>

#include "ntl_tag_anchors.h"  // NTL_TAG_ANCHORS

void ntl_tag_render(ImDrawList* dl, ImTextureID tex, const ntl_tag_def* td,
                    const ImVec2* pls, int np, float wide, float ang, float sw,
                    float sh, float a) {
  if (!td || np < 1) return;
  float mcr = td->cm[0] / 255.0f, mcg = td->cm[1] / 255.0f, mcb = td->cm[2] / 255.0f;
  float ecr = td->ce[0] / 255.0f, ecg = td->ce[1] / 255.0f, ecb = td->ce[2] / 255.0f;
  if (wide < 2.5f) wide = 2.5f;

  // string: faded black halo, then a smooth rim->core colour gradient
  if (np >= 2) {
    const float bw[3] = {wide * 1.55f, wide * 1.35f, wide * 1.18f};
    const float bal[3] = {0.10f, 0.24f, 0.42f};
    for (int b = 0; b < 3; b++) {
      ImU32 c = igColorConvertFloat4ToU32((ImVec4){0, 0, 0, bal[b] * a});
      ImDrawList_AddPolyline(dl, pls, np, c, 0, bw[b]);
      ImDrawList_AddCircleFilled(dl, pls[0], bw[b] * 0.5f, c, 16);
      ImDrawList_AddCircleFilled(dl, pls[np - 1], bw[b] * 0.5f, c, 16);
    }
    const int GL = 7;
    for (int L = 0; L < GL; L++) {
      float t = (float)L / (float)(GL - 1);
      float lw = wide * (1.0f - 0.55f * t);
      float r = ecr + (mcr - ecr) * t;
      float g = ecg + (mcg - ecg) * t;
      float b2 = ecb + (mcb - ecb) * t;
      float al = (0.5f + 0.45f * t) * a;
      ImU32 c = igColorConvertFloat4ToU32((ImVec4){r, g, b2, al});
      ImDrawList_AddPolyline(dl, pls, np, c, 0, lw);
      ImDrawList_AddCircleFilled(dl, pls[0], lw * 0.5f, c, 16);
      ImDrawList_AddCircleFilled(dl, pls[np - 1], lw * 0.5f, c, 16);
    }
  }

  ImVec2 tagscreen = pls[np - 1];
  if (!tex) return;
  float ca = cosf(ang), sa = sinf(ang), hw = sw * 0.5f, hh = sh * 0.5f;
  int tdi = (int)(td - NTL_TAGS);
  float lead = (tdi >= 0 && tdi < NTL_TAG_COUNT) ? NTL_TAG_ANCHORS[tdi] : td->cl;
  float ul = lead + 0.10f * (td->cr - td->cl);
  float vc = (td->ct + td->cb) * 0.5f;
  float au = (ul - 0.5f) * sw, av = (vc - 0.5f) * sh;
  float sx = tagscreen.x - au * ca + av * sa;
  float sy = tagscreen.y - au * sa - av * ca;
  ImVec2 q1 = {sx - hw * ca + hh * sa, sy - hw * sa - hh * ca};
  ImVec2 q2 = {sx + hw * ca + hh * sa, sy + hw * sa - hh * ca};
  ImVec2 q3 = {sx + hw * ca - hh * sa, sy + hw * sa + hh * ca};
  ImVec2 q4 = {sx - hw * ca - hh * sa, sy - hw * sa + hh * ca};
  ImTextureRef tr = {NULL, tex};
  ImU32 tcol = igColorConvertFloat4ToU32((ImVec4){1, 1, 1, a});
  ImDrawList_AddImageQuad(dl, tr, q1, q2, q3, q4, (ImVec2){td->u0, td->v0},
                          (ImVec2){td->u1, td->v0}, (ImVec2){td->u1, td->v1},
                          (ImVec2){td->u0, td->v1}, tcol);
}
