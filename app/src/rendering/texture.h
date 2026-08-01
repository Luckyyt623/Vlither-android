#ifndef TEXTURE_H
#define TEXTURE_H

#include <thermite.h>
#include <stddef.h>

typedef struct texture {
  ivec2 size;
  VkImage image;
  VkImageView view;
  VmaAllocation memory;
} texture;

texture* create_mipmap_texture(tcontext* ctx, const char* filename);
texture* create_mipmap_texture_from_memory(tcontext* ctx,
                                            const unsigned char* encoded,
                                            size_t encoded_size);
texture* create_minimap_texture(tcontext* ctx, int width);
void destroy_texture(tcontext* ctx, texture* tex);

#endif
