#pragma once

#include <windows.h>
#include <stdint.h>
#include "skyoverlay_api.h"

namespace skyoverlay {

bool start(HMODULE hModule);
void stop();

// Raw render callbacks
int registerRender(SkyOverlayRenderFn callback);
void unregisterRender(SkyOverlayRenderFn callback);

// Named plugin window registration with dedicated hotkey & visibility
int registerPluginWindow(const char* name, SkyOverlayRenderFn callback, uint32_t defaultVk, int defaultVisible);
void unregisterPluginWindow(int windowId);
int getPluginWindowVisible(int windowId);
void setPluginWindowVisible(int windowId, int visible);
void setPluginWindowHotkey(int windowId, uint32_t vk);

// Plugin Manager window controls
void showManager(int show);
int isManagerVisible();

// Master Overlay Controls
void* getImGuiContext();
bool isVisible();
void setVisible(bool visible);

}  // namespace skyoverlay
