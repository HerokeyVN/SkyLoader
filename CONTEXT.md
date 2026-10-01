# SkyLoader

SkyLoader manages native DLL plugins for a Sky game session and its local
managed-plugin library.

## Plugin metadata

**Plugin identity**:
The permanent, case-insensitive `SkyPluginId` that identifies one plugin across
renamed files and releases.
_Avoid_: filename, display name

**Plugin manifest**:
The Windows `VERSIONINFO` resource carrying a plugin's identity and display
metadata.
_Avoid_: sidecar file, DLL export metadata

**Managed plugin**:
A plugin DLL copied into SkyLoader's local plugin directory and referenced by
SkyLoader's saved plugin list.
_Avoid_: source DLL, registered path

## Plugin presentation

**Plugin name**:
The human-readable `ProductName` shown in SkyLoader's plugin list.
_Avoid_: plugin identity, filename

**Plugin author**:
The human-readable `CompanyName` shown in SkyLoader's plugin list.
_Avoid_: plugin identity, publisher key
