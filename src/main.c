#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  VkInstance *instance;
} HelloTriangleApplication;

bool get_extensions(Arena *vulkan_arena, u32 *extension_count,
                    const char ***extension_names);
bool has_extension(u32 actual_count, VkExtensionProperties *actual_props,
                   const char *const expected);
bool get_layers(Arena *arena, u32 *layer_count, const char ***layer_properties);

bool read_shader_file(Arena *arena, const char *path, Str *out);

//@ Init
static int initVulkan(HelloTriangleApplication *app) {
  Arena *vulkan_arena = arena_create(ARENA_DEFAULT_BLOCK_SIZE);
  Arena *scratch_arena = arena_create(ARENA_DEFAULT_BLOCK_SIZE);
  VkResult result = VK_SUCCESS;
  VkInstance instance = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue graphic_queue = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  u32 swapchain_images_count = 0;
  VkImage *swapchain_images = VK_NULL_HANDLE;
  VkImageView *swapchain_image_views = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkBuffer buffer = VK_NULL_HANDLE;

  // @instance
  {
    ArenaTemp scratch = arena_temp_begin(scratch_arena);
    u32 extension_count;
    const char **extension_names = NULL;
    if (!get_extensions(scratch_arena, &extension_count, &extension_names)) {
      fprintf(stderr, "Vulkan error: failed to resolve instance extensions\n");
      goto cleanup;
    }

    u32 layer_count;
    const char **layer_names = NULL;
    if (!get_layers(scratch_arena, &layer_count, &layer_names)) {
      fprintf(stderr, "Vulkan error: failed to resolve validation layers\n");
      goto cleanup;
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

    result = vkCreateInstance(&createInfo, NULL, &instance);

    if (result != VK_SUCCESS) {
      fprintf(stderr, "Vulkan error: failed to create instance\n");
      goto cleanup;
    }

    printf("vk::Instance created\n");
    arena_temp_end(scratch);
  }
  // @surface
  {
    result = glfwCreateWindowSurface(instance, app->window, NULL, &surface);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "GLFW error: failed to create a window surface");
      goto cleanup;
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
    ArenaTemp scratch = arena_temp_begin(scratch_arena);
    u32 physical_device_count = 0;
    result = vkEnumeratePhysicalDevices(instance, &physical_device_count, NULL);
    if (result != VK_SUCCESS || physical_device_count == 0) {
      fprintf(stderr, "Vulkan error: no physical device found\n");
      goto cleanup;
    }
    VkPhysicalDevice *physical_devices = ARENA_PUSH_ARRAY(
        scratch_arena, physical_device_count, VkPhysicalDevice);
    result = vkEnumeratePhysicalDevices(instance, &physical_device_count,
                                        physical_devices);

    if (result != VK_SUCCESS) {
      fprintf(stderr, "Vulkan error: failed to enumerate physical devices\n");
      goto cleanup;
    }
    physical_device = physical_devices[0];

    // verify 1.3 support
    VkPhysicalDeviceProperties2 physical_device_properties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    vkGetPhysicalDeviceProperties2(physical_device,
                                   &physical_device_properties);
    if (physical_device_properties.properties.apiVersion < VK_API_VERSION_1_3) {
      fprintf(stderr, "Vulkan error: physical device do not support 1.3.");
      goto cleanup;
    }

    // verify graphic queue
    u32 physical_device_queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(
        physical_device, &physical_device_queue_count, NULL);
    VkQueueFamilyProperties *physical_device_queue_properties =
        ARENA_PUSH_ARRAY(scratch_arena, physical_device_queue_count,
                         VkQueueFamilyProperties);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                             &physical_device_queue_count,
                                             physical_device_queue_properties);
    VkBool32 supports_surface = false;
    for (u32 i = 0; i < physical_device_queue_count; ++i) {
      result = vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface,
                                                    &supports_surface);
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
      goto cleanup;
    }

    // verify swapchain extension
    u32 extension_count = 0;
    vkEnumerateDeviceExtensionProperties(physical_device, NULL,
                                         &extension_count, NULL);
    VkExtensionProperties *extension_properties =
        ARENA_PUSH_ARRAY(scratch_arena, extension_count, VkExtensionProperties);
    vkEnumerateDeviceExtensionProperties(
        physical_device, NULL, &extension_count, extension_properties);

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
      goto cleanup;
    }

    // verify dynamic rendering feature
    vkGetPhysicalDeviceFeatures2(physical_device, &device_features);

    if (!vulkan13_features.dynamicRendering ||
        !extended_dynamic_state_features.extendedDynamicState) {
      fprintf(stderr,
              "Vulkan error: physical device does not dynamic rendering\n");
      goto cleanup;
    }

    printf("vk::PhysicalDevice created\n");
    arena_temp_end(scratch);
  }

  // @logical device
  {
    float queue_priority = 1.f;
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
    result = vkCreateDevice(physical_device, &device_info, NULL, &device);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Vulkan error: failed to create logical device\n");
      goto cleanup;
    }
    vkGetDeviceQueue(device, graphic_queue_index, 0, &graphic_queue);

    printf("vk::LogicalDevice (and queue handle) created\n");
  }

  // @swapchain
  VkSurfaceFormatKHR swapchain_format;
  ArenaTemp swapchain_scratch = arena_temp_begin(scratch_arena);
  VkExtent2D swap_extent;
  {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface,
                                              &capabilities);
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
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface,
                                         &formats_count, NULL);
    if (formats_count == 0) {
      fprintf(stderr, "Vulkan error: no present mode available\n");
      goto cleanup;
    }
    VkSurfaceFormatKHR *available_formats =
        ARENA_PUSH_ARRAY(scratch_arena, formats_count, VkSurfaceFormatKHR);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface,
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
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface,
                                              &present_modes_count, NULL);
    if (present_modes_count == 0) {
      fprintf(stderr, "Vulkan error: no present mode available\n");
      goto cleanup;
    }
    VkPresentModeKHR *available_present_modes =
        ARENA_PUSH_ARRAY(scratch_arena, present_modes_count, VkPresentModeKHR);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface,
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
        .surface = surface,
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

    vkCreateSwapchainKHR(device, &swapchain_create_info, NULL, &swapchain);
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_images_count, NULL);
    swapchain_images =
        ARENA_PUSH_ARRAY(scratch_arena, swapchain_images_count, VkImage);
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_images_count,
                            swapchain_images);

    printf("vk::Swapchain (and images) created\n");
  }

  // @image views
  {
    if (swapchain_images_count == 0) {
      fprintf(stderr, "Vulkan error: swapchain is empty\n");
      goto cleanup;
    }

    swapchain_image_views =
        ARENA_PUSH_ARRAY(vulkan_arena, swapchain_images_count, VkImageView);
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

    for (u32 i = 0; i < swapchain_images_count; i++) {
      image_view_create_info.image = swapchain_images[i];
      result = vkCreateImageView(device, &image_view_create_info, NULL,
                                 &swapchain_image_views[i]);
      if (result != VK_SUCCESS) {
        fprintf(stderr,
                "Vulkan error: failed to created image view for swapchain "
                "image %u",
                i);
        goto cleanup;
      }
    }

    printf("vk::ImageView (for swapchain) created\n");
  }
  arena_temp_end(swapchain_scratch);

  // @render pipeline
  VkPipelineShaderStageCreateInfo shader_stage_infos[2];
  {
    ArenaTemp scratch = arena_temp_begin(scratch_arena);

    Str shader_code = {0};
    read_shader_file(scratch_arena, "./build/shaders/slang.spv", &shader_code);
    str_print_hex(&shader_code);

    VkShaderModule shader_module;
    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shader_code.length,
        .pCode = (const u32 *)shader_code.value};
    result = vkCreateShaderModule(device, &createInfo, NULL, &shader_module);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Vulkan error: Failed to create shader module\n");
      return false;
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
                           .width = (float)swap_extent.width,
                           .height = (float)swap_extent.height,
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
    result = vkCreatePipelineLayout(device, &pipeline_layout_info, NULL,
                                    &pipeline_layout);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Vulkan error: Failed to create pipeline layout\n");
      return false;
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

    result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                       &pipeline_info, NULL, &pipeline);
    arena_temp_end(scratch);
  }

  return 0;

cleanup:
  if (buffer != VK_NULL_HANDLE)
    vkDestroyBuffer(device, buffer, NULL);

  if (swapchain_image_views != VK_NULL_HANDLE) {
    for (u32 i = 0; i < swapchain_images_count; i++)
      vkDestroyImageView(device, swapchain_image_views[i], NULL);
  }

  if (swapchain != VK_NULL_HANDLE)
    vkDestroySwapchainKHR(device, swapchain, NULL);

  if (device != VK_NULL_HANDLE)
    vkDestroyDevice(device, NULL);

  if (surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance, surface, NULL);
  }

  if (instance != VK_NULL_HANDLE)
    vkDestroyInstance(instance, NULL);

  arena_destroy(vulkan_arena);
  arena_destroy(scratch_arena);
  return -1;
}

//@ Dispose
static void cleanup(HelloTriangleApplication *app) {
  if (app->window != NULL) {
    glfwDestroyWindow(app->window);
    app->window = NULL;
  }

  glfwTerminate();
}

static int initWindow(HelloTriangleApplication *app) {
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

//@ MainLoop
static void mainLoop(HelloTriangleApplication *app) {
  while (!glfwWindowShouldClose(app->window)) {
    glfwPollEvents();
  }
}

int main(void) {
  HelloTriangleApplication app = {0};

  if (initWindow(&app) != 0)
    return EXIT_FAILURE;

  if (initVulkan(&app) != 0)
    return EXIT_FAILURE;

  mainLoop(&app);
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
