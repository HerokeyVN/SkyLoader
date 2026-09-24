# SkyBootstrap

`SkyBootstrap.dll` is the small in-process plugin host injected by
[SkyLoader](../../README.md). In the D3D11 host-window branch it has no Vulkan
layer, renderer hook, or game-window subclassing code.

## Responsibilities

- Start `\\.\pipe\sky_bootstrap`.
- Load requested 64-bit plugin DLLs.
- Call the required `SkyPluginInit(const SkyBootstrapApi*)` entry point.
- Keep loaded plugins alive for the game-process lifetime.
- Write diagnostics to `%LOCALAPPDATA%\SkyLoader\logs\SkyBootstrap.log`, falling back to the DLL directory if the log directory is unavailable.

Plugins that need a UI create their own host window and rendering device. For
example, SkyToolkit owns a transparent D3D11 + ImGui window, so failures in its
UI renderer cannot modify Sky's Vulkan presentation path.

## Build

```powershell
cd D:\Develop\Language\C++\SkyLoader
mingw32-make -C libraries\SkyBootstrap all
```

The output is `dist\SkyBootstrap.dll`.

## Plugin contract

The public C ABI is in `include/skybootstrap_api.h`. A plugin must export:

```text
SkyPluginInit(const SkyBootstrapApi*)
```

`SkyPluginShutdown()` is optional. The current pipe protocol is one-way:

```text
LOAD D:\path\to\plugin.dll
```
