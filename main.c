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
void get_layers(uint32_t *layer_count, const char ***layer_properties);

//@ Init
static int initVulkan(HelloTriangleApplication *app) {

  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkBuffer buffer = VK_NULL_HANDLE;

  uint32_t required_layer_count = 0;
  const char **required_layers = NULL;

  const VkApplicationInfo appInfo = {.pApplicationName = "Hello Triangle",
                                     .applicationVersion =
                                         VK_MAKE_VERSION(1, 0, 0),
                                     .pEngineName = "No Engine",
                                     .engineVersion = VK_MAKE_VERSION(1, 0, 0),
                                     .apiVersion = VK_API_VERSION_1_4};

  uint32_t extension_count;
  const char **extension_names = NULL;
  get_extensions(&extension_count, &extension_names);

  uint32_t layer_count;
  const char **layer_names = NULL;
  get_layers(&layer_count, &layer_names);

  // instance
  const VkInstanceCreateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &appInfo,
      .enabledExtensionCount = extension_count,
      .ppEnabledExtensionNames = extension_names,
      .enabledLayerCount = layer_count,
      .ppEnabledLayerNames = layer_names,
  };

  VkResult result = vkCreateInstance(&createInfo, NULL, &instance);

  if (result != VK_SUCCESS) {
    fprintf(stderr, "Vulkan error: failed to create instance\n");
    goto cleanup;
  }

  printf("vk::Instance created\n");

  // physical device
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

  printf("vk::PhysicalDevice created\n");

  // logical device
  VkDeviceCreateInfo device_info = {.sType =
                                        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  result = vkCreateDevice(physical_device, &device_info, NULL, &device);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "Vulkan error: failed to create logical device\n");
    goto cleanup;
  }
  printf("vk::LogicalDevice created\n");

  // buffer
  VkBufferCreateInfo buffer_info = {.sType =
                                        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                    .size = 1024,
                                    .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
  result = vkCreateBuffer(device, &buffer_info, NULL, &buffer);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "Vulkan error: failed to create buffer\n");
    goto cleanup;
  }
  printf("vk::Buffer created\n");

  return 0;

cleanup:
  if (buffer != VK_NULL_HANDLE)
    vkDestroyBuffer(device, buffer, NULL);

  if (device != VK_NULL_HANDLE)
    vkDestroyDevice(device, NULL);

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

bool has_ext(uint32_t actual_count, VkExtensionProperties *actual_props,
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
    if (!has_ext(vk_available_extension_count,
                 vk_available_extension_properties,
                 (glfw_extension_names)[i])) {
      goto ext_not_found;
    }
  }

  // extra
  int extra_extension_count = 0;
  if (is_validation_enabled) {
    extra_extension_count++;
    if (!has_ext(vk_available_extension_count,
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
    if (!has_layer(*layer_count, vk_layer_properties,
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
