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
//  SDK drift notes (QA r-B2, pinned dist = cef_binary_151.x):
//    * Browser params/returns across the public C++ API are Chromium's
//      scoped_refptr<T>, NOT CefRefPtr<T> - overrides must match exactly.
//    * WIN32_LEAN_AND_MEAN hides CoInitializeEx -> include <objbase.h>.
//
//  Diagnostics: every early-return failure path in wWinMain raises a blocking
//  MessageBoxW (ReportFatal) instead of exiting invisibly. Process exit codes:
//    1 CoInitializeEx · 2 CefInitialize · 3 CreateWindowExW ·
//    4 RegisterClassExW. (A >= 0 return from CefExecuteProcess is the normal
//    child-process handoff, not a failure.)
//    Silent abort()s inside libcef.dll bypass any UI entirely; CEF logging is
//    therefore enabled (LOGSEVERITY_INFO -> debug.log beside the exe) so
//    Chromium itself records WHY a CHECK() fired.
//
//  Built ONLY when -DSX_ENABLE_CEF_SPIKE=ON (see apps/spike-cef/CMakeLists.txt).
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>                 // CoInitializeEx/CoUninitialize (LEAN_AND_MEAN hides them)

#include <include/cef_app.h>
#include <include/cef_browser.h>
#include <include/cef_command_line.h>
#include <include/cef_browser_process_handler.h>
#include <include/cef_client.h>
#include <include/cef_life_span_handler.h>
#include <include/cef_render_handler.h>
#include <include/wrapper/cef_helpers.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <string>

#include "spike_page.hpp"

namespace {

constexpr wchar_t kWndClass[] = L"SigmaxxPhase0WndCef";
constexpr UINT kTimerPump = 1;

HWND g_hwnd = nullptr;
scoped_refptr<CefBrowser> g_browser;   // CEF 151+: public API uses Chromium's scoped_refptr
std::atomic<unsigned long long> g_paintFrames{ 0 };

// Blocking fatal-error dialog for wWinMain's early-return paths. A silent
// "create cef-cache then vanish" launch is undiagnosable; this surfaces the
// failing step plus the Win32 error text on screen before the process exits.
void ReportFatal(const wchar_t* what, int exitCode) {
  const DWORD err = GetLastError();
  wchar_t sys[256] = L"<no system message>";
  FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                 nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                 sys, 255, nullptr);
  wchar_t msg[640];
  swprintf_s(msg,
             L"%s failed.\n\n"
             L"Win32 error %lu: %s\n\n"
             L"The process will exit (code %d).\n\n"
             L"Typical causes: CEF runtime files (libcef.dll, *.pak, "
             L"icudtl.dat, snapshot/v8_context blobs) missing next to the exe.",
             what, (unsigned long)err, sys, exitCode);
  MessageBoxW(nullptr, msg, L"Sigmaxx Phase 0 - Fatal Error",
              MB_ICONERROR | MB_OK | MB_SETFOREGROUND);
}

class SpikeApp : public CefApp, public CefBrowserProcessHandler {
 public:
  scoped_refptr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  void OnBeforeCommandLineProcessing(const CefString& process_type, scoped_refptr<CefCommandLine> command_line) override {
    command_line->AppendSwitchWithValue("lang", "en-US");
    command_line->AppendSwitch("disable-gpu");
    command_line->AppendSwitch("disable-gpu-compositing");
  }
 private:
  IMPLEMENT_REFCOUNTING(SpikeApp);
};

// Minimal render handler: OSR geometry + frame counter (shared-texture path
// lands in the next iteration per the plan doc).
//
// NOTE (SDK drift, QA r-B2): modern CEF passes browsers as Chromium's
// scoped_refptr<CefBrowser>, NOT CefRefPtr - overriding with the exact base
// signatures is mandatory or 'override' fails with C3668.
class SpikeRenderHandler : public CefRenderHandler {
 public:
  void GetViewRect(scoped_refptr<CefBrowser>, CefRect& rect) override {
    RECT rc{};
    int w = 1280;
    int h = 840;
    if (g_hwnd && GetClientRect(g_hwnd, &rc)) {
      if (rc.right > rc.left) w = rc.right;
      if (rc.bottom > rc.top) h = rc.bottom;
    }
    rect = CefRect(0, 0, w, h);
  }

  void OnPaint(scoped_refptr<CefBrowser>, PaintElementType, const RectList&,
               const void*, int, int) override {
    ++g_paintFrames;
  }

 private:
  IMPLEMENT_REFCOUNTING(SpikeRenderHandler);
};

// Captures the browser handle so WM_SIZE can drive WasResized().
class SpikeLifeSpanHandler : public CefLifeSpanHandler {
 public:
  void OnAfterCreated(scoped_refptr<CefBrowser> browser) override {
    g_browser = browser;
  }

 private:
  IMPLEMENT_REFCOUNTING(SpikeLifeSpanHandler);
};

class SpikeClient : public CefClient {
 public:
  scoped_refptr<CefRenderHandler> GetRenderHandler() override {
    return render_handler_;
  }
  scoped_refptr<CefLifeSpanHandler> GetLifeSpanHandler() override {
    return life_span_handler_;
  }

 private:
  // CEF 151+: store members as scoped_refptr so they convert cleanly to the
  // scoped_refptr return types of the overridden getters above.
  scoped_refptr<SpikeRenderHandler> render_handler_ = new SpikeRenderHandler();
  scoped_refptr<SpikeLifeSpanHandler> life_span_handler_ = new SpikeLifeSpanHandler();
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
  scoped_refptr<CefApp> app = new SpikeApp();

  // Child-process handoff (GPU/renderer helpers re-enter here). NOTE: this is
  // a silent exit path BY DESIGN - a >= 0 return means "I am a helper
  // process"; the shell itself never takes it.
  if (CefExecuteProcess(main_args, app, nullptr) >= 0) return 0;

  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
    ReportFatal(L"CoInitializeEx", 1);
    return 1;
  }

  wchar_t exePath[MAX_PATH]{};
  GetModuleFileNameW(nullptr, exePath, MAX_PATH);
  std::wstring base = std::filesystem::path(exePath).parent_path().wstring();

  CefSettings settings;
  settings.no_sandbox = true;
  settings.windowless_rendering_enabled = true;
  CefString(&settings.cache_path) = base + L"\\cef-cache";
  CefString(&settings.browser_subprocess_path) = exePath;

  // Crash forensics (QA r-B5): fatal CHECK() failures inside Chromium call
  // abort() and kill the process before any UI can appear. Route CEF's own
  // log to disk so the real cause lands in debug.log beside the exe.
  settings.log_severity = LOGSEVERITY_INFO;
  CefString(&settings.log_file) = base + L"\\debug.log";

  // Locale pinning (QA r-B6, confirmed by debug.log): Chromium failed to
  // resolve a default locale ("locale_file_path.empty() for locale ") and
  // CHECK()ed inside the resource manager. Pin en-US explicitly and point
  // CEF at the locales/ tree shipped beside the exe by spike_cef's
  // POST_BUILD copy_directory step.
  CefString(&settings.locale).FromString("en-US");
  CefString(&settings.locales_dir_path) = base + L"\\locales";
  CefString(&settings.resources_dir_path) = base;

  // CEF creates the cache directory early during initialization, so a failure
  // here matches the observed "cef-cache exists, no window, instant exit"
  // symptom. Surface the real cause instead of returning invisibly.
  if (!CefInitialize(main_args, settings, app, nullptr)) {
    ReportFatal(L"CefInitialize", 2);
    return 2;
  }

  WNDCLASSEXW wc{ sizeof(wc) };
  wc.lpfnWndProc   = WndProc;
  wc.hInstance     = hInst;
  wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  wc.lpszClassName = kWndClass;

  // Registration was present but UNCHECKED: a failed RegisterClassExW makes
  // the CreateWindowExW below fail with ERROR_CANNOT_FIND_WND_CLASS, so guard
  // it explicitly and report it independently of window creation.
  if (!RegisterClassExW(&wc)) {
    ReportFatal(L"RegisterClassExW", 4);
    CefShutdown();
    CoUninitialize();
    return 4;
  }

  RECT rd{ 0, 0, 1280, 840 };
  AdjustWindowRect(&rd, WS_OVERLAPPEDWINDOW, FALSE);
  HWND hwnd = CreateWindowExW(0, kWndClass,
      L"Sigmaxx - Phase 0 - CEF-OSR Spike (WIP)", WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, rd.right - rd.left, rd.bottom - rd.top,
      nullptr, nullptr, hInst, nullptr);
  if (!hwnd) {
    ReportFatal(L"CreateWindowExW", 3);
    CefShutdown();
    CoUninitialize();
    return 3;
  }
  g_hwnd = hwnd;
  ShowWindow(hwnd, SW_SHOW);

  DumpBenchmarkPage(base + L"\\www");

  CefWindowInfo info;
  // Pass the host window (QA r-B5): modern OSR expects a parent HWND; with
  // nullptr the windowless browser creation can fail silently inside
  // libcef.dll. Binding it also anchors future input/focus routing.
  info.SetAsWindowless(hwnd);
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
