# SkyLoader

SkyLoader is an open-source Windows launcher, injector, and local DLL manager for **Sky: Children of the Light**. It manages plugin lifecycle, process watching, and rapid in-game UI prototyping.

---

## Runtime Layout

```text
SkyLoader.exe
├── Win32 settings UI & process watcher
├── LoaderController & injector
└── SkyLoader.ini

Sky.exe
├── Native Vulkan Engine (Untouched presentation path)
└── SkyBootstrap.dll (Core In-Process Kernel)
    ├── Named-pipe IPC server & VEH exception shield
    └── Loaded plugin DLLs
        ├── SkyOverlay.dll (Open-source D3D11 + ImGui canvas & mod hub)
        │   ├── In-game Plugin Manager UI
        │   ├── Per-plugin dedicated hotkeys & auto-wakeup
        │   └── Community plugin ImGui render callbacks
        └── Independent / native plugins (e.g., SkyToolkit)
```

---

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
dist\SkyOverlay.dll
```

To create the installer after building:

```powershell
iscc installer\SkyLoader.iss
```

---

## Use

1. Run `SkyLoader.exe`.
2. Verify the Sky path, then add 64-bit plugin DLLs (such as `SkyOverlay.dll` or custom mods).
3. Select **Launch Sky**. SkyLoader starts the game through Steam and waits for `Sky.exe`.
4. Bootstrap is injected and loads registered plugins via named pipes.

SkyLoader's process watcher also catches Sky started directly from Steam. Use **Inject selected** to ask the running Bootstrap host to load one plugin again.

---

## Rapid UI Prototyping with SkyOverlay

For plugins requiring an in-game user interface (Dear ImGui), SkyLoader bundles [`SkyOverlay`](libraries/SkyOverlay/README.md), an open-source companion library providing:
- A non-invasive transparent Direct3D 11 canvas with **zero Vulkan hooking** (safe on all GPU architectures, including AMD Radeon).
- An in-game **SkyOverlay Plugin Manager** for per-mod toggles (`[x]`) and hotkey rebinding (`F1-F12`, `Home`, `NumPad`).
- Public C ABI (`skyoverlay_api.h`) allowing plugins to register ImGui render callbacks in just a few lines of code.

---

## Plugin ABI

The kernel contract is defined in [`libraries/SkyBootstrap/include/skybootstrap_api.h`](libraries/SkyBootstrap/include/skybootstrap_api.h).
Every plugin must export:

```c
SkyPluginInit(const SkyBootstrapApi*)
```

`SkyPluginShutdown()` is optional.

---

## Logs

Bootstrap writes `SkyBootstrap.log` next to itself in `dist/`. If a plugin fails to load, check that the managed DLL exists at the path recorded in `SkyLoader.ini`, that both DLLs are 64-bit, and that the configured `GamePath` is the running Sky executable.

---

## Credits & Acknowledgements

SkyLoader builds upon ideas and pioneering research from the Sky PC modding community:

- **Lukas (`sml-pc`)**: Original Sky mod loader research, early injection concepts, and DLL proxy architecture.
- **XeTrinityz (`ThatSkyLoader`)**: Rapid ImGui plugin rendering concept, in-game menu workflow, and community modding ergonomics.
- **MrGatto**: Named Pipe IPC design and robust remote injection patterns.
- **HerokeyVN**: Multi-architecture loader design, SkyLoader orchestration, and SkyOverlay architecture.
- **Dear ImGui (Omar Cornut)**: Bloat-free immediate mode graphical user interface library for C++.

---

<p align="center">
  Made with ❤️ for the Sky: Children of the Light community.
</p>