#include <windows.h>
#include <shlobj.h>

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

std::wstring defaultLogDirectory() {
  wchar_t localAppData[MAX_PATH]{};
  if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData) == S_OK) {
    const std::wstring root = std::wstring(localAppData) + L"\\SkyLoader";
    const std::wstring logDir = root + L"\\logs";
    CreateDirectoryW(root.c_str(), nullptr);
    CreateDirectoryW(logDir.c_str(), nullptr);
    if (GetFileAttributesW(logDir.c_str()) != INVALID_FILE_ATTRIBUTES) {
      return logDir;
    }
  }
  return moduleDirectory();
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
  writeLog("Loaded plugin: %ls", path.c_str());
  return 1;
}

int unloadPlugin(const wchar_t* rawPath) {
  if (!rawPath || !*rawPath) return 0;
  const std::wstring path = absolutePath(rawPath);
  Plugin targetPlugin{};
  bool found = false;

  {
    std::lock_guard<std::mutex> lock(gPluginsLock);
    for (auto it = gPlugins.begin(); it != gPlugins.end(); ++it) {
      if (samePath(it->path, path)) {
        targetPlugin = *it;
        gPlugins.erase(it);
        found = true;
        break;
      }
    }
  }

  if (!found) {
    writeLog("Plugin not found to unload: %ls", path.c_str());
    return 0;
  }

  if (targetPlugin.shutdown) {
    writeLog("Calling SkyPluginShutdown for %ls", path.c_str());
    targetPlugin.shutdown();
  }

  if (targetPlugin.module) {
    FreeLibrary(targetPlugin.module);
  }

  writeLog("Unloaded plugin: %ls", path.c_str());
  return 1;
}

bool handleCommand(const char* command, std::string& response) {
  constexpr const char loadPrefix[] = "LOAD ";
  constexpr const char unloadPrefix[] = "UNLOAD ";

  if (command && strncmp(command, loadPrefix, sizeof(loadPrefix) - 1) == 0) {
    wchar_t path[MAX_PATH]{};
    if (!MultiByteToWideChar(CP_UTF8, 0, command + sizeof(loadPrefix) - 1, -1, path, MAX_PATH)) {
      writeLog("Could not decode plugin path from pipe command.");
      response = "FAIL DECODE_ERROR\n";
      return false;
    }
    int result = loadPlugin(path);
    if (result) {
      response = "OK\n";
      return true;
    } else {
      response = "FAIL LOAD_FAILED\n";
      return false;
    }
  }

  if (command && strncmp(command, unloadPrefix, sizeof(unloadPrefix) - 1) == 0) {
    wchar_t path[MAX_PATH]{};
    if (!MultiByteToWideChar(CP_UTF8, 0, command + sizeof(unloadPrefix) - 1, -1, path, MAX_PATH)) {
      writeLog("Could not decode plugin path from pipe command.");
      response = "FAIL DECODE_ERROR\n";
      return false;
    }
    int result = unloadPlugin(path);
    if (result) {
      response = "OK\n";
      return true;
    } else {
      response = "FAIL UNLOAD_FAILED\n";
      return false;
    }
  }

  writeLog("Ignored pipe command: %s", command ? command : "(null)");
  response = "FAIL UNKNOWN_COMMAND\n";
  return false;
}

DWORD WINAPI pipeThread(void*) {
  constexpr char pipeName[] = "\\\\.\\pipe\\sky_bootstrap";
  char buffer[32768]{};
  for (;;) {
    HANDLE pipe = CreateNamedPipeA(pipeName, PIPE_ACCESS_DUPLEX,
                                   PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                   PIPE_UNLIMITED_INSTANCES, sizeof(buffer), sizeof(buffer), 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
      Sleep(100);
      continue;
    }
    if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
      DWORD read = 0;
      while (ReadFile(pipe, buffer, sizeof(buffer) - 1, &read, nullptr) && read) {
        buffer[read] = '\0';
        std::string response;
        handleCommand(buffer, response);
        DWORD written = 0;
        WriteFile(pipe, response.c_str(), static_cast<DWORD>(response.size()), &written, nullptr);
        FlushFileBuffers(pipe);
      }
    }
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
  }
}

DWORD WINAPI bootstrapThread(void*) {
  gDirectory = defaultLogDirectory();
  gLogPath = gDirectory + L"\\SkyBootstrap.log";
  writeLog("Started in PID %lu", GetCurrentProcessId());
  writeLog("D3D11 host-window mode active; log located at %ls", gLogPath.c_str());
  CreateThread(nullptr, 0, pipeThread, nullptr, 0, nullptr);
  return 0;
}
}  // namespace

extern "C" SKYBOOTSTRAP_API int SKYBOOTSTRAP_CALL SkyBootstrapLoadPluginW(const wchar_t* path) {
  return loadPlugin(path);
}

extern "C" SKYBOOTSTRAP_API int SKYBOOTSTRAP_CALL SkyBootstrapUnloadPluginW(const wchar_t* path) {
  return unloadPlugin(path);
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
