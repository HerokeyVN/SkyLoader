// Vulkan implicit layer hosted by SkyBootstrap. It deliberately owns only the
// Vulkan lifetime/presentation plumbing; UI belongs to loaded plugins.
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <map>
#include <mutex>
#include <vector>
#include <cstring>
#include <cstdio>

#include "skybootstrap_internal.h"

namespace {
constexpr uint32_t kMaxFrames = 8;

struct InstanceTable {
  PFN_vkGetInstanceProcAddr getInstanceProcAddr{};
  PFN_vkDestroyInstance destroyInstance{};
  PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices{};
};
struct DeviceTable {
  PFN_vkGetDeviceProcAddr getDeviceProcAddr{};
  PFN_vkDestroyDevice destroyDevice{};
  PFN_vkQueuePresentKHR queuePresent{};
  PFN_vkCreateSwapchainKHR createSwapchain{};
  PFN_vkDestroySwapchainKHR destroySwapchain{};
  PFN_vkGetDeviceQueue getDeviceQueue{};
  PFN_vkGetSwapchainImagesKHR getSwapchainImages{};
  PFN_vkCreateRenderPass createRenderPass{};
  PFN_vkDestroyRenderPass destroyRenderPass{};
  PFN_vkCreateDescriptorPool createDescriptorPool{};
  PFN_vkDestroyDescriptorPool destroyDescriptorPool{};
  PFN_vkCreateCommandPool createCommandPool{};
  PFN_vkDestroyCommandPool destroyCommandPool{};
  PFN_vkAllocateCommandBuffers allocateCommandBuffers{};
  PFN_vkCreateFence createFence{};
  PFN_vkDestroyFence destroyFence{};
  PFN_vkCreateSemaphore createSemaphore{};
  PFN_vkDestroySemaphore destroySemaphore{};
  PFN_vkCreateImageView createImageView{};
  PFN_vkDestroyImageView destroyImageView{};
  PFN_vkCreateFramebuffer createFramebuffer{};
  PFN_vkDestroyFramebuffer destroyFramebuffer{};
  PFN_vkWaitForFences waitForFences{};
  PFN_vkResetFences resetFences{};
  PFN_vkResetCommandPool resetCommandPool{};
  PFN_vkBeginCommandBuffer beginCommandBuffer{};
  PFN_vkCmdBeginRenderPass cmdBeginRenderPass{};
  PFN_vkCmdEndRenderPass cmdEndRenderPass{};
  PFN_vkEndCommandBuffer endCommandBuffer{};
  PFN_vkQueueSubmit queueSubmit{};
};
struct DeviceData {
  DeviceTable table{};
  VkDevice device{};
  VkPhysicalDevice physicalDevice{};
  VkInstance instance{};
  PFN_vkSetDeviceLoaderData setLoaderData{};
  VkQueue graphicsQueue{};
  uint32_t graphicsFamily{};
};
struct Frame {
  VkCommandPool pool{};
  VkCommandBuffer command{};
  VkFence fence{};
  VkImageView view{};
  VkFramebuffer framebuffer{};
  VkSemaphore ready{};
};
// OverlaySession owns every resource tied to one device/swapchain pair.
// The Vulkan hooks only route lifecycle events; they do not expose the
// resource implementation to plugins or configuration code.
struct OverlaySession {
  VkDevice device{};
  VkQueue queue{};
  VkSwapchainKHR swapchain{};
  VkRenderPass renderPass{};
  VkDescriptorPool descriptorPool{};
  VkExtent2D extent{};
  VkFormat format{};
  uint32_t imageCount{};
  bool pluginsInitialized{};
  Frame frames[kMaxFrames]{};
};
struct LayerCreateInfo {
  VkStructureType sType;
  const void *pNext;
  VkLayerFunction function;
};

std::mutex gLock;
std::map<VkInstance, InstanceTable> gInstances;
std::map<VkPhysicalDevice, VkInstance> gPhysicalDevices;
std::map<VkDevice, DeviceData> gDevices;
std::map<VkQueue, VkDevice> gQueues;
using OverlaySessionKey = std::pair<VkDevice, VkSwapchainKHR>;
std::map<OverlaySessionKey, OverlaySession> gOverlaySessions;
OverlaySession *gActiveOverlaySession = nullptr;
#define gOverlaySession (*gActiveOverlaySession)

extern "C" SKYBOOTSTRAP_API VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL SkyVkGetInstanceProcAddr(VkInstance instance, const char *name);
extern "C" SKYBOOTSTRAP_API VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL SkyVkGetDeviceProcAddr(VkDevice device, const char *name);
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance, uint32_t *count, VkPhysicalDevice *devices);

// Vulkan Loader 1.3 uses interface negotiation before it ever calls a layer's
// Get*ProcAddr functions. Without this export it treats an old-style layer as
// malformed and may abort while building the instance chain.
extern "C" SKYBOOTSTRAP_API VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *version) {
  SkyBootstrapLayerTrace("vkNegotiateLoaderLayerInterfaceVersion entered");
  if (!version || version->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT ||
      version->loaderLayerInterfaceVersion < 2) {
    SkyBootstrapLayerTrace("vkNegotiate rejected loader interface");
    return VK_ERROR_INITIALIZATION_FAILED;
  }
  version->loaderLayerInterfaceVersion = 2;
  version->pfnGetInstanceProcAddr = SkyVkGetInstanceProcAddr;
  version->pfnGetDeviceProcAddr = SkyVkGetDeviceProcAddr;
  version->pfnGetPhysicalDeviceProcAddr = nullptr;
  SkyBootstrapLayerTrace("vkNegotiate completed (interface v2)");
  return VK_SUCCESS;
}

LayerCreateInfo *findLayerInfo(const void *createInfo, VkStructureType type, VkLayerFunction function) {
  auto *node = reinterpret_cast<LayerCreateInfo *>(const_cast<void *>(createInfo));
  for (node = reinterpret_cast<LayerCreateInfo *>(const_cast<void *>(node->pNext)); node; node = reinterpret_cast<LayerCreateInfo *>(const_cast<void *>(node->pNext)))
    if (node->sType == type && node->function == function) return node;
  return nullptr;
}

DeviceData *deviceData(VkDevice device) {
  auto it = gDevices.find(device);
  return it == gDevices.end() ? nullptr : &it->second;
}

BOOL CALLBACK findWindow(HWND window, LPARAM data) {
  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);
  if (pid == GetCurrentProcessId() && GetWindow(window, GW_OWNER) == nullptr && IsWindowVisible(window)) {
    *reinterpret_cast<HWND *>(data) = window;
    return FALSE;
  }
  return TRUE;
}

// Stage 2 deliberately does not submit any command buffer or touch the
// game's synchronisation chain.  It only proves that resource functions from
// the downstream device dispatch can be used safely, then releases the test
// resource immediately.  This isolates Vulkan loader/lifetime mistakes from
// overlay rendering mistakes.
void probeSwapchainResources(const DeviceData &data, VkSwapchainKHR swapchain) {
  uint32_t imageCount = 0;
  if (!data.table.getSwapchainImages ||
      data.table.getSwapchainImages(data.device, swapchain, &imageCount, nullptr) != VK_SUCCESS ||
      imageCount == 0) {
    SkyBootstrapLayerTrace("probe: swapchain image query failed");
    return;
  }
  VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  poolInfo.queueFamilyIndex = data.graphicsFamily;
  VkCommandPool pool{};
  if (!data.table.createCommandPool || !data.table.destroyCommandPool ||
      data.table.createCommandPool(data.device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
    SkyBootstrapLayerTrace("probe: command-pool create failed");
    return;
  }
  data.table.destroyCommandPool(data.device, pool, nullptr);
  SkyBootstrapLayerTrace("probe: swapchain image query and command-pool lifetime passed");
}

void destroyOverlaySession(DeviceData *data) {
  if (!gOverlaySession.device || !data || data->device != gOverlaySession.device) return;
  for (Frame &frame : gOverlaySession.frames) {
    if (frame.framebuffer && data->table.destroyFramebuffer) data->table.destroyFramebuffer(data->device, frame.framebuffer, nullptr);
    if (frame.view && data->table.destroyImageView) data->table.destroyImageView(data->device, frame.view, nullptr);
    if (frame.ready && data->table.destroySemaphore) data->table.destroySemaphore(data->device, frame.ready, nullptr);
    if (frame.fence && data->table.destroyFence) data->table.destroyFence(data->device, frame.fence, nullptr);
    // Destroying a command pool releases all buffers allocated from it.
    if (frame.pool && data->table.destroyCommandPool) data->table.destroyCommandPool(data->device, frame.pool, nullptr);
    frame = {};
  }
  if (gOverlaySession.descriptorPool && data->table.destroyDescriptorPool) data->table.destroyDescriptorPool(data->device, gOverlaySession.descriptorPool, nullptr);
  if (gOverlaySession.renderPass && data->table.destroyRenderPass) data->table.destroyRenderPass(data->device, gOverlaySession.renderPass, nullptr);
  gOverlaySession = {};
}

bool initializeOverlaySession(const DeviceData &data, VkSwapchainKHR swapchain, const VkSwapchainCreateInfoKHR *createInfo) {
  uint32_t count = 0;
  if (!data.graphicsQueue || !data.table.getSwapchainImages ||
      data.table.getSwapchainImages(data.device, swapchain, &count, nullptr) != VK_SUCCESS ||
      count == 0 || count > kMaxFrames) {
    SkyBootstrapLayerTrace("sync probe: no usable graphics queue or swapchain images");
    return false;
  }
  gOverlaySession.device = data.device;
  gOverlaySession.queue = data.graphicsQueue;
  gOverlaySession.swapchain = swapchain;
  gOverlaySession.imageCount = count;
  gOverlaySession.extent = createInfo->imageExtent;
  gOverlaySession.format = createInfo->imageFormat;
  std::vector<VkImage> images;
  if (SkyBootstrapRenderPassProbeEnabled()) {
    images.resize(count);
    if (!data.table.getSwapchainImages ||
        data.table.getSwapchainImages(data.device, swapchain, &count, images.data()) != VK_SUCCESS) {
      destroyOverlaySession(const_cast<DeviceData *>(&data));
      SkyBootstrapLayerTrace("render-pass probe: swapchain images unavailable");
      return false;
    }
    VkAttachmentDescription attachment{};
    attachment.format = gOverlaySession.format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass.attachmentCount = 1;
    pass.pAttachments = &attachment;
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    pass.dependencyCount = 2;
    pass.pDependencies = dependencies;
    if (!data.table.createRenderPass || data.table.createRenderPass(data.device, &pass, nullptr, &gOverlaySession.renderPass) != VK_SUCCESS) {
      destroyOverlaySession(const_cast<DeviceData *>(&data));
      SkyBootstrapLayerTrace("render-pass probe: render-pass creation failed");
      return false;
    }
  }
  if (SkyBootstrapPluginInitProbeEnabled()) {
    VkDescriptorPoolSize poolSizes[] = {
      {VK_DESCRIPTOR_TYPE_SAMPLER, 256}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 256}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256},
    };
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool.maxSets = 1024;
    pool.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    pool.pPoolSizes = poolSizes;
    if (!data.table.createDescriptorPool ||
        data.table.createDescriptorPool(data.device, &pool, nullptr, &gOverlaySession.descriptorPool) != VK_SUCCESS) {
      destroyOverlaySession(const_cast<DeviceData *>(&data));
      SkyBootstrapLayerTrace("plugin-init probe: descriptor-pool creation failed");
      return false;
    }
  }
  for (uint32_t i = 0; i < count; ++i) {
    Frame &frame = gOverlaySession.frames[i];
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = data.graphicsFamily;
    VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (!data.table.createCommandPool || !data.table.allocateCommandBuffers || !data.table.createFence ||
        !data.table.createSemaphore || data.table.createCommandPool(data.device, &pool, nullptr, &frame.pool) != VK_SUCCESS) {
      destroyOverlaySession(const_cast<DeviceData *>(&data));
      SkyBootstrapLayerTrace("sync probe: command-pool creation failed");
      return false;
    }
    command.commandPool = frame.pool;
    command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command.commandBufferCount = 1;
    if (data.table.allocateCommandBuffers(data.device, &command, &frame.command) != VK_SUCCESS ||
        data.table.createFence(data.device, &fence, nullptr, &frame.fence) != VK_SUCCESS ||
        data.table.createSemaphore(data.device, &semaphore, nullptr, &frame.ready) != VK_SUCCESS) {
      destroyOverlaySession(const_cast<DeviceData *>(&data));
      SkyBootstrapLayerTrace("sync probe: frame resource creation failed");
      return false;
    }
    if (gOverlaySession.renderPass) {
      VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      view.image = images[i];
      view.viewType = VK_IMAGE_VIEW_TYPE_2D;
      view.format = gOverlaySession.format;
      view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      view.subresourceRange.levelCount = 1;
      view.subresourceRange.layerCount = 1;
      VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
      framebuffer.renderPass = gOverlaySession.renderPass;
      framebuffer.attachmentCount = 1;
      framebuffer.pAttachments = &frame.view;
      framebuffer.width = gOverlaySession.extent.width;
      framebuffer.height = gOverlaySession.extent.height;
      framebuffer.layers = 1;
      if (!data.table.createImageView || !data.table.createFramebuffer ||
          data.table.createImageView(data.device, &view, nullptr, &frame.view) != VK_SUCCESS) {
        destroyOverlaySession(const_cast<DeviceData *>(&data));
        SkyBootstrapLayerTrace("render-pass probe: image-view creation failed");
        return false;
      }
      if (data.table.createFramebuffer(data.device, &framebuffer, nullptr, &frame.framebuffer) != VK_SUCCESS) {
        destroyOverlaySession(const_cast<DeviceData *>(&data));
        SkyBootstrapLayerTrace("render-pass probe: framebuffer creation failed");
        return false;
      }
    }
  }
  SkyBootstrapLayerTrace(gOverlaySession.renderPass ? "overlay session: render-pass resources initialized" : "overlay session: command resources initialized");
  return true;
}

bool createOverlaySession(const DeviceData &data, VkSwapchainKHR swapchain, const VkSwapchainCreateInfoKHR *createInfo) {
  const OverlaySessionKey key{data.device, swapchain};
  auto existing = gOverlaySessions.find(key);
  if (existing != gOverlaySessions.end()) {
    gActiveOverlaySession = &existing->second;
    destroyOverlaySession(const_cast<DeviceData *>(&data));
    gOverlaySessions.erase(existing);
  }
  auto inserted = gOverlaySessions.emplace(key, OverlaySession{}).first;
  gActiveOverlaySession = &inserted->second;
  if (initializeOverlaySession(data, swapchain, createInfo)) return true;
  gOverlaySessions.erase(inserted);
  gActiveOverlaySession = nullptr;
  return false;
}

VkResult presentOverlaySession(VkQueue queue, const VkPresentInfoKHR *presentInfo, DeviceData *data) {
  // Multiple swapchains or a separate present queue need a dedicated handoff.
  // Preserve Sky's original present in those cases.
  if (!presentInfo || presentInfo->swapchainCount != 1) return data->table.queuePresent(queue, presentInfo);
  auto session = gOverlaySessions.find({data->device, presentInfo->pSwapchains[0]});
  if (session == gOverlaySessions.end()) return data->table.queuePresent(queue, presentInfo);
  gActiveOverlaySession = &session->second;
  if (queue != gOverlaySession.queue) return data->table.queuePresent(queue, presentInfo);
  if (SkyBootstrapPluginInitProbeEnabled() && !gOverlaySession.pluginsInitialized) {
    HWND window = nullptr;
    EnumWindows(findWindow, reinterpret_cast<LPARAM>(&window));
    if (window) {
      SkyVulkanContext context{};
      context.version = SKYBOOTSTRAP_API_VERSION;
      context.imageCount = gOverlaySession.imageCount;
      context.queueFamily = data->graphicsFamily;
      context.window = reinterpret_cast<uintptr_t>(window);
      context.instance = reinterpret_cast<uint64_t>(data->instance);
      context.physicalDevice = reinterpret_cast<uint64_t>(data->physicalDevice);
      context.device = reinterpret_cast<uint64_t>(data->device);
      context.queue = reinterpret_cast<uint64_t>(gOverlaySession.queue);
      context.renderPass = reinterpret_cast<uintptr_t>(gOverlaySession.renderPass);
      context.descriptorPool = reinterpret_cast<uintptr_t>(gOverlaySession.descriptorPool);
      context.getVulkanProc = SkyBootstrapResolveVulkanProc;
      SkyBootstrapLayerTrace("plugin-init probe: dispatching SkyPluginVulkanInit");
      SkyBootstrapDispatchVulkanInit(&context);
      gOverlaySession.pluginsInitialized = true;
      SkyBootstrapLayerTrace("plugin-init probe: SkyPluginVulkanInit returned");
    }
  }
  const uint32_t image = presentInfo->pImageIndices[0];
  if (image >= gOverlaySession.imageCount) return data->table.queuePresent(queue, presentInfo);
  Frame &frame = gOverlaySession.frames[image];
  if (data->table.waitForFences(data->device, 1, &frame.fence, VK_TRUE, 0) != VK_SUCCESS)
    return data->table.queuePresent(queue, presentInfo);
  if (data->table.resetFences(data->device, 1, &frame.fence) != VK_SUCCESS ||
      data->table.resetCommandPool(data->device, frame.pool, 0) != VK_SUCCESS) return data->table.queuePresent(queue, presentInfo);
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (data->table.beginCommandBuffer(frame.command, &begin) != VK_SUCCESS) return data->table.queuePresent(queue, presentInfo);
  if (gOverlaySession.renderPass) {
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = gOverlaySession.renderPass;
    pass.framebuffer = frame.framebuffer;
    pass.renderArea.extent = gOverlaySession.extent;
    data->table.cmdBeginRenderPass(frame.command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    if (SkyBootstrapPluginUiProbeEnabled())
      SkyBootstrapDispatchVulkanRender(reinterpret_cast<uint64_t>(frame.command));
    else if (SkyBootstrapPluginFrameProbeEnabled())
      SkyBootstrapDispatchVulkanWarmup(reinterpret_cast<uint64_t>(frame.command));
    data->table.cmdEndRenderPass(frame.command);
  }
  if (data->table.endCommandBuffer(frame.command) != VK_SUCCESS) return data->table.queuePresent(queue, presentInfo);
  std::vector<VkPipelineStageFlags> stages(presentInfo->waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.waitSemaphoreCount = presentInfo->waitSemaphoreCount;
  submit.pWaitSemaphores = presentInfo->pWaitSemaphores;
  submit.pWaitDstStageMask = stages.empty() ? nullptr : stages.data();
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &frame.command;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &frame.ready;
  if (data->table.queueSubmit(queue, 1, &submit, frame.fence) != VK_SUCCESS) return data->table.queuePresent(queue, presentInfo);
  VkPresentInfoKHR next = *presentInfo;
  next.waitSemaphoreCount = 1;
  next.pWaitSemaphores = &frame.ready;
  SkyBootstrapLayerTrace("sync probe: first empty submit forwarded to present");
  return data->table.queuePresent(queue, &next);
}

} // namespace

extern "C" uint64_t SKYBOOTSTRAP_CALL SkyBootstrapResolveVulkanProc(const char *name) {
  if (!name) return 0;
  // The active session owns swapchain resources. The resolver is called while
  // the layer lock is held, so it intentionally does not lock again.
  const VkDevice activeDevice = gActiveOverlaySession ? gOverlaySession.device : VK_NULL_HANDLE;
  DeviceData *device = deviceData(activeDevice);
  if (device && device->table.getDeviceProcAddr) {
    if (auto proc = device->table.getDeviceProcAddr(activeDevice, name)) {
      return reinterpret_cast<uintptr_t>(proc);
    }
  }
  if (!gInstances.empty()) {
    const VkInstance instanceHandle = device ? device->instance : gInstances.begin()->first;
    auto instance = gInstances.find(instanceHandle);
    if (instance != gInstances.end() && instance->second.getInstanceProcAddr) {
      if (auto proc = instance->second.getInstanceProcAddr(instanceHandle, name)) {
        return reinterpret_cast<uintptr_t>(proc);
      }
    }
    if (instanceHandle != gInstances.begin()->first) {
      const auto &fallback = *gInstances.begin();
      if (auto proc = fallback.second.getInstanceProcAddr(fallback.first, name)) {
      return reinterpret_cast<uintptr_t>(proc);
      }
    }
  }
  char trace[256]{};
  std::snprintf(trace, sizeof(trace), "resolver: MISSING %s", name);
  SkyBootstrapLayerTrace(trace);
  return 0;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo *info, const VkAllocationCallbacks *allocator, VkInstance *instance) {
  SkyBootstrapLayerTrace("vkCreateInstance entered");
  auto *link = reinterpret_cast<VkLayerInstanceCreateInfo *>(findLayerInfo(info, VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO, VK_LAYER_LINK_INFO));
  if (!link) { SkyBootstrapLayerTrace("vkCreateInstance: missing link info"); return VK_ERROR_INITIALIZATION_FAILED; }
  auto nextGipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  link->u.pLayerInfo = link->u.pLayerInfo->pNext;
  auto nextCreate = reinterpret_cast<PFN_vkCreateInstance>(nextGipa(VK_NULL_HANDLE, "vkCreateInstance"));
  if (!nextCreate) return VK_ERROR_INITIALIZATION_FAILED;
  const VkResult result = nextCreate(info, allocator, instance);
  if (result == VK_SUCCESS) {
    std::lock_guard<std::mutex> lock(gLock);
    gInstances[*instance] = {
      nextGipa,
      reinterpret_cast<PFN_vkDestroyInstance>(nextGipa(*instance, "vkDestroyInstance")),
      reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(nextGipa(*instance, "vkEnumeratePhysicalDevices"))
    };
  }
  SkyBootstrapLayerTrace(result == VK_SUCCESS ? "vkCreateInstance completed" : "vkCreateInstance failed downstream");
  return result;
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *allocator) {
  std::lock_guard<std::mutex> lock(gLock);
  auto it = gInstances.find(instance);
  if (it != gInstances.end() && it->second.destroyInstance) it->second.destroyInstance(instance, allocator);
  for (auto physical = gPhysicalDevices.begin(); physical != gPhysicalDevices.end();) {
    if (physical->second == instance) physical = gPhysicalDevices.erase(physical);
    else ++physical;
  }
  gInstances.erase(instance);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance instance, uint32_t *count, VkPhysicalDevice *devices) {
  std::lock_guard<std::mutex> lock(gLock);
  auto it = gInstances.find(instance);
  if (it == gInstances.end() || !it->second.enumeratePhysicalDevices)
    return VK_ERROR_INITIALIZATION_FAILED;
  const VkResult result = it->second.enumeratePhysicalDevices(instance, count, devices);
  if (result == VK_SUCCESS && devices && count) {
    for (uint32_t i = 0; i < *count; ++i) gPhysicalDevices[devices[i]] = instance;
  }
  return result;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *info, const VkAllocationCallbacks *allocator, VkDevice *device) {
  SkyBootstrapLayerTrace("vkCreateDevice entered");
  auto *link = reinterpret_cast<VkLayerDeviceCreateInfo *>(findLayerInfo(info, VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO, VK_LAYER_LINK_INFO));
  if (!link) { SkyBootstrapLayerTrace("vkCreateDevice: missing link info"); return VK_ERROR_INITIALIZATION_FAILED; }
  auto nextGipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
  auto nextGdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
  link->u.pLayerInfo = link->u.pLayerInfo->pNext;
  auto nextCreate = reinterpret_cast<PFN_vkCreateDevice>(nextGipa(VK_NULL_HANDLE, "vkCreateDevice"));
  if (!nextCreate) { SkyBootstrapLayerTrace("vkCreateDevice: downstream entry missing"); return VK_ERROR_INITIALIZATION_FAILED; }
  SkyBootstrapLayerTrace("vkCreateDevice calling downstream");
  const VkResult result = nextCreate(physicalDevice, info, allocator, device);
  if (result != VK_SUCCESS) { SkyBootstrapLayerTrace("vkCreateDevice downstream failed"); return result; }
  SkyBootstrapLayerTrace("vkCreateDevice downstream completed");
  auto *loaderData = reinterpret_cast<VkLayerDeviceCreateInfo *>(findLayerInfo(info, VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO, VK_LOADER_DATA_CALLBACK));
  SkyBootstrapLayerTrace(loaderData ? "vkCreateDevice loader data found" : "vkCreateDevice loader data absent");
  DeviceData data{};
  data.device = *device;
  data.physicalDevice = physicalDevice;
  auto physicalInstance = gPhysicalDevices.find(physicalDevice);
  data.instance = physicalInstance == gPhysicalDevices.end()
    ? (gInstances.empty() ? VK_NULL_HANDLE : gInstances.begin()->first)
    : physicalInstance->second;
  data.setLoaderData = loaderData ? loaderData->u.pfnSetDeviceLoaderData : nullptr;
  data.table.getDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(nextGdpa(*device, "vkGetDeviceProcAddr"));
  data.table.destroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(nextGdpa(*device, "vkDestroyDevice"));
  data.table.queuePresent = reinterpret_cast<PFN_vkQueuePresentKHR>(nextGdpa(*device, "vkQueuePresentKHR"));
  data.table.createSwapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(nextGdpa(*device, "vkCreateSwapchainKHR"));
  data.table.destroySwapchain = reinterpret_cast<PFN_vkDestroySwapchainKHR>(nextGdpa(*device, "vkDestroySwapchainKHR"));
  data.table.getDeviceQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(nextGdpa(*device, "vkGetDeviceQueue"));
  data.table.getSwapchainImages = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(nextGdpa(*device, "vkGetSwapchainImagesKHR"));
  data.table.createRenderPass = reinterpret_cast<PFN_vkCreateRenderPass>(nextGdpa(*device, "vkCreateRenderPass"));
  data.table.destroyRenderPass = reinterpret_cast<PFN_vkDestroyRenderPass>(nextGdpa(*device, "vkDestroyRenderPass"));
  data.table.createDescriptorPool = reinterpret_cast<PFN_vkCreateDescriptorPool>(nextGdpa(*device, "vkCreateDescriptorPool"));
  data.table.destroyDescriptorPool = reinterpret_cast<PFN_vkDestroyDescriptorPool>(nextGdpa(*device, "vkDestroyDescriptorPool"));
  data.table.createCommandPool = reinterpret_cast<PFN_vkCreateCommandPool>(nextGdpa(*device, "vkCreateCommandPool"));
  data.table.destroyCommandPool = reinterpret_cast<PFN_vkDestroyCommandPool>(nextGdpa(*device, "vkDestroyCommandPool"));
  data.table.allocateCommandBuffers = reinterpret_cast<PFN_vkAllocateCommandBuffers>(nextGdpa(*device, "vkAllocateCommandBuffers"));
  data.table.createFence = reinterpret_cast<PFN_vkCreateFence>(nextGdpa(*device, "vkCreateFence"));
  data.table.destroyFence = reinterpret_cast<PFN_vkDestroyFence>(nextGdpa(*device, "vkDestroyFence"));
  data.table.createSemaphore = reinterpret_cast<PFN_vkCreateSemaphore>(nextGdpa(*device, "vkCreateSemaphore"));
  data.table.destroySemaphore = reinterpret_cast<PFN_vkDestroySemaphore>(nextGdpa(*device, "vkDestroySemaphore"));
  data.table.createImageView = reinterpret_cast<PFN_vkCreateImageView>(nextGdpa(*device, "vkCreateImageView"));
  data.table.destroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(nextGdpa(*device, "vkDestroyImageView"));
  data.table.createFramebuffer = reinterpret_cast<PFN_vkCreateFramebuffer>(nextGdpa(*device, "vkCreateFramebuffer"));
  data.table.destroyFramebuffer = reinterpret_cast<PFN_vkDestroyFramebuffer>(nextGdpa(*device, "vkDestroyFramebuffer"));
  data.table.waitForFences = reinterpret_cast<PFN_vkWaitForFences>(nextGdpa(*device, "vkWaitForFences"));
  data.table.resetFences = reinterpret_cast<PFN_vkResetFences>(nextGdpa(*device, "vkResetFences"));
  data.table.resetCommandPool = reinterpret_cast<PFN_vkResetCommandPool>(nextGdpa(*device, "vkResetCommandPool"));
  data.table.beginCommandBuffer = reinterpret_cast<PFN_vkBeginCommandBuffer>(nextGdpa(*device, "vkBeginCommandBuffer"));
  data.table.cmdBeginRenderPass = reinterpret_cast<PFN_vkCmdBeginRenderPass>(nextGdpa(*device, "vkCmdBeginRenderPass"));
  data.table.cmdEndRenderPass = reinterpret_cast<PFN_vkCmdEndRenderPass>(nextGdpa(*device, "vkCmdEndRenderPass"));
  data.table.endCommandBuffer = reinterpret_cast<PFN_vkEndCommandBuffer>(nextGdpa(*device, "vkEndCommandBuffer"));
  data.table.queueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(nextGdpa(*device, "vkQueueSubmit"));
  SkyBootstrapLayerTrace("vkCreateDevice dispatch table created");
  // A layer must call extra instance commands through the next instance
  // dispatch chain, never through the global loader trampoline.
  PFN_vkGetPhysicalDeviceQueueFamilyProperties getQueueFamilies = nullptr;
  if (data.instance != VK_NULL_HANDLE) {
    const auto instance = gInstances.find(data.instance);
    if (instance != gInstances.end()) {
    getQueueFamilies = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
      instance->second.getInstanceProcAddr(data.instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    }
  }
  if (!getQueueFamilies) {
    SkyBootstrapLayerTrace("vkCreateDevice: queue-family entry missing");
    return VK_ERROR_INITIALIZATION_FAILED;
  }
  uint32_t familyCount = 0;
  SkyBootstrapLayerTrace("vkCreateDevice querying queue families");
  getQueueFamilies(physicalDevice, &familyCount, nullptr);
  std::vector<VkQueueFamilyProperties> families(familyCount);
  getQueueFamilies(physicalDevice, &familyCount, families.data());
  SkyBootstrapLayerTrace("vkCreateDevice queue families queried");
  for (uint32_t i = 0; i < info->queueCreateInfoCount; ++i) {
    const auto &queueInfo = info->pQueueCreateInfos[i];
    for (uint32_t j = 0; j < queueInfo.queueCount; ++j) {
      VkQueue queue{}; data.table.getDeviceQueue(*device, queueInfo.queueFamilyIndex, j, &queue);
      if (data.setLoaderData) data.setLoaderData(*device, queue);
      gQueues[queue] = *device;
      if (!data.graphicsQueue && queueInfo.queueFamilyIndex < families.size() &&
          (families[queueInfo.queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
        data.graphicsQueue = queue;
        data.graphicsFamily = queueInfo.queueFamilyIndex;
      }
    }
  }
  // A presentation-only queue is still preferable to failing a game which
  // created no graphics queue in this device. The layer will simply render on
  // that queue in this rare fallback case.
  if (!data.graphicsQueue && info->queueCreateInfoCount) {
    VkQueue queue{};
    data.table.getDeviceQueue(*device, info->pQueueCreateInfos[0].queueFamilyIndex, 0, &queue);
    data.graphicsQueue = queue;
    data.graphicsFamily = info->pQueueCreateInfos[0].queueFamilyIndex;
  }
  SkyBootstrapLayerTrace("vkCreateDevice queues recorded");
  std::lock_guard<std::mutex> lock(gLock);
  gDevices[*device] = data;
  SkyBootstrapLayerTrace("vkCreateDevice completed");
  return result;
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *allocator) {
  std::lock_guard<std::mutex> lock(gLock);
  auto it = gDevices.find(device);
  if (it != gDevices.end()) {
    for (auto session = gOverlaySessions.begin(); session != gOverlaySessions.end();) {
      if (session->first.first != device) { ++session; continue; }
      gActiveOverlaySession = &session->second;
      destroyOverlaySession(&it->second);
      session = gOverlaySessions.erase(session);
    }
    gActiveOverlaySession = nullptr;
    for (auto queue = gQueues.begin(); queue != gQueues.end();) {
      if (queue->second == device) queue = gQueues.erase(queue);
      else ++queue;
    }
  }
  if (it != gDevices.end() && it->second.table.destroyDevice) it->second.table.destroyDevice(device, allocator);
  gDevices.erase(device);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *info, const VkAllocationCallbacks *allocator, VkSwapchainKHR *swapchain) {
  SkyBootstrapLayerTrace("vkCreateSwapchainKHR entered");
  std::lock_guard<std::mutex> lock(gLock);
  DeviceData *data = deviceData(device);
  if (!data || !data->table.createSwapchain) { SkyBootstrapLayerTrace("vkCreateSwapchainKHR device table missing"); return VK_ERROR_INITIALIZATION_FAILED; }
  const VkResult result = data->table.createSwapchain(device, info, allocator, swapchain);
  SkyBootstrapLayerTrace(result == VK_SUCCESS ? "vkCreateSwapchainKHR downstream completed" : "vkCreateSwapchainKHR downstream failed");
  if (result == VK_SUCCESS) {
    if (SkyBootstrapResourceProbeEnabled()) probeSwapchainResources(*data, *swapchain);
    if (SkyBootstrapSyncProbeEnabled()) createOverlaySession(*data, *swapchain, info);
  }
  return result;
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *allocator) {
  std::lock_guard<std::mutex> lock(gLock);
  DeviceData *data = deviceData(device);
  if (!data || !data->table.destroySwapchain) return;
  auto session = gOverlaySessions.find({device, swapchain});
  if (session != gOverlaySessions.end()) {
    gActiveOverlaySession = &session->second;
    destroyOverlaySession(data);
    gOverlaySessions.erase(session);
    gActiveOverlaySession = nullptr;
  }
  data->table.destroySwapchain(device, swapchain, allocator);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *info) {
  std::lock_guard<std::mutex> lock(gLock);
  auto found = gQueues.find(queue);
  DeviceData *data = found == gQueues.end() ? nullptr : deviceData(found->second);
  if (!data || !data->table.queuePresent) return VK_ERROR_INITIALIZATION_FAILED;
  if (SkyBootstrapSyncProbeEnabled()) return presentOverlaySession(queue, info, data);
  return data->table.queuePresent(queue, info);
}

extern "C" SKYBOOTSTRAP_API VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL SkyVkGetInstanceProcAddr(VkInstance instance, const char *name) {
  if (name && (!strcmp(name, "vkCreateInstance") || !strcmp(name, "vkCreateDevice")))
    SkyBootstrapLayerTrace(name);
  if (!strcmp(name, "vkGetInstanceProcAddr")) {
    PFN_vkGetInstanceProcAddr self = &SkyVkGetInstanceProcAddr;
    return reinterpret_cast<PFN_vkVoidFunction>(self);
  }
  if (!strcmp(name, "vkCreateInstance")) return reinterpret_cast<PFN_vkVoidFunction>(vkCreateInstance);
  if (!strcmp(name, "vkDestroyInstance")) return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyInstance);
  if (!strcmp(name, "vkGetDeviceProcAddr")) return reinterpret_cast<PFN_vkVoidFunction>(SkyVkGetDeviceProcAddr);
  if (!strcmp(name, "vkCreateDevice")) return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDevice);
  if (!strcmp(name, "vkDestroyDevice")) return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyDevice);
  if (!strcmp(name, "vkEnumeratePhysicalDevices")) {
    const PFN_vkEnumeratePhysicalDevices enumerate = &vkEnumeratePhysicalDevices;
    return reinterpret_cast<PFN_vkVoidFunction>(enumerate);
  }
  std::lock_guard<std::mutex> lock(gLock);
  auto it = gInstances.find(instance);
  return it != gInstances.end() ? it->second.getInstanceProcAddr(instance, name) : nullptr;
}

extern "C" SKYBOOTSTRAP_API VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL SkyVkGetDeviceProcAddr(VkDevice device, const char *name) {
  if (name && (!strcmp(name, "vkCreateSwapchainKHR") || !strcmp(name, "vkQueuePresentKHR")))
    SkyBootstrapLayerTrace(name);
  if (!strcmp(name, "vkGetDeviceProcAddr")) {
    PFN_vkGetDeviceProcAddr self = &SkyVkGetDeviceProcAddr;
    return reinterpret_cast<PFN_vkVoidFunction>(self);
  }
  if (!strcmp(name, "vkCreateSwapchainKHR")) return reinterpret_cast<PFN_vkVoidFunction>(vkCreateSwapchainKHR);
  if (!strcmp(name, "vkDestroySwapchainKHR")) return reinterpret_cast<PFN_vkVoidFunction>(vkDestroySwapchainKHR);
  if (!strcmp(name, "vkQueuePresentKHR")) return reinterpret_cast<PFN_vkVoidFunction>(vkQueuePresentKHR);
  if (!strcmp(name, "vkDestroyDevice")) return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyDevice);
  std::lock_guard<std::mutex> lock(gLock);
  DeviceData *data = deviceData(device);
  return data && data->table.getDeviceProcAddr ? data->table.getDeviceProcAddr(device, name) : nullptr;
}
