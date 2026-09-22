#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "skybootstrap_api.h"

namespace {
HMODULE gModule = nullptr;
std::wstring gDirectory;
std::wstring gLogPath;
std::mutex gPluginsLock;
std::mutex gLogLock;

struct Plugin {
  HMODULE module{};
  std::wstring path;
  SkyPluginShutdownFn shutdown{};
};
std::vector<Plugin> gPlugins;

std::wstring moduleDirectory() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(gModule, path, MAX_PATH);
  std::wstring value(path);
  const size_t slash = value.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : value.substr(0, slash);
}

void writeLog(const char* format, ...) {
  char message[1600]{};
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);

  const std::string line = "[SkyBootstrap] " + std::string(message) + "\r\n";
  OutputDebugStringA(line.c_str());
  std::lock_guard<std::mutex> lock(gLogLock);
  FILE* file = _wfopen(gLogPath.c_str(), L"ab");
  if (file) {
    fputs(line.c_str(), file);
    fclose(file);
  }
}

void SKYBOOTSTRAP_CALL apiLog(const char* message) {
  writeLog("%s", message ? message : "(null)");
}

SkyBootstrapApi gApi{SKYBOOTSTRAP_API_VERSION, apiLog};

bool samePath(const std::wstring& first, const std::wstring& second) {
  return CompareStringOrdinal(first.c_str(), -1, second.c_str(), -1, TRUE) == CSTR_EQUAL;
}

std::wstring absolutePath(const wchar_t* path) {
  wchar_t full[MAX_PATH]{};
  const DWORD size = GetFullPathNameW(path, MAX_PATH, full, nullptr);
  return size && size < MAX_PATH ? std::wstring(full) : std::wstring(path);
}

int loadPlugin(const wchar_t* rawPath) {
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
  const auto init = reinterpret_cast<SkyPluginInitFn>(GetProcAddress(module, "SkyPluginInit"));
  const auto shutdown = reinterpret_cast<SkyPluginShutdownFn>(GetProcAddress(module, "SkyPluginShutdown"));
#pragma GCC diagnostic pop
  if (!init) {
    writeLog("Plugin rejected (SkyPluginInit export missing): %ls", path.c_str());
    FreeLibrary(module);
    return 0;
  }
  if (!init(&gApi)) {
    writeLog("SkyPluginInit rejected %ls", path.c_str());
    FreeLibrary(module);
    return 0;
  }

  {
    std::lock_guard<std::mutex> lock(gPluginsLock);
    gPlugins.push_back({module, path, shutdown});
  }
  writeLog("Loaded D3D11-hosted plugin: %ls", path.c_str());
  return 1;
}

void handleCommand(const char* command) {
  constexpr const char prefix[] = "LOAD ";
  if (!command || strncmp(command, prefix, sizeof(prefix) - 1) != 0) {
    writeLog("Ignored pipe command: %s", command ? command : "(null)");
    return;
  }

  wchar_t path[MAX_PATH]{};
  if (!MultiByteToWideChar(CP_UTF8, 0, command + sizeof(prefix) - 1, -1, path, MAX_PATH)) {
    writeLog("Could not decode plugin path from pipe command.");
    return;
  }
  loadPlugin(path);
}

DWORD WINAPI pipeThread(void*) {
  constexpr char pipeName[] = "\\\\.\\pipe\\sky_bootstrap";
  char buffer[32768]{};
  for (;;) {
    HANDLE pipe = CreateNamedPipeA(pipeName, PIPE_ACCESS_INBOUND,
                                   PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                   PIPE_UNLIMITED_INSTANCES, 0, sizeof(buffer), 0, nullptr);
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

DWORD WINAPI bootstrapThread(void*) {
  gDirectory = moduleDirectory();
  gLogPath = gDirectory + L"\\SkyBootstrap.log";
  writeLog("Started in PID %lu", GetCurrentProcessId());
  writeLog("D3D11 host-window mode active; Vulkan interception is not installed.");
  CreateThread(nullptr, 0, pipeThread, nullptr, 0, nullptr);
  return 0;
}
}  // namespace

extern "C" SKYBOOTSTRAP_API int SKYBOOTSTRAP_CALL SkyBootstrapLoadPluginW(const wchar_t* path) {
  return loadPlugin(path);
}

extern "C" SKYBOOTSTRAP_API uint32_t SKYBOOTSTRAP_CALL SkyBootstrapPluginCount(void) {
  std::lock_guard<std::mutex> lock(gPluginsLock);
  return static_cast<uint32_t>(gPlugins.size());
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason != DLL_PROCESS_ATTACH) return TRUE;
  gModule = module;
  DisableThreadLibraryCalls(module);
  CreateThread(nullptr, 0, bootstrapThread, nullptr, 0, nullptr);
  return TRUE;
}
