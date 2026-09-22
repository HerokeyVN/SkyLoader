#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "skybootstrap_api.h"
#include "skybootstrap_internal.h"

namespace {
HMODULE gModule = nullptr;
std::wstring gDirectory;
std::wstring gLogPath;
std::mutex gPluginsLock;
std::mutex gLogLock;
bool gVulkanResourceProbeEnabled = false;
bool gVulkanSyncProbeEnabled = false;
bool gVulkanRenderPassProbeEnabled = false;
bool gVulkanPluginInitProbeEnabled = false;
bool gVulkanPluginFrameProbeEnabled = false;
bool gVulkanPluginUiProbeEnabled = false;
bool gVulkanInputProbeEnabled = false;

struct Plugin {
  HMODULE module;
  std::wstring path;
  SkyPluginShutdownFn shutdown;
  SkyPluginVulkanInitFn vulkanInit;
  SkyPluginVulkanWarmupFn vulkanWarmup;
  SkyPluginVulkanRenderFn vulkanRender;
  SkyPluginWindowMessageFn windowMessage;
  bool vulkanReady;
};
std::vector<Plugin> gPlugins;
void writeLog(const char *format, ...);

struct PluginCallback {
  HMODULE module{};
  std::wstring path;
  SkyPluginVulkanInitFn init{};
  SkyPluginVulkanWarmupFn warmup{};
  SkyPluginVulkanRenderFn render{};
  SkyPluginWindowMessageFn windowMessage{};
};

// Plugin callbacks are untrusted code. Build a snapshot under the registry
// lock, then invoke it after releasing that lock so a slow or re-entrant
// plugin cannot stall loading, unloading, or another plugin's dispatch.
std::vector<PluginCallback> pluginSnapshot(bool readyOnly) {
  std::vector<PluginCallback> result;
  std::lock_guard<std::mutex> lock(gPluginsLock);
  result.reserve(gPlugins.size());
  for (const Plugin &plugin : gPlugins) {
    if (readyOnly && !plugin.vulkanReady) continue;
    result.push_back({plugin.module, plugin.path, plugin.vulkanInit, plugin.vulkanWarmup,
                      plugin.vulkanRender, plugin.windowMessage});
  }
  return result;
}

void markPluginReady(HMODULE module, bool ready) {
  std::lock_guard<std::mutex> lock(gPluginsLock);
  for (Plugin &plugin : gPlugins) {
    if (plugin.module != module) continue;
    plugin.vulkanReady = ready;
    writeLog("Vulkan init for %ls: %s", plugin.path.c_str(), ready ? "ready" : "rejected");
    return;
  }
}

std::wstring moduleDirectory() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(gModule, path, MAX_PATH);
  std::wstring value(path);
  const auto slash = value.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : value.substr(0, slash);
}

void writeLog(const char *format, ...) {
  char body[1600]{};
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(body, sizeof(body), format, arguments);
  va_end(arguments);
  std::string line = "[SkyBootstrap] " + std::string(body) + "\r\n";
  OutputDebugStringA(line.c_str());
  std::lock_guard<std::mutex> lock(gLogLock);
  FILE *file = _wfopen(gLogPath.c_str(), L"ab");
  if (file) {
    fputs(line.c_str(), file);
    fclose(file);
  }
}

void SKYBOOTSTRAP_CALL apiLog(const char *message) {
  writeLog("%s", message ? message : "(null)");
}

SkyBootstrapApi gApi{SKYBOOTSTRAP_API_VERSION, apiLog};

bool samePath(const std::wstring& first, const std::wstring& second) {
  return CompareStringOrdinal(first.c_str(), -1, second.c_str(), -1, TRUE) == CSTR_EQUAL;
}

std::wstring absolutePath(const wchar_t *path) {
  wchar_t full[MAX_PATH]{};
  const DWORD size = GetFullPathNameW(path, MAX_PATH, full, nullptr);
  return size && size < MAX_PATH ? std::wstring(full) : std::wstring(path);
}

void prepareVulkanLayer() {
  // A layer is in the application's critical graphics path. Keep it opt-in
  // until it has been validated against the installed Vulkan loader/driver;
  // a malformed layer must never stop Sky from launching.
  const std::wstring ini = gDirectory + L"\\SkyBootstrap.ini";
  if (GetPrivateProfileIntW(L"Vulkan", L"EnableOverlay", 0, ini.c_str()) != 1) {
    writeLog("Vulkan overlay is disabled (set [Vulkan] EnableOverlay=1 after validation).");
    return;
  }
  // The staged validation switches are now a single production setting.
  // Keep the known-safe path intact: swapchain resources, pass-through
  // synchronisation, render pass, plugin init and UI rendering are enabled
  // together. The unsafe WndProc bridge stays permanently off.
  const bool overlayEnabled = true;
  gVulkanResourceProbeEnabled = overlayEnabled;
  gVulkanSyncProbeEnabled = overlayEnabled;
  gVulkanRenderPassProbeEnabled = overlayEnabled;
  gVulkanPluginInitProbeEnabled = overlayEnabled;
  gVulkanPluginFrameProbeEnabled = overlayEnabled;
  gVulkanPluginUiProbeEnabled = overlayEnabled;
  gVulkanInputProbeEnabled = false;
  const wchar_t* legacyKeys[] = {
    L"EnableRenderer", L"EnableResourceProbe", L"EnableSyncProbe",
    L"EnableRenderPassProbe", L"EnablePluginInitProbe",
    L"EnablePluginFrameProbe", L"EnablePluginUiProbe", L"EnableInputProbe"
  };
  for (const wchar_t* key : legacyKeys) {
    wchar_t value[8]{};
    GetPrivateProfileStringW(L"Vulkan", key, L"", value, static_cast<DWORD>(std::size(value)), ini.c_str());
    if (value[0]) writeLog("Ignoring legacy [Vulkan] %ls; use EnableOverlay only.", key);
  }
  // The loader discovers an implicit layer from this process-local manifest.
  // Nothing is copied to the game directory or registered globally.
  const std::wstring manifest = gDirectory + L"\\SkyBootstrap-vulkan-layer.json";
  const char json[] =
    "{\n"
    "  \"file_format_version\": \"1.1.0\",\n"
    "  \"layer\": {\n"
    "    \"name\": \"VK_LAYER_SKYLOADER_OVERLAY\",\n"
    "    \"type\": \"GLOBAL\",\n"
    "    \"api_version\": \"1.0\",\n"
    "    \"implementation_version\": \"1\",\n"
    "    \"description\": \"SkyLoader Bootstrap Vulkan overlay\",\n"
    "    \"library_path\": \"SkyBootstrap.dll\",\n"
    "    \"functions\": { \"vkNegotiateLoaderLayerInterfaceVersion\": \"vkNegotiateLoaderLayerInterfaceVersion\" }\n"
    "  }\n"
    "}\n";
  FILE *file = _wfopen(manifest.c_str(), L"wb");
  if (file) { fwrite(json, 1, sizeof(json) - 1, file); fclose(file); }

  wchar_t existing[32767]{};
  GetEnvironmentVariableW(L"VK_LAYER_PATH", existing, static_cast<DWORD>(std::size(existing)));
  std::wstring layerPath = gDirectory;
  if (existing[0]) layerPath += L";" + std::wstring(existing);
  SetEnvironmentVariableW(L"VK_LAYER_PATH", layerPath.c_str());
  GetEnvironmentVariableW(L"VK_INSTANCE_LAYERS", existing, static_cast<DWORD>(std::size(existing)));
  std::wstring layers = L"VK_LAYER_SKYLOADER_OVERLAY";
  if (existing[0] && wcsstr(existing, layers.c_str()) == nullptr) layers += L";" + std::wstring(existing);
  SetEnvironmentVariableW(L"VK_INSTANCE_LAYERS", layers.c_str());
  writeLog("Prepared production Vulkan overlay: %ls (WndProc bridge disabled; polling input enabled in plugin)", manifest.c_str());
}

int loadPlugin(const wchar_t *rawPath) {
  if (!rawPath || !*rawPath) return 0;
  const std::wstring path = absolutePath(rawPath);
  if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    writeLog("Plugin not found: %ls", path.c_str());
    return 0;
  }
  {
    std::lock_guard<std::mutex> lock(gPluginsLock);
    for (const Plugin& plugin : gPlugins)
      if (samePath(plugin.path, path)) return 1;
  }

  HMODULE module = LoadLibraryW(path.c_str());
  if (!module) {
    writeLog("LoadLibraryW failed for %ls (error %lu)", path.c_str(), GetLastError());
    return 0;
  }
  const auto init = reinterpret_cast<SkyPluginInitFn>(GetProcAddress(module, "SkyPluginInit"));
  const auto shutdown = reinterpret_cast<SkyPluginShutdownFn>(GetProcAddress(module, "SkyPluginShutdown"));
  const auto vulkanInit = reinterpret_cast<SkyPluginVulkanInitFn>(GetProcAddress(module, "SkyPluginVulkanInit"));
  const auto vulkanWarmup = reinterpret_cast<SkyPluginVulkanWarmupFn>(GetProcAddress(module, "SkyPluginVulkanWarmup"));
  const auto vulkanRender = reinterpret_cast<SkyPluginVulkanRenderFn>(GetProcAddress(module, "SkyPluginVulkanRender"));
  const auto windowMessage = reinterpret_cast<SkyPluginWindowMessageFn>(GetProcAddress(module, "SkyPluginWindowMessage"));
  if (init && !init(&gApi)) {
    writeLog("SkyPluginInit rejected %ls", path.c_str());
    FreeLibrary(module);
    return 0;
  }
  {
    std::lock_guard<std::mutex> lock(gPluginsLock);
    gPlugins.push_back({module, path, shutdown, vulkanInit, vulkanWarmup, vulkanRender, windowMessage, false});
  }
  writeLog("Loaded plugin: %ls%s", path.c_str(), vulkanRender ? " (overlay capable)" : "");
  return 1;
}

void handleCommand(const char *command) {
  if (!command) return;
  constexpr const char prefix[] = "LOAD ";
  if (strncmp(command, prefix, sizeof(prefix) - 1) != 0) {
    writeLog("Ignored pipe command: %s", command);
    return;
  }
  wchar_t path[MAX_PATH]{};
  MultiByteToWideChar(CP_UTF8, 0, command + sizeof(prefix) - 1, -1, path, MAX_PATH);
  loadPlugin(path);
}

DWORD WINAPI pipeThread(void *) {
  const char pipeName[] = "\\\\.\\pipe\\sky_bootstrap";
  char buffer[32768];
  for (;;) {
    HANDLE pipe = CreateNamedPipeA(pipeName, PIPE_ACCESS_INBOUND,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES,
      0, sizeof(buffer), 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) continue;
    if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
      DWORD read = 0;
      while (ReadFile(pipe, buffer, sizeof(buffer) - 1, &read, nullptr) && read) {
        buffer[read] = '\0';
        handleCommand(buffer);
      }
    }
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
  }
}

DWORD WINAPI bootstrapThread(void *) {
  gDirectory = moduleDirectory();
  gLogPath = gDirectory + L"\\SkyBootstrap.log";
  writeLog("Started in PID %lu", GetCurrentProcessId());
  prepareVulkanLayer();
  writeLog("Plugin source of truth: SkyLoader named pipe.");
  CreateThread(nullptr, 0, pipeThread, nullptr, 0, nullptr);
  return 0;
}
} // namespace

extern "C" SKYBOOTSTRAP_API int SKYBOOTSTRAP_CALL SkyBootstrapLoadPluginW(const wchar_t *path) {
  return loadPlugin(path);
}

extern "C" SKYBOOTSTRAP_API uint32_t SKYBOOTSTRAP_CALL SkyBootstrapPluginCount(void) {
  std::lock_guard<std::mutex> lock(gPluginsLock);
  return static_cast<uint32_t>(gPlugins.size());
}

extern "C" void SkyBootstrapDispatchVulkanInit(const SkyVulkanContext *context) {
  for (const PluginCallback &plugin : pluginSnapshot(false)) {
    if (!plugin.init) continue;
    bool alreadyReady = false;
    {
      std::lock_guard<std::mutex> lock(gPluginsLock);
      for (const Plugin &registered : gPlugins)
        if (registered.module == plugin.module) { alreadyReady = registered.vulkanReady; break; }
    }
    if (!alreadyReady) markPluginReady(plugin.module, plugin.init(context) != 0);
  }
}

extern "C" void SkyBootstrapDispatchVulkanRender(uint64_t commandBuffer) {
  for (const PluginCallback &plugin : pluginSnapshot(true))
    if (plugin.render) plugin.render(commandBuffer);
}

extern "C" void SkyBootstrapDispatchVulkanWarmup(uint64_t commandBuffer) {
  for (const PluginCallback &plugin : pluginSnapshot(true))
    if (plugin.warmup) plugin.warmup(commandBuffer);
}

extern "C" void SkyBootstrapDispatchWindowMessage(uintptr_t window, uint32_t message, uintptr_t wParam, intptr_t lParam) {
  for (const PluginCallback &plugin : pluginSnapshot(true))
    if (plugin.windowMessage) plugin.windowMessage(window, message, wParam, lParam);
}

extern "C" void SkyBootstrapLayerTrace(const char *message) {
  writeLog("[VulkanLayer] %s", message ? message : "(null)");
}

extern "C" int SkyBootstrapRendererEnabled(void) {
  // Kept as an ABI compatibility alias. The standalone renderer was retired;
  // all presentation now goes through OverlaySession.
  return 0;
}

extern "C" int SkyBootstrapResourceProbeEnabled(void) {
  return gVulkanResourceProbeEnabled ? 1 : 0;
}

extern "C" int SkyBootstrapSyncProbeEnabled(void) {
  return gVulkanSyncProbeEnabled ? 1 : 0;
}

extern "C" int SkyBootstrapRenderPassProbeEnabled(void) {
  return gVulkanRenderPassProbeEnabled ? 1 : 0;
}

extern "C" int SkyBootstrapPluginInitProbeEnabled(void) {
  return gVulkanPluginInitProbeEnabled ? 1 : 0;
}

extern "C" int SkyBootstrapPluginFrameProbeEnabled(void) {
  return gVulkanPluginFrameProbeEnabled ? 1 : 0;
}

extern "C" int SkyBootstrapPluginUiProbeEnabled(void) {
  return gVulkanPluginUiProbeEnabled ? 1 : 0;
}

extern "C" int SkyBootstrapInputProbeEnabled(void) {
  return gVulkanInputProbeEnabled ? 1 : 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason != DLL_PROCESS_ATTACH) return TRUE;
  gModule = module;
  DisableThreadLibraryCalls(module);
  CreateThread(nullptr, 0, bootstrapThread, nullptr, 0, nullptr);
  return TRUE;
}
