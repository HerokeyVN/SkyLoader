#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <string>
#include <vector>

#include "loader_controller.h"

namespace {
constexpr int kIdGamePath = 1001;
constexpr int kIdBrowseGame = 1002;
constexpr int kIdLaunchGame = 1003;
constexpr int kIdDllList = 1004;
constexpr int kIdAddDll = 1005;
constexpr int kIdRemoveDll = 1006;
constexpr int kIdInject = 1007;
constexpr int kIdRefresh = 1008;
constexpr int kIdStatus = 1009;
constexpr wchar_t kSkySteamUri[] = L"steam://run/2325290";
constexpr UINT_PTR kAutoInjectTimer = 1;
constexpr UINT kAutoInjectPollMs = 50;

HWND gGamePath = nullptr;
HWND gDllList = nullptr;
HWND gStatus = nullptr;
std::wstring gIniPath;
std::wstring gBootstrapPath;
std::vector<std::wstring> gDllPaths;
bool gAutoInjectPending = false;
DWORD gPendingSkyPid = 0;
UINT gPendingSkyTicks = 0;

std::wstring moduleDirectory() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring result(path);
  const auto separator = result.find_last_of(L"\\/");
  return separator == std::wstring::npos ? L"." : result.substr(0, separator);
}

std::wstring normalizedPath(const std::wstring& path) {
  wchar_t full[MAX_PATH]{};
  const DWORD count = GetFullPathNameW(path.c_str(), MAX_PATH, full, nullptr);
  return count && count < MAX_PATH ? std::wstring(full) : path;
}

std::wstring fileNameOnly(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring pluginIdentity(const std::wstring& path) {
  std::wstring filename = fileNameOnly(path);
  const size_t dot = filename.find_last_of(L'.');
  if (dot == std::wstring::npos) return filename;
  std::wstring stem = filename.substr(0, dot);
  const size_t versionDot = stem.find_last_of(L'.');
  if (versionDot != std::wstring::npos && versionDot + 1 < stem.size()) {
    bool numericSuffix = true;
    for (size_t i = versionDot + 1; i < stem.size(); ++i) {
      if (stem[i] < L'0' || stem[i] > L'9') { numericSuffix = false; break; }
    }
    if (numericSuffix) stem.resize(versionDot);
  }
  return stem + filename.substr(dot);
}

bool samePath(const std::wstring& first, const std::wstring& second);

std::wstring managedPluginDirectory() {
  wchar_t localAppData[MAX_PATH]{};
  if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData) != S_OK)
    return L"";
  const std::wstring root = std::wstring(localAppData) + L"\\SkyLoader";
  const std::wstring directory = root + L"\\plugins";
  CreateDirectoryW(root.c_str(), nullptr);
  CreateDirectoryW(directory.c_str(), nullptr);
  return GetFileAttributesW(directory.c_str()) != INVALID_FILE_ATTRIBUTES ? directory : L"";
}

bool importPlugin(const std::wstring& source, std::wstring& destination, std::wstring& error) {
  const std::wstring directory = managedPluginDirectory();
  if (directory.empty()) {
    error = L"Could not create the managed plugin directory.";
    return false;
  }
  const std::wstring filename = fileNameOnly(source);
  destination = directory + L"\\" + filename;
  if (samePath(source, destination)) return true;
  if (!CopyFileW(source.c_str(), destination.c_str(), FALSE)) {
    error = L"Could not copy the DLL into the managed plugin directory (" +
            std::to_wstring(GetLastError()) + L").";
    destination.clear();
    return false;
  }
  return true;
}

bool isManagedPluginPath(const std::wstring& path) {
  const std::wstring directory = managedPluginDirectory();
  if (directory.empty() || path.size() <= directory.size()) return false;
  if (CompareStringOrdinal(path.c_str(), static_cast<int>(directory.size()),
                           directory.c_str(), static_cast<int>(directory.size()), TRUE) != CSTR_EQUAL)
    return false;
  return path[directory.size()] == L'\\' || path[directory.size()] == L'/';
}

bool samePath(const std::wstring& first, const std::wstring& second) {
  return CompareStringOrdinal(first.c_str(), -1, second.c_str(), -1, TRUE) == CSTR_EQUAL;
}

void setStatus(const std::wstring& text) {
  SetWindowTextW(gStatus, text.c_str());
}

std::wstring selectedPath() {
  const int selected = ListView_GetNextItem(gDllList, -1, LVNI_SELECTED);
  if (selected < 0 || static_cast<size_t>(selected) >= gDllPaths.size())
    return L"";
  return gDllPaths[static_cast<size_t>(selected)];
}

void populateDllList() {
  ListView_DeleteAllItems(gDllList);
  for (size_t i = 0; i < gDllPaths.size(); ++i) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = static_cast<int>(i);
    const std::wstring displayName = fileNameOnly(gDllPaths[i]);
    item.pszText = const_cast<wchar_t*>(displayName.c_str());
    ListView_InsertItem(gDllList, &item);
  }
}

void saveSettings() {
  wchar_t gamePath[MAX_PATH]{};
  GetWindowTextW(gGamePath, gamePath, MAX_PATH);
  WritePrivateProfileStringW(L"SkyLoader", L"GamePath", gamePath, gIniPath.c_str());
  WritePrivateProfileStringW(L"SkyLoader", L"BootstrapPath", gBootstrapPath.c_str(), gIniPath.c_str());
  WritePrivateProfileStringW(L"Dlls", L"Count", std::to_wstring(gDllPaths.size()).c_str(), gIniPath.c_str());

  for (size_t i = 0; i < gDllPaths.size(); ++i) {
    const std::wstring key = L"Dll" + std::to_wstring(i);
    WritePrivateProfileStringW(L"Dlls", key.c_str(), gDllPaths[i].c_str(), gIniPath.c_str());
  }
  // Clear stale numbered values left by a previous, longer list.
  for (size_t i = gDllPaths.size(); i < 256; ++i) {
    const std::wstring key = L"Dll" + std::to_wstring(i);
    WritePrivateProfileStringW(L"Dlls", key.c_str(), nullptr, gIniPath.c_str());
  }
}

void loadSettings() {
  const wchar_t* fallback =
    L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Sky Children of the Light\\Sky.exe";
  wchar_t gamePath[MAX_PATH]{};
  GetPrivateProfileStringW(L"SkyLoader", L"GamePath", fallback, gamePath, MAX_PATH, gIniPath.c_str());
  SetWindowTextW(gGamePath, gamePath);

  const std::wstring defaultBootstrap = moduleDirectory() + L"\\SkyBootstrap.dll";
  wchar_t bootstrapPath[MAX_PATH]{};
  GetPrivateProfileStringW(L"SkyLoader", L"BootstrapPath", defaultBootstrap.c_str(),
                           bootstrapPath, MAX_PATH, gIniPath.c_str());
  gBootstrapPath = bootstrapPath;
  // Migrate the development path used before SkyBootstrap became part of
  // SkyLoader. Custom paths remain untouched.
  const std::wstring legacyBootstrap =
    L"D:\\Develop\\Language\\C++\\SkyBootstrap\\dist\\SkyBootstrap.dll";
  if (samePath(gBootstrapPath, legacyBootstrap))
    gBootstrapPath = defaultBootstrap;

  const UINT count = GetPrivateProfileIntW(L"Dlls", L"Count", 0, gIniPath.c_str());
  gDllPaths.clear();
  for (UINT i = 0; i < count && i < 256; ++i) {
    const std::wstring key = L"Dll" + std::to_wstring(i);
    wchar_t value[MAX_PATH]{};
    GetPrivateProfileStringW(L"Dlls", key.c_str(), L"", value, MAX_PATH, gIniPath.c_str());
    if (value[0] && GetFileAttributesW(value) != INVALID_FILE_ATTRIBUTES)
      gDllPaths.emplace_back(value);
  }
  populateDllList();
}

bool chooseFile(HWND owner, const wchar_t* filter, std::wstring& output) {
  wchar_t path[MAX_PATH]{};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = owner;
  dialog.lpstrFilter = filter;
  dialog.lpstrFile = path;
  dialog.nMaxFile = MAX_PATH;
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (!GetOpenFileNameW(&dialog))
    return false;
  output = normalizedPath(path);
  return true;
}

DWORD findSkyProcess(const std::wstring& expectedPath) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE)
    return 0;

  PROCESSENTRY32W process{};
  process.dwSize = sizeof(process);
  DWORD pid = 0;
  if (Process32FirstW(snapshot, &process)) {
    do {
      if (CompareStringOrdinal(process.szExeFile, -1, L"Sky.exe", -1, TRUE) != CSTR_EQUAL)
        continue;
      HANDLE candidate = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.th32ProcessID);
      if (!candidate) continue;
      wchar_t imagePath[MAX_PATH]{};
      DWORD imageLength = MAX_PATH;
      const bool queried = QueryFullProcessImageNameW(candidate, 0, imagePath, &imageLength) != FALSE;
      CloseHandle(candidate);
      if (queried && samePath(normalizedPath(imagePath), normalizedPath(expectedPath))) {
        pid = process.th32ProcessID;
        break;
      }
    } while (Process32NextW(snapshot, &process));
  }
  CloseHandle(snapshot);
  return pid;
}


void requestAutoInject() {
  gAutoInjectPending = true;
  gPendingSkyPid = 0;
  gPendingSkyTicks = 0;
}


void autoInjectViaController(DWORD pid) {
  LoaderController controller(gBootstrapPath, gDllPaths);
  const LoaderResult result = controller.installBootstrapAndPlugins(pid);
  setStatus(result.message + L" (PID " + std::to_wstring(pid) + L").");
}

void loadSelectedPluginViaController(const std::wstring& path, DWORD pid) {
  LoaderController controller(gBootstrapPath, {});
  setStatus(controller.loadPlugin(pid, path).message);
}


void layout(HWND window) {
  RECT client{};
  GetClientRect(window, &client);
  const int width = client.right;
  const int height = client.bottom;
  const int margin = 12;
  const int buttonWidth = 130;

  MoveWindow(gGamePath, margin, 34, width - margin * 3 - buttonWidth * 2, 26, TRUE);
  MoveWindow(GetDlgItem(window, kIdBrowseGame), width - margin * 2 - buttonWidth * 2, 34, buttonWidth, 26, TRUE);
  MoveWindow(GetDlgItem(window, kIdLaunchGame), width - margin - buttonWidth, 34, buttonWidth, 26, TRUE);
  MoveWindow(gDllList, margin, 96, width - margin * 2, height - 182, TRUE);
  MoveWindow(GetDlgItem(window, kIdAddDll), margin, height - 74, buttonWidth, 28, TRUE);
  MoveWindow(GetDlgItem(window, kIdRemoveDll), margin + buttonWidth + 8, height - 74, buttonWidth, 28, TRUE);
  MoveWindow(GetDlgItem(window, kIdRefresh), margin + (buttonWidth + 8) * 2, height - 74, buttonWidth, 28, TRUE);
  MoveWindow(GetDlgItem(window, kIdInject), width - margin - 170, height - 74, 170, 28, TRUE);
  MoveWindow(gStatus, margin, height - 37, width - margin * 2, 22, TRUE);
  ListView_SetColumnWidth(gDllList, 0, width - margin * 2 - 6);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE: {
      INITCOMMONCONTROLSEX controls{};
      controls.dwSize = sizeof(controls);
      controls.dwICC = ICC_LISTVIEW_CLASSES;
      InitCommonControlsEx(&controls);
      CreateWindowW(L"STATIC", L"Sky executable", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
      gGamePath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                  0, 0, 0, 0, window, reinterpret_cast<HMENU>(kIdGamePath), nullptr, nullptr);
      CreateWindowW(L"BUTTON", L"Browse Sky...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    0, 0, 0, 0, window, reinterpret_cast<HMENU>(kIdBrowseGame), nullptr, nullptr);
      CreateWindowW(L"BUTTON", L"Launch Sky", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    0, 0, 0, 0, window, reinterpret_cast<HMENU>(kIdLaunchGame), nullptr, nullptr);
      CreateWindowW(L"STATIC", L"Registered DLL files", WS_CHILD | WS_VISIBLE, 12, 72, 200, 20, window, nullptr, nullptr, nullptr);
      gDllList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL,
                                  0, 0, 0, 0, window, reinterpret_cast<HMENU>(kIdDllList), nullptr, nullptr);
      ListView_SetExtendedListViewStyle(gDllList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
      LVCOLUMNW column{};
      column.mask = LVCF_TEXT | LVCF_WIDTH;
       column.pszText = const_cast<wchar_t*>(L"Plugin");
      column.cx = 600;
      ListView_InsertColumn(gDllList, 0, &column);
      CreateWindowW(L"BUTTON", L"Add DLL...", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kIdAddDll), nullptr, nullptr);
      CreateWindowW(L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kIdRemoveDll), nullptr, nullptr);
      CreateWindowW(L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kIdRefresh), nullptr, nullptr);
      CreateWindowW(L"BUTTON", L"Inject selected", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kIdInject), nullptr, nullptr);
      gStatus = CreateWindowW(L"STATIC", L"Ready. Start Sky, select a DLL, then inject.", WS_CHILD | WS_VISIBLE,
                              0, 0, 0, 0, window, reinterpret_cast<HMENU>(kIdStatus), nullptr, nullptr);
      loadSettings();
      // Behave like ThatSkyApp's auto-inject mode even if the user starts
      // Sky from the Steam client instead of pressing Launch Sky here.
      requestAutoInject();
      SetTimer(window, kAutoInjectTimer, kAutoInjectPollMs, nullptr);
      return 0;
    }
    case WM_SIZE:
      layout(window);
      return 0;
    case WM_COMMAND: {
      switch (LOWORD(wParam)) {
        case kIdBrowseGame: {
          std::wstring path;
          if (chooseFile(window, L"Sky executable\0Sky.exe\0Executables\0*.exe\0\0", path)) {
            SetWindowTextW(gGamePath, path.c_str());
            saveSettings();
          }
          return 0;
        }
        case kIdLaunchGame: {
          wchar_t configuredPath[MAX_PATH]{};
          GetWindowTextW(gGamePath, configuredPath, MAX_PATH);
          const DWORD runningPid = findSkyProcess(configuredPath);
          requestAutoInject();
          if (runningPid) {
            gPendingSkyPid = runningPid;
            gPendingSkyTicks = 0;
            setStatus(L"Sky.exe is already running; loading Bootstrap now.");
            return 0;
          }
          // Sky must be started through Steam. Launching the executable itself
          // makes Sky reject the session before a mod can be injected.
          const HINSTANCE result = ShellExecuteW(window, L"open", kSkySteamUri, nullptr, nullptr, SW_SHOWNORMAL);
          setStatus(reinterpret_cast<INT_PTR>(result) <= 32
            ? L"Could not open Steam. Install or start Steam and try again."
            : L"Steam was asked to start Sky. Waiting for Sky.exe before loading DLLs.");
          saveSettings();
          return 0;
        }
        case kIdAddDll: {
          std::wstring path;
          if (chooseFile(window, L"Dynamic-link libraries\0*.dll\0\0", path)) {
            const std::wstring identity = pluginIdentity(path);
            bool replacing = false;
            for (const auto& registered : gDllPaths) {
              if (CompareStringOrdinal(pluginIdentity(registered).c_str(), -1,
                                       identity.c_str(), -1, TRUE) == CSTR_EQUAL) {
                replacing = true;
                break;
              }
            }
            if (replacing && MessageBoxW(window,
                L"A plugin with this name is already imported. Replace it?",
                L"Update plugin", MB_YESNO | MB_ICONQUESTION) != IDYES)
              return 0;
            std::wstring importedPath;
            std::wstring importError;
            if (!importPlugin(path, importedPath, importError)) {
              setStatus(importError);
              return 0;
            }
            std::vector<std::wstring> updatedPaths;
            bool replacedEntry = false;
            for (const auto& registered : gDllPaths) {
              const bool samePlugin = CompareStringOrdinal(pluginIdentity(registered).c_str(), -1,
                                                           identity.c_str(), -1, TRUE) == CSTR_EQUAL;
              if (!samePlugin) {
                updatedPaths.push_back(registered);
                continue;
              }
              if (!replacedEntry) {
                updatedPaths.push_back(importedPath);
                replacedEntry = true;
              } else if (isManagedPluginPath(registered) && !samePath(registered, importedPath)) {
                DeleteFileW(registered.c_str());
              }
            }
            if (!replacedEntry) updatedPaths.push_back(importedPath);
            gDllPaths = std::move(updatedPaths);
            populateDllList();
            saveSettings();
            setStatus(replacing ? L"Plugin updated in SkyLoader's managed directory."
                                : L"DLL imported into SkyLoader's managed plugin directory.");
          }
          return 0;
        }
        case kIdRemoveDll: {
          const int selected = ListView_GetNextItem(gDllList, -1, LVNI_SELECTED);
          if (selected < 0) setStatus(L"Select a DLL first.");
          else {
            gDllPaths.erase(gDllPaths.begin() + selected);
            populateDllList();
            saveSettings();
            setStatus(L"DLL removed from the list; the original file was not deleted.");
          }
          return 0;
        }
        case kIdRefresh:
          loadSettings();
          setStatus(L"Reloaded the saved DLL list.");
          return 0;
        case kIdInject: {
          const std::wstring path = selectedPath();
          if (path.empty()) { setStatus(L"Select a DLL to inject first."); return 0; }
          if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) { setStatus(L"The DLL file no longer exists."); return 0; }
          wchar_t configuredPath[MAX_PATH]{};
          GetWindowTextW(gGamePath, configuredPath, MAX_PATH);
          const DWORD pid = findSkyProcess(configuredPath);
          if (!pid) { setStatus(L"The configured Sky.exe is not running."); return 0; }
          loadSelectedPluginViaController(path, pid);
          return 0;
        }
      }
      break;
    }
    case WM_TIMER:
      if (wParam == kAutoInjectTimer && gAutoInjectPending) {
        wchar_t configuredPath[MAX_PATH]{};
        GetWindowTextW(gGamePath, configuredPath, MAX_PATH);
        const DWORD currentPid = findSkyProcess(configuredPath);
        if (!currentPid) {
          setStatus(L"Waiting for Steam to start Sky.exe...");
          return 0;
        }
        if (currentPid != gPendingSkyPid) {
          gPendingSkyPid = currentPid;
          gPendingSkyTicks = 0;
          setStatus(L"Sky.exe detected; loading Bootstrap DLL...");
        }
        // Inject before renderer startup. The DLL itself waits for game
        // signatures, so it does not touch Lua until Sky is ready.
        gAutoInjectPending = false;
        autoInjectViaController(currentPid);
      }
      return 0;
    case WM_DESTROY:
      KillTimer(window, kAutoInjectTimer);
      saveSettings();
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
  const std::wstring directory = moduleDirectory();
  gIniPath = directory + L"\\SkyLoader.ini";

  WNDCLASSW windowClass{};
  windowClass.hInstance = instance;
  windowClass.lpszClassName = L"SkyLoaderWindow";
  windowClass.lpfnWndProc = windowProc;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  if (!RegisterClassW(&windowClass)) return 1;

  HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"SkyLoader",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 760, 520,
                                nullptr, nullptr, instance, nullptr);
  if (!window) return 1;
  ShowWindow(window, showCommand);
  UpdateWindow(window);

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return static_cast<int>(message.wParam);
}
