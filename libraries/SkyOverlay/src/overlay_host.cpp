#include "overlay_host.h"

#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>

#ifndef WS_EX_NOREDIRECTIONBITMAP
#  define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

#include <atomic>
#include <vector>
#include <string>
#include <mutex>
#include <algorithm>
#include <csetjmp>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message,
                                                             WPARAM wParam, LPARAM lParam);

namespace skyoverlay {

namespace {

constexpr wchar_t kOverlayClassName[] = L"SkyOverlayHostD3D11";
constexpr int kHotkeyF5 = 1;
constexpr int kHotkeyInsert = 2;
constexpr int kPluginHotkeyBase = 100;
constexpr int kSafeWindowMargin = 48;
constexpr int kSafeWindowPreferredWidth = 980;
constexpr int kSafeWindowPreferredHeight = 720;
constexpr UINT kShutdownMessage = WM_APP + 20;

struct PluginWindowEntry {
  int id;
  std::string name;
  SkyOverlayRenderFn callback;
  uint32_t hotkeyModifiers;
  uint32_t hotkeyVk;
  int hotkeyId;
  bool visible;
};

struct HotkeyAction {
  int hotkeyId;
  uint32_t modifiers;
  uint32_t vk; // 0 to unregister
};

struct HotkeyOption {
  const char* name;
  uint32_t vk;
};

static const HotkeyOption kAvailableHotkeys[] = {
  { "None", 0 },
  { "F1", VK_F1 },
  { "F2", VK_F2 },
  { "F3", VK_F3 },
  { "F4", VK_F4 },
  { "F6", VK_F6 },
  { "F7", VK_F7 },
  { "F8", VK_F8 },
  { "F9", VK_F9 },
  { "F10", VK_F10 },
  { "F11", VK_F11 },
  { "F12", VK_F12 },
  { "Home", VK_HOME },
  { "End", VK_END },
  { "Delete", VK_DELETE },
  { "Page Up", VK_PRIOR },
  { "Page Down", VK_NEXT },
  { "NumPad 0", VK_NUMPAD0 },
  { "NumPad 1", VK_NUMPAD1 },
  { "NumPad 2", VK_NUMPAD2 },
  { "NumPad 3", VK_NUMPAD3 },
  { "NumPad 4", VK_NUMPAD4 },
  { "NumPad 5", VK_NUMPAD5 },
  { "NumPad 6", VK_NUMPAD6 },
  { "NumPad 7", VK_NUMPAD7 },
  { "NumPad 8", VK_NUMPAD8 },
  { "NumPad 9", VK_NUMPAD9 },
};

HMODULE gModule = nullptr;
std::atomic<HWND> gOverlayWindow{nullptr};
std::atomic<bool> gStarted{false};
std::atomic<bool> gVisible{true};
std::atomic<bool> gManagerVisible{false};
std::atomic<bool> gShutdownRequested{false};
HANDLE gShutdownEvent = nullptr;

ID3D11Device* gDevice = nullptr;
ID3D11DeviceContext* gContext = nullptr;
IDXGISwapChain1*          gSwapChain          = nullptr;
IDCompositionDevice*      gCompositionDevice  = nullptr;
IDCompositionTarget*      gCompositionTarget  = nullptr;
IDCompositionVisual*      gCompositionVisual  = nullptr;
ID3D11RenderTargetView* gRenderTarget = nullptr;
ImGuiContext* gImGuiContext = nullptr;
bool gImGuiReady = false;
bool gSafeWindowMode = false;

std::vector<RECT> gInteractiveRects;
std::vector<RECT> gRenderRects;
std::mutex gInteractiveRectsLock;

// Raw anonymous render callbacks
std::vector<SkyOverlayRenderFn> gRawRenderCallbacks;
std::mutex gRawRenderLock;

// Registered plugin windows
std::vector<PluginWindowEntry> gPluginWindows;
std::mutex gPluginsLock;
int gNextWindowId = 1;

// Hotkey queue for window thread affinity
std::vector<HotkeyAction> gPendingHotkeys;
std::mutex gPendingHotkeysLock;

// VEH exception handling
bool gInPluginRender = false;
jmp_buf gPluginRenderEnv;

LONG WINAPI renderExceptionHandler(EXCEPTION_POINTERS* info) {
  if (gInPluginRender && info && info->ExceptionRecord) {
    DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
        code == EXCEPTION_DATATYPE_MISALIGNMENT || code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED ||
        code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_INT_DIVIDE_BY_ZERO) {
      longjmp(gPluginRenderEnv, 1);
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

void safeCallRender(SkyOverlayRenderFn fn) {
  if (!fn) return;
  PVOID handler = AddVectoredExceptionHandler(1, renderExceptionHandler);
  gInPluginRender = true;
  if (setjmp(gPluginRenderEnv) == 0) {
    fn();
  }
  gInPluginRender = false;
  if (handler) {
    RemoveVectoredExceptionHandler(handler);
  }
}

std::wstring getIniPath() {
  wchar_t buffer[MAX_PATH];
  if (GetModuleFileNameW(gModule, buffer, MAX_PATH)) {
    wchar_t* lastSlash = wcsrchr(buffer, L'\\');
    if (lastSlash) {
      *(lastSlash + 1) = L'\0';
      return std::wstring(buffer) + L"SkyOverlay.ini";
    }
  }
  return L"SkyOverlay.ini";
}

void queueHotkeyAction(int hotkeyId, uint32_t modifiers, uint32_t vk) {
  std::lock_guard<std::mutex> lock(gPendingHotkeysLock);
  gPendingHotkeys.push_back({hotkeyId, modifiers, vk});
}

void loadPluginSettings(PluginWindowEntry& entry) {
  std::wstring ini = getIniPath();
  std::wstring section = L"Plugin_" + std::wstring(entry.name.begin(), entry.name.end());
  entry.visible = GetPrivateProfileIntW(section.c_str(), L"Visible", entry.visible ? 1 : 0, ini.c_str()) != 0;
  entry.hotkeyModifiers = static_cast<uint32_t>(GetPrivateProfileIntW(section.c_str(), L"HotkeyModifiers", entry.hotkeyModifiers, ini.c_str()));
  entry.hotkeyVk = static_cast<uint32_t>(GetPrivateProfileIntW(section.c_str(), L"HotkeyVk", entry.hotkeyVk, ini.c_str()));
}

void savePluginSettings(const PluginWindowEntry& entry) {
  std::wstring ini = getIniPath();
  std::wstring section = L"Plugin_" + std::wstring(entry.name.begin(), entry.name.end());
  WritePrivateProfileStringW(section.c_str(), L"Visible", entry.visible ? L"1" : L"0", ini.c_str());
  wchar_t vkBuf[16];
  wsprintfW(vkBuf, L"%u", entry.hotkeyModifiers);
  WritePrivateProfileStringW(section.c_str(), L"HotkeyModifiers", vkBuf, ini.c_str());
  wsprintfW(vkBuf, L"%u", entry.hotkeyVk);
  WritePrivateProfileStringW(section.c_str(), L"HotkeyVk", vkBuf, ini.c_str());
}

bool createRenderTarget() {
  ID3D11Texture2D* backBuffer = nullptr;
  if (FAILED(gSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) || !backBuffer) {
    return false;
  }
  HRESULT hr = gDevice->CreateRenderTargetView(backBuffer, nullptr, &gRenderTarget);
  backBuffer->Release();
  return SUCCEEDED(hr);
}

void destroyRenderTarget() {
  if (gRenderTarget) {
    gRenderTarget->Release();
    gRenderTarget = nullptr;
  }
}

bool createDevice(HWND window) {
  const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  D3D_FEATURE_LEVEL featureLevel{};
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                  nullptr, 0, D3D11_SDK_VERSION,
                                  &gDevice, &featureLevel, &gContext);
  if (FAILED(hr)) {
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                            nullptr, 0, D3D11_SDK_VERSION,
                            &gDevice, &featureLevel, &gContext);
    if (FAILED(hr)) return false;
  }

  IDXGIDevice*  dxgiDevice = nullptr;
  IDXGIAdapter* adapter    = nullptr;
  IDXGIFactory2* factory   = nullptr;

  hr = gDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
  if (FAILED(hr)) return false;

  dxgiDevice->GetAdapter(&adapter);
  adapter->GetParent(IID_PPV_ARGS(&factory));

  RECT rc{};
  GetClientRect(window, &rc);

  DXGI_SWAP_CHAIN_DESC1 desc{};
  desc.Width        = static_cast<UINT>(rc.right  - rc.left);
  desc.Height       = static_cast<UINT>(rc.bottom - rc.top);
  desc.Format       = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount  = 2;
  desc.Scaling      = DXGI_SCALING_STRETCH;
  desc.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

  if (gSafeWindowMode) {
    // Safe mode uses a regular HWND swapchain. It intentionally avoids the
    // full-screen DirectComposition alpha path because some GPU/compositor
    // stacks turn transparent clears into an opaque black screen.
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    hr = factory->CreateSwapChainForHwnd(gDevice, window, &desc, nullptr, nullptr, &gSwapChain);
    factory->Release();
    adapter->Release();
    dxgiDevice->Release();
    if (FAILED(hr)) return false;
    return createRenderTarget();
  }

  desc.AlphaMode    = DXGI_ALPHA_MODE_PREMULTIPLIED;

  hr = factory->CreateSwapChainForComposition(gDevice, &desc, nullptr, &gSwapChain);
  factory->Release();
  adapter->Release();

  if (FAILED(hr)) { dxgiDevice->Release(); return false; }

  hr = DCompositionCreateDevice(dxgiDevice, IID_PPV_ARGS(&gCompositionDevice));
  dxgiDevice->Release();
  if (FAILED(hr)) return false;

  hr = gCompositionDevice->CreateTargetForHwnd(window, TRUE, &gCompositionTarget);
  if (FAILED(hr)) return false;

  hr = gCompositionDevice->CreateVisual(&gCompositionVisual);
  if (FAILED(hr)) return false;

  gCompositionVisual->SetContent(gSwapChain);
  gCompositionTarget->SetRoot(gCompositionVisual);
  gCompositionDevice->Commit();

  return createRenderTarget();
}

bool pointOverInteractiveRect(HWND window, POINT screenPoint) {
  POINT clientPoint = screenPoint;
  ScreenToClient(window, &clientPoint);
  std::lock_guard<std::mutex> lock(gInteractiveRectsLock);
  for (const auto& r : gInteractiveRects) {
    if (PtInRect(&r, clientPoint)) return true;
  }
  return false;
}

LRESULT overlayHitTest(HWND window, LPARAM lParam) {
  if (gSafeWindowMode) {
    return HTCLIENT;
  }
  if (!gVisible.load(std::memory_order_relaxed)) {
    return HTTRANSPARENT;
  }
  POINT point{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
  return pointOverInteractiveRect(window, point) ? HTCLIENT : HTTRANSPARENT;
}

void disableClickThrough(HWND window) {
  if (gSafeWindowMode) return;
  LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
  if (!exStyle) return;
  exStyle &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
  SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle);
  SetWindowPos(window, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void applyInteractiveWindowRegion(HWND window) {
  if (gSafeWindowMode) return;

  HRGN combined = CreateRectRgn(0, 0, 0, 0);
  if (!combined) return;

  bool hasRegion = false;
  {
    std::lock_guard<std::mutex> lock(gInteractiveRectsLock);
    for (const auto& rect : gRenderRects) {
      if (rect.right <= rect.left || rect.bottom <= rect.top) continue;
      HRGN item = CreateRectRgn(rect.left, rect.top, rect.right, rect.bottom);
      if (!item) continue;
      CombineRgn(combined, combined, item, RGN_OR);
      DeleteObject(item);
      hasRegion = true;
    }
  }

  if (!hasRegion) {
    HRGN empty = CreateRectRgn(0, 0, 1, 1);
    if (empty) {
      DeleteObject(combined);
      combined = empty;
    }
  }

  // After success, Windows owns the HRGN handle. The fullscreen overlay no
  // longer covers the game outside visible ImGui windows, so mouse/keyboard
  // input goes to Sky without relying on HTTRANSPARENT cross-thread behavior.
  if (!SetWindowRgn(window, combined, FALSE)) {
    DeleteObject(combined);
  }
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_NCHITTEST) {
    return overlayHitTest(window, lParam);
  }

  if (gImGuiReady && ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam)) {
    return 1;
  }

  switch (message) {
    case kShutdownMessage:
      PostQuitMessage(0);
      return 0;

    case WM_HOTKEY:
      // Master toggle keys: F5 (ThatSkyLoader convention) or Insert (PC modding standard)
      if (wParam == kHotkeyF5 || wParam == kHotkeyInsert) {
        const bool newState = !gVisible.load(std::memory_order_relaxed);
        gVisible.store(newState, std::memory_order_relaxed);
        ShowWindow(window, newState ? (gSafeWindowMode ? SW_SHOW : SW_SHOWNA) : SW_HIDE);
        if (newState && !gSafeWindowMode) disableClickThrough(window);
        if (newState && gSafeWindowMode) {
          SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
          SetForegroundWindow(window);
          SetFocus(window);
        }
        return 0;
      }
      // Plugin-specific hotkey toggle
      if (wParam >= kPluginHotkeyBase) {
        std::lock_guard<std::mutex> lock(gPluginsLock);
        for (auto& entry : gPluginWindows) {
          if (entry.hotkeyId == static_cast<int>(wParam)) {
            const bool masterHidden = !gVisible.load(std::memory_order_relaxed);
            entry.visible = masterHidden ? true : !entry.visible;
            savePluginSettings(entry);

            // If the plugin was just toggled visible, automatically wake up canvas
            if (entry.visible && !gVisible.load(std::memory_order_relaxed)) {
              gVisible.store(true, std::memory_order_relaxed);
              ShowWindow(window, gSafeWindowMode ? SW_SHOW : SW_SHOWNA);
              if (gSafeWindowMode) {
                SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                SetForegroundWindow(window);
                SetFocus(window);
              }
            }
            return 0;
          }
        }
      }
      break;

    case WM_SIZE:
      if (gDevice && wParam != SIZE_MINIMIZED) {
        const UINT w = LOWORD(lParam);
        const UINT h = HIWORD(lParam);
        if (w > 0 && h > 0) {
          destroyRenderTarget();
          gSwapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
          createRenderTarget();
          if (gCompositionDevice) gCompositionDevice->Commit();
        }
      }
      return 0;

    case WM_CLOSE:
      ShowWindow(window, SW_HIDE);
      gVisible.store(false, std::memory_order_relaxed);
      return 0;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

DWORD WINAPI overlayThread(void*) {
  // CRITICAL ARCHITECTURAL SAFETY:
  // Wait 5 seconds for Sky.exe to complete its Vulkan graphics and window startup!
  if (WaitForSingleObject(gShutdownEvent, 5000) == WAIT_OBJECT_0) {
    gStarted = false;
    return 0;
  }

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = windowProc;
  wc.hInstance = gModule;
  wc.lpszClassName = kOverlayClassName;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  RegisterClassExW(&wc);

  gSafeWindowMode =
      GetPrivateProfileIntW(L"Overlay", L"SafeWindowMode", 0, getIniPath().c_str()) != 0;

  const int virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  const int x = gSafeWindowMode ? virtualX + kSafeWindowMargin : virtualX;
  const int y = gSafeWindowMode ? virtualY + kSafeWindowMargin : virtualY;
  const int width = gSafeWindowMode
      ? std::min(kSafeWindowPreferredWidth, std::max(640, virtualWidth - kSafeWindowMargin * 2))
      : virtualWidth;
  const int height = gSafeWindowMode
      ? std::min(kSafeWindowPreferredHeight, std::max(420, virtualHeight - kSafeWindowMargin * 2))
      : virtualHeight;
  const DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
      (gSafeWindowMode ? 0 : WS_EX_NOREDIRECTIONBITMAP);

  HWND window = CreateWindowExW(
      exStyle,
      kOverlayClassName, L"Sky Loader", WS_POPUP,
      x, y, width, height, nullptr, nullptr, gModule, nullptr);

  if (!window) {
    gStarted = false;
    return 0;
  }
  gOverlayWindow.store(window, std::memory_order_release);

  if (!createDevice(window)) {
    DestroyWindow(window);
    gOverlayWindow.store(nullptr, std::memory_order_release);
    gStarted = false;
    return 0;
  }

  IMGUI_CHECKVERSION();
  gImGuiContext = ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;

  ImFontConfig fontConfig{};
  fontConfig.OversampleH = 3;
  fontConfig.OversampleV = 2;
  fontConfig.PixelSnapH = false;
  static ImVector<ImWchar> sFontRanges;
  ImFontGlyphRangesBuilder builder;
  builder.AddRanges(io.Fonts->GetGlyphRangesVietnamese());
  builder.AddChar(0x21BB); // ↻
  builder.AddChar(0x21BA); // ↺
  builder.BuildRanges(&sFontRanges);

  if (!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f,
                                    &fontConfig, sFontRanges.Data)) {
    io.Fonts->AddFontDefault();
  }

  ImGui_ImplWin32_Init(window);
  ImGui_ImplDX11_Init(gDevice, gContext);
  gImGuiReady = true;

  // Register master hotkeys: F5 and Insert
  RegisterHotKey(window, kHotkeyF5, 0, VK_F5);
  RegisterHotKey(window, kHotkeyInsert, 0, VK_INSERT);

  // Register initial plugin hotkeys
  {
    std::lock_guard<std::mutex> lock(gPluginsLock);
    for (auto& entry : gPluginWindows) {
      if (entry.hotkeyVk != 0) {
        RegisterHotKey(window, entry.hotkeyId, entry.hotkeyModifiers, entry.hotkeyVk);
      }
    }
  }

  ShowWindow(window, gSafeWindowMode ? SW_SHOW : SW_SHOWNA);
  if (gSafeWindowMode) {
    SetForegroundWindow(window);
    SetFocus(window);
  }
  UpdateWindow(window);

  MSG msg{};
  while (!gShutdownRequested.load(std::memory_order_relaxed)) {
    // Process any queued hotkey changes on the window thread
    {
      std::vector<HotkeyAction> actions;
      {
        std::lock_guard<std::mutex> lock(gPendingHotkeysLock);
        actions.swap(gPendingHotkeys);
      }
      for (const auto& act : actions) {
        UnregisterHotKey(window, act.hotkeyId);
        if (act.vk != 0) {
          RegisterHotKey(window, act.hotkeyId, act.modifiers, act.vk);
        }
      }
    }

    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        gShutdownRequested.store(true);
        break;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (gShutdownRequested.load(std::memory_order_relaxed)) break;

    const bool isVisible = gVisible.load(std::memory_order_relaxed);
    if (!isVisible) {
      Sleep(20);
      continue;
    }
    disableClickThrough(window);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Render registered named plugin windows (only if visible)
    std::vector<PluginWindowEntry> activePlugins;
    {
      std::lock_guard<std::mutex> lock(gPluginsLock);
      activePlugins = gPluginWindows;
    }
    for (const auto& plugin : activePlugins) {
      if (plugin.visible && plugin.callback) {
        safeCallRender(plugin.callback);
      }
    }

    // Render raw anonymous callbacks
    std::vector<SkyOverlayRenderFn> rawCallbacks;
    {
      std::lock_guard<std::mutex> lock(gRawRenderLock);
      rawCallbacks = gRawRenderCallbacks;
    }
    for (auto fn : rawCallbacks) {
      if (fn) safeCallRender(fn);
    }

    // Refresh interactive bounding rects for WM_NCHITTEST click-through routing.
    // Safe mode owns only a bounded tool window, so it keeps the whole client
    // interactive and does not need per-window click-through routing.
    if (!gSafeWindowMode) {
      std::lock_guard<std::mutex> lock(gInteractiveRectsLock);
      gInteractiveRects.clear();
      gRenderRects.clear();
      ImGuiContext* ctx = ImGui::GetCurrentContext();
      if (ctx) {
        for (ImGuiWindow* win : ctx->Windows) {
          if (win && win->WasActive && !win->Hidden &&
              win->Rect().GetWidth() > 0 && win->Rect().GetHeight() > 0) {
            RECT r{
              static_cast<LONG>(win->Rect().Min.x) - 4,
              static_cast<LONG>(win->Rect().Min.y) - 4,
              static_cast<LONG>(win->Rect().Max.x) + 4,
              static_cast<LONG>(win->Rect().Max.y) + 4
            };
            gRenderRects.push_back(r);
            if (!(win->Flags & ImGuiWindowFlags_NoInputs)) {
              gInteractiveRects.push_back(r);
            }
          }
        }
      }
    }
    applyInteractiveWindowRegion(window);

    ImGui::Render();
    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    gContext->OMSetRenderTargets(1, &gRenderTarget, nullptr);
    gContext->ClearRenderTargetView(gRenderTarget, clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    gSwapChain->Present(1, 0); // VSync enabled
  }

  // Teardown
  gImGuiReady = false;
  UnregisterHotKey(window, kHotkeyF5);
  UnregisterHotKey(window, kHotkeyInsert);
  {
    std::lock_guard<std::mutex> lock(gPluginsLock);
    for (const auto& entry : gPluginWindows) {
      if (entry.hotkeyVk != 0) {
        UnregisterHotKey(window, entry.hotkeyId);
      }
    }
  }

  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  if (gImGuiContext) {
    ImGui::DestroyContext(gImGuiContext);
    gImGuiContext = nullptr;
  }

  destroyRenderTarget();
  if (gCompositionVisual)  { gCompositionVisual->Release();  gCompositionVisual  = nullptr; }
  if (gCompositionTarget)  { gCompositionTarget->Release();  gCompositionTarget  = nullptr; }
  if (gCompositionDevice)  { gCompositionDevice->Release();  gCompositionDevice  = nullptr; }
  if (gSwapChain)  { gSwapChain->Release();  gSwapChain  = nullptr; }
  if (gContext)    { gContext->Release();     gContext    = nullptr; }
  if (gDevice)     { gDevice->Release();     gDevice     = nullptr; }

  DestroyWindow(window);
  UnregisterClassW(kOverlayClassName, gModule);
  gOverlayWindow.store(nullptr, std::memory_order_release);
  gStarted = false;
  return 0;
}

}  // namespace

bool start(HMODULE hModule) {
  if (gStarted.exchange(true)) return true;
  gModule = hModule;
  gShutdownRequested.store(false);
  gShutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

  HANDLE thread = CreateThread(nullptr, 0, overlayThread, nullptr, 0, nullptr);
  if (thread) {
    CloseHandle(thread);
    return true;
  }
  gStarted = false;
  return false;
}

void stop() {
  if (!gStarted.load()) return;
  gShutdownRequested.store(true);
  if (gShutdownEvent) {
    SetEvent(gShutdownEvent);
  }
  HWND window = gOverlayWindow.load(std::memory_order_acquire);
  if (window) {
    PostMessageW(window, kShutdownMessage, 0, 0);
  }
  for (int i = 0; i < 50 && gStarted.load(); ++i) {
    Sleep(20);
  }
  if (gShutdownEvent) {
    CloseHandle(gShutdownEvent);
    gShutdownEvent = nullptr;
  }
}

int registerRender(SkyOverlayRenderFn callback) {
  if (!callback) return 0;
  std::lock_guard<std::mutex> lock(gRawRenderLock);
  auto it = std::find(gRawRenderCallbacks.begin(), gRawRenderCallbacks.end(), callback);
  if (it == gRawRenderCallbacks.end()) {
    gRawRenderCallbacks.push_back(callback);
  }
  return 1;
}

void unregisterRender(SkyOverlayRenderFn callback) {
  if (!callback) return;
  std::lock_guard<std::mutex> lock(gRawRenderLock);
  auto it = std::find(gRawRenderCallbacks.begin(), gRawRenderCallbacks.end(), callback);
  if (it != gRawRenderCallbacks.end()) {
    gRawRenderCallbacks.erase(it);
  }
}

int registerPluginWindowEx(const char* name, SkyOverlayRenderFn callback, uint32_t defaultModifiers, uint32_t defaultVk, int defaultVisible) {
  if (!name || !callback) return 0;
  std::lock_guard<std::mutex> lock(gPluginsLock);

  // Check if already registered by name
  for (auto& entry : gPluginWindows) {
    if (entry.name == name) {
      entry.callback = callback;
      if (defaultVk != 0 && entry.hotkeyVk == 0) {
        entry.hotkeyModifiers = defaultModifiers;
        entry.hotkeyVk = defaultVk;
        queueHotkeyAction(entry.hotkeyId, entry.hotkeyModifiers, entry.hotkeyVk);
        savePluginSettings(entry);
      }
      return entry.id;
    }
  }

  PluginWindowEntry entry{};
  entry.id = gNextWindowId++;
  entry.name = name;
  entry.callback = callback;
  entry.hotkeyModifiers = defaultModifiers;
  entry.hotkeyVk = defaultVk;
  entry.hotkeyId = kPluginHotkeyBase + entry.id;
  entry.visible = (defaultVisible != 0);

  // Load any persisted user preferences
  loadPluginSettings(entry);

  if (entry.hotkeyVk != 0) {
    queueHotkeyAction(entry.hotkeyId, entry.hotkeyModifiers, entry.hotkeyVk);
  }

  gPluginWindows.push_back(entry);
  return entry.id;
}

int registerPluginWindow(const char* name, SkyOverlayRenderFn callback, uint32_t defaultVk, int defaultVisible) {
  return registerPluginWindowEx(name, callback, 0, defaultVk, defaultVisible);
}

void unregisterPluginWindow(int windowId) {
  std::lock_guard<std::mutex> lock(gPluginsLock);
  for (auto it = gPluginWindows.begin(); it != gPluginWindows.end(); ++it) {
    if (it->id == windowId) {
      queueHotkeyAction(it->hotkeyId, 0, 0);
      gPluginWindows.erase(it);
      break;
    }
  }
}

int getPluginWindowVisible(int windowId) {
  std::lock_guard<std::mutex> lock(gPluginsLock);
  for (const auto& entry : gPluginWindows) {
    if (entry.id == windowId) {
      return entry.visible ? 1 : 0;
    }
  }
  return 0;
}

void setPluginWindowVisible(int windowId, int visible) {
  std::lock_guard<std::mutex> lock(gPluginsLock);
  for (auto& entry : gPluginWindows) {
    if (entry.id == windowId) {
      entry.visible = (visible != 0);
      savePluginSettings(entry);
      if (entry.visible && !gVisible.load(std::memory_order_relaxed)) {
        gVisible.store(true, std::memory_order_relaxed);
        HWND window = gOverlayWindow.load(std::memory_order_relaxed);
        if (window) ShowWindow(window, SW_SHOWNA);
      }
      break;
    }
  }
}

void setPluginWindowHotkey(int windowId, uint32_t vk) {
  setPluginWindowHotkeyEx(windowId, 0, vk);
}

void setPluginWindowHotkeyEx(int windowId, uint32_t modifiers, uint32_t vk) {
  std::lock_guard<std::mutex> lock(gPluginsLock);
  for (auto& entry : gPluginWindows) {
    if (entry.id == windowId) {
      entry.hotkeyModifiers = modifiers;
      entry.hotkeyVk = vk;
      queueHotkeyAction(entry.hotkeyId, modifiers, vk);
      savePluginSettings(entry);
      break;
    }
  }
}

void showManager(int show) {
  (void)show;
  // The old manager UI has intentionally been removed. Keep this API as a
  // compatibility wake-up call for plugins built against older SkyOverlay.
  if (!gVisible.load(std::memory_order_relaxed)) {
    gVisible.store(true, std::memory_order_relaxed);
    HWND window = gOverlayWindow.load(std::memory_order_relaxed);
    if (window) ShowWindow(window, SW_SHOWNA);
  }
}

int isManagerVisible() {
  return 0;
}

void* getImGuiContext() {
  return gImGuiContext;
}

bool isVisible() {
  return gVisible.load(std::memory_order_relaxed);
}

void setVisible(bool visible) {
  gVisible.store(visible, std::memory_order_relaxed);
  HWND window = gOverlayWindow.load(std::memory_order_relaxed);
  if (window) {
    ShowWindow(window, visible ? SW_SHOWNA : SW_HIDE);
  }
}

}  // namespace skyoverlay
