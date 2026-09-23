# SkyLoader

SkyLoader is a Windows launcher and local DLL manager for **Sky: Children of
the Light**. It starts Sky through Steam.

## Runtime layout

```text
SkyLoader.exe
├── Win32 settings UI
├── LoaderController
└── SkyLoader.ini

Sky.exe
└── SkyBootstrap.dll
    ├── named-pipe plugin host
    └── loaded plugin DLLs
        └── optional independent D3D11/Win32 UI host
```

## Build

```powershell
cd D:\Develop\Language\C++\SkyLoader
mingw32-make clean
mingw32-make all
```

Outputs:

```text
dist\SkyLoader.exe
dist\SkyBootstrap.dll
```

To create the installer after building:

```powershell
iscc installer\SkyLoader.iss
```

## Use

1. Run `SkyLoader.exe`.
2. Verify the Sky path, then add a 64-bit plugin DLL.
3. Select **Launch Sky**. SkyLoader uses Steam and waits for the configured
   `Sky.exe`.
4. Bootstrap is injected and loads registered plugins.

SkyLoader's watcher also catches Sky started directly from Steam. Use **Inject
selected** to ask the running Bootstrap host to load one plugin again.

## Plugin ABI

The public contract is in
[`libraries/SkyBootstrap/include/skybootstrap_api.h`](libraries/SkyBootstrap/include/skybootstrap_api.h).
Every plugin must export:

```text
SkyPluginInit(const SkyBootstrapApi*)
```

`SkyPluginShutdown()` is optional. Plugins that render UI must create and own
their own window/device; Bootstrap does not provide a graphics callback.

## Logs

Bootstrap writes `SkyBootstrap.log` next to itself. If a plugin fails to load,
check that the managed DLL exists at the path recorded in `SkyLoader.ini`, that
both DLLs are 64-bit, and that the configured `GamePath` is the running Sky
executable.
