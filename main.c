#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

#include "base.c"

#define VK_ENABLE_VALIDATION
#ifdef VK_ENABLE_VALIDATION
static const bool is_validation_enabled = 1;
char const *validation_layer_names[] = {"VK_LAYER_KHRONOS_validation"};
const int validation_layer_count =
    sizeof(validation_layer_names) / sizeof(validation_layer_names[0]);

#else
static const bool is_validation_enabled = 0;
#endif

typedef struct HelloTriangleApplication {
  GLFWwindow *window;
  VkInstance *instance;
} HelloTriangleApplication;

void get_extensions(uint32_t *extension_count, const char ***extension_names);
bool has_extension(uint32_t actual_count, VkExtensionProperties *actual_props,
                   const char *const expected);
void get_layers(uint32_t *layer_count, const char ***layer_properties);

//@ Init
static int initVulkan(HelloTriangleApplication *app) {
  VkResult result = VK_SUCCESS;
  VkInstance instance = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue graphic_queue = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  uint32_t swapchain_images_count = 0;
  VkImage *swapchain_images = VK_NULL_HANDLE;
  VkImageView *swapchain_image_views = VK_NULL_HANDLE;

  uint32_t required_layer_count = 0;
  const char **required_layers = NULL;

  const VkApplicationInfo appInfo = {.pApplicationName = "Hello Triangle",
                                     .applicationVersion =
                                         VK_MAKE_VERSION(1, 0, 0),
                                     .pEngineName = "No Engine",
                                     .engineVersion = VK_MAKE_VERSION(1, 0, 0),
                                     .apiVersion = VK_API_VERSION_1_4};

  // @instance
  {
    uint32_t extension_count;
    const char **extension_names = NULL;
    get_extensions(&extension_count, &extension_names);

    uint32_t layer_count;
    const char **layer_names = NULL;
    get_layers(&layer_count, &layer_names);
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
  }
  // @surface
  {
    result = glfwCreateWindowSurface(instance, app->window, NULL, &surface);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "GLFW error: failed to create a window surface");
      goto cleanup;
    }
  }

  // @physical device
  uint32_t graphic_queue_index = ~0;
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
  const uint32_t required_device_extension_count =
      sizeof(required_device_extension_names) /
      sizeof(required_device_extension_names[0]);
  {
    uint32_t physical_device_count = 0;
    result = vkEnumeratePhysicalDevices(instance, &physical_device_count, NULL);
    if (result != VK_SUCCESS || physical_device_count == 0) {
      fprintf(stderr, "Vulkan error: no physical device found\n");
      goto cleanup;
    }
    VkPhysicalDevice *physical_devices =
        malloc(sizeof(*physical_devices) * physical_device_count);
    if (physical_devices == NULL) {
      fprintf(stderr, "Error: out of memory\n");
      goto cleanup;
    }
    result = vkEnumeratePhysicalDevices(instance, &physical_device_count,
                                        physical_devices);

    if (result != VK_SUCCESS) {
      fprintf(stderr, "Vulkan error: failed to enumerate physical devices\n");
      free(physical_devices);
      goto cleanup;
    }
    physical_device = physical_devices[0];
    free(physical_devices);

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
    uint32_t physical_device_queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(
        physical_device, &physical_device_queue_count, NULL);
    VkQueueFamilyProperties *physical_device_queue_properties =
        malloc(sizeof(VkQueueFamilyProperties) * physical_device_queue_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                             &physical_device_queue_count,
                                             physical_device_queue_properties);
    VkBool32 supports_surface = false;
    for (uint32_t i = 0; i < physical_device_queue_count; ++i) {
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
    if (graphic_queue_index == ~0) {
      fprintf(stderr,
              "Vulkan error: physical device does not support graphic queue\n");
      goto cleanup;
    }

    // verify swapchain extension
    uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(physical_device, NULL,
                                         &extension_count, NULL);
    VkExtensionProperties *extension_properties =
        malloc(sizeof(VkExtensionProperties) * extension_count);
    vkEnumerateDeviceExtensionProperties(
        physical_device, NULL, &extension_count, extension_properties);

    bool supports_swapchain = false;
    for (int i = 0; i < required_device_extension_count; i++) {
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
  {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface,
                                              &capabilities);

    VkExtent2D swap_extent = capabilities.currentExtent;
    if (swap_extent.width == UINT32_MAX) {
      int w, h;
      glfwGetFramebufferSize(app->window, &w, &h);
      swap_extent = (VkExtent2D){
          .width = clamp(w, capabilities.minImageExtent.width,
                         capabilities.maxImageExtent.width),
          .height = clamp(h, capabilities.minImageExtent.height,
                          capabilities.maxImageExtent.height),
      };
    }

    uint32_t swap_image_count = max(3, capabilities.minImageCount);
    if (0 < capabilities.maxImageCount &&
        capabilities.maxImageCount < swap_image_count) {
      swap_image_count = capabilities.maxImageCount;
    }

    uint32_t formats_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface,
                                         &formats_count, NULL);
    if (formats_count == 0) {
      fprintf(stderr, "Vulkan error: no present mode available\n");
      goto cleanup;
    }
    VkSurfaceFormatKHR *available_formats =
        malloc(sizeof(VkSurfaceFormatKHR) * formats_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface,
                                         &formats_count, available_formats);

    uint32_t format_index = 0;
    for (int i = 0; i < formats_count; i++) {
      if (available_formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
          available_formats[i].colorSpace ==
              VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        format_index = i;
        break;
      }
    }
    swapchain_format = available_formats[format_index];

    uint32_t present_modes_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface,
                                              &present_modes_count, NULL);
    if (present_modes_count == 0) {
      fprintf(stderr, "Vulkan error: no present mode available\n");
      goto cleanup;
    }
    VkPresentModeKHR *available_present_modes =
        malloc(sizeof(VkPresentModeKHR) * present_modes_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface,
                                              &present_modes_count,
                                              available_present_modes);

    uint32_t present_mode_index = 0;
    for (int i = 0; i < present_modes_count; i++) {
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
    swapchain_images = malloc(sizeof(VkImage) * swapchain_images_count);
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_images_count,
                            swapchain_images);

    free(available_formats);
    free(available_present_modes);
    printf("vk::Swapchain (and images) created\n");
  }

  // @image views
  {
    if (swapchain_images_count == 0) {
      fprintf(stderr, "Vulkan error: swapchain is empty\n");
      goto cleanup;
    }

    swapchain_image_views =
        malloc(sizeof(VkImageView) * swapchain_images_count);
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

    for (int i = 0; i < swapchain_images_count; i++) {
      image_view_create_info.image = swapchain_images[i];
      result = vkCreateImageView(device, &image_view_create_info, NULL,
                                 &swapchain_image_views[i]);
      if (result != VK_SUCCESS) {
        fprintf(
            stderr,
            "Vulkan error: failed to created image view for swapchain image %u",
            i);
        goto cleanup;
      }
    }

    printf("vk::ImageView (for swapchain) created\n");
  }

  // @buffer
  {
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 1024,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    result = vkCreateBuffer(device, &buffer_info, NULL, &buffer);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "Vulkan error: failed to create buffer\n");
      goto cleanup;
    }
    printf("vk::Buffer created\n");
  }

  return 0;

cleanup:
  if (buffer != VK_NULL_HANDLE)
    vkDestroyBuffer(device, buffer, NULL);

  if (swapchain_image_views != VK_NULL_HANDLE) {
    for (int i = 0; i < swapchain_images_count; i++)
      vkDestroyImageView(device, swapchain_image_views[i], NULL);

    free(swapchain_image_views);
  }

  if (swapchain_images != VK_NULL_HANDLE) {
    for (int i = 0; i < swapchain_images_count; i++)
      vkDestroyImage(device, swapchain_images[i], NULL);

    free(swapchain_images);
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

static int run(HelloTriangleApplication *app) { return 0; }

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

bool has_extension(uint32_t actual_count, VkExtensionProperties *actual_props,
                   const char *const expected) {
  for (uint32_t j = 0; j < actual_count; ++j) {
    if (strcmp(expected, actual_props[j].extensionName) == 0) {
      printf("extension %s found.\n", expected);
      return true;
    }
  }
  fprintf(stderr, "Required extension not supported: %s\n", expected);
  return false;
}
// TODO improve error handling
void get_extensions(uint32_t *extension_count, const char ***extension_names) {
  // actual ext of vk
  uint32_t vk_available_extension_count = 0;
  vkEnumerateInstanceExtensionProperties(NULL, &vk_available_extension_count,
                                         NULL);
  VkExtensionProperties *vk_available_extension_properties =
      malloc(sizeof(VkExtensionProperties) * vk_available_extension_count);
  if (vk_available_extension_properties == NULL) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }
  vkEnumerateInstanceExtensionProperties(NULL, &vk_available_extension_count,
                                         vk_available_extension_properties);

  // expected ext by glfw
  uint32_t glfw_extension_count = 0; // TODO change
  const char **glfw_extension_names =
      glfwGetRequiredInstanceExtensions(&glfw_extension_count);
  if (glfw_extension_names == NULL) {
    fprintf(stderr, "Failed to get GLFW required extensions\n");
    exit(EXIT_FAILURE);
  }
  for (uint32_t i = 0; i < glfw_extension_count; ++i) {
    if (!has_extension(vk_available_extension_count,
                       vk_available_extension_properties,
                       (glfw_extension_names)[i])) {
      goto ext_not_found;
    }
  }

  // extra
  int extra_extension_count = 0;
  if (is_validation_enabled) {
    extra_extension_count++;
    if (!has_extension(vk_available_extension_count,
                       vk_available_extension_properties,
                       VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
      goto ext_not_found;
    }
  }

  *extension_count = glfw_extension_count + extra_extension_count;
  *extension_names = malloc(sizeof(**extension_names) * *extension_count);
  memcpy(*extension_names, glfw_extension_names,
         sizeof(*glfw_extension_names) * glfw_extension_count);
  (*extension_names)[glfw_extension_count] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

  return;

// TODO: add portability
ext_not_found:
  if (vk_available_extension_properties) {
    free(vk_available_extension_properties);
  }

  exit(EXIT_FAILURE);
}

bool has_layer(uint32_t actual_count, VkLayerProperties *actual_props,
               const char *const expected) {
  for (int i = 0; i < actual_count; i++) {
    if (strcmp(actual_props[i].layerName, expected) == 0) {
      printf("layer %s found.\n", expected);
      return true;
    }
  }
  fprintf(stderr, "Required layer do not exist: %s\n", expected);
  return false;
}
void get_layers(uint32_t *layer_count, const char ***layer_names) {
  uint32_t required_layer_count = 0;
  const char **required_layer_names = NULL;
  if (is_validation_enabled) {
    required_layer_count = validation_layer_count;
    required_layer_names =
        malloc(sizeof(*required_layer_names) * required_layer_count);

    memcpy(required_layer_names, validation_layer_names,
           sizeof(validation_layer_names) * validation_layer_count);
  }

  uint32_t vk_layer_count;
  vkEnumerateInstanceLayerProperties(&vk_layer_count, NULL);
  VkLayerProperties *vk_layer_properties =
      malloc(sizeof(VkLayerProperties) * vk_layer_count);
  if (vk_layer_properties == NULL) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }
  vkEnumerateInstanceLayerProperties(&vk_layer_count, vk_layer_properties);

  for (int i = 0; i < required_layer_count; i++) {
    if (!has_layer(vk_layer_count, vk_layer_properties,
                   required_layer_names[i])) {
      goto layer_not_found;
    }
  }
  *layer_count = required_layer_count;
  *layer_names = required_layer_names;

  return;

layer_not_found:
  if (vk_layer_properties) {
    free(vk_layer_properties);
  }
  exit(EXIT_FAILURE);
}
