#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "base.c"

#ifdef VK_ENABLE_VALIDATION
static const bool is_validation_enabled = 1;
char const *validation_layer_names[] = {"VK_LAYER_KHRONOS_validation"};
const u32 validation_layer_count =
    sizeof(validation_layer_names) / sizeof(validation_layer_names[0]);
#else
static const bool is_validation_enabled = 0;
char const *validation_layer_names[] = {};
const u32 validation_layer_count = 0;
#endif

typedef struct HelloTriangleApplication {
  GLFWwindow *window;

  Arena *vulkan_arena;
  Arena *scratch_arena;

  VkInstance instance;
  VkSurfaceKHR surface;

  VkPhysicalDevice physical_device;
  u32 graphic_queue_index;
  VkDevice device;
  VkQueue graphic_queue;

  VkSwapchainKHR swapchain;
  VkSurfaceFormatKHR swapchain_format;
  VkExtent2D swap_extent;
  u32 swapchain_images_count;
  VkImage *swapchain_images;
  VkImageView *swapchain_image_views;

  VkPipelineLayout pipeline_layout;
  VkPipeline pipeline;

  VkCommandPool command_pool;
  VkCommandBuffer command_buffer;

  VkSemaphore present_sema;
  VkSemaphore render_sema;
  VkFence draw_fence;
} Application;

bool get_extensions(Arena *vulkan_arena, u32 *extension_count,
                    const char ***extension_names);
bool has_extension(u32 actual_count, VkExtensionProperties *actual_props,
                   const char *const expected);
bool get_layers(Arena *arena, u32 *layer_count, const char ***layer_properties);

bool read_shader_file(Arena *arena, const char *path, Str *out);

static int init_vulkan(Application *app);
static int init_glfw_window(Application *app);
static void cleanup(Application *app);
void transition_image_layout(Application *app, u32 imageIndex,
                             VkImageLayout old_layout, VkImageLayout new_layout,
                             VkAccessFlags2 src_access_mask,
                             VkAccessFlags2 dst_access_mask,
                             VkPipelineStageFlags2 src_stage_mask,
                             VkPipelineStageFlags2 dst_stage_mask);

//@ Init
static int init_vulkan(Application *app) {
  app->vulkan_arena = arena_create(ARENA_DEFAULT_BLOCK_SIZE);
  app->scratch_arena = arena_create(ARENA_DEFAULT_BLOCK_SIZE);
  app->instance = VK_NULL_HANDLE;
  app->surface = VK_NULL_HANDLE;
  app->physical_device = VK_NULL_HANDLE;
  app->device = VK_NULL_HANDLE;
  app->graphic_queue = VK_NULL_HANDLE;
  app->swapchain = VK_NULL_HANDLE;
  app->swapchain_images_count = 0;
  app->swapchain_images = VK_NULL_HANDLE;
  app->swapchain_image_views = VK_NULL_HANDLE;
  app->pipeline = VK_NULL_HANDLE;
  app->command_pool = VK_NULL_HANDLE;
  app->command_buffer = VK_NULL_HANDLE;
  app->present_sema = VK_NULL_HANDLE;
  app->render_sema = VK_NULL_HANDLE;
  app->draw_fence = VK_NULL_HANDLE;

  VkResult result = VK_SUCCESS;

  // @instance
  {
    ArenaTemp scratch = arena_temp_begin(app->scratch_arena);
    u32 extension_count;
    const char **extension_names = NULL;
    if (!get_extensions(scratch.arena, &extension_count, &extension_names)) {
      fprintf(stderr, "Vulkan error: failed to resolve instance extensions\n");
      cleanup(app);
      return -1;
    }

    u32 layer_count;
    const char **layer_names = NULL;
    if (!get_layers(scratch.arena, &layer_count, &layer_names)) {
      fprintf(stderr, "Vulkan error: failed to resolve validation layers\n");
      cleanup(app);
      return -1;
    }
    const VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Hello Triangle",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3};

    const VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extension_names,
        .enabledLayerCount = layer_count,
        .ppEnabledLayerNames = layer_names,
    };

    result = vkCreateInstance(&createInfo, NULL, &app->instance);

    if (result != VK_SUCCESS) {
      fprintf(stderr, "Vulkan error: failed to create instance\n");
      cleanup(app);
      return -1;
    }

    printf("vk::Instance created\n");
    arena_temp_end(scratch);
  }
  // @surface
  {
    result = glfwCreateWindowSurface(app->instance, app->window, NULL,
                                     &app->surface);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "GLFW error: failed to create a window surface");
      cleanup(app);
      return -1;
    }
    printf("vk::Surface created\n");
  }

  // @physical device
  u32 graphic_queue_index = UINT32_MAX;
  VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extended_dynamic_state_features =
      {.sType =
           VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
  VkPhysicalDeviceVulkan13Features vulkan13_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = &extended_dynamic_state_features};
  VkPhysicalDeviceFeatures2 device_features = (VkPhysicalDeviceFeatures2){
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &vulkan13_features};
  const char *required_device_extension_names[] = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  const u32 required_device_extension_count =
      sizeof(required_device_extension_names) /
      sizeof(required_device_extension_names[0]);
  {
    ArenaTemp scratch = arena_temp_begin(app->scratch_arena);
    u32 physical_device_count = 0;
    result =
        vkEnumeratePhysicalDevices(app->instance, &physical_device_count, NULL);
    if (result != VK_SUCCESS || physical_device_count == 0) {
      fprintf(stderr, "Vulkan error: no physical device found\n");
      cleanup(app);
      return -1;
    }
    VkPhysicalDevice *physical_devices = ARENA_PUSH_ARRAY(
        scratch.arena, physical_device_count, VkPhysicalDevice);
    result = vkEnumeratePhysicalDevices(app->instance, &physical_device_count,
                                        physical_devices);

    if (result != VK_SUCCESS) {
      fprintf(stderr, "Vulkan error: failed to enumerate physical devices\n");
      cleanup(app);
      return -1;
    }
    app->physical_device = physical_devices[0];

    // verify 1.3 support
    VkPhysicalDeviceProperties2 physical_device_properties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    vkGetPhysicalDeviceProperties2(app->physical_device,
                                   &physical_device_properties);
    if (physical_device_properties.properties.apiVersion < VK_API_VERSION_1_3) {
      fprintf(stderr, "Vulkan error: physical device do not support 1.3.");
      cleanup(app);
      return -1;
    }

    // verify graphic queue
    u32 physical_device_queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(
        app->physical_device, &physical_device_queue_count, NULL);
    VkQueueFamilyProperties *physical_device_queue_properties =
        ARENA_PUSH_ARRAY(scratch.arena, physical_device_queue_count,
                         VkQueueFamilyProperties);
    vkGetPhysicalDeviceQueueFamilyProperties(app->physical_device,
                                             &physical_device_queue_count,
                                             physical_device_queue_properties);
    VkBool32 supports_surface = false;
    for (u32 i = 0; i < physical_device_queue_count; ++i) {
      result = vkGetPhysicalDeviceSurfaceSupportKHR(
          app->physical_device, i, app->surface, &supports_surface);
      if (result != VK_SUCCESS) {
        fprintf(stderr, "Vulkan error: unable to test if physical device "
                        "support vk surface.");
        continue;
      }
      if (physical_device_queue_properties[i].queueFlags &
              VK_QUEUE_GRAPHICS_BIT &&
          supports_surface) {
        graphic_queue_index = i;
        break;
      }
    }
    if (graphic_queue_index == UINT32_MAX) {
      fprintf(stderr,
              "Vulkan error: physical device does not support graphic queue\n");
      cleanup(app);
      return -1;
    }

    // verify swapchain extension
    u32 extension_count = 0;
    vkEnumerateDeviceExtensionProperties(app->physical_device, NULL,
                                         &extension_count, NULL);
    VkExtensionProperties *extension_properties =
        ARENA_PUSH_ARRAY(scratch.arena, extension_count, VkExtensionProperties);
    vkEnumerateDeviceExtensionProperties(
        app->physical_device, NULL, &extension_count, extension_properties);

    bool supports_swapchain = false;
    for (u32 i = 0; i < required_device_extension_count; i++) {
      if (has_extension(extension_count, extension_properties,
                        required_device_extension_names[i])) {
        supports_swapchain = true;
        break;
      }
    }
    if (!supports_swapchain) {
      fprintf(stderr,
              "Vulkan error: physical device does not support swapchain\n");
      cleanup(app);
      return -1;
    }

    // verify dynamic rendering feature
    vkGetPhysicalDeviceFeatures2(app->physical_device, &device_features);

    if (!vulkan13_features.dynamicRendering ||
        !extended_dynamic_state_features.extendedDynamicState) {
      fprintf(stderr,
              "Vulkan error: physical device does not dynamic rendering\n");
      cleanup(app);
      return -1;
    }

    printf("vk::PhysicalDevice created\n");
    arena_temp_end(scratch);
  }

  // @logical device
  {
    f32 queue_priority = 1.f;
    VkDeviceQueueCreateInfo device_queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
        .queueFamilyIndex = graphic_queue_index};
    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &device_features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &device_queue_info,
        .enabledExtensionCount = required_device_extension_count,
        .ppEnabledExtensionNames = required_device_extension_names};
    result =
        vkCreateDevice(app->physical_device, &device_info, NULL, &app->device);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Vulkan error: failed to create logical device\n");
      cleanup(app);
      return -1;
    }
    vkGetDeviceQueue(app->device, graphic_queue_index, 0, &app->graphic_queue);

    printf("vk::LogicalDevice (and queue handle) created\n");
  }

  // @swapchain
  VkSurfaceFormatKHR swapchain_format;
  ArenaTemp swapchain_scratch = arena_temp_begin(app->scratch_arena);
  VkExtent2D swap_extent;
  {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app->physical_device,
                                              app->surface, &capabilities);
    swap_extent = capabilities.currentExtent;

    if (swap_extent.width == UINT32_MAX) {
      int w, h;
      glfwGetFramebufferSize(app->window, &w, &h);
      swap_extent = (VkExtent2D){
          .width = clamp((u32)w, capabilities.minImageExtent.width,
                         capabilities.maxImageExtent.width),
          .height = clamp((u32)h, capabilities.minImageExtent.height,
                          capabilities.maxImageExtent.height),
      };
    }

    u32 swap_image_count = max(3, capabilities.minImageCount);
    if (0 < capabilities.maxImageCount &&
        capabilities.maxImageCount < swap_image_count) {
      swap_image_count = capabilities.maxImageCount;
    }

    u32 formats_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(app->physical_device, app->surface,
                                         &formats_count, NULL);
    if (formats_count == 0) {
      fprintf(stderr, "Vulkan error: no present mode available\n");
      cleanup(app);
      return -1;
    }
    VkSurfaceFormatKHR *available_formats = ARENA_PUSH_ARRAY(
        swapchain_scratch.arena, formats_count, VkSurfaceFormatKHR);
    vkGetPhysicalDeviceSurfaceFormatsKHR(app->physical_device, app->surface,
                                         &formats_count, available_formats);

    u32 format_index = 0;
    for (u32 i = 0; i < formats_count; i++) {
      if (available_formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
          available_formats[i].colorSpace ==
              VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        format_index = i;
        break;
      }
    }
    swapchain_format = available_formats[format_index];

    u32 present_modes_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        app->physical_device, app->surface, &present_modes_count, NULL);
    if (present_modes_count == 0) {
      fprintf(stderr, "Vulkan error: no present mode available\n");
      cleanup(app);
      return -1;
    }
    VkPresentModeKHR *available_present_modes = ARENA_PUSH_ARRAY(
        swapchain_scratch.arena, present_modes_count, VkPresentModeKHR);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        app->physical_device, app->surface, &present_modes_count,
        available_present_modes);

    u32 present_mode_index = 0;
    for (u32 i = 0; i < present_modes_count; i++) {
      if (available_present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
        present_mode_index = i;
        break;
      }
      if (available_present_modes[i] == VK_PRESENT_MODE_FIFO_KHR) {
        present_mode_index = i;
      }
    }

    VkSwapchainCreateInfoKHR swapchain_create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = app->surface,
        .minImageCount = swap_image_count,
        .imageFormat = swapchain_format.format,
        .imageColorSpace = swapchain_format.colorSpace,
        .imageExtent = swap_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = available_present_modes[present_mode_index],
        .clipped = true};

    vkCreateSwapchainKHR(app->device, &swapchain_create_info, NULL,
                         &app->swapchain);
    vkGetSwapchainImagesKHR(app->device, app->swapchain,
                            &app->swapchain_images_count, NULL);
    app->swapchain_images = ARENA_PUSH_ARRAY(
        app->scratch_arena, app->swapchain_images_count, VkImage);
    vkGetSwapchainImagesKHR(app->device, app->swapchain,
                            &app->swapchain_images_count,
                            app->swapchain_images);

    printf("vk::Swapchain (and images) created\n");
  }

  // @image views
  {
    if (app->swapchain_images_count == 0) {
      fprintf(stderr, "Vulkan error: swapchain is empty\n");
      cleanup(app);
      return -1;
    }

    app->swapchain_image_views = ARENA_PUSH_ARRAY(
        app->vulkan_arena, app->swapchain_images_count, VkImageView);
    VkImageViewCreateInfo image_view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = swapchain_format.format,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel =
                                 0, // only mip 0 => image view for swapchain
                             .levelCount = 1,
                             .baseArrayLayer =
                                 0, // only layer 0 => texture 2D normal (not
                                    // cubemap or multiviewVR)
                             .layerCount = 1},
        // TODO experiment with swizzle
        .components = {
            .r = VK_COMPONENT_SWIZZLE_R,
            .g = VK_COMPONENT_SWIZZLE_G,
            .b = VK_COMPONENT_SWIZZLE_B,
            .a = VK_COMPONENT_SWIZZLE_A,
        }};

    for (u32 i = 0; i < app->swapchain_images_count; i++) {
      image_view_create_info.image = app->swapchain_images[i];
      result = vkCreateImageView(app->device, &image_view_create_info, NULL,
                                 &app->swapchain_image_views[i]);
      if (result != VK_SUCCESS) {
        fprintf(stderr,
                "Vulkan error: failed to created image view for swapchain "
                "image %u",
                i);
        cleanup(app);
        return -1;
      }
    }

    printf("vk::ImageView (for swapchain) created\n");
  }
  arena_temp_end(swapchain_scratch);

  // @render pipeline
  VkPipelineShaderStageCreateInfo shader_stage_infos[2];
  {
    ArenaTemp scratch = arena_temp_begin(app->scratch_arena);

    Str shader_code = {0};
    read_shader_file(scratch.arena, "./build/shaders/slang.spv", &shader_code);
    str_print_hex(&shader_code);

    VkShaderModule shader_module;
    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shader_code.length,
        .pCode = (const u32 *)shader_code.value};
    result =
        vkCreateShaderModule(app->device, &createInfo, NULL, &shader_module);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Vulkan error: Failed to create shader module\n");
      cleanup(app);
      return -1;
    }

    VkPipelineShaderStageCreateInfo vertex_stage_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = shader_module,
        .pName = "vertMain"};
    VkPipelineShaderStageCreateInfo fragment_stage_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = shader_module,
        .pName = "fragMain"};

    shader_stage_infos[0] = vertex_stage_info;
    shader_stage_infos[1] = fragment_stage_info;
    printf("%p\n", (void *)shader_stage_infos);

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                       VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states};

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};

    VkViewport viewport = {.x = 0.0f,
                           .y = 0.0f,
                           .width = (f32)swap_extent.width,
                           .height = (f32)swap_extent.height,
                           .minDepth = 0.0f,
                           .maxDepth = 1.0f};
    VkRect2D scissor = {.offset = (VkOffset2D){0, 0}, .extent = swap_extent};
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor};

    VkPipelineRasterizationStateCreateInfo raterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = false,
        .rasterizerDiscardEnable = false,
        .polygonMode = VK_POLYGON_MODE_FILL, // TODO test all the MODE
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = false,
        .lineWidth = 1.0f};

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = false};

    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .blendEnable = false,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo color_blend_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = false,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment};

    VkPipelineLayout pipeline_layout = NULL;
    VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0,
        .pushConstantRangeCount = 0};
    result = vkCreatePipelineLayout(app->device, &pipeline_layout_info, NULL,
                                    &pipeline_layout);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Vulkan error: Failed to create pipeline layout\n");
      cleanup(app);
      return -1;
    }
    VkPipelineRenderingCreateInfo pipeline_rendering_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &swapchain_format.format};
    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = shader_stage_infos,
        .pVertexInputState = &vertex_input_info,
        .pInputAssemblyState = &input_assembly_info,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raterizer,
        .pMultisampleState = &multisampling,
        .pColorBlendState = &color_blend_info,
        .pDynamicState = &dynamic_state,
        .layout = pipeline_layout,
        .renderPass = NULL,
        .pNext = &pipeline_rendering_info};

    result = vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1,
                                       &pipeline_info, NULL, &app->pipeline);
    arena_temp_end(scratch);
  }

  // @command pool & buffer
  {
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphic_queue_index};
    result =
        vkCreateCommandPool(app->device, &pool_info, NULL, &app->command_pool);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Vulkan error: Failed to create command pool\n");
      cleanup(app);
      return -1;
    }

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = app->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1};

    result = vkAllocateCommandBuffers(app->device, &alloc_info,
                                      &app->command_buffer);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Vulkan error: Failed to allocate command buffers\n");
      cleanup(app);
      return -1;
    }
  }

  // @sync object
  {
    result =
        vkCreateSemaphore(app->device,
                          &(VkSemaphoreCreateInfo){
                              .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                          },
                          NULL, &app->present_sema);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Vulkan error: Failed to create present semaphore\n");
      cleanup(app);
      return -1;
    }
    result =
        vkCreateSemaphore(app->device,
                          &(VkSemaphoreCreateInfo){
                              .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                          },
                          NULL, &app->render_sema);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Vulkan error: Failed to create render semaphore\n");
      cleanup(app);
      return -1;
    }

    result = vkCreateFence(app->device,
                           &(VkFenceCreateInfo){
                               .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                               .flags = VK_FENCE_CREATE_SIGNALED_BIT
                           },
                           NULL, &app->draw_fence);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Vulkan error: Failed to create draw fence\n");
      cleanup(app);
      return -1;
    }
  }

  return 0;
}

static void cleanup(Application *app) {
  if (app->command_buffer != VK_NULL_HANDLE) {
  }

  if (app->command_pool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(app->device, app->command_pool, NULL);
  }

  if (app->swapchain_image_views != VK_NULL_HANDLE) {
    for (u32 i = 0; i < app->swapchain_images_count; i++)
      vkDestroyImageView(app->device, app->swapchain_image_views[i], NULL);
  }

  if (app->swapchain != VK_NULL_HANDLE)
    vkDestroySwapchainKHR(app->device, app->swapchain, NULL);

  if (app->device != VK_NULL_HANDLE)
    vkDestroyDevice(app->device, NULL);

  if (app->surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(app->instance, app->surface, NULL);
  }

  if (app->instance != VK_NULL_HANDLE)
    vkDestroyInstance(app->instance, NULL);

  arena_destroy(app->vulkan_arena);
  arena_destroy(app->scratch_arena);

  if (app->window != NULL) {
    glfwDestroyWindow(app->window);
    app->window = NULL;
  }
  glfwTerminate();
}

void transition_image_layout(Application *app, u32 imageIndex,
                             VkImageLayout old_layout, VkImageLayout new_layout,
                             VkAccessFlags2 src_access_mask,
                             VkAccessFlags2 dst_access_mask,
                             VkPipelineStageFlags2 src_stage_mask,
                             VkPipelineStageFlags2 dst_stage_mask) {
  VkImageMemoryBarrier2 barrier = {
      .srcStageMask = src_stage_mask,
      .srcAccessMask = src_access_mask,
      .dstStageMask = dst_stage_mask,
      .dstAccessMask = dst_access_mask,
      .oldLayout = old_layout,
      .newLayout = new_layout,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = (app->swapchain_images)[imageIndex],
      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .baseMipLevel = 0,
                           .levelCount = 1,
                           .baseArrayLayer = 0,
                           .layerCount = 1}};

  VkDependencyInfo dependency_info = {.sType =
                                          VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                      .dependencyFlags = 0,
                                      .imageMemoryBarrierCount = 1,
                                      .pImageMemoryBarriers = &barrier};

  vkCmdPipelineBarrier2(app->command_buffer, &dependency_info);
}

static int init_glfw_window(Application *app) {
  if (!glfwInit()) {
    fprintf(stderr, "Failed to initialize GLFW\n");
    return -1;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  app->window = glfwCreateWindow(800, 600, "Vulkan", NULL, NULL);

  if (app->window == NULL) {
    fprintf(stderr, "Failed to create GLFW window\n");
    glfwTerminate();
    return -1;
  }

  printf("GLFW window created\n");

  return 0;
}

int draw_frame(Application *app) {
  // await for previous frame to be drawn
  VkResult result =
      vkWaitForFences(app->device, 1, &app->draw_fence, true, UINT64_MAX);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "Draw frame error: unable to wait for draw fence");
    return -1;
  }
  result = vkResetFences(app->device, 1, &app->draw_fence);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "Draw frame error: unable to reset draw fence");
    return -1;
  }

  // acquire next image from swapchain
  uint32_t image_index = 0;
  result = vkAcquireNextImageKHR(app->device, app->swapchain, UINT64_MAX,
                                 app->present_sema, NULL, &image_index);
  if (result != VK_SUCCESS) {
    fprintf(stderr,
            "Draw frame error: unable to acquire next image from swapchain");
    return -1;
  }

  // draw command recording
  vkBeginCommandBuffer(app->command_buffer,
                       &(VkCommandBufferBeginInfo){
                           .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                       });

  transition_image_layout(
      app,
      image_index, // todo change
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      (VkAccessFlags2){}, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
  VkRenderingAttachmentInfo attachment_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = app->swapchain_image_views[image_index],
      .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}}};
  VkRenderingInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {.offset = {0, 0}, .extent = app->swap_extent},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &attachment_info};

  vkCmdBeginRendering(app->command_buffer, &rendering_info);

  vkCmdBindPipeline(app->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    app->pipeline);

  vkCmdSetViewport(app->command_buffer, 0., 1.,
                   &(VkViewport){0., 0., (f32)app->swap_extent.width,
                                 (f32)app->swap_extent.width, 0., 1.});
  vkCmdSetScissor(app->command_buffer, 0., 1.,
                  &(VkRect2D){{0, 0}, app->swap_extent});

  vkCmdDraw(app->command_buffer, 3, 1, 0, 0);

  vkCmdEndRendering(app->command_buffer);

  transition_image_layout(
      app,
      image_index, // todo change
      VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, (VkAccessFlags2){},
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

  vkEndCommandBuffer(app->command_buffer);

  // submit command buffer to queue
  const VkPipelineStageFlags wait_dst_stage_mask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  const VkSubmitInfo submit_info = {

      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &app->present_sema,
      .pWaitDstStageMask = &wait_dst_stage_mask,
      .commandBufferCount = 1,
      .pCommandBuffers = &app->command_buffer,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = &app->render_sema};

  vkQueueSubmit(app->graphic_queue, 1, &submit_info, app->draw_fence);

  const VkPresentInfoKHR present_info = {.sType =
                                             VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                         .waitSemaphoreCount = 1,
                                         .pWaitSemaphores = &app->render_sema,
                                         .swapchainCount = 1,
                                         .pSwapchains = &app->swapchain,
                                         .pImageIndices = &image_index};
  result = vkQueuePresentKHR(app->graphic_queue, &present_info);
  if (result != VK_SUCCESS) {
    fprintf(stderr,
            "Draw frame error: unable to present image");
    return -1;
  }

  return 0;
}

//@ MainLoop
static void main_loop(Application *app) {
  printf("hi\n");
  while (!glfwWindowShouldClose(app->window)) {
    printf("start\n");
    // glfwPollEvents();
    draw_frame(app);
    printf("ha\n");
  }
  printf("hum\n");
}

int main(void) {
  Application app = {0};

  if (init_glfw_window(&app) != 0)
    return EXIT_FAILURE;

  if (init_vulkan(&app) != 0)
    return EXIT_FAILURE;

  printf("oi\n");
  main_loop(&app);
  cleanup(&app);

  return EXIT_SUCCESS;
}

bool has_extension(u32 actual_count, VkExtensionProperties *actual_props,
                   const char *const expected) {
  for (u32 j = 0; j < actual_count; ++j) {
    if (strcmp(expected, actual_props[j].extensionName) == 0) {
      printf("extension %s found.\n", expected);
      return true;
    }
  }
  fprintf(stderr, "Required extension not supported: %s\n", expected);
  return false;
}
// TODO improve error handling
bool get_extensions(Arena *arena, u32 *extension_count,
                    const char ***extension_names) {
  // actual ext of vk
  u32 vk_available_extension_count = 0;

  vkEnumerateInstanceExtensionProperties(NULL, &vk_available_extension_count,
                                         NULL);
  VkExtensionProperties *vk_available_extension_properties = ARENA_PUSH_ARRAY(
      arena, vk_available_extension_count, VkExtensionProperties);
  vkEnumerateInstanceExtensionProperties(NULL, &vk_available_extension_count,
                                         vk_available_extension_properties);

  // expected ext by glfw
  u32 glfw_extension_count = 0; // TODO change
  const char **glfw_extension_names =
      glfwGetRequiredInstanceExtensions(&glfw_extension_count);
  if (glfw_extension_names == NULL) {
    fprintf(stderr, "Failed to get GLFW required extensions\n");
    return false;
  }
  for (u32 i = 0; i < glfw_extension_count; ++i) {
    if (!has_extension(vk_available_extension_count,
                       vk_available_extension_properties,
                       (glfw_extension_names)[i])) {
      return false;
    }
  }

  // extra
  int extra_extension_count = 0;
  if (is_validation_enabled) {
    extra_extension_count++;
    if (!has_extension(vk_available_extension_count,
                       vk_available_extension_properties,
                       VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
      return false;
    }
  }

  *extension_count = glfw_extension_count + (u32)extra_extension_count;
  *extension_names = ARENA_PUSH_ARRAY(arena, *extension_count, const char *);
  memcpy(*extension_names, glfw_extension_names,
         sizeof(*glfw_extension_names) * glfw_extension_count);
  if (is_validation_enabled) {
    (*extension_names)[glfw_extension_count] =
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
  }

  return true;
}

bool has_layer(u32 actual_count, VkLayerProperties *actual_props,
               const char *const expected) {
  for (u32 i = 0; i < actual_count; i++) {
    if (strcmp(actual_props[i].layerName, expected) == 0) {
      printf("layer %s found.\n", expected);
      return true;
    }
  }
  fprintf(stderr, "Required layer do not exist: %s\n", expected);
  return false;
}
bool get_layers(Arena *arena, u32 *layer_count, const char ***layer_names) {
  u32 required_layer_count = 0;
  const char **required_layer_names = NULL;
  if (is_validation_enabled) {
    required_layer_count = validation_layer_count;
    required_layer_names =
        ARENA_PUSH_ARRAY(arena, required_layer_count, const char *);

    memcpy(required_layer_names, validation_layer_names,
           sizeof(validation_layer_names) * validation_layer_count);
  }

  u32 vk_layer_count;
  vkEnumerateInstanceLayerProperties(&vk_layer_count, NULL);
  VkLayerProperties *vk_layer_properties =
      ARENA_PUSH_ARRAY(arena, vk_layer_count, VkLayerProperties);
  vkEnumerateInstanceLayerProperties(&vk_layer_count, vk_layer_properties);

  for (u32 i = 0; i < required_layer_count; i++) {
    if (!has_layer(vk_layer_count, vk_layer_properties,
                   required_layer_names[i])) {
      return false;
    }
  }
  *layer_count = required_layer_count;
  *layer_names = required_layer_names;

  return true;
}

bool read_shader_file(Arena *arena, const char *path, Str *out) {
  FILE *file = fopen(path, "rb");

  if (file == NULL) {
    fprintf(stderr, "Failed to open shader file %s\n", path);
    return false;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fprintf(stderr, "Failed to seek shader file %s\n", path);
    fclose(file);
    return false;
  }

  long file_size = ftell(file);

  if (file_size < 0) {
    fprintf(stderr, "Failed to get shader file size: %s\n", path);
    fclose(file);
    return false;
  }

  rewind(file);

  u8 *buffer = ARENA_PUSH_ARRAY(arena, (size_t)file_size, u8);

  size_t read_size = fread(buffer, 1, (size_t)file_size, file);

  fclose(file);

  if (read_size != (size_t)file_size) {
    fprintf(stderr, "Failed to read shader file: %s\n", path);
    return false;
  }

  if (file_size % 4 != 0) {
    fprintf(stderr, "Shader byte code is not multiple of 4\n");
    return false;
  }

  out->length = (u64)file_size;
  out->value = buffer;

  return true;
}
