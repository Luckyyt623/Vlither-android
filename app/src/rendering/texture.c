#include "texture.h"
#include <external/stb/stb_image.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stddef.h>

#ifdef ANDROID
#include "../android_jni.h"
#include <android/asset_manager.h>
#include <android_native_app_glue.h>
#include <android/log.h>
#define LOG_TAG "vlither"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
extern struct android_app* g_android_app;

static const char* _asset_path(const char* filename) {
    const char* prefix = "app/res/";
    if (strncmp(filename, prefix, 8) == 0)
        return filename + 8;
    return filename;
}

static stbi_uc* _load_from_asset(const char* filename, int* w, int* h, int* c) {
    AAssetManager* am = g_android_app->activity->assetManager;
    const char* path  = _asset_path(filename);
    AAsset* asset     = AAssetManager_open(am, path, AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE("Failed to open texture asset: %s", path);
        return NULL;
    }
    off_t len    = AAsset_getLength(asset);
    void* buf    = malloc(len);
    AAsset_read(asset, buf, len);
    AAsset_close(asset);
    stbi_uc* pixels = stbi_load_from_memory((stbi_uc*)buf, (int)len, w, h, c, 4);
    free(buf);
    if (!pixels) {
        /* stb_image has no WebP decoder. Android's BitmapFactory does, and
           the JNI bridge returns the same malloc/free-compatible RGBA layout
           expected by the Vulkan upload path. */
        pixels = android_jni_decode_asset_rgba(filename, w, h);
        if (pixels) *c = 4;
        else LOGE("Failed to decode texture asset: %s", path);
    }
    return pixels;
}
#endif

texture* create_mipmap_texture_from_rgba(tcontext* ctx,
                                         const unsigned char* data,
                                         int w, int h) {
  if (!ctx || !data || w <= 0 || h <= 0) return NULL;
  texture* r = malloc(sizeof(texture));
  if (!r) return NULL;

  VkBuffer staging_buffer;
  VmaAllocation staging_memory;
  VmaAllocationInfo staging_info;
  int mip_levels = (uint32_t)(floorf(log2f(GLM_MAX(w, h))) + 1);

  if (vmaCreateBuffer(
          ctx->allocator,
          &(VkBufferCreateInfo){.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                .pNext = NULL,
                                .flags = 0,
                                .size = (VkDeviceSize)w * (VkDeviceSize)h * 4,
                                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
          &(VmaAllocationCreateInfo){
              .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                       VMA_ALLOCATION_CREATE_MAPPED_BIT,
              .usage = VMA_MEMORY_USAGE_AUTO},
          &staging_buffer, &staging_memory, &staging_info) != VK_SUCCESS) {
    free(r);
    return NULL;
  }

  memcpy(staging_info.pMappedData, data, (size_t)w * (size_t)h * 4);

  if (vmaCreateImage(
          ctx->allocator,
          &(VkImageCreateInfo){.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                               .pNext = NULL,
                               .flags = 0,
                               .imageType = VK_IMAGE_TYPE_2D,
                               .format = VK_FORMAT_R8G8B8A8_UNORM,
                               .extent = {(uint32_t)w, (uint32_t)h, 1},
                               .mipLevels = (uint32_t)mip_levels,
                               .arrayLayers = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .tiling = VK_IMAGE_TILING_OPTIMAL,
                               .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                        VK_IMAGE_USAGE_SAMPLED_BIT,
                               .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                               .queueFamilyIndexCount = 0,
                               .pQueueFamilyIndices = NULL,
                               .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED},
          &(VmaAllocationCreateInfo){
              .flags = 0, .usage = VMA_MEMORY_USAGE_AUTO, .priority = 1.0f},
          &r->image, &r->memory, NULL) != VK_SUCCESS) {
    vmaDestroyBuffer(ctx->allocator, staging_buffer, staging_memory);
    free(r);
    return NULL;
  }

  vkResetCommandBuffer(ctx->transfer_cmd, 0);
  vkBeginCommandBuffer(ctx->transfer_cmd,
                       &(VkCommandBufferBeginInfo){
                           .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                           .pNext = NULL,
                           .flags = 0,
                           .pInheritanceInfo = NULL});
  vkCmdPipelineBarrier(
      ctx->transfer_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
      &(VkImageMemoryBarrier){
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = NULL,
          .srcAccessMask = VK_ACCESS_NONE,
          .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          .srcQueueFamilyIndex = ctx->queue_family,
          .dstQueueFamilyIndex = ctx->queue_family,
          .image = r->image,
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .baseMipLevel = 0,
                               .levelCount = 1,
                               .baseArrayLayer = 0,
                               .layerCount = 1}});
  vkCmdCopyBufferToImage(
      ctx->transfer_cmd, staging_buffer, r->image,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &(VkBufferImageCopy){
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .mipLevel = 0,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {(uint32_t)w, (uint32_t)h, 1}});

  vkCmdPipelineBarrier(
      ctx->transfer_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
      &(VkImageMemoryBarrier){
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = NULL,
          .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          .srcQueueFamilyIndex = ctx->queue_family,
          .dstQueueFamilyIndex = ctx->queue_family,
          .image = r->image,
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .baseMipLevel = 0,
                               .levelCount = 1,
                               .baseArrayLayer = 0,
                               .layerCount = 1}});

  for (int i = 1; i < mip_levels; i++) {
    vkCmdPipelineBarrier(
      ctx->transfer_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
      &(VkImageMemoryBarrier){
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = NULL,
          .srcAccessMask = VK_ACCESS_NONE,
          .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          .srcQueueFamilyIndex = ctx->queue_family,
          .dstQueueFamilyIndex = ctx->queue_family,
          .image = r->image,
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .baseMipLevel = (uint32_t)i,
                               .levelCount = 1,
                               .baseArrayLayer = 0,
                               .layerCount = 1}});

    int32_t src_w = GLM_MAX(1, w >> (i - 1));
    int32_t src_h = GLM_MAX(1, h >> (i - 1));
    int32_t dst_w = GLM_MAX(1, w >> i);
    int32_t dst_h = GLM_MAX(1, h >> i);
    vkCmdBlitImage(ctx->transfer_cmd, r->image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, r->image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &(VkImageBlit){
                     .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,
                                        (uint32_t)(i - 1), 0, 1},
                     .srcOffsets = {{0, 0, 0}, {src_w, src_h, 1}},
                     .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,
                                        (uint32_t)i, 0, 1},
                     .dstOffsets = {{0, 0, 0}, {dst_w, dst_h, 1}}
                   }, VK_FILTER_LINEAR);

    vkCmdPipelineBarrier(
      ctx->transfer_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
      &(VkImageMemoryBarrier){
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = NULL,
          .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          .srcQueueFamilyIndex = ctx->queue_family,
          .dstQueueFamilyIndex = ctx->queue_family,
          .image = r->image,
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .baseMipLevel = (uint32_t)i,
                               .levelCount = 1,
                               .baseArrayLayer = 0,
                               .layerCount = 1}});
  }

  vkCmdPipelineBarrier(
      ctx->transfer_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
      &(VkImageMemoryBarrier){
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = NULL,
          .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
          .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          .srcQueueFamilyIndex = ctx->queue_family,
          .dstQueueFamilyIndex = ctx->queue_family,
          .image = r->image,
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .baseMipLevel = 0,
                               .levelCount = (uint32_t)mip_levels,
                               .baseArrayLayer = 0,
                               .layerCount = 1}});

  vkEndCommandBuffer(ctx->transfer_cmd);
  vkQueueSubmit(ctx->queue, 1,
                &(VkSubmitInfo){.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                .pNext = NULL,
                                .commandBufferCount = 1,
                                .pCommandBuffers = &ctx->transfer_cmd},
                ctx->transfer_fence);
  vkWaitForFences(ctx->device, 1, &ctx->transfer_fence, VK_TRUE, UINT64_MAX);
  vkResetFences(ctx->device, 1, &ctx->transfer_fence);

  if (vkCreateImageView(
          ctx->device,
          &(VkImageViewCreateInfo){
              .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
              .pNext = NULL,
              .flags = 0,
              .image = r->image,
              .viewType = VK_IMAGE_VIEW_TYPE_2D,
              .format = VK_FORMAT_R8G8B8A8_UNORM,
              .components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY},
              .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                   .baseMipLevel = 0,
                                   .levelCount = (uint32_t)mip_levels,
                                   .baseArrayLayer = 0,
                                   .layerCount = 1}},
          NULL, &r->view) != VK_SUCCESS) {
    vmaDestroyImage(ctx->allocator, r->image, r->memory);
    vmaDestroyBuffer(ctx->allocator, staging_buffer, staging_memory);
    free(r);
    return NULL;
  }

  vmaDestroyBuffer(ctx->allocator, staging_buffer, staging_memory);
  r->size[0] = w;
  r->size[1] = h;
  return r;
}

/* Same upload as create_mipmap_texture_from_rgba(), except it submits on
   the dedicated async_transfer_cmd/fence and returns immediately instead
   of blocking on vkWaitForFences(UINT64_MAX). The returned texture's image
   + view are valid to hand to the caller right away (creating a view
   doesn't require the copy to have finished); what the caller must NOT do
   yet is start SAMPLING it (e.g. via igImplVulkan_AddTexture + swapping it
   into active use) until tcontext_async_transfer_busy(ctx) reports false,
   since that's the only way to know the GPU copy has actually landed. */
texture* create_mipmap_texture_from_rgba_async(tcontext* ctx,
                                               const unsigned char* data,
                                               int w, int h) {
  if (!ctx || !data || w <= 0 || h <= 0) return NULL;

  /* Only one async upload in flight at a time. If a previous one hasn't
     been reclaimed by tcontext_poll_async_transfer() yet (rare — e.g. two
     catalog refreshes landing the same frame), flush it here. This still
     avoids the *common* case of blocking every texture upload; it only
     blocks on the unusual case of back-to-back async uploads. */
  if (ctx->async_transfer_pending) {
    vkWaitForFences(ctx->device, 1, &ctx->async_transfer_fence, VK_TRUE,
                    UINT64_MAX);
    vmaDestroyBuffer(ctx->allocator, ctx->async_staging_buffer,
                     ctx->async_staging_memory);
    vkResetFences(ctx->device, 1, &ctx->async_transfer_fence);
    ctx->async_transfer_pending = false;
  }

  texture* r = malloc(sizeof(texture));
  if (!r) return NULL;

  VkBuffer staging_buffer;
  VmaAllocation staging_memory;
  VmaAllocationInfo staging_info;
  int mip_levels = (uint32_t)(floorf(log2f(GLM_MAX(w, h))) + 1);

  if (vmaCreateBuffer(
          ctx->allocator,
          &(VkBufferCreateInfo){.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                .pNext = NULL,
                                .flags = 0,
                                .size = (VkDeviceSize)w * (VkDeviceSize)h * 4,
                                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
          &(VmaAllocationCreateInfo){
              .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                       VMA_ALLOCATION_CREATE_MAPPED_BIT,
              .usage = VMA_MEMORY_USAGE_AUTO},
          &staging_buffer, &staging_memory, &staging_info) != VK_SUCCESS) {
    free(r);
    return NULL;
  }

  memcpy(staging_info.pMappedData, data, (size_t)w * (size_t)h * 4);

  if (vmaCreateImage(
          ctx->allocator,
          &(VkImageCreateInfo){.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                               .pNext = NULL,
                               .flags = 0,
                               .imageType = VK_IMAGE_TYPE_2D,
                               .format = VK_FORMAT_R8G8B8A8_UNORM,
                               .extent = {(uint32_t)w, (uint32_t)h, 1},
                               .mipLevels = (uint32_t)mip_levels,
                               .arrayLayers = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .tiling = VK_IMAGE_TILING_OPTIMAL,
                               .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                        VK_IMAGE_USAGE_SAMPLED_BIT,
                               .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                               .queueFamilyIndexCount = 0,
                               .pQueueFamilyIndices = NULL,
                               .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED},
          &(VmaAllocationCreateInfo){
              .flags = 0, .usage = VMA_MEMORY_USAGE_AUTO, .priority = 1.0f},
          &r->image, &r->memory, NULL) != VK_SUCCESS) {
    vmaDestroyBuffer(ctx->allocator, staging_buffer, staging_memory);
    free(r);
    return NULL;
  }

  vkResetCommandBuffer(ctx->async_transfer_cmd, 0);
  vkBeginCommandBuffer(ctx->async_transfer_cmd,
                       &(VkCommandBufferBeginInfo){
                           .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                           .pNext = NULL,
                           .flags = 0,
                           .pInheritanceInfo = NULL});
  vkCmdPipelineBarrier(
      ctx->async_transfer_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
      &(VkImageMemoryBarrier){
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = NULL,
          .srcAccessMask = VK_ACCESS_NONE,
          .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          .srcQueueFamilyIndex = ctx->queue_family,
          .dstQueueFamilyIndex = ctx->queue_family,
          .image = r->image,
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .baseMipLevel = 0,
                               .levelCount = 1,
                               .baseArrayLayer = 0,
                               .layerCount = 1}});
  vkCmdCopyBufferToImage(
      ctx->async_transfer_cmd, staging_buffer, r->image,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &(VkBufferImageCopy){
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .mipLevel = 0,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {(uint32_t)w, (uint32_t)h, 1}});

  vkCmdPipelineBarrier(
      ctx->async_transfer_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
      &(VkImageMemoryBarrier){
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = NULL,
          .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          .srcQueueFamilyIndex = ctx->queue_family,
          .dstQueueFamilyIndex = ctx->queue_family,
          .image = r->image,
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .baseMipLevel = 0,
                               .levelCount = 1,
                               .baseArrayLayer = 0,
                               .layerCount = 1}});

  for (int i = 1; i < mip_levels; i++) {
    vkCmdPipelineBarrier(
      ctx->async_transfer_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
      &(VkImageMemoryBarrier){
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = NULL,
          .srcAccessMask = VK_ACCESS_NONE,
          .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          .srcQueueFamilyIndex = ctx->queue_family,
          .dstQueueFamilyIndex = ctx->queue_family,
          .image = r->image,
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .baseMipLevel = (uint32_t)i,
                               .levelCount = 1,
                               .baseArrayLayer = 0,
                               .layerCount = 1}});

    int32_t src_w = GLM_MAX(1, w >> (i - 1));
    int32_t src_h = GLM_MAX(1, h >> (i - 1));
    int32_t dst_w = GLM_MAX(1, w >> i);
    int32_t dst_h = GLM_MAX(1, h >> i);
    vkCmdBlitImage(ctx->async_transfer_cmd, r->image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, r->image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &(VkImageBlit){
                     .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,
                                        (uint32_t)(i - 1), 0, 1},
                     .srcOffsets = {{0, 0, 0}, {src_w, src_h, 1}},
                     .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,
                                        (uint32_t)i, 0, 1},
                     .dstOffsets = {{0, 0, 0}, {dst_w, dst_h, 1}}
                   }, VK_FILTER_LINEAR);

    vkCmdPipelineBarrier(
      ctx->async_transfer_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
      &(VkImageMemoryBarrier){
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = NULL,
          .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          .srcQueueFamilyIndex = ctx->queue_family,
          .dstQueueFamilyIndex = ctx->queue_family,
          .image = r->image,
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .baseMipLevel = (uint32_t)i,
                               .levelCount = 1,
                               .baseArrayLayer = 0,
                               .layerCount = 1}});
  }

  vkCmdPipelineBarrier(
      ctx->async_transfer_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
      &(VkImageMemoryBarrier){
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = NULL,
          .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
          .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          .srcQueueFamilyIndex = ctx->queue_family,
          .dstQueueFamilyIndex = ctx->queue_family,
          .image = r->image,
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .baseMipLevel = 0,
                               .levelCount = (uint32_t)mip_levels,
                               .baseArrayLayer = 0,
                               .layerCount = 1}});

  vkEndCommandBuffer(ctx->async_transfer_cmd);
  vkQueueSubmit(ctx->queue, 1,
                &(VkSubmitInfo){.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                .pNext = NULL,
                                .commandBufferCount = 1,
                                .pCommandBuffers = &ctx->async_transfer_cmd},
                ctx->async_transfer_fence);

  /* No wait here — this is the whole point. The staging buffer stays
     alive (owned by ctx) until tcontext_poll_async_transfer() sees the
     fence signal and frees it. */
  ctx->async_staging_buffer = staging_buffer;
  ctx->async_staging_memory = staging_memory;
  ctx->async_transfer_pending = true;

  if (vkCreateImageView(
          ctx->device,
          &(VkImageViewCreateInfo){
              .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
              .pNext = NULL,
              .flags = 0,
              .image = r->image,
              .viewType = VK_IMAGE_VIEW_TYPE_2D,
              .format = VK_FORMAT_R8G8B8A8_UNORM,
              .components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY,
                             VK_COMPONENT_SWIZZLE_IDENTITY},
              .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                   .baseMipLevel = 0,
                                   .levelCount = (uint32_t)mip_levels,
                                   .baseArrayLayer = 0,
                                   .layerCount = 1}},
          NULL, &r->view) != VK_SUCCESS) {
    /* Can't safely tear down the image/staging here — the GPU may still
       be mid-copy into it. Let it leak this one time rather than corrupt
       an in-flight submission; this path only triggers on driver-level
       view-creation failure, which is already an abnormal condition. */
    r->view = VK_NULL_HANDLE;
  }

  r->size[0] = w;
  r->size[1] = h;
  return r;
}

texture* create_mipmap_texture(tcontext* ctx, const char* filename) {
  int w = 0, h = 0, c = 0;
#ifdef ANDROID
  stbi_uc* data = filename && filename[0] == '/'
                      ? stbi_load(filename, &w, &h, &c, 4)
                      : _load_from_asset(filename, &w, &h, &c);
#else
  stbi_uc* data = stbi_load(filename, &w, &h, &c, 4);
#endif
  if (!data) return NULL;
  texture* result = create_mipmap_texture_from_rgba(ctx, data, w, h);
  stbi_image_free(data);
  return result;
}

texture* create_mipmap_texture_from_memory(tcontext* ctx,
                                            const unsigned char* encoded,
                                            size_t encoded_size) {
  int w = 0, h = 0;
  unsigned char* data = decode_texture_rgba_from_memory(
      encoded, encoded_size, &w, &h);
  if (!data) return NULL;
  texture* result = create_mipmap_texture_from_rgba(ctx, data, w, h);
  free_texture_rgba(data);
  return result;
}

unsigned char* decode_texture_rgba_from_memory(const unsigned char* encoded,
                                                size_t encoded_size,
                                                int* width, int* height) {
  if (width) *width = 0;
  if (height) *height = 0;
  if (!encoded || encoded_size == 0 || encoded_size > INT_MAX ||
      !width || !height)
    return NULL;
  int w = 0, h = 0, c = 0;
  stbi_uc* data = stbi_load_from_memory(encoded, (int)encoded_size,
                                        &w, &h, &c, 4);
#ifdef ANDROID
  if (!data) data = android_jni_decode_image_rgba(encoded, encoded_size, &w, &h);
#endif
  if (!data || w <= 0 || h <= 0) return NULL;
  *width = w;
  *height = h;
  return data;
}

void free_texture_rgba(unsigned char* rgba) {
  stbi_image_free(rgba);
}

texture* create_minimap_texture(tcontext* ctx, int width) {
  texture* r = malloc(sizeof(texture));
  vmaCreateImage(
      ctx->allocator,
      &(VkImageCreateInfo){
          .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
          .pNext = NULL,
          .flags = 0,
          .imageType = VK_IMAGE_TYPE_2D,
          .format = VK_FORMAT_R8_UNORM,
          .extent = {width, width, 1},
          .mipLevels = 1,
          .arrayLayers = 1,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .tiling = VK_IMAGE_TILING_OPTIMAL,
          .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
          .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
          .queueFamilyIndexCount = 0,
          .pQueueFamilyIndices = NULL,
          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED},
      &(VmaAllocationCreateInfo){
          .flags = 0, .usage = VMA_MEMORY_USAGE_AUTO, .priority = 1.0f},
      &r->image, &r->memory, NULL);

  vkResetCommandBuffer(ctx->transfer_cmd, 0);
  vkBeginCommandBuffer(ctx->transfer_cmd,
                       &(VkCommandBufferBeginInfo){
                           .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                           .pNext = NULL,
                           .flags = 0,
                           .pInheritanceInfo = NULL});
  vkCmdPipelineBarrier(
      ctx->transfer_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
      &(VkImageMemoryBarrier){
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = NULL,
          .srcAccessMask = VK_ACCESS_NONE,
          .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          .srcQueueFamilyIndex = ctx->queue_family,
          .dstQueueFamilyIndex = ctx->queue_family,
          .image = r->image,
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .baseMipLevel = 0,
                               .levelCount = 1,
                               .baseArrayLayer = 0,
                               .layerCount = 1}});

  vkEndCommandBuffer(ctx->transfer_cmd);

  vkQueueSubmit(ctx->queue, 1,
                &(VkSubmitInfo){.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                .pNext = NULL,
                                .commandBufferCount = 1,
                                .pCommandBuffers = &ctx->transfer_cmd},
                ctx->transfer_fence);

  vkWaitForFences(ctx->device, 1, &ctx->transfer_fence, VK_TRUE, UINT64_MAX);
  vkResetFences(ctx->device, 1, &ctx->transfer_fence);

  vkCreateImageView(
      ctx->device,
      &(VkImageViewCreateInfo){
          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .pNext = NULL,
          .flags = 0,
          .image = r->image,
          .viewType = VK_IMAGE_VIEW_TYPE_2D,
          .format = VK_FORMAT_R8_UNORM,
          .components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                         VK_COMPONENT_SWIZZLE_IDENTITY,
                         VK_COMPONENT_SWIZZLE_IDENTITY,
                         VK_COMPONENT_SWIZZLE_IDENTITY},
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .baseMipLevel = 0,
                               .levelCount = 1,
                               .baseArrayLayer = 0,
                               .layerCount = 1}},
      NULL, &r->view);

  r->size[0] = width;
  r->size[1] = width;

  return r;
}

void destroy_texture(tcontext* ctx, texture* tex) {
  vkDestroyImageView(ctx->device, tex->view, NULL);
  vmaDestroyImage(ctx->allocator, tex->image, tex->memory);

  free(tex);
}
