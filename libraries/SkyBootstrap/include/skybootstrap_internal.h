#pragma once

#include "skybootstrap_api.h"

// Internal bridge used by the Vulkan layer. Kept separate from the public API
// so ordinary plugins never have to know about Bootstrap's container types.
extern "C" void SkyBootstrapDispatchVulkanInit(const SkyVulkanContext *context);
extern "C" void SkyBootstrapDispatchVulkanWarmup(uint64_t commandBuffer);
extern "C" void SkyBootstrapDispatchVulkanRender(uint64_t commandBuffer);
extern "C" void SkyBootstrapDispatchWindowMessage(uintptr_t window, uint32_t message, uintptr_t wParam, intptr_t lParam);
extern "C" void SkyBootstrapLayerTrace(const char *message);
extern "C" uint64_t SKYBOOTSTRAP_CALL SkyBootstrapResolveVulkanProc(const char *name);
extern "C" int SkyBootstrapRendererEnabled(void);
extern "C" int SkyBootstrapResourceProbeEnabled(void);
extern "C" int SkyBootstrapSyncProbeEnabled(void);
extern "C" int SkyBootstrapRenderPassProbeEnabled(void);
extern "C" int SkyBootstrapPluginInitProbeEnabled(void);
extern "C" int SkyBootstrapPluginFrameProbeEnabled(void);
extern "C" int SkyBootstrapPluginUiProbeEnabled(void);
extern "C" int SkyBootstrapInputProbeEnabled(void);
