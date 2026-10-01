# Creating a SkyLoader plugin

A SkyLoader plugin is a 64-bit Windows DLL that exports `SkyPluginInit`. The
Bootstrap host loads it inside Sky and passes a small logging API. A plugin may
also export `SkyPluginShutdown` for cleanup.

This guide uses C++ and MinGW, but the ABI is C-compatible and works from any
Windows toolchain that produces an x64 DLL.

## 1. Minimal plugin source

Create `plugin.cpp`:

```cpp
#include <windows.h>
#include "skybootstrap_api.h"

static const SkyBootstrapApi* gApi = nullptr;

extern "C" __declspec(dllexport)
int __stdcall SkyPluginInit(const SkyBootstrapApi* api) {
  gApi = api;
  if (gApi && gApi->log)
    gApi->log("Example plugin initialized.");
  return 1; // Return zero to reject initialization.
}

extern "C" __declspec(dllexport)
void __stdcall SkyPluginShutdown() {
  if (gApi && gApi->log)
    gApi->log("Example plugin shutting down.");
  gApi = nullptr;
}
```

`SkyPluginInit` must return quickly. Do not call game APIs from `DllMain`, and
do not retain pointers whose owner has not documented their lifetime.

## 2. Give the plugin a stable identity

Create `plugin.rc`. SkyLoader reads this resource directly from disk before it
imports the DLL; it never loads the DLL merely to inspect metadata.

```rc
#include <windows.h>

VS_VERSION_INFO VERSIONINFO
 FILEVERSION 1,0,0,0
 PRODUCTVERSION 1,0,0,0
 FILEFLAGSMASK 0x3fL
 FILEFLAGS 0x0L
 FILEOS VOS__WINDOWS32
 FILETYPE VFT_DLL
 FILESUBTYPE VFT2_UNKNOWN
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904B0"
    BEGIN
      VALUE "CompanyName", "Example Author\0"
      VALUE "FileDescription", "Example SkyLoader plugin\0"
      VALUE "FileVersion", "1.0.0\0"
      VALUE "ProductName", "Example Plugin\0"
      VALUE "ProductVersion", "1.0.0\0"
      VALUE "SkyPluginId", "example.author.example-plugin\0"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x0409, 1200
  END
END
```

`SkyPluginId` is the package identity, not the display name. It is
case-insensitive and may contain only letters, digits, `.`, `_`, and `-`. Pick
one once and never change it. `ProductName`, `ProductVersion`, and
`CompanyName` are what SkyLoader displays in its `Plugin`, `Version`, and
`Author` columns.

## 3. Build

With the SkyLoader checkout adjacent to your project:

```make
CXX := g++
WINDRES := windres
CXXFLAGS := -std=c++17 -O2 -Wall \
  -ID:/Develop/Language/C++/SkyLoader/libraries/SkyBootstrap/include

plugin.res: plugin.rc
	$(WINDRES) -i $< -O coff -o $@

example-plugin.dll: plugin.cpp plugin.res
	$(CXX) $(CXXFLAGS) -shared plugin.cpp plugin.res -o $@
```

Build an x64 DLL: a 32-bit DLL cannot be loaded into Sky's 64-bit process.
Check that the DLL exports `SkyPluginInit`; `SkyPluginShutdown` is optional but
strongly recommended when a plugin creates threads, hooks, registrations, or
other resources that must be released.

## 4. Import and update

Use **Add DLL...** or drag the DLL into SkyLoader. The loader copies it into
its managed plugin folder and records that copy in `SkyLoader.ini`.

For a manifest-aware plugin, a later DLL with the same `SkyPluginId` replaces
the registered file even if the new filename is different. An equal version is
also accepted to support rebuilt release artifacts. A lower version is not
installed automatically, preventing accidental downgrades. The old managed DLL
is deleted only after the replacement has copied successfully.

If Sky currently has the old DLL loaded, Windows can keep that file locked.
Close Sky before importing an update, or restart Sky and import again if
SkyLoader reports that the old managed DLL could not be deleted.

Plugins without `VERSIONINFO` remain supported but only use legacy filename
matching and show their filename with `—` for version and author.

## 5. Test checklist

1. Import the plugin and verify `ProductName`, `ProductVersion`, and
   `CompanyName` appear in SkyLoader.
2. Launch Sky and inspect `SkyBootstrap.log` for the initialization message.
3. Build a new DLL with the same `SkyPluginId`, a higher version, and a
   different filename; import it and verify one list entry remains.
4. Try importing an older version and verify SkyLoader keeps the newer one.
5. Close Sky, remove the plugin from the list, and confirm shutdown cleanup is
   safe on the next launch.
