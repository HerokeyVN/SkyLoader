#include "overlay_host.h"
#include "skyoverlay_api.h"
#include <windows.h>

static HMODULE gSelfModule = nullptr;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
  switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
      DisableThreadLibraryCalls(hinstDLL);
      gSelfModule = hinstDLL;
      skyoverlay::start(hinstDLL);
      break;
    case DLL_PROCESS_DETACH:
      if (!lpvReserved) {
        skyoverlay::stop();
      }
      break;
  }
  return TRUE;
}

// SkyBootstrap plugin hooks
extern "C" __declspec(dllexport) int __stdcall SkyPluginInit(const void* /*api*/) {
  skyoverlay::start(gSelfModule);
  return 1;
}

extern "C" __declspec(dllexport) void __stdcall SkyPluginShutdown(void) {
  skyoverlay::stop();
}

// Public API implementation
extern "C" {

SKYOVERLAY_API int SKYOVERLAY_CALL SkyOverlayRegisterRender(SkyOverlayRenderFn callback) {
  return skyoverlay::registerRender(callback);
}

SKYOVERLAY_API void SKYOVERLAY_CALL SkyOverlayUnregisterRender(SkyOverlayRenderFn callback) {
  skyoverlay::unregisterRender(callback);
}

SKYOVERLAY_API int SKYOVERLAY_CALL SkyOverlayRegisterPluginWindow(
    const char* name,
    SkyOverlayRenderFn callback,
    uint32_t defaultVk,
    int defaultVisible) {
  return skyoverlay::registerPluginWindow(name, callback, defaultVk, defaultVisible);
}

SKYOVERLAY_API void SKYOVERLAY_CALL SkyOverlayUnregisterPluginWindow(int windowId) {
  skyoverlay::unregisterPluginWindow(windowId);
}

SKYOVERLAY_API int SKYOVERLAY_CALL SkyOverlayGetPluginWindowVisible(int windowId) {
  return skyoverlay::getPluginWindowVisible(windowId);
}

SKYOVERLAY_API void SKYOVERLAY_CALL SkyOverlaySetPluginWindowVisible(int windowId, int visible) {
  skyoverlay::setPluginWindowVisible(windowId, visible);
}

SKYOVERLAY_API void SKYOVERLAY_CALL SkyOverlaySetPluginWindowHotkey(int windowId, uint32_t vk) {
  skyoverlay::setPluginWindowHotkey(windowId, vk);
}

SKYOVERLAY_API void SKYOVERLAY_CALL SkyOverlayShowManager(int show) {
  skyoverlay::showManager(show);
}

SKYOVERLAY_API int SKYOVERLAY_CALL SkyOverlayIsManagerVisible(void) {
  return skyoverlay::isManagerVisible();
}

SKYOVERLAY_API void* SKYOVERLAY_CALL SkyOverlayGetImGuiContext(void) {
  return skyoverlay::getImGuiContext();
}

SKYOVERLAY_API int SKYOVERLAY_CALL SkyOverlayIsVisible(void) {
  return skyoverlay::isVisible() ? 1 : 0;
}

SKYOVERLAY_API void SKYOVERLAY_CALL SkyOverlaySetVisible(int visible) {
  skyoverlay::setVisible(visible != 0);
}

}
