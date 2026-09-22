# SkyLoader

SkyLoader is a Windows launcher and local DLL manager for **Sky: Children of the Light**. It starts Sky through Steam, injects `SkyBootstrap.dll` as soon as the configured game process appears, and asks Bootstrap to load registered DLLs as plugins.

The project is intentionally local-only: it does not download plugins, modify the game installation, or contact a server.

## Features

- Launch Sky through Steam using `steam://run/2325290`.
- Match the configured, canonical `Sky.exe` path before injecting.
- Persist the game path and plugin list in `SkyLoader.ini`.
- Inject the Bootstrap host before loading registered plugins.
- Load additional DLLs through a named pipe without copying them into the game directory.
- Provide a Vulkan implicit layer for swapchain and present lifecycle integration.
- Keep overlay resources scoped to each `(VkDevice, VkSwapchainKHR)` pair.
- Fail open: if a session is not ready, the original game present call is forwarded.

## Architecture

```text
SkyLoader.exe
├── Win32 UI
├── LoaderController
│   ├── Sky process matching
│   ├── Bootstrap injection
│   └── plugin-load requests
└── SkyLoader.ini

Sky.exe
└── SkyBootstrap.dll
    ├── local Vulkan layer manifest
    ├── named-pipe plugin host
    ├── Vulkan instance/device registries
    └── OverlaySession registry
        └── SkyToolkit or other plugin DLLs
```

### Runtime responsibilities

- **Win32 UI** edits settings and displays status. It does not own injection details.
- **LoaderController** owns process discovery, Bootstrap installation, and plugin-load policy.
- **SkyBootstrap** is the in-process host. It owns plugin lifetime and prepares the process-local Vulkan layer.
- **OverlaySession** owns Vulkan resources for one device/swapchain pair. Swapchain recreation creates a new session and destroys the old one.
- **Plugins** own their feature logic and ImGui context. Bootstrap supplies the Vulkan context and command buffer.

More project terminology is documented in [CONTEXT.md](CONTEXT.md). The session-registry decision is recorded in [ADR 0001](docs/adr/0001-overlay-session-registry.md).

## Requirements

- Windows 10/11, 64-bit.
- Steam installed and signed in.
- A 64-bit `Sky.exe` installed through Steam.
- MinGW-w64 with `g++` and `mingw32-make` on `PATH`.
- Vulkan headers available through the SkyToolkit reference tree used by the Bootstrap Makefile.

## Build

Run from the SkyLoader directory:

```powershell
cd D:\Develop\Language\C++\SkyLoader
mingw32-make clean
mingw32-make all
```

Build outputs:

```text
dist\SkyLoader.exe
dist\SkyBootstrap.dll
```

`SkyBootstrap.dll` is built automatically by the top-level Makefile. To build it separately:

```powershell
cd D:\Develop\Language\C++\SkyLoader\libraries\SkyBootstrap
mingw32-make all
```

The build currently emits warnings for Vulkan aggregate initialization and dynamic callback casts; these are non-fatal and do not prevent the binaries from being produced.

## Configuration

`SkyLoader.ini` is created beside `SkyLoader.exe`.

```ini
[SkyLoader]
GamePath=C:\Program Files (x86)\Steam\steamapps\common\Sky Children of the Light\Sky.exe
BootstrapPath=SkyBootstrap.dll

[Dlls]
Count=1
Dll0=D:\Mods\SkyToolkit\sky-toolkit-plugin.dll
```

Bootstrap reads `SkyBootstrap.ini` beside `SkyBootstrap.dll`:

```ini
[Vulkan]
EnableOverlay=1
```

The old staged Vulkan keys are ignored for compatibility. `EnableOverlay=1` enables the validated production path; input is polled by the plugin and no game-window subclass is installed.

## Usage

1. Start `SkyLoader.exe`.
2. Verify the `Sky.exe` path with **Browse Sky...**, or keep the Steam default.
3. Add one or more 64-bit DLLs with **Add DLL...**.
4. Press **Launch Sky**. SkyLoader opens Steam instead of executing `Sky.exe` directly.
5. SkyLoader waits for the configured executable, injects Bootstrap, then sends plugin-load requests.

SkyLoader also starts its watcher immediately, so launching Sky directly from the Steam client still triggers the same injection flow. **Inject selected** can be used to request one plugin again after Sky is already running.

> [!IMPORTANT]
> A DLL cannot be injected before a matching `Sky.exe` process exists. SkyLoader injects into the configured executable path only; it does not attach to an arbitrary process with the same filename.

## Plugin ABI

The stable C ABI is declared in [`libraries/SkyBootstrap/include/skybootstrap_api.h`](libraries/SkyBootstrap/include/skybootstrap_api.h).

Optional exports are discovered with `GetProcAddress`:

| Export | Purpose |
| --- | --- |
| `SkyPluginInit(const SkyBootstrapApi*)` | Process-level plugin initialization |
| `SkyPluginShutdown()` | Plugin shutdown hook |
| `SkyPluginVulkanInit(const SkyVulkanContext*)` | Initialize Vulkan resources for the overlay context |
| `SkyPluginVulkanWarmup(uint64_t)` | Record a non-UI validation frame |
| `SkyPluginVulkanRender(uint64_t)` | Record overlay UI commands |
| `SkyPluginWindowMessage(...)` | Optional window-message callback |

Plugins must use `SkyVulkanContext::getVulkanProc` for Vulkan commands instead of importing loader entry points directly. The ABI transports Vulkan handles as integers so plugins do not need to share Bootstrap's C++ types.

## Bootstrap pipe

SkyLoader sends UTF-8 commands to:

```text
\\.\pipe\sky_bootstrap
```

Current command format:

```text
LOAD D:\path\to\plugin.dll
```

Bootstrap keeps the plugin DLL loaded for the lifetime of the game process. The loader treats a successful pipe write as an accepted request; plugin initialization errors are recorded in `SkyBootstrap.log`.

## Logs and troubleshooting

Bootstrap writes its diagnostic log beside the DLL:

```text
dist\SkyBootstrap.log
```

If the overlay does not appear:

1. Confirm `SkyBootstrap.dll` and the plugin DLL are 64-bit.
2. Confirm the configured `GamePath` matches the running executable path.
3. Confirm `SkyBootstrap.ini` contains `EnableOverlay=1`.
4. Check `SkyBootstrap.log` for layer negotiation, swapchain, and plugin-init messages.
5. Rebuild both the loader and Bootstrap; do not mix binaries from different builds.

If Sky starts but the plugin is absent, verify that the DLL exists at the exact path stored in `SkyLoader.ini` and that its dependencies are available to `Sky.exe`.

## Known limitations

- The current pipe protocol has no request ID or acknowledgement payload; a successful write means only that Bootstrap accepted the command transport.
- Plugin callbacks execute on the Vulkan present path, so plugin code must remain fast and must not block.
- Plugin state is currently owned by the Bootstrap registry rather than a fully independent per-session host.
- No automated Vulkan integration test suite is included yet; validation currently relies on clean builds, launcher smoke tests, and in-game testing.
