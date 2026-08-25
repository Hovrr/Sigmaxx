// ============================================================================
//  Sigmaxx - Phase 0 · Spike B skeleton: CEF-OSR shell
//  ----------------------------------------------------------------------------
//  STATUS: scaffold. Boots an off-screen CEF browser on this window, pumps
//  CefDoMessageLoopWork from a timer, navigates to the same embedded benchmark
//  page, and counts OnPaint frames so the pipeline is provably alive.
//
//  NEXT (per docs/spikes/spike-b-cef-osr-plan.md):
//    - full harness wiring (ready/hostinfo/mem/inject-stats/save handlers),
//    - input injection via Chrome_RenderWidgetHostHWND (round-7 method),
//    - OnAcceleratedPaint shared-D3D-texture capture + composition,
//    - results JSON parity with Spike A for ADR-0002.
//
//  Built ONLY when -DSX_ENABLE_CEF_SPIKE=ON (see apps/spike-cef/CMakeLists.txt).
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <include/cef_app.h>
#include <include/cef_browser.h>
#include <include/cef_client.h>
#include <include/cef_render_handler.h>
#include <include/wrapper/cef_helpers.h>

#include <atomic>
#include <filesystem>
#include <string>

#include "spike_page.hpp"

namespace {

constexpr wchar_t kWndClass[] = L"SigmaxxPhase0WndCef";
constexpr UINT kTimerPump = 1;

HWND g_hwnd = nullptr;
CefRefPtr<CefBrowser> g_browser;
std::atomic<unsigned long long> g_paintFrames{ 0 };

// Minimal render handler: OSR geometry + frame counter (shared-texture path
// lands in the next iteration per the plan doc).
class SpikeRenderHandler : public CefRenderHandler {
 public:
  void GetViewRect(CefRefPtr<CefBrowser>, int& w, int& h) override {
    RECT rc{};
    if (g_hwnd && GetClientRect(g_hwnd, &rc)) { w = rc.right; h = rc.bottom; }
    else { w = 1280; h = 840; }
  }
  void OnPaint(CefRefPtr<CefBrowser>, PaintElementType, const RectList&,
               const void*, int, int) override {
    ++g_paintFrames;
  }

 private:
  IMPLEMENT_REFCOUNTING(SpikeRenderHandler);
};

class SpikeClient : public CefClient {
 public:
  CefRefPtr<CefRenderHandler> GetRenderHandler() override {
    return g_render_handler_;
  }

 private:
  CefRefPtr<SpikeRenderHandler> g_render_handler_ = new SpikeRenderHandler();
  IMPLEMENT_REFCOUNTING(SpikeClient);
};

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_SIZE:
      if (g_browser) {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        g_browser->GetHost()->WasResized();
      }
      return 0;
    case WM_TIMER:
      if (wp == kTimerPump) CefDoMessageLoopWork();
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, kTimerPump);
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

void DumpBenchmarkPage(const std::wstring& dir) {
  CreateDirectoryW(dir.c_str(), nullptr);
  std::wstring file = dir + L"\\index.html";
  HANDLE f = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(f, sxspike::kIndexHtml,
            (DWORD)(sizeof(sxspike::kIndexHtml) - 1), &written, nullptr);
  CloseHandle(f);
}

} // namespace

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
  CefMainArgs main_args(GetModuleHandleW(nullptr));
  CefRefPtr<CefApp> app;

  // Child-process handoff (GPU/renderer helpers re-enter here).
  if (CefExecuteProcess(main_args, app, nullptr) >= 0) return 0;

  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;

  wchar_t exePath[MAX_PATH]{};
  GetModuleFileNameW(nullptr, exePath, MAX_PATH);
  std::wstring base = std::filesystem::path(exePath).parent_path().wstring();

  CefSettings settings;
  settings.no_sandbox = true;
  settings.windowless_rendering_enabled = true;
  CefString(&settings.cache_path) = base + L"\\cef-cache";
  CefString(&settings.browser_subprocess_path) = exePath;

  if (!CefInitialize(main_args, settings, app, nullptr)) return 2;

  WNDCLASSEXW wc{ sizeof(wc) };
  wc.lpfnWndProc   = WndProc;
  wc.hInstance     = hInst;
  wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  wc.lpszClassName = kWndClass;
  RegisterClassExW(&wc);

  RECT rd{ 0, 0, 1280, 840 };
  AdjustWindowRect(&rd, WS_OVERLAPPEDWINDOW, FALSE);
  HWND hwnd = CreateWindowExW(0, kWndClass,
      L"Sigmaxx - Phase 0 - CEF-OSR Spike (WIP)", WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, rd.right - rd.left, rd.bottom - rd.top,
      nullptr, nullptr, hInst, nullptr);
  if (!hwnd) return 3;
  g_hwnd = hwnd;
  ShowWindow(hwnd, nCmdShow);

  DumpBenchmarkPage(base + L"\\www");

  CefWindowInfo info;
  info.SetAsWindowless(0);                       // true OSR; compositor next step
  CefBrowserSettings bsettings;
  bsettings.windowless_frame_rate = 120;

  auto client = new SpikeClient();
  CefBrowserHost::CreateBrowser(info, client,
                                L"file:///" + base + L"\\www\\index.html",
                                bsettings, nullptr, nullptr);

  SetTimer(hwnd, kTimerPump, 4, nullptr);        // pump @ ~250 Hz

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  CefShutdown();
  CoUninitialize();
  return (int)msg.wParam;
}
