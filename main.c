#include <string.h>
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>

typedef struct HelloTriangleApplication {
  GLFWwindow *window;
  VkInstance *instance;
} HelloTriangleApplication;

void validate_ext(uint32_t *extension_count,
                  const char *const **extension_names);

//@ Init
static int initVulkan(HelloTriangleApplication *app) {

  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkBuffer buffer = VK_NULL_HANDLE;

  const VkApplicationInfo appInfo = {.pApplicationName = "Hello Triangle",
                                     .applicationVersion =
                                         VK_MAKE_VERSION(1, 0, 0),
                                     .pEngineName = "No Engine",
                                     .engineVersion = VK_MAKE_VERSION(1, 0, 0),
                                     .apiVersion = VK_API_VERSION_1_4};

  uint32_t extension_count;
  const char *const *extension_names = NULL;
  validate_ext(&extension_count, &extension_names);

  // instance
  const VkInstanceCreateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &appInfo,
      .enabledExtensionCount = extension_count,
      .ppEnabledExtensionNames = extension_names};

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

  printf("Hello World !\n");

  if (initWindow(&app) != 0)
    return EXIT_FAILURE;

  if (initVulkan(&app) != 0)
    return EXIT_FAILURE;

  mainLoop(&app);
  cleanup(&app);

  return EXIT_SUCCESS;
}

void validate_ext(uint32_t *extension_count,
                  const char *const **extension_names) {
  // expected ext by glfw
  *extension_count = 0;
  *extension_names = glfwGetRequiredInstanceExtensions(extension_count);
  if (*extension_names == NULL) {
    fprintf(stderr, "Failed to get GLFW required extensions\n");
    exit(EXIT_FAILURE);
  }

  // actual ext of vk
  uint32_t vk_extension_count = 0;
  vkEnumerateInstanceExtensionProperties(NULL, &vk_extension_count, NULL);
  VkExtensionProperties *extension_properties =
      malloc(sizeof(VkExtensionProperties) * vk_extension_count);
  if (extension_properties == NULL) {
    fprintf(stderr, "Out of memory\n");
    exit(EXIT_FAILURE);
  }
  vkEnumerateInstanceExtensionProperties(NULL, &vk_extension_count,
                                         extension_properties);

  // compare
  for (uint32_t i = 0; i < *extension_count; ++i) {
    int found = 0;

    for (uint32_t j = 0; j < vk_extension_count; ++j) {
      if (strcmp((*extension_names)[i],
                 extension_properties[j].extensionName) == 0) {
        printf("glfw expected %s: found.\n", (*extension_names)[i]);
        found = 1;
        break;
      }
    }

    if (!found) {
      fprintf(stderr, "Required GLFW extension not supported: %s\n",
              (*extension_names)[i]);

      free(extension_properties);

      exit(EXIT_FAILURE);
    }
  }
}
