#pragma once

#include <stdint.h>

#ifdef _WIN32
  #ifdef SKYOVERLAY_BUILD
    #define SKYOVERLAY_API __declspec(dllexport)
  #else
    #define SKYOVERLAY_API __declspec(dllimport)
  #endif
  #define SKYOVERLAY_CALL __stdcall
#else
  #define SKYOVERLAY_API
  #define SKYOVERLAY_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (SKYOVERLAY_CALL *SkyOverlayRenderFn)(void);

/**
 * @brief Registers a raw render callback invoked every frame within the D3D11 ImGui loop.
 * @param callback Function pointer to the render callback.
 * @return 1 on success, 0 on failure.
 */
SKYOVERLAY_API int SKYOVERLAY_CALL SkyOverlayRegisterRender(SkyOverlayRenderFn callback);

/**
 * @brief Unregisters a previously registered raw render callback.
 * @param callback Function pointer to unregister.
 */
SKYOVERLAY_API void SKYOVERLAY_CALL SkyOverlayUnregisterRender(SkyOverlayRenderFn callback);

/**
 * @brief Registers a named plugin window with individual hotkey and visibility controls.
 *
 * This integrates the plugin directly into the SkyOverlay Plugin Manager window,
 * allowing users to toggle it with a dedicated hotkey or via a checkbox in the UI.
 *
 * @param name Unique display name of the plugin (e.g. "Community Speedrun Tool").
 * @param callback Function pointer to render the plugin's ImGui interface.
 * @param defaultVk Virtual Key code for toggling this plugin (e.g. VK_F6, 0 for none).
 * @param defaultVisible Initial visibility state (1 = visible, 0 = hidden).
 * @return Unique window ID (> 0) on success, or 0 on failure.
 */
SKYOVERLAY_API int SKYOVERLAY_CALL SkyOverlayRegisterPluginWindow(
    const char* name,
    SkyOverlayRenderFn callback,
    uint32_t defaultVk,
    int defaultVisible);

/**
 * @brief Unregisters a named plugin window by ID.
 * @param windowId The window ID returned by SkyOverlayRegisterPluginWindow.
 */
SKYOVERLAY_API void SKYOVERLAY_CALL SkyOverlayUnregisterPluginWindow(int windowId);

/**
 * @brief Checks if a registered plugin window is currently visible.
 * @param windowId The window ID returned by SkyOverlayRegisterPluginWindow.
 * @return 1 if visible, 0 if hidden or ID not found.
 */
SKYOVERLAY_API int SKYOVERLAY_CALL SkyOverlayGetPluginWindowVisible(int windowId);

/**
 * @brief Sets the visibility of a registered plugin window.
 * @param windowId The window ID returned by SkyOverlayRegisterPluginWindow.
 * @param visible 1 to show, 0 to hide.
 */
SKYOVERLAY_API void SKYOVERLAY_CALL SkyOverlaySetPluginWindowVisible(int windowId, int visible);

/**
 * @brief Sets the dedicated hotkey for a registered plugin window.
 * @param windowId The window ID returned by SkyOverlayRegisterPluginWindow.
 * @param vk Virtual Key code (0 to disable hotkey).
 */
SKYOVERLAY_API void SKYOVERLAY_CALL SkyOverlaySetPluginWindowHotkey(int windowId, uint32_t vk);

/**
 * @brief Controls the visibility of the central SkyOverlay Plugin Manager window.
 * @param show 1 to show, 0 to hide.
 */
SKYOVERLAY_API void SKYOVERLAY_CALL SkyOverlayShowManager(int show);

/**
 * @brief Checks if the central SkyOverlay Plugin Manager window is currently visible.
 * @return 1 if visible, 0 if hidden.
 */
SKYOVERLAY_API int SKYOVERLAY_CALL SkyOverlayIsManagerVisible(void);

/**
 * @brief Returns the shared ImGuiContext pointer managed by SkyOverlay.
 *
 * Plugins calling ImGui APIs across DLL boundaries should call:
 *   ImGui::SetCurrentContext((ImGuiContext*)SkyOverlayGetImGuiContext());
 * during their initialization to bind their ImGui commands to this overlay.
 */
SKYOVERLAY_API void* SKYOVERLAY_CALL SkyOverlayGetImGuiContext(void);

/**
 * @brief Checks if the master overlay canvas is currently visible.
 */
SKYOVERLAY_API int SKYOVERLAY_CALL SkyOverlayIsVisible(void);

/**
 * @brief Sets the visibility of the master overlay canvas.
 */
SKYOVERLAY_API void SKYOVERLAY_CALL SkyOverlaySetVisible(int visible);

#ifdef __cplusplus
}
#endif
