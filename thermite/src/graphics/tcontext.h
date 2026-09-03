#ifndef TCONTEXT_H
#define TCONTEXT_H

#include <vulkan/vulkan.h>
#include <stdbool.h>

#include "../external/vma/vma.h"
#include "../framework/twindow.h"

#ifdef ANDROID
#include <android_native_app_glue.h>

extern struct android_app* g_android_app;
#endif

typedef struct tcontext_frame {
  VkSemaphore     present_complete;
  VkFence         wait_fence;
  VkCommandBuffer cmd;
} tcontext_frame;

typedef struct _tcontext_swapchain_frame {
  VkImage       image;
  VkImageView   image_view;
  VkFramebuffer framebuffer;
} _tcontext_swapchain_frame;

typedef struct tcontext {
  VkInstance       instance;
  VkSurfaceKHR     surface;
  VkPhysicalDevice ph_device;
  VkDevice         device;
  VkQueue          queue;
  VkSwapchainKHR   old_swapchain;
  VkSwapchainKHR   swapchain;
  VkDescriptorPool descriptor_pool;

  int   fif;
  int   current_frame;
  bool  swapchain_ok;

  VkCommandPool    cmd_pool;
  tcontext_frame*  frames;
  VkSemaphore*     render_completes;
  VkCommandBuffer  transfer_cmd;
  VkFence          transfer_fence;

  /* Separate from transfer_cmd/transfer_fence above (which callers wait on
     synchronously). This pair backs create_*_async() texture uploads: the
     submit happens here and the caller returns immediately without
     blocking; tcontext_poll_async_transfer() reclaims the staging buffer
     once the GPU actually finishes, checked non-blockingly once a frame. */
  VkCommandBuffer  async_transfer_cmd;
  VkFence          async_transfer_fence;
  bool             async_transfer_pending;
  VkBuffer         async_staging_buffer;
  VmaAllocation    async_staging_memory;

  uint32_t                   image_count;
  uint32_t                   current_image;
  uint32_t                   min_image_count;
  _tcontext_swapchain_frame* swapchain_frames;
  VkRenderPass               renderpass;

  VmaAllocator  allocator;

  uint32_t          queue_family;
  VkSurfaceFormatKHR surface_format;
  ivec2             size;
  ivec2             swapchain_size;
  bool              supports_immediate;
} tcontext;

VkShaderModule tcontext_create_shader(tcontext* context, const char* filename);
tcontext*      tcontext_create(twindow* window, bool vsync, int fif);
void           tcontext_resize(tcontext* context, const ivec2 size, bool vsync);
void           tcontext_recreate_surface(tcontext* context, twindow* window, bool vsync);
bool           tcontext_begin(tcontext* context);
void           tcontext_clear(tcontext* context, const vec4 clear_color);
void           tcontext_end(tcontext* context);
void           tcontext_wait_idle(tcontext* context);
void           tcontext_destroy(tcontext* context);

/* Non-blocking: call once per frame (e.g. from main.c's frame loop). Frees
   the pending async staging buffer once its GPU transfer has finished. */
void           tcontext_poll_async_transfer(tcontext* context);
/* True while an async upload's staging buffer is still owned by the GPU. */
bool           tcontext_async_transfer_busy(tcontext* context);

#endif
