#include "loader_controller.h"

#include <windows.h>

#include <cstring>

LoaderController::LoaderController(std::wstring bootstrapPath, std::vector<std::wstring> plugins)
    : bootstrapPath_(std::move(bootstrapPath)), plugins_(std::move(plugins)) {}

bool LoaderController::injectDll(unsigned long processId, const std::wstring& path, std::wstring& error) const {
  HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                               PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                               FALSE, processId);
  if (!process) { error = L"OpenProcess failed (" + std::to_wstring(GetLastError()) + L")."; return false; }
  const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
  void* remotePath = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!remotePath) { error = L"VirtualAllocEx failed (" + std::to_wstring(GetLastError()) + L")."; CloseHandle(process); return false; }
  SIZE_T written = 0;
  if (!WriteProcessMemory(process, remotePath, path.c_str(), bytes, &written) || written != bytes) {
    error = L"WriteProcessMemory failed (" + std::to_wstring(GetLastError()) + L").";
    VirtualFreeEx(process, remotePath, 0, MEM_RELEASE); CloseHandle(process); return false;
  }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
  const auto loader = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
#pragma GCC diagnostic pop
  HANDLE thread = loader ? CreateRemoteThread(process, nullptr, 0, loader, remotePath, 0, nullptr) : nullptr;
  if (!thread) { error = L"CreateRemoteThread failed (" + std::to_wstring(GetLastError()) + L")."; VirtualFreeEx(process, remotePath, 0, MEM_RELEASE); CloseHandle(process); return false; }
  const DWORD waited = WaitForSingleObject(thread, 15000);
  DWORD module = 0; GetExitCodeThread(thread, &module);
  CloseHandle(thread); VirtualFreeEx(process, remotePath, 0, MEM_RELEASE); CloseHandle(process);
  if (waited != WAIT_OBJECT_0 || !module) { error = L"Sky.exe did not load the DLL."; return false; }
  return true;
}

bool LoaderController::sendLoadCommand(const std::wstring& path) const {
  char command[MAX_PATH * 3]{};
  memcpy(command, "LOAD ", 5);
  if (!WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, command + 5, static_cast<int>(sizeof(command) - 5), nullptr, nullptr)) return false;
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (WaitNamedPipeA("\\\\.\\pipe\\sky_bootstrap", 50)) {
      HANDLE pipe = CreateFileA("\\\\.\\pipe\\sky_bootstrap", GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
      if (pipe != INVALID_HANDLE_VALUE) {
        DWORD written = 0; const DWORD bytes = static_cast<DWORD>(strlen(command));
        const bool ok = WriteFile(pipe, command, bytes, &written, nullptr) && written == bytes;
        CloseHandle(pipe); return ok;
      }
    }
    Sleep(25);
  }
  return false;
}

LoaderResult LoaderController::installBootstrapAndPlugins(unsigned long processId) const {
  LoaderResult bootstrap = installBootstrap(processId);
  if (!bootstrap.ok) return bootstrap;
  unsigned requested = 0;
  unsigned loaded = 0;
  for (const auto& plugin : plugins_) {
    if (GetFileAttributesW(plugin.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
    ++requested;
    if (sendLoadCommand(plugin)) ++loaded;
  }
  if (loaded == requested) return {loaded, L"Bootstrap and registered plugins loaded.", true};
  return {loaded, L"Bootstrap loaded; one or more plugins were not accepted by its pipe.", false};
}

LoaderResult LoaderController::installBootstrap(unsigned long processId) const {
  if (GetFileAttributesW(bootstrapPath_.c_str()) == INVALID_FILE_ATTRIBUTES) return {0, L"SkyBootstrap.dll was not found.", false};
  std::wstring error;
  if (!injectDll(processId, bootstrapPath_, error)) return {0, L"Could not inject SkyBootstrap: " + error, false};
  return {0, L"Bootstrap loaded.", true};
}

LoaderResult LoaderController::loadPlugin(unsigned long processId, const std::wstring& pluginPath) const {
  LoaderResult bootstrap = installBootstrap(processId);
  if (!bootstrap.ok) return bootstrap;
  if (!sendLoadCommand(pluginPath)) return {0, L"SkyBootstrap pipe is not ready.", false};
  return {1, L"SkyBootstrap loaded the selected plugin.", true};
}
