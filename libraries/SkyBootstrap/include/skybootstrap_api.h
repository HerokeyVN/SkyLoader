#pragma once

#include <stdint.h>

#ifdef _WIN32
#ifdef SKYBOOTSTRAP_BUILD
#define SKYBOOTSTRAP_API __declspec(dllexport)
#else
#define SKYBOOTSTRAP_API
#endif
#define SKYBOOTSTRAP_CALL __stdcall
#else
#define SKYBOOTSTRAP_API
#define SKYBOOTSTRAP_CALL
#endif

#define SKYBOOTSTRAP_API_VERSION 2u

// Raw Vulkan handles are transported as integers so plugins do not need to
// share a Vulkan SDK or C++ ABI with the bootstrap DLL. A plugin that uses the
// Vulkan headers can cast these back to VkInstance/VkDevice/etc.
typedef struct SkyVulkanContext {
  uint32_t version;
  uint32_t imageCount;
  uint32_t queueFamily;
  uint32_t reserved;
  uintptr_t window;
  uint64_t instance;
  uint64_t physicalDevice;
  uint64_t device;
  uint64_t queue;
  uint64_t renderPass;
  uint64_t descriptorPool;
  // Resolves Vulkan commands through Bootstrap's next-layer dispatch table.
  // Plugins must use this rather than importing Vulkan loader entry points.
  uint64_t (SKYBOOTSTRAP_CALL *getVulkanProc)(const char *name);
} SkyVulkanContext;

typedef struct SkyBootstrapApi {
  uint32_t version;
  void (SKYBOOTSTRAP_CALL *log)(const char *message);
} SkyBootstrapApi;

// Optional exports implemented by plugins loaded through SkyBootstrap.
typedef int (SKYBOOTSTRAP_CALL *SkyPluginInitFn)(const SkyBootstrapApi *api);
typedef void (SKYBOOTSTRAP_CALL *SkyPluginShutdownFn)(void);
// These exports are optional. They turn a normal loaded DLL into an overlay
// plugin. Bootstrap owns the Vulkan layer, command buffers and presentation;
// the plugin owns its ImGui context and writes draw commands for its UI.
typedef int (SKYBOOTSTRAP_CALL *SkyPluginVulkanInitFn)(const SkyVulkanContext *context);
// Records an otherwise empty ImGui frame. This is useful to validate a
// plugin's renderer without exposing its UI.
typedef void (SKYBOOTSTRAP_CALL *SkyPluginVulkanWarmupFn)(uint64_t commandBuffer);
typedef void (SKYBOOTSTRAP_CALL *SkyPluginVulkanRenderFn)(uint64_t commandBuffer);
typedef void (SKYBOOTSTRAP_CALL *SkyPluginWindowMessageFn)(uintptr_t window, uint32_t message, uintptr_t wParam, intptr_t lParam);

// Bootstrap exports. SkyLoader normally talks to the named pipe instead of
// resolving these functions remotely.
#ifdef __cplusplus
extern "C" {
#endif
SKYBOOTSTRAP_API int SKYBOOTSTRAP_CALL SkyBootstrapLoadPluginW(const wchar_t *path);
SKYBOOTSTRAP_API uint32_t SKYBOOTSTRAP_CALL SkyBootstrapPluginCount(void);
#ifdef __cplusplus
}
#endif
