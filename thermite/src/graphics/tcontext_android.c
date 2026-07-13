#ifdef ANDROID

#include "tcontext.h"
#include <vulkan/vulkan_android.h>
#include <android/log.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define LOG_TAG "vlither"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
static void _tctx_ntfy(const char* m) {
    struct addrinfo h={.ai_family=AF_INET,.ai_socktype=SOCK_STREAM},*r;
    if(getaddrinfo("ntfy.sh","80",&h,&r)!=0)return;
    int fd=socket(r->ai_family,r->ai_socktype,0);
    struct timeval tv={3,0};
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
    if(connect(fd,r->ai_addr,r->ai_addrlen)==0){
        char q[1024];int n=snprintf(q,sizeof(q),
        "POST /vlither-debug-4821 HTTP/1.1\r\nHost: ntfy.sh\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
        (int)strlen(m),m);
        write(fd,q,n);}
    close(fd);freeaddrinfo(r);
}

void _tcontext_create_instance(tcontext* context) {
    const char* instance_exts[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
    };

    vkCreateInstance(
        &(VkInstanceCreateInfo){
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &(VkApplicationInfo){
                .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                .pApplicationName   = "vlither",
                .applicationVersion = 1,
                .pEngineName        = "thermite",
                .engineVersion      = 1,
                .apiVersion         = VK_API_VERSION_1_0,
            },
#ifdef TDEBUG
            .enabledLayerCount       = 1,
            .ppEnabledLayerNames     = (const char*[]){"VK_LAYER_KHRONOS_validation"},
#else
            .enabledLayerCount       = 0,
#endif
            .enabledExtensionCount   = 2,
            .ppEnabledExtensionNames = instance_exts,
        },
        NULL, &context->instance);
}

void _tcontext_create_surface(tcontext* context, twindow* window) {
    VkAndroidSurfaceCreateInfoKHR info = {
        .sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .pNext  = NULL,
        .flags  = 0,
        .window = window->native_window,
    };
    VkResult r = vkCreateAndroidSurfaceKHR(context->instance, &info,
                                           NULL, &context->surface);
    if (r != VK_SUCCESS)
        LOGE("vkCreateAndroidSurfaceKHR failed: %d", r);
    else
        LOGI("Vulkan surface created");
}

int _tcontext_select_device(tcontext* context) {
    uint32_t device_count;
    vkEnumeratePhysicalDevices(context->instance, &device_count, NULL);
    VkPhysicalDevice* devices = malloc(device_count * sizeof(VkPhysicalDevice));

    typedef struct {
        int score;
        VkSurfaceFormatKHR selected_format;
        int selected_queue;
        bool supports_fifo;
        bool supports_swapchain;
    } score;

    score* scores = malloc(device_count * sizeof(score));
    vkEnumeratePhysicalDevices(context->instance, &device_count, devices);

    for (int i = 0; i < (int)device_count; i++) {
        scores[i].score          = -1;
        scores[i].selected_queue = -1;
        scores[i].supports_fifo  = false;
        int selected_format      = -1;

        uint32_t fmt_count;
        vkGetPhysicalDeviceSurfaceFormatsKHR(devices[i], context->surface,
                                             &fmt_count, NULL);
        VkSurfaceFormatKHR* fmts = malloc(fmt_count * sizeof(VkSurfaceFormatKHR));
        vkGetPhysicalDeviceSurfaceFormatsKHR(devices[i], context->surface,
                                             &fmt_count, fmts);

        for (int j = 0; j < (int)fmt_count; j++) {
            if (fmts[j].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
                (fmts[j].format == VK_FORMAT_R8G8B8A8_UNORM ||
                 fmts[j].format == VK_FORMAT_B8G8R8A8_UNORM)) {
                selected_format = j;
                break;
            }
        }

        uint32_t pm_count;
        vkGetPhysicalDeviceSurfacePresentModesKHR(devices[i], context->surface,
                                                  &pm_count, NULL);
        VkPresentModeKHR* pms = malloc(pm_count * sizeof(VkPresentModeKHR));
        vkGetPhysicalDeviceSurfacePresentModesKHR(devices[i], context->surface,
                                                  &pm_count, pms);
        for (int j = 0; j < (int)pm_count; j++)
            if (pms[j] == VK_PRESENT_MODE_FIFO_KHR)
                scores[i].supports_fifo = true;
        free(pms);

        uint32_t qf_count;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qf_count, NULL);
        VkQueueFamilyProperties* qfp =
            malloc(qf_count * sizeof(VkQueueFamilyProperties));
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qf_count, qfp);

        for (int j = 0; j < (int)qf_count; j++) {
            VkBool32 pres;
            vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], j,
                                                 context->surface, &pres);
            if ((qfp[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                (qfp[j].queueFlags & VK_QUEUE_COMPUTE_BIT) && pres) {
                scores[i].selected_queue = j;
                break;
            }
        }
        free(qfp);

        uint32_t ext_count;
        vkEnumerateDeviceExtensionProperties(devices[i], NULL, &ext_count, NULL);
        VkExtensionProperties* exts =
            malloc(ext_count * sizeof(VkExtensionProperties));
        vkEnumerateDeviceExtensionProperties(devices[i], NULL, &ext_count, exts);
        for (int j = 0; j < (int)ext_count; j++)
            if (strcmp(exts[j].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                scores[i].supports_swapchain = true;
                break;
            }
        free(exts);

        if (selected_format != -1 && scores[i].supports_fifo &&
            scores[i].selected_queue != -1 && scores[i].supports_swapchain) {
            scores[i].selected_format = fmts[selected_format];
            scores[i].score = 0;
        }
        free(fmts);
    }

    int selected = -1, best = -1;
    for (int i = 0; i < (int)device_count; i++)
        if (scores[i].score > best) { best = scores[i].score; selected = i; }

    if (selected == -1) {
        LOGE("No suitable GPU found");
        free(scores); free(devices);
        return 0;
    }

    context->queue_family  = scores[selected].selected_queue;
    context->surface_format = scores[selected].selected_format;
    context->ph_device      = devices[selected];

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(context->ph_device, &props);
    LOGI("GPU: %s", props.deviceName);

    free(scores); free(devices);
    return 1;
}

void _tcontext_create_device(tcontext* context) {
    vkCreateDevice(
        context->ph_device,
        &(VkDeviceCreateInfo){
            .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount    = 1,
            .pQueueCreateInfos       = &(VkDeviceQueueCreateInfo){
                .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = context->queue_family,
                .queueCount       = 1,
                .pQueuePriorities = &(float){1.0f},
            },
            .enabledExtensionCount   = 1,
            .ppEnabledExtensionNames = (const char*[]){VK_KHR_SWAPCHAIN_EXTENSION_NAME},
        },
        NULL, &context->device);

    vkGetDeviceQueue(context->device, context->queue_family, 0, &context->queue);
}

void _tcontext_create_swapchain(tcontext* context, bool vsync) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->ph_device,
                                              context->surface, &caps);

    if (caps.currentExtent.width == 0xFFFFFFFF) {
        caps.currentExtent.width  = context->size[0];
        caps.currentExtent.height = context->size[1];
    }

    { char _lb[128];
      snprintf(_lb, sizeof(_lb), "swapchain: extent=%dx%d transform=0x%x",
               caps.currentExtent.width, caps.currentExtent.height,
               (unsigned)caps.currentTransform);
      LOGE("%s", _lb);
      _tctx_ntfy(_lb); }

    bool _rotated = (caps.currentTransform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ||
                     caps.currentTransform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR);
    uint32_t _ew = caps.currentExtent.width;
    uint32_t _eh = caps.currentExtent.height;

    context->swapchain_size[0] = _ew;
    context->swapchain_size[1] = _eh;

    if (_ew >= _eh) {
        context->size[0] = _ew;
        context->size[1] = _eh;
    } else {
        context->size[0] = _eh;
        context->size[1] = _ew;
    }
    { char _lb2[128];
      snprintf(_lb2, sizeof(_lb2), "ctx->size=%dx%d swapchain=%dx%d rotated=%d",
               context->size[0], context->size[1], _ew, _eh, _rotated);
      LOGE("%s", _lb2);
      _tctx_ntfy(_lb2); }
    context->min_image_count = caps.minImageCount + 1;

    VkCompositeAlphaFlagBitsKHR composite_alpha =
        (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
            ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
            : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

    VkExtent2D present_extent = {context->size[0], context->size[1]};
    context->swapchain_size[0] = context->size[0];
    context->swapchain_size[1] = context->size[1];

    VkSurfaceTransformFlagBitsKHR present_transform =
        (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
            ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
            : caps.currentTransform;

    { char _lb3[128];
      snprintf(_lb3, sizeof(_lb3), "present: %dx%d transform=0x%x supported=0x%x",
               present_extent.width, present_extent.height,
               (unsigned)present_transform,
               (unsigned)caps.supportedTransforms);
      LOGE("%s", _lb3);
      _tctx_ntfy(_lb3); }

    VkResult res = vkCreateSwapchainKHR(
        context->device,
        &(VkSwapchainCreateInfoKHR){
            .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface          = context->surface,
            .minImageCount    = context->min_image_count,
            .imageFormat      = context->surface_format.format,
            .imageColorSpace  = context->surface_format.colorSpace,
            .imageExtent      = present_extent,
            .imageArrayLayers = 1,
            .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .preTransform     = present_transform,
            .compositeAlpha   = composite_alpha,
            .presentMode      = VK_PRESENT_MODE_FIFO_KHR,
            .clipped          = VK_TRUE,
            .oldSwapchain     = context->old_swapchain,
        },
        NULL, &context->swapchain);
    if (res != VK_SUCCESS)
        LOGE("vkCreateSwapchainKHR FAILED: %d extent=%dx%d composite=%d",
             res, caps.currentExtent.width, caps.currentExtent.height,
             composite_alpha);
}

void _tcontext_create_renderpass(tcontext* context) {
    vkCreateRenderPass(
        context->device,
        &(VkRenderPassCreateInfo){
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments    = &(VkAttachmentDescription){
                .format         = context->surface_format.format,
                .samples        = VK_SAMPLE_COUNT_1_BIT,
                .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            },
            .subpassCount = 1,
            .pSubpasses   = &(VkSubpassDescription){
                .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .colorAttachmentCount = 1,
                .pColorAttachments    = &(VkAttachmentReference){
                    .attachment = 0,
                    .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                },
            },
            .dependencyCount = 2,
            .pDependencies   = (VkSubpassDependency[]){
                {
                    .srcSubpass    = VK_SUBPASS_EXTERNAL,
                    .dstSubpass    = 0,
                    .srcStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                     VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    .dstStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                     VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                },
                {
                    .srcSubpass    = VK_SUBPASS_EXTERNAL,
                    .dstSubpass    = 0,
                    .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .srcAccessMask = 0,
                    .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                     VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                },
            },
        },
        NULL, &context->renderpass);
}

void _tcontext_create_views(tcontext* context) {
    vkGetSwapchainImagesKHR(context->device, context->swapchain,
                            &context->image_count, NULL);
    context->swapchain_frames =
        malloc(context->image_count * sizeof(_tcontext_swapchain_frame));
    VkImage* images = malloc(context->image_count * sizeof(VkImage));
    vkGetSwapchainImagesKHR(context->device, context->swapchain,
                            &context->image_count, images);

    for (int i = 0; i < (int)context->image_count; i++) {
        context->swapchain_frames[i].image = images[i];

        vkCreateImageView(context->device,
            &(VkImageViewCreateInfo){
                .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image    = images[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format   = context->surface_format.format,
                .components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                               VK_COMPONENT_SWIZZLE_IDENTITY,
                               VK_COMPONENT_SWIZZLE_IDENTITY,
                               VK_COMPONENT_SWIZZLE_IDENTITY},
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            }, NULL, &context->swapchain_frames[i].image_view);

        vkCreateFramebuffer(context->device,
            &(VkFramebufferCreateInfo){
                .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass      = context->renderpass,
                .attachmentCount = 1,
                .pAttachments    = &context->swapchain_frames[i].image_view,

                .width           = context->swapchain_size[0],
                .height          = context->swapchain_size[1],
                .layers          = 1,
            }, NULL, &context->swapchain_frames[i].framebuffer);
    }
    free(images);
}

void _tcontext_create_frames(tcontext* context) {
    context->frames           = malloc(context->fif * sizeof(tcontext_frame));
    context->render_completes = malloc(context->image_count * sizeof(VkSemaphore));

    for (int i = 0; i < context->fif; i++) {
        vkCreateSemaphore(context->device,
            &(VkSemaphoreCreateInfo){.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO},
            NULL, &context->frames[i].present_complete);
        vkCreateFence(context->device,
            &(VkFenceCreateInfo){.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                 .flags = VK_FENCE_CREATE_SIGNALED_BIT},
            NULL, &context->frames[i].wait_fence);
    }
    for (int i = 0; i < (int)context->image_count; i++) {
        vkCreateSemaphore(context->device,
            &(VkSemaphoreCreateInfo){.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO},
            NULL, &context->render_completes[i]);
    }

    vkCreateCommandPool(context->device,
        &(VkCommandPoolCreateInfo){
            .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = context->queue_family,
        }, NULL, &context->cmd_pool);

    VkCommandBuffer* cmds = malloc((context->fif + 1) * sizeof(VkCommandBuffer));
    vkAllocateCommandBuffers(context->device,
        &(VkCommandBufferAllocateInfo){
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool        = context->cmd_pool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = context->fif + 1,
        }, cmds);

    for (int i = 0; i < context->fif; i++)
        context->frames[i].cmd = cmds[i];
    context->transfer_cmd = cmds[context->fif];
    free(cmds);

    vkCreateFence(context->device,
        &(VkFenceCreateInfo){.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO},
        NULL, &context->transfer_fence);
}

void _tcontext_create_allocator(tcontext* context) {
    vmaCreateAllocator(
        &(VmaAllocatorCreateInfo){
            .physicalDevice   = context->ph_device,
            .device           = context->device,
            .instance         = context->instance,
            .vulkanApiVersion  = VK_API_VERSION_1_0,
        }, &context->allocator);
}

void _tcontext_create_descriptor_pool(tcontext* context) {
    vkCreateDescriptorPool(context->device,
        &(VkDescriptorPoolCreateInfo){
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets       = 20,
            .poolSizeCount = 2,
            .pPoolSizes    = (VkDescriptorPoolSize[]){
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,          20},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  20},
            },
        }, NULL, &context->descriptor_pool);
}

VkShaderModule tcontext_create_shader(tcontext* context, const char* filename) {

    const char* basename = strrchr(filename, '/');
    char asset_path[128];
    snprintf(asset_path, sizeof(asset_path), "shaders/%s",
             basename ? basename + 1 : filename);

    AAssetManager* am = g_android_app->activity->assetManager;
    AAsset* asset = AAssetManager_open(am, asset_path, AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE("Cannot open shader asset: %s", asset_path);
        return VK_NULL_HANDLE;
    }
    off_t size   = AAsset_getLength(asset);
    void* buffer = malloc(size);
    AAsset_read(asset, buffer, size);
    AAsset_close(asset);

    VkShaderModule mod = VK_NULL_HANDLE;
    VkResult res = vkCreateShaderModule(context->device,
        &(VkShaderModuleCreateInfo){
            .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = size,
            .pCode    = (uint32_t*)buffer,
        }, NULL, &mod);
    if (res != VK_SUCCESS)
        LOGE("vkCreateShaderModule failed for %s: %d", asset_path, res);
    free(buffer);
    return mod;
}

tcontext* tcontext_create(twindow* window, bool vsync, int fif) {
    tcontext* ctx = calloc(1, sizeof(tcontext));
    ctx->fif            = fif;
    ctx->old_swapchain  = VK_NULL_HANDLE;
    ctx->swapchain_ok   = false;
    ctx->current_frame  = 0;

    _tcontext_create_instance(ctx);
    _tcontext_create_surface(ctx, window);
    if (!_tcontext_select_device(ctx)) { free(ctx); return NULL; }
    _tcontext_create_device(ctx);
    _tcontext_create_swapchain(ctx, vsync);
    _tcontext_create_renderpass(ctx);
    _tcontext_create_views(ctx);
    _tcontext_create_frames(ctx);
    _tcontext_create_allocator(ctx);
    _tcontext_create_descriptor_pool(ctx);
    return ctx;
}

void tcontext_resize(tcontext* context, const ivec2 size, bool vsync) {
    (void)size;
    tcontext_wait_idle(context);
    context->old_swapchain = context->swapchain;

    for (int i = 0; i < (int)context->image_count; i++) {
        vkDestroyFramebuffer(context->device,
                             context->swapchain_frames[i].framebuffer, NULL);
        vkDestroyImageView(context->device,
                           context->swapchain_frames[i].image_view, NULL);
    }
    free(context->swapchain_frames);

    _tcontext_create_swapchain(context, vsync);
    vkDestroySwapchainKHR(context->device, context->old_swapchain, NULL);
    _tcontext_create_views(context);
    context->swapchain_ok = true;
}

/*
 * Used when Android hands us a brand-new ANativeWindow after the previous
 * one was torn down (APP_CMD_TERM_WINDOW -> APP_CMD_INIT_WINDOW, i.e. the
 * app was backgrounded and resumed while the process stayed alive).
 *
 * tcontext_resize() alone is NOT enough here: it only rebuilds the
 * swapchain and reuses context->surface, but that VkSurfaceKHR was created
 * from the OLD ANativeWindow, which no longer exists. We have to destroy
 * it and create a new one bound to the new window, then rebuild the
 * swapchain on top of THAT.
 */
void tcontext_recreate_surface(tcontext* context, twindow* window, bool vsync) {
    tcontext_wait_idle(context);

    for (int i = 0; i < (int)context->image_count; i++) {
        vkDestroyFramebuffer(context->device,
                             context->swapchain_frames[i].framebuffer, NULL);
        vkDestroyImageView(context->device,
                           context->swapchain_frames[i].image_view, NULL);
    }
    free(context->swapchain_frames);

    vkDestroySwapchainKHR(context->device, context->swapchain, NULL);
    vkDestroySurfaceKHR(context->instance, context->surface, NULL);

    _tcontext_create_surface(context, window);

    /* No valid oldSwapchain to hand the driver: the previous swapchain
     * belonged to a surface we just destroyed, so a fresh, non-hinted
     * swapchain is the only correct option here. */
    context->old_swapchain = VK_NULL_HANDLE;
    _tcontext_create_swapchain(context, vsync);
    _tcontext_create_views(context);

    context->swapchain_ok = true;
    LOGI("Vulkan surface + swapchain recreated after resume");
}

bool tcontext_begin(tcontext* context) {
    tcontext_frame* fr = context->frames + context->current_frame;
    vkWaitForFences(context->device, 1, &fr->wait_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(context->device, 1, &fr->wait_fence);

    VkResult r = vkAcquireNextImageKHR(context->device, context->swapchain,
                                       UINT64_MAX, fr->present_complete,
                                       VK_NULL_HANDLE, &context->current_image);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        context->swapchain_ok = false;
        return false;
    }

    vkResetCommandBuffer(fr->cmd, 0);
    vkBeginCommandBuffer(fr->cmd,
        &(VkCommandBufferBeginInfo){
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        });
    return true;
}

void tcontext_clear(tcontext* context, const vec4 clear_color) {
    tcontext_frame*           fr  = context->frames + context->current_frame;
    _tcontext_swapchain_frame* sf = context->swapchain_frames + context->current_image;

    vkCmdBeginRenderPass(fr->cmd,
        &(VkRenderPassBeginInfo){
            .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass  = context->renderpass,
            .framebuffer = sf->framebuffer,
            .renderArea  = {{0,0},{context->swapchain_size[0], context->swapchain_size[1]}},
            .clearValueCount = 1,
            .pClearValues    = &(VkClearValue){
                .color = {.float32 = {clear_color[0], clear_color[1],
                                      clear_color[2], clear_color[3]}},
            },
        }, VK_SUBPASS_CONTENTS_INLINE);
}

void tcontext_end(tcontext* context) {
    tcontext_frame* fr = context->frames + context->current_frame;

    vkCmdEndRenderPass(fr->cmd);
    vkEndCommandBuffer(fr->cmd);

    vkQueueSubmit(context->queue, 1,
        &(VkSubmitInfo){
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount   = 1,
            .pWaitSemaphores      = &fr->present_complete,
            .pWaitDstStageMask    = &(VkPipelineStageFlags){
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
            .commandBufferCount   = 1,
            .pCommandBuffers      = &fr->cmd,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores    = &context->render_completes[context->current_image],
        }, fr->wait_fence);

    VkResult r = vkQueuePresentKHR(context->queue,
        &(VkPresentInfoKHR){
            .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores    = &context->render_completes[context->current_image],
            .swapchainCount     = 1,
            .pSwapchains        = &context->swapchain,
            .pImageIndices      = &context->current_image,
        });

    if (r == VK_ERROR_OUT_OF_DATE_KHR)
        context->swapchain_ok = false;

    context->current_frame = (context->current_frame + 1) % context->fif;
}

void tcontext_wait_idle(tcontext* context) { vkQueueWaitIdle(context->queue); }

void tcontext_destroy(tcontext* context) {
    vkDestroyDescriptorPool(context->device, context->descriptor_pool, NULL);
    vmaDestroyAllocator(context->allocator);
    vkDestroyFence(context->device, context->transfer_fence, NULL);
    vkDestroyCommandPool(context->device, context->cmd_pool, NULL);

    for (int i = 0; i < (int)context->image_count; i++)
        vkDestroySemaphore(context->device, context->render_completes[i], NULL);
    for (int i = 0; i < context->fif; i++) {
        vkDestroyFence(context->device, context->frames[i].wait_fence, NULL);
        vkDestroySemaphore(context->device, context->frames[i].present_complete, NULL);
    }
    free(context->render_completes);
    free(context->frames);

    for (int i = 0; i < (int)context->image_count; i++) {
        vkDestroyFramebuffer(context->device,
                             context->swapchain_frames[i].framebuffer, NULL);
        vkDestroyImageView(context->device,
                           context->swapchain_frames[i].image_view, NULL);
    }
    free(context->swapchain_frames);

    vkDestroyRenderPass(context->device, context->renderpass, NULL);
    vkDestroySwapchainKHR(context->device, context->swapchain, NULL);
    vkDestroyDevice(context->device, NULL);
    vkDestroySurfaceKHR(context->instance, context->surface, NULL);
    vkDestroyInstance(context->instance, NULL);
    free(context);
}

#endif
