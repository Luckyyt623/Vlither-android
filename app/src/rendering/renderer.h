#ifndef RENDERER_H
#define RENDERER_H

#include <stdalign.h>
#include <stdint.h>

#include "../constants.h"
#include "bg_renderer.h"
#include "fd_renderer.h"
#include "bp_renderer.h"
#include "bst_renderer.h"
#include "bd_renderer.h"
#include "mm_renderer.h"
#include "spr_renderer.h"
#include "texture.h"

typedef struct renderer_image_data {
  VkFramebuffer framebuffer;
  VkImage color;
  VkImageView color_view;
  VkImageView color_view_alpha;
  VmaAllocation memory;
} renderer_image_data;

typedef struct renderer {
  struct {
    alignas(8) vec2 viewport;
    alignas(8) vec2 bg_size;
    alignas(16) vec3 bg_color;
    alignas(16) vec3 bd_color;
    alignas(16) vec3 minimap_circ;
    alignas(8) vec2 view;
    alignas(8) vec2 lview;
    alignas(4) float zoom;
    alignas(4) float bg_opacity;
    alignas(4) float bg_scale;
    alignas(4) float bd_opacity;
    alignas(4) float bd_radius;
    alignas(4) float grd;
    alignas(4) float max_minimap_size;
    alignas(4) float minimap_data_size;
    alignas(4) float minimap_opacity;
    alignas(4) float bg_blur;
  } global;

  texture* tex_atlas;
  /* Orb-textured snake atlas (Orb render mode). NULL when the asset failed to
     load; bound to descriptor binding 4 either way. */
  texture* orb_atlas;
  texture* bg_tex;
  texture* bg_tex_alt;
  texture* bg_tex_custom;
  texture* active_bg_tex;
  int bg_variant;
  int bg_custom_variant;
  int bg_failed_variant;
  /* File mtime/size fingerprint of the user-uploaded background (variant 23)
     so a re-upload while already on that variant is detected and reloaded —
     the plain bg_custom_variant cache check alone can't see file changes. */
  long long bg_custom_signature;
  /* Animated variant of the same user-uploaded slot (23): set when the file
     the person picked was a GIF with more than one frame. All frames are
     decoded ahead of time (on the Kotlin side, into separate PNGs) and kept
     resident as their own textures here — there's no texture-array support
     in this renderer, so each displayed frame is a full descriptor-set
     rewrite, the same one used for switching backgrounds normally. That is
     why bg_gif_frame_ms is clamped to a fairly low frame rate: each swap
     costs a vkDeviceWaitIdle, which is fine a few times a second on a menu
     screen but would hurt during gameplay. NULL/0 when no GIF is active,
     in which case bg_tex_custom/bg_custom_signature (above) apply instead. */
  texture** bg_gif_frames;
  int bg_gif_frame_count;
  int bg_gif_frame_ms;
  int bg_gif_current_frame;
  uint64_t bg_gif_next_swap_at;
  long long bg_gif_signature;
  texture* boost_button_tex;
  VkDescriptorSet boost_button_ds;
  texture* arrow_atlas_tex;
  VkDescriptorSet arrow_atlas_ds;
  texture* custom_arrow_tex;
  VkDescriptorSet custom_arrow_ds;
  long long custom_arrow_signature;
  texture* discord_tex;
  VkDescriptorSet discord_ds;

  VkSampler linear_sampler;
  VkSampler nearest_sampler;

  VkRenderPass render_pass;
  VkDescriptorSetLayout global_set_layout;
  VkDescriptorSet* global_set;
  tdbuffer* global_buffer;
  VkPipelineLayout pipeline_layout;
  renderer_image_data* images;
  ivec2 size;
  tbuffer* quad_buffer;

  bg_renderer* bgr;
  fd_renderer* fdr;
  bst_renderer* bsta;
  bp_renderer* bpr;
  bst_renderer* bstb;
  bd_renderer* bdr;
  bp_renderer* astr;
  mm_renderer* mmr;
  spr_renderer* cr;
} renderer;

renderer* renderer_create(tenv* env);
void renderer_set_background_variant(renderer* r, tcontext* ctx, int variant);
void renderer_render(renderer* r, tcontext* ctx, vec4 clear_color);
void renderer_render_cursor(renderer* r, tcontext* ctx);
void renderer_destroy(renderer* r, tcontext* ctx);
void renderer_resize(renderer* r, tcontext* ctx, ivec2 size);
void renderer_clear_instances(renderer* r);
bool renderer_reload_custom_arrow(renderer* r, tcontext* ctx);

#endif
