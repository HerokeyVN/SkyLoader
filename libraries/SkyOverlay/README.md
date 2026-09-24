# SkyOverlay

`SkyOverlay.dll` is the official open-source Direct3D 11 + Dear ImGui transparent canvas and mod hub for the [SkyLoader](../../README.md) ecosystem.

It brings community mod developers the rapid prototyping experience of **ThatSkyLoader** (immediate ImGui UI callbacks) while running safely on an independent D3D11 canvas that **never hooks or interferes with Sky's Vulkan graphics pipeline**.

---

## Architecture & Safety Guarantees

```text
Sky.exe
├── Native Vulkan Engine (Untouched presentation path)
└── SkyBootstrap.dll (Kernel Host)
    └── SkyOverlay.dll (Plugin Loaded via Named Pipe)
        ├── Transparent D3D11 Host Window (WS_EX_LAYERED | WS_EX_TOPMOST)
        │   ├── Safe Startup (Eliminates loader-lock with Steam DRM & Vulkan ICD)
        │   ├── Intelligent Click-Through Routing (WM_NCHITTEST)
        │   ├── AMD MPO Transparency Fix (DwmExtendFrameIntoClientArea)
        │   ├── Extended Glyph Ranges (Segoe UI Symbols)
        │   └── Shared ImGui Context & Dispatcher
        └── SkyOverlay Plugin Manager & Hotkey System
            ├── Master Canvas Toggle: F5 / Insert
            ├── Dedicated Per-Plugin Hotkeys (e.g., F6, F7, NumPad)
            └── Persistent User Preferences (SkyOverlay.ini)
```

### Key Architectural Highlights:
1. **Zero Vulkan Hooking**: Runs on a separate, borderless, transparent D3D11 device composed by Windows Desktop Window Manager (DWM).
2. **Safe Delayed Startup**: Waits 5 seconds for `Sky.exe` to complete its Vulkan engine initialization and window creation before creating the D3D11 device, preventing early startup loader-lock conflicts in `ntdll.dll`.
3. **AMD MPO & Transparency Stability**: Employs `DwmExtendFrameIntoClientArea` with negative margins to ensure pure transparency without black-box artifacts on AMD Radeon GPUs.
4. **Intelligent Click-Through**: Clicks outside active ImGui windows pass through seamlessly to the game (`HTTRANSPARENT`), allowing unrestricted gameplay while menus are open.
5. **Conflict-Free Hotkey Hierarchy**:
   | Component | Hotkeys | Description |
   |---|---|---|
   | **Master Canvas** | `F5` or `Insert` | Toggles the entire D3D11 overlay canvas (master sleep/wake). |
   | **SkyToolkit** | `Alt + \`` | Exclusively reserved for SkyToolkit. Never conflicted. |
   | **Individual Plugins** | Customizable (`F6`, `Home`, etc.) | Configurable per-plugin via UI or API. Wakes canvas automatically. |

---

## SkyOverlay Plugin Manager

When `SkyOverlay` is active (`F5` / `Insert`), it renders a subtle status badge in the top-left corner with an **`Open Manager`** button.

The **SkyOverlay Plugin Manager** window provides:
- **Active Checkbox (`[x]`)**: Show or hide individual plugin windows independently without affecting other mods.
- **Toggle Hotkey Selector**: A dropdown selector allowing players to rebind individual plugin shortcuts on the fly (`None`, `F1-F4`, `F6-F12`, `Home`, `End`, `Delete`, `Page Up/Down`, `NumPad 0-9`).
- **Automatic Wakeup**: Pressing a plugin's assigned hotkey when the master canvas is hidden will automatically wake up the overlay and reveal that plugin's window.
- **Persistence**: Window visibility and hotkey choices are automatically saved to `dist/SkyOverlay.ini`.

---

## Writing an ImGui Mod with SkyOverlay

### Option A: Named Plugin Window with Dedicated Hotkey (Recommended)

Register your plugin with `SkyOverlayRegisterPluginWindow` to gain automatic hotkey toggles, manager UI integration, and INI persistence:

```cpp
#include <windows.h>
#include <imgui.h>
#include "skybootstrap_api.h"
#include "skyoverlay_api.h"

static int g_MyWindowId = 0;
static float g_SpeedMultiplier = 1.0f;

// 1. Define your ImGui render callback
void MySpeedrunModRender() {
    ImGui::SetNextWindowSize(ImVec2(300, 160), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Community Speedrun Tool")) {
        ImGui::Text("Welcome to Sky PC Modding!");
        ImGui::SliderFloat("Speed Multiplier", &g_SpeedMultiplier, 0.5f, 3.0f);
        if (ImGui::Button("Reset to Default")) {
            g_SpeedMultiplier = 1.0f;
        }
    }
    ImGui::End();
}

// 2. SkyBootstrap entry point
extern "C" __declspec(dllexport) int __stdcall SkyPluginInit(const SkyBootstrapApi* api) {
    if (api && api->log) {
        api->log("SpeedrunMod loaded.");
    }

    HMODULE overlayMod = GetModuleHandleW(L"SkyOverlay.dll");
    if (overlayMod) {
        auto getCtx = reinterpret_cast<void*(*)()>(
            GetProcAddress(overlayMod, "SkyOverlayGetImGuiContext"));
        auto regWin = reinterpret_cast<int(*)(const char*, SkyOverlayRenderFn, uint32_t, int)>(
            GetProcAddress(overlayMod, "SkyOverlayRegisterPluginWindow"));

        if (getCtx && regWin) {
            // Bind ImGui context to SkyOverlay
            ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(getCtx()));

            // Register window: Name, Render callback, Default Hotkey (VK_F6), Default Visible (1)
            g_MyWindowId = regWin("Speedrun Tool", MySpeedrunModRender, VK_F6, 1);
        }
    }
    return 1;
}

// 3. Cleanup on unload
extern "C" __declspec(dllexport) void __stdcall SkyPluginShutdown() {
    HMODULE overlayMod = GetModuleHandleW(L"SkyOverlay.dll");
    if (overlayMod && g_MyWindowId > 0) {
        auto unregWin = reinterpret_cast<void(*)(int)>(
            GetProcAddress(overlayMod, "SkyOverlayUnregisterPluginWindow"));
        if (unregWin) {
            unregWin(g_MyWindowId);
        }
    }
}
```

---

### Option B: Raw Render Callback (Low-Level)

For mods requiring full control over when and how their windows are rendered:

```cpp
extern "C" __declspec(dllexport) int __stdcall SkyPluginInit(const SkyBootstrapApi*) {
    HMODULE overlayMod = GetModuleHandleW(L"SkyOverlay.dll");
    if (overlayMod) {
        auto getCtx = reinterpret_cast<void*(*)()>(GetProcAddress(overlayMod, "SkyOverlayGetImGuiContext"));
        auto regRender = reinterpret_cast<int(*)(SkyOverlayRenderFn)>(GetProcAddress(overlayMod, "SkyOverlayRegisterRender"));
        if (getCtx && regRender) {
            ImGui::SetCurrentContext((ImGuiContext*)getCtx());
            regRender(MySpeedrunModRender);
        }
    }
    return 1;
}
```

---

## Public C ABI (`include/skyoverlay_api.h`)

```c
typedef void (__stdcall *SkyOverlayRenderFn)(void);

// Named Plugin Window Management (Features Hotkey & UI Manager Integration)
SKYOVERLAY_API int  __stdcall SkyOverlayRegisterPluginWindow(const char* name, SkyOverlayRenderFn cb, uint32_t defaultVk, int defaultVisible);
SKYOVERLAY_API void __stdcall SkyOverlayUnregisterPluginWindow(int windowId);
SKYOVERLAY_API int  __stdcall SkyOverlayGetPluginWindowVisible(int windowId);
SKYOVERLAY_API void __stdcall SkyOverlaySetPluginWindowVisible(int windowId, int visible);
SKYOVERLAY_API void __stdcall SkyOverlaySetPluginWindowHotkey(int windowId, uint32_t vk);

// Plugin Manager Window Controls
SKYOVERLAY_API void __stdcall SkyOverlayShowManager(int show);
SKYOVERLAY_API int  __stdcall SkyOverlayIsManagerVisible(void);

// Raw Render Registration
SKYOVERLAY_API int  __stdcall SkyOverlayRegisterRender(SkyOverlayRenderFn callback);
SKYOVERLAY_API void __stdcall SkyOverlayUnregisterRender(SkyOverlayRenderFn callback);

// Master Canvas Controls
SKYOVERLAY_API void* __stdcall SkyOverlayGetImGuiContext(void);
SKYOVERLAY_API int   __stdcall SkyOverlayIsVisible(void);
SKYOVERLAY_API void  __stdcall SkyOverlaySetVisible(int visible);
```

---

## Building SkyOverlay

To build `SkyOverlay` standalone:

```powershell
cd D:\Develop\Language\C++\SkyLoader
mingw32-make -C libraries\SkyOverlay all
```

Output: `dist\SkyOverlay.dll`

To build the complete SkyLoader suite:

```powershell
mingw32-make all
```

---

## Configuration (`dist\SkyLoader.ini`)

Register `SkyOverlay.dll` alongside your custom plugins in `SkyLoader.ini`:

```ini
[Dlls]
Count=2
Dll0=D:\Develop\Language\C++\SkyLoader\dist\SkyOverlay.dll
DllEnabled0=1
Dll1=D:\path\to\YourCustomMod.dll
DllEnabled1=1
```

---

## Community Credits

- **Lukas (`sml-pc`)**: Original Sky mod loader research and DLL proxy design.
- **XeTrinityz (`ThatSkyLoader`)**: Rapid ImGui plugin rendering concept and plugin management workflow.
- **MrGatto**: Named Pipe IPC and remote injection patterns.
- **HerokeyVN**: Multi-architecture loader design, SkyLoader orchestration, and SkyOverlay architecture.
