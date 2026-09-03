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
texture* create_mipmap_texture_from_rgba(tcontext* ctx,
                                         const unsigned char* rgba,
                                         int width, int height);
/* Non-blocking upload — see tcontext_async_transfer_busy() in tcontext.h.
   Don't sample the returned texture (create a descriptor set / swap it
   into active use) until that returns false. */
texture* create_mipmap_texture_from_rgba_async(tcontext* ctx,
                                               const unsigned char* rgba,
                                               int width, int height);
texture* create_mipmap_texture_from_memory(tcontext* ctx,
                                            const unsigned char* encoded,
                                            size_t encoded_size);
unsigned char* decode_texture_rgba_from_memory(const unsigned char* encoded,
                                                size_t encoded_size,
                                                int* width, int* height);
void free_texture_rgba(unsigned char* rgba);
texture* create_minimap_texture(tcontext* ctx, int width);
void destroy_texture(tcontext* ctx, texture* tex);

#endif
