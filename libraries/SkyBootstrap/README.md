# SkyBootstrap

`SkyBootstrap.dll` is the in-process host injected by [SkyLoader](../../README.md). It prepares a process-local Vulkan implicit-layer manifest, hosts plugin DLLs, and forwards Vulkan lifecycle events to compatible plugins.

## Responsibilities

- Start a named-pipe server at `\\.\pipe\sky_bootstrap`.
- Load plugin DLLs requested by SkyLoader.
- Expose the C ABI in [`include/skybootstrap_api.h`](include/skybootstrap_api.h).
- Track Vulkan instances, devices, queues, and per-swapchain `OverlaySession` resources.
- Fail open when a Vulkan session is unavailable.
- Write diagnostics to `SkyBootstrap.log` beside the DLL.

Bootstrap does not copy files into the Sky installation directory and does not register a global Vulkan layer. It writes `SkyBootstrap-vulkan-layer.json` beside itself and sets the layer environment for the current process only.

## Build

Build it from the SkyLoader root so the output lands in the shared `dist` directory:

```powershell
cd D:\Develop\Language\C++\SkyLoader
mingw32-make -C libraries/SkyBootstrap all
```

Output:

```text
dist\SkyBootstrap.dll
```

The top-level `mingw32-make all` command builds both SkyLoader and Bootstrap.

## Configuration

Create `SkyBootstrap.ini` beside `SkyBootstrap.dll`:

```ini
[Vulkan]
EnableOverlay=1
```

The old staged Vulkan switches are accepted only as ignored compatibility keys. The production path is controlled by `EnableOverlay`.

## Plugin contract

Plugins are ordinary 64-bit DLLs. Optional exports include:

```text
SkyPluginInit(const SkyBootstrapApi*)
SkyPluginShutdown()
SkyPluginVulkanInit(const SkyVulkanContext*)
SkyPluginVulkanWarmup(uint64_t commandBuffer)
SkyPluginVulkanRender(uint64_t commandBuffer)
SkyPluginWindowMessage(...)
```

The exact declarations and calling conventions are in `include/skybootstrap_api.h`. Plugins should resolve Vulkan functions through `SkyVulkanContext::getVulkanProc` and must keep render callbacks short because they run from the Vulkan present path.

## Pipe protocol

SkyLoader sends UTF-8 commands after Bootstrap has been injected:

```text
LOAD D:\path\to\plugin.dll
```

The current protocol is one-way. A successful write confirms transport, not plugin initialization; detailed load and Vulkan errors are written to `SkyBootstrap.log`.
