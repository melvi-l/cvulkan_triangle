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

#define VKTRY(expr, msg)                                                       \
  do {                                                                         \
    VkResult vk_result__ = (expr);                                             \
    if (vk_result__ != VK_SUCCESS) {                                           \
      fprintf(stderr, "%s: VkResult=%d\n", msg, vk_result__);                  \
      return -1;                                                               \
    }                                                                          \
  } while (0)

#define MAX_FRAMES_IN_FLIGHT 2

typedef struct Application {
  GLFWwindow *window;

  Arena *vulkan_arena;
  Arena *scratch_arena;

  VkInstance instance;
  VkSurfaceKHR surface;

  u32 inflight_count;
  u32 frame_index;

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

  VkSemaphore *image_available_semas;
  VkSemaphore *render_finish_semas;
  VkFence *draw_fences;
} Application;

bool get_extensions(Arena *vulkan_arena, u32 *extension_count,
                    const char ***extension_names);
bool has_extension(u32 actual_count, VkExtensionProperties *actual_props,
                   const char *const expected);
bool get_layers(Arena *arena, u32 *layer_count, const char ***layer_properties);

bool read_shader_file(Arena *arena, const char *path, Str *out);

static int init_vulkan(Application *app);
int create_swapchain(Arena *arena, Application *app,
                     VkSurfaceFormatKHR *swapchain_format);
static int init_glfw_window(Application *app);
static void cleanup(Application *app);
void transition_image_layout(Application *app, VkCommandBuffer command_buffer,
                             u32 image_index, VkImageLayout old_layout,
                             VkImageLayout new_layout,
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
  app->inflight_count = MAX_FRAMES_IN_FLIGHT;
  app->frame_index = 0;
  app->physical_device = VK_NULL_HANDLE;
  app->device = VK_NULL_HANDLE;
  app->graphic_queue_index = UINT32_MAX;
  app->graphic_queue = VK_NULL_HANDLE;
  app->swapchain = VK_NULL_HANDLE;
  app->swapchain_images_count = 0;
  app->swapchain_images = VK_NULL_HANDLE;
  app->swapchain_image_views = VK_NULL_HANDLE;
  app->pipeline = VK_NULL_HANDLE;
  app->vertex_buffer = VK_NULL_HANDLE;
  app->vertex_buffer_memory = VK_NULL_HANDLE;
  app->graphic_command_pool = VK_NULL_HANDLE;
  app->graphic_command_buffers = NULL;
  app->image_available_semas = NULL;
  app->render_finish_semas = NULL;
  app->draw_fences = NULL;

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

    VKTRY(vkCreateInstance(&createInfo, NULL, &app->instance),
          "Vulkan error: failed to create instance");

    printf("vk::Instance created\n");
    arena_temp_end(scratch);
  }
  // @surface
  {
    VKTRY(glfwCreateWindowSurface(app->instance, app->window, NULL,
                                  &app->surface),
          "GLFW error: failed to create a window surface");
    printf("vk::Surface created\n");
  }

  // @physical device
  VkPhysicalDeviceVulkan11Features vulkan11_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
      .shaderDrawParameters = true,
  };
  VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extended_dynamic_state_features =
      {.sType =
           VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT,
       .pNext = &vulkan11_features};
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
    VKTRY(
        vkEnumeratePhysicalDevices(app->instance, &physical_device_count, NULL),
        "Vulkan error: no physical device found");
    VkPhysicalDevice *physical_devices = ARENA_PUSH_ARRAY(
        scratch.arena, physical_device_count, VkPhysicalDevice);

    VKTRY(vkEnumeratePhysicalDevices(app->instance, &physical_device_count,
                                     physical_devices),
          "Vulkan error: failed to enumerate physical devices");
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
      VKTRY(vkGetPhysicalDeviceSurfaceSupportKHR(
                app->physical_device, i, app->surface, &supports_surface),
            "Vulkan error: unable to test if physical device");
      if (physical_device_queue_properties[i].queueFlags &
              VK_QUEUE_GRAPHICS_BIT &&
          supports_surface) {
        app->graphic_queue_index = i;
        break;
      }
    }
    if (graphic_queue_index == UINT32_MAX) {
    if (app->graphic_queue_index == UINT32_MAX) {
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
        .queueFamilyIndex = app->graphic_queue_index};
    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &device_features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &device_queue_info,
        .enabledExtensionCount = required_device_extension_count,
        .ppEnabledExtensionNames = required_device_extension_names};
    VKTRY(
        vkCreateDevice(app->physical_device, &device_info, NULL, &app->device),
        "Vulkan error: failed to create logical device");
    vkGetDeviceQueue(app->device, app->graphic_queue_index, 0,
                     &app->graphic_queue);

    printf("vk::LogicalDevice (and queue handle) created\n");
  }

  // @swapchain
  VkSurfaceFormatKHR swapchain_format;
  ArenaTemp swapchain_scratch = arena_temp_begin(app->scratch_arena);
  create_swapchain(swapchain_scratch.arena, app, &swapchain_format);
  arena_temp_end(swapchain_scratch);
  printf("vk::Swapchain (and images) created\n");
  printf("vk::ImageView (for swapchain) created\n");

  // @image views
  {
  }

  // @render pipeline
  VkPipelineShaderStageCreateInfo shader_stage_infos[2];
  {
    ArenaTemp scratch = arena_temp_begin(app->scratch_arena);

    Str shader_code = {0};
    read_shader_file(scratch.arena, "./build/shaders/slang.spv", &shader_code);
    // str_print_hex(&shader_code);

    VkShaderModule shader_module;
    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shader_code.length,
        .pCode = (const u32 *)shader_code.value};
    VKTRY(vkCreateShaderModule(app->device, &createInfo, NULL, &shader_module),
          "Vulkan error: Failed to create shader module");

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
                           .width = (f32)app->swap_extent.width,
                           .height = (f32)app->swap_extent.height,
                           .minDepth = 0.0f,
                           .maxDepth = 1.0f};
    VkRect2D scissor = {.offset = (VkOffset2D){0, 0},
                        .extent = app->swap_extent};
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
    VKTRY(vkCreatePipelineLayout(app->device, &pipeline_layout_info, NULL,
                                 &pipeline_layout),
          "Vulkan error: Failed to create pipeline layout");
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

    VKTRY(vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1,
                                    &pipeline_info, NULL, &app->pipeline),
          "Vulkan error: Failed to create graphics pipeline");
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
        .queueFamilyIndex = app->graphic_queue_index};
    VKTRY(
        vkCreateCommandPool(app->device, &pool_info, NULL, &app->graphic_command_pool),
        "Vulkan error: Failed to create command pool");

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = app->graphic_command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = app->inflight_count};

    result = vkAllocateCommandBuffers(app->device, &alloc_info,
                                      &app->command_buffer);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Vulkan error: Failed to allocate command buffers\n");
      cleanup(app);
      return -1;
    }
    app->graphic_command_buffers = ARENA_PUSH_ARRAY(
        app->vulkan_arena, app->inflight_count, VkCommandBuffer);
    VKTRY(vkAllocateCommandBuffers(app->device, &alloc_info,
                                   app->graphic_command_buffers),
          "Vulkan error: Failed to allocate command buffers");
    app->graphic_command_buffers = ARENA_PUSH_ARRAY(
        app->vulkan_arena, app->inflight_count, VkCommandBuffer);
    VKTRY(vkAllocateCommandBuffers(app->device, &alloc_info,
                                   app->graphic_command_buffers),
          "Vulkan error: Failed to allocate command buffers");
  }

  // @sync object
  {
    app->image_available_semas =
        ARENA_PUSH_ARRAY(app->vulkan_arena, app->inflight_count, VkSemaphore);
    app->draw_fences =
        ARENA_PUSH_ARRAY(app->vulkan_arena, app->inflight_count, VkFence);
    for (u32 i = 0; i < app->inflight_count; i++) {
      VKTRY(vkCreateSemaphore(
                app->device,
                &(VkSemaphoreCreateInfo){
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                },
                NULL, &app->image_available_semas[i]),
            "Vulkan error: Failed to create present semaphore");
      VKTRY(vkCreateFence(app->device,
                          &(VkFenceCreateInfo){
                              .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                              .flags = VK_FENCE_CREATE_SIGNALED_BIT},
                          NULL, &app->draw_fences[i]),
            "Vulkan error: Failed to create draw fence");
    }
    app->render_finish_semas = ARENA_PUSH_ARRAY(
        app->vulkan_arena, app->swapchain_images_count, VkSemaphore);
    for (u32 i = 0; i < app->swapchain_images_count; i++) {
      VKTRY(vkCreateSemaphore(
                app->device,
                &(VkSemaphoreCreateInfo){
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                },
                NULL, &app->render_finish_semas[i]),
            "Vulkan error: Failed to create a render semaphore");
    }
  }

  return 0;
}

int create_swapchain(Arena *arena, Application *app,
                     VkSurfaceFormatKHR *swapchain_format) {

  VkSurfaceCapabilitiesKHR capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app->physical_device, app->surface,
                                            &capabilities);
  app->swap_extent = capabilities.currentExtent;

  if (app->swap_extent.width == UINT32_MAX) {
    int w, h;
    glfwGetFramebufferSize(app->window, &w, &h);
    app->swap_extent = (VkExtent2D){
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
  VkSurfaceFormatKHR *available_formats =
      ARENA_PUSH_ARRAY(arena, formats_count, VkSurfaceFormatKHR);
  vkGetPhysicalDeviceSurfaceFormatsKHR(app->physical_device, app->surface,
                                       &formats_count, available_formats);

  u32 format_index = 0;
  for (u32 i = 0; i < formats_count; i++) {
    if (available_formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
        available_formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      format_index = i;
      break;
    }
  }
  VkSurfaceFormatKHR format = available_formats[format_index];
  if (swapchain_format != NULL) {
    *swapchain_format = format;
  }

  u32 present_modes_count;
  vkGetPhysicalDeviceSurfacePresentModesKHR(app->physical_device, app->surface,
                                            &present_modes_count, NULL);
  if (present_modes_count == 0) {
    fprintf(stderr, "Vulkan error: no present mode available\n");
    cleanup(app);
    return -1;
  }
  VkPresentModeKHR *available_present_modes =
      ARENA_PUSH_ARRAY(arena, present_modes_count, VkPresentModeKHR);
  vkGetPhysicalDeviceSurfacePresentModesKHR(app->physical_device, app->surface,
                                            &present_modes_count,
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
      .imageFormat = format.format,
      .imageColorSpace = format.colorSpace,
      .imageExtent = app->swap_extent,
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform = capabilities.currentTransform,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = available_present_modes[present_mode_index],
      .clipped = true};

  VKTRY(vkCreateSwapchainKHR(app->device, &swapchain_create_info, NULL,
                             &app->swapchain),
        "Vulkan error: unable to create swapchain");

  // allocate only on initialization (suppose count will not change)
  if (app->swapchain_images_count == 0) {
    VKTRY(vkGetSwapchainImagesKHR(app->device, app->swapchain,
                                  &app->swapchain_images_count, NULL),
          "Vulkan error: unable to get swapchain image count");
    app->swapchain_images = ARENA_PUSH_ARRAY(
        app->vulkan_arena, app->swapchain_images_count, VkImage);
  } else {
    printf("recreate\n");
  }
  vkGetSwapchainImagesKHR(app->device, app->swapchain,
                          &app->swapchain_images_count, app->swapchain_images);

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
      .format = format.format,
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
    VKTRY(vkCreateImageView(app->device, &image_view_create_info, NULL,
                            &app->swapchain_image_views[i]),
          "Vulkan error: failed to created image view for a swapchain image");
  }

  return 0;
}

int cleanup_swapchain(Application *app) {
  if (app->swapchain_image_views != VK_NULL_HANDLE) {
    for (u32 i = 0; i < app->swapchain_images_count; i++) {
      printf("%i => %p\n", i, (void *)app->swapchain_image_views[i]);
      vkDestroyImageView(app->device, app->swapchain_image_views[i], NULL);
      app->swapchain_image_views[i] = VK_NULL_HANDLE;
    }
  }

  // voluntarily keep swapchain image allocate, suppose swapchain image count
  // will not change
  if (app->swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(app->device, app->swapchain, NULL);
    app->swapchain = VK_NULL_HANDLE;
  }

  return 0;
}

// @Clean up
static void cleanup(Application *app) {
  if (app->image_available_semas != NULL) {
    for (u32 i = 0; i < app->inflight_count; i++) {
      vkDestroySemaphore(app->device, app->image_available_semas[i], NULL);
    }
  }
  if (app->render_finish_semas != NULL) {
    for (u32 i = 0; i < app->inflight_count; i++) {
      vkDestroySemaphore(app->device, app->render_finish_semas[i], NULL);
    }
  }
  if (app->draw_fences != NULL) {
    for (u32 i = 0; i < app->inflight_count; i++) {
      vkDestroyFence(app->device, app->draw_fences[i], NULL);
    }
  }

  if (app->graphic_command_buffers != NULL) {
    vkFreeCommandBuffers(app->device, app->graphic_command_pool, 1,
                         app->graphic_command_buffers);
  }
  if (app->graphic_command_pool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(app->device, app->graphic_command_pool, NULL);
  }

  cleanup_swapchain(app);

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

void transition_image_layout(Application *app, VkCommandBuffer command_buffer,
                             u32 image_index, VkImageLayout old_layout,
                             VkImageLayout new_layout,
                             VkAccessFlags2 src_access_mask,
                             VkAccessFlags2 dst_access_mask,
                             VkPipelineStageFlags2 src_stage_mask,
                             VkPipelineStageFlags2 dst_stage_mask) {
  VkImageMemoryBarrier2 barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = src_stage_mask,
      .srcAccessMask = src_access_mask,
      .dstStageMask = dst_stage_mask,
      .dstAccessMask = dst_access_mask,
      .oldLayout = old_layout,
      .newLayout = new_layout,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = (app->swapchain_images)[image_index],
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

  vkCmdPipelineBarrier2(command_buffer, &dependency_info);
}

static void framebuffer_resize_callback(GLFWwindow *window, int width,
                                        int height) {

  printf("GLFW Resize (width=%4i; height=%4i)\n", width, height);
  f64 start_time = now_seconds();
  Application *app = glfwGetWindowUserPointer(window);

  vkDeviceWaitIdle(app->device);

  cleanup_swapchain(app);

  ArenaTemp temp = arena_temp_begin(app->scratch_arena);
  create_swapchain(temp.arena, app, NULL);
  arena_temp_end(temp);
  f64 end_time = now_seconds();
  f64 elapsed = end_time - start_time;
  printf("resizing take %.2fms.\n", elapsed * 1000);
}
static int init_glfw_window(Application *app) {
  if (!glfwInit()) {
    fprintf(stderr, "Failed to initialize GLFW\n");
    return -1;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  // glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  app->window = glfwCreateWindow(1000, 1000, "Vulkan", NULL, NULL);

  glfwSetWindowUserPointer(app->window, app);
  glfwSetFramebufferSizeCallback(app->window, framebuffer_resize_callback);

  if (app->window == NULL) {
    fprintf(stderr, "Failed to create GLFW window\n");
    return -1;
  }

  printf("GLFW window created\n");
  int win_w, win_h;
  int fb_w, fb_h;

  glfwGetWindowSize(app->window, &win_w, &win_h);
  glfwGetFramebufferSize(app->window, &fb_w, &fb_h);

  printf("window size: %d x %d\n", win_w, win_h);
  printf("framebuffer size: %d x %d\n", fb_w, fb_h);

  return 0;
}

int draw_frame(Application *app) {
  // await for previous frame to be drawn
  VKTRY(vkWaitForFences(app->device, 1, &app->draw_fences[app->frame_index],
                        true, UINT64_MAX),
        "Draw frame error: unable to wait for draw fence");
  VKTRY(vkResetFences(app->device, 1, &app->draw_fences[app->frame_index]),
        "Draw frame error: unable to reset draw fence");

  VkCommandBuffer *current_command_buffer =
      &app->graphic_command_buffers[app->frame_index];

  // acquire next image from swapchain
  uint32_t image_index = 0;
  VKTRY(vkAcquireNextImageKHR(app->device, app->swapchain, UINT64_MAX,
                              app->image_available_semas[app->frame_index],
                              NULL, &image_index),
        "Draw frame error: unable to acquire next image from swapchain");
  VKTRY(vkResetCommandBuffer(*current_command_buffer, 0),
        "Draw frame error: unable to reset the command buffer");

  // draw command recording
  vkBeginCommandBuffer(*current_command_buffer,
                       &(VkCommandBufferBeginInfo){
                           .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                       });

  transition_image_layout(
      app, *current_command_buffer, image_index, VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, (VkAccessFlags2){},
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
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

  vkCmdBeginRendering(*current_command_buffer, &rendering_info);

  vkCmdBindPipeline(*current_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    app->pipeline);

  vkCmdSetViewport(app->command_buffer, 0., 1.,
  // dynamic
  vkCmdSetViewport(*current_command_buffer, 0., 1.,
                   &(VkViewport){0., 0., (f32)app->swap_extent.width,
                                 (f32)app->swap_extent.height, 0., 1.});
  vkCmdSetScissor(*current_command_buffer, 0., 1.,
                  &(VkRect2D){{0, 0}, app->swap_extent});

  vkCmdDraw(*current_command_buffer, vertices_count, 1, 0, 0);

  vkCmdEndRendering(*current_command_buffer);

  transition_image_layout(
      app, *current_command_buffer, image_index,
      VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, (VkAccessFlags2){},
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

  vkEndCommandBuffer(*current_command_buffer);

  // submit command buffer to queue
  const VkPipelineStageFlags wait_dst_stage_mask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  const VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &app->image_available_semas[app->frame_index],
      .pWaitDstStageMask = &wait_dst_stage_mask,
      .commandBufferCount = 1,
      .pCommandBuffers = current_command_buffer,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = &app->render_finish_semas[image_index]};
  vkQueueSubmit(app->graphic_queue, 1, &submit_info,
                app->draw_fences[app->frame_index]);

  const VkPresentInfoKHR present_info = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &app->render_finish_semas[image_index],
      .swapchainCount = 1,
      .pSwapchains = &app->swapchain,
      .pImageIndices = &image_index};
  VKTRY(vkQueuePresentKHR(app->graphic_queue, &present_info),
        "Draw frame error: unable to present image");

  app->frame_index = (app->frame_index + 1) % app->inflight_count;

  return 0;
}

//@ MainLoop
static void main_loop(Application *app) {
  char title[64];
  f64 last_time = now_seconds();
  uint32_t frame_count = 0;
  while (!glfwWindowShouldClose(app->window)) {
    glfwPollEvents();
    draw_frame(app);
    frame_count++;

    f64 current_time = now_seconds();
    f64 elapsed = current_time - last_time;

    if (elapsed >= 1.0) {
      f64 fps = (f64)frame_count / elapsed;

      printf("FPS: %.1f\n", fps);

      frame_count = 0;
      last_time = current_time;
      snprintf(title, sizeof(title), "Vulkan - FPS: %.1f", fps);
      glfwSetWindowTitle(app->window, title);
    }
  }
}

int main(void) {
  Application app = {0};

  if (init_glfw_window(&app) != 0) {
    cleanup(&app);
    return EXIT_FAILURE;
  }

  if (init_vulkan(&app) != 0) {
    cleanup(&app);
    return EXIT_FAILURE;
  }

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
