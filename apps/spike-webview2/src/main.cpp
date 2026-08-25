// ============================================================================
//  Sigmaxx - Phase 0 · Spike A: WebView2 shell
//  ----------------------------------------------------------------------------
//  Native Win32 host that boots an Evergreen WebView2 environment, serves the
//  embedded benchmark page over a virtual host name, injects real OS-level
//  pointer input during Phase B (SendInput), streams process working-set
//  telemetry into the page, and persists the benchmark report JSON next to the
//  executable for QA collection.
//
//  Gates under test (BLUEPRINT.md §1.3):
//    pointer→paint p95 ≤16 ms · FPS ≥ ~refresh·0.9 · jank <1%
//    throughput ≥5k ops/s · cold start ≤3 s · working set ≤700 MB
//
//  SDK contract notes (pinned Microsoft.Web.WebView2 1.0.2210.55):
//    * add_NavigationCompleted / add_WebMessageReceived take an
//      EventRegistrationToken* out-param as their second argument.
//    * SetVirtualHostNameToFolderMapping lives on ICoreWebView2_3+, so we QI.
//    * Settings property is IsZoomControlEnabled; string web messages are
//      retrieved with TryGetWebMessageAsString.
//    * Handlers are kept as ComPtrs; .Get() is only used where consumed.
//
//  Input-injection contract (QA round 3):
//    Motion uses RELATIVE SendInput deltas anchored by SetCursorPos - no
//    absolute 0..65535 mapping, which is unreliable across mixed-DPI
//    monitors. A watchdog snaps the cursor back if it leaves the window.
//
//  Build requirements: UNICODE/_UNICODE are defined by CMakeLists.txt.
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

#include <wrl.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "spike_page.hpp"
#include "sx_eal/contract.hpp"

namespace fs = std::filesystem;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kWndClass[]   = L"SigmaxxPhase0Wnd";
constexpr wchar_t kHostName[]   = L"spike.sigmaxx.app";
constexpr UINT    kTimerMem     = 1;

HWND                              g_hwnd       = nullptr;
ComPtr<ICoreWebView2Environment>  g_env;
ComPtr<ICoreWebView2Controller>   g_ctl;
ComPtr<ICoreWebView2>             g_web;

ULONGLONG                         g_tEntryMs   = 0;
std::wstring                      g_rtvVer;
fs::path                          g_exeDir, g_userDataDir, g_wwwDir;

std::atomic<bool>                 g_injectRun{ false };
std::thread                       g_injectThread;
POINT                             g_cursorRestore{};

// Telemetry snapshots for the page's pull-model initial sync ({type:"ready"}).
ULONGLONG                         g_coldStartMs = 0;
unsigned long long                g_lastMemMb   = 0;

ULONGLONG NowMs() { return GetTickCount64(); }

// ---------------------------------------------------------------- utilities
std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}
std::string WideToUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0,
                              nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n,
                      nullptr, nullptr);
  return s;
}

std::wstring JsonEscape(const std::wstring& v) {
  std::wstring out;
  out.reserve(v.size() + 8);
  for (wchar_t c : v) {
    switch (c) {
      case L'"':  out += L"\\\""; break;
      case L'\\': out += L"\\\\"; break;
      case L'\n': out += L"\\n";  break;
      case L'\r': out += L"\\r";  break;
      case L'\t': out += L"\\t";  break;
      default:    out += c;       break;
    }
  }
  return out;
}

// Pulls a JSON string value ("key":"value") out of our own well-formed
// messages, decoding the escapes emitted by JsonEscape(). Deliberately tiny:
// the spike carries no JSON dependency.
bool ExtractQuoted(const std::wstring& src, const std::wstring& key,
                   std::wstring& out) {
  const std::wstring pat = L"\"" + key + L"\":\"";
  size_t p = src.find(pat);
  if (p == std::wstring::npos) return false;
  p += pat.size();
  bool esc = false;
  for (size_t q = p; q < src.size(); ++q) {
    wchar_t ch = src[q];
    if (esc) {
      esc = false;
      switch (ch) {
        case L'"': case L'\\': case L'/': out += ch; break;
        case L'n': out += L'\n'; break;
        case L't': out += L'\t'; break;
        case L'r': out += L'\r'; break;
        case L'u': q += 4; break;             // BMP escape: not produced by us
        default:   out += ch;  break;
      }
    } else if (ch == L'\\') esc = true;
      else if (ch == L'"')  return true;
      else out += ch;
  }
  return false;
}

void PostJson(const std::wstring& json) {
  if (g_web) g_web->PostWebMessageAsJson(json.c_str());
}

void AlertHr(const wchar_t* what, HRESULT hr) {
  wchar_t msg[512];
  swprintf_s(msg, L"%s failed.\n\nHRESULT: 0x%08X\n\n"
                  L"If this mentions the WebView2 runtime, install the "
                  L"Evergreen Runtime:\n"
                  L"https://go.microsoft.com/fwlink/p/?LinkId=2124703",
             what, (unsigned)hr);
  MessageBoxW(nullptr, msg, L"Sigmaxx Phase 0", MB_ICONERROR | MB_OK);
}

// ------------------------------------------------------- input injection
// Strategy (QA round 3): avoid ABSOLUTE SendInput mapping altogether.
// MOUSEEVENTF_ABSOLUTE(+VIRTUALDESK) demands exact physical-pixel knowledge of
// the desktop bounds, which breaks on mixed-DPI multi-monitor layouts (QA:
// v1 flew across screens, v2 pinned dead). Instead we:
//   1. anchor the cursor at the window center via SetCursorPos - same
//      coordinate space as ClientToScreen, immune to DPI/virtual-desk algebra,
//   2. drive motion with RELATIVE SendInput deltas along the lissajous path,
//   3. run a containment watchdog that snaps the cursor back to the window
//      center whenever it escapes the window's own screen rectangle.
void StartInjection(HWND hwnd) {
  if (g_injectRun.exchange(true)) return;

  RECT rc{};
  if (!GetClientRect(hwnd, &rc) || rc.right <= rc.left || rc.bottom <= rc.top) {
    g_injectRun = false;
    return;
  }
  const POINT center{ (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
  const float ax = (rc.right - rc.left) * 0.30f;
  const float ay = (rc.bottom - rc.top) * 0.30f;
  GetCursorPos(&g_cursorRestore);

  POINT start{ center };
  ClientToScreen(hwnd, &start);
  SetCursorPos(start.x, start.y);            // deterministic anchor

  g_injectThread = std::thread([hwnd, center, ax, ay, start] {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    long long tick = 0;
    POINT prev = start;

    while (g_injectRun.load(std::memory_order_relaxed)) {
      const auto next = t0 + std::chrono::milliseconds(++tick * 4);   // 250 Hz
      const double t = std::chrono::duration<double>(next - t0).count();

      RECT crc{};
      if (!GetClientRect(hwnd, &crc) || crc.right <= crc.left ||
          crc.bottom <= crc.top)
        break;                                   // window gone / minimized

      POINT p{
        std::min(std::max(center.x + LONG(ax * sin(2.0 * t)), crc.left),
                 crc.right - 1),
        std::min(std::max(center.y + LONG(ay * sin(3.0 * t + 0.7)), crc.top),
                 crc.bottom - 1)
      };
      ClientToScreen(hwnd, &p);

      const LONG dx = p.x - prev.x;
      const LONG dy = p.y - prev.y;
      if (dx != 0 || dy != 0) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_MOVE;        // RELATIVE: no DPI algebra
        in.mi.dx = dx;
        in.mi.dy = dy;
        if (SendInput(1, &in, sizeof(INPUT)) == 1) prev = p;
      }

      // Containment watchdog (~every 128 ms): OS pointer-acceleration may
      // distort relative deltas; if we ever leave the window rect, snap back.
      if ((tick % 32) == 0) {
        POINT cur{};
        if (GetCursorPos(&cur)) {
          RECT wr{};
          if (GetWindowRect(hwnd, &wr) && !PtInRect(&wr, cur)) {
            POINT c{ center };
            ClientToScreen(hwnd, &c);
            SetCursorPos(c.x, c.y);
            prev = c;
          }
        }
      }

      std::this_thread::sleep_until(next);
    }
  });
}

void StopInjection() {
  if (!g_injectRun.exchange(false)) return;
  if (g_injectThread.joinable()) g_injectThread.join();
  SetCursorPos(g_cursorRestore.x, g_cursorRestore.y);
}

// ------------------------------------------------------------ persistence
fs::path SaveResultsFile(const std::wstring& filename,
                         const std::wstring& content) {
  const auto name = filename.empty()
                        ? std::wstring(L"sigmaxx-phase0-webview2-results.json")
                        : filename;

  std::wstring usedPath;
  auto tryWrite = [&](const fs::path& p) -> bool {
    try {
      std::error_code ec;
      fs::create_directories(p.parent_path(), ec);
      std::ofstream f(p, std::ios::binary | std::ios::trunc);
      if (!f) return false;
      const std::string bytes = WideToUtf8(content);
      f.write(bytes.data(), (std::streamsize)bytes.size());
      if (!f.good()) return false;
      usedPath = p.wstring();
      return true;
    } catch (...) { return false; }
  };

  if (!tryWrite(g_exeDir / name))
    tryWrite(g_userDataDir / name);

  if (usedPath.empty())
    usedPath = L"(could not write file - use the Copy results JSON button)";

  PostJson(L"{\"type\":\"saved\",\"path\":\"" + JsonEscape(usedPath) + L"\"}");
  return usedPath;
}

void HandleWebMessage(const std::wstring& msg) {
  if (msg.find(L"\"type\":\"ready\"") != std::wstring::npos) {
    // Pull-model initial sync: the page asks for current host state as soon
    // as its listener is wired, so no push can ever be "too early".
    wchar_t b[224];
    swprintf_s(b,
               L"{\"type\":\"hostinfo\",\"coldStartMs\":%llu,\"runtime\":\"%s\"}",
               (unsigned long long)g_coldStartMs, JsonEscape(g_rtvVer).c_str());
    PostJson(b);
    wchar_t m[64];
    swprintf_s(m, L"{\"type\":\"mem\",\"mb\":%llu}", g_lastMemMb);
    PostJson(m);
  } else if (msg.find(L"\"type\":\"inject-start\"") != std::wstring::npos) {
    StartInjection(g_hwnd);
  } else if (msg.find(L"\"type\":\"inject-stop\"") != std::wstring::npos) {
    StopInjection();
  } else if (msg.find(L"\"type\":\"save\"") != std::wstring::npos) {
    std::wstring content, filename;
    if (ExtractQuoted(msg, L"content", content)) {
      ExtractQuoted(msg, L"filename", filename);
      SaveResultsFile(filename, content);
    }
  }
}

// ------------------------------------------------------------- app plumbing
void UpdateBounds() {
  RECT rc{};
  if (g_ctl && GetClientRect(g_hwnd, &rc) && rc.right > rc.left &&
      rc.bottom > rc.top)
    g_ctl->put_Bounds(rc);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_SIZE:
      UpdateBounds();
      return 0;
    case WM_TIMER:
      if (wp == kTimerMem && g_web) {
        PROCESS_MEMORY_COUNTERS pmc{ sizeof(pmc) };
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
          g_lastMemMb = pmc.WorkingSetSize / (1024ULL * 1024);
          wchar_t buf[96];
          swprintf_s(buf, L"{\"type\":\"mem\",\"mb\":%llu}", g_lastMemMb);
          PostJson(buf);
        }
      }
      return 0;
    case WM_DESTROY:
      StopInjection();
      KillTimer(hwnd, kTimerMem);
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

void DumpBenchmarkPage() {
  std::error_code ec;
  fs::create_directories(g_wwwDir, ec);
  std::ofstream f(g_wwwDir / L"index.html", std::ios::binary | std::ios::trunc);
  f.write(sxspike::kIndexHtml, (std::streamsize)(sizeof(sxspike::kIndexHtml) - 1));
}

HRESULT InitWebView(HWND hwnd) {
  // -- controller-completed handler -------------------------------------
  auto onController =
      Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
          [hwnd](HRESULT hr, ICoreWebView2Controller* ctl) -> HRESULT {
            if (FAILED(hr) || !ctl) {
              AlertHr(L"CreateCoreWebView2Controller", hr);
              return E_FAIL;
            }
            g_ctl = ctl;
            ctl->get_CoreWebView2(&g_web);

            ComPtr<ICoreWebView2Settings> settings;
            if (SUCCEEDED(g_web->get_Settings(&settings)))
              settings->put_IsZoomControlEnabled(FALSE);

            RECT rc{};
            GetClientRect(hwnd, &rc);
            ctl->put_Bounds(rc);
            ctl->put_IsVisible(TRUE);
            ctl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);

            // Virtual host mapping needs ICoreWebView2_3+; fall back to a
            // plain file URL on ancient runtimes so QA still gets a window.
            std::wstring url;
            ComPtr<ICoreWebView2_3> web3;
            if (SUCCEEDED(g_web.As(&web3)) && web3) {
              web3->SetVirtualHostNameToFolderMapping(
                  kHostName, g_wwwDir.c_str(),
                  COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
              url = std::wstring(L"https://") + kHostName + L"/index.html";
            } else {
              std::wstring dir = g_wwwDir.wstring();
              for (wchar_t& c : dir) if (c == L'\\') c = L'/';
              url = L"file:///" + dir + L"/index.html";
            }
            g_web->Navigate(url.c_str());

            EventRegistrationToken tokNav{};
            g_web->add_NavigationCompleted(
                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                    [hwnd](ICoreWebView2*,
                           ICoreWebView2NavigationCompletedEventArgs* args)
                        -> HRESULT {
                      BOOL ok = FALSE;
                      if (SUCCEEDED(args->get_IsSuccess(&ok)) && !ok) {
                        SetWindowTextW(hwnd,
                                       L"Sigmaxx P0 spike - page load FAILED");
                        return S_OK;
                      }
                      const ULONGLONG cold = NowMs() - g_tEntryMs;
                      g_coldStartMs = cold;
                      wchar_t buf[192];
                      swprintf_s(buf,
                                 L"{\"type\":\"hostinfo\",\"coldStartMs\":%llu,"
                                 L"\"runtime\":\"%s\"}",
                                 (unsigned long long)cold,
                                 JsonEscape(g_rtvVer).c_str());
                      PostJson(buf);
                      SetTimer(hwnd, kTimerMem, 1000, nullptr);
                      PostJson(L"{\"type\":\"begin\"}");
                      return S_OK;
                }).Get(),
                &tokNav);

            EventRegistrationToken tokMsg{};
            g_web->add_WebMessageReceived(
                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                    [](ICoreWebView2*,
                       ICoreWebView2WebMessageReceivedEventArgs* args)
                        -> HRESULT {
                      LPWSTR raw = nullptr;
                      if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) &&
                          raw) {
                        std::wstring text(raw);
                        CoTaskMemFree(raw);
                        HandleWebMessage(text);
                      }
                      return S_OK;
                }).Get(),
                &tokMsg);

            (void)tokNav; (void)tokMsg;  // lifetime = window; never revoked
            return S_OK;
          });

  // -- environment-completed handler ------------------------------------
  // NOTE: keep handlers as ComPtrs. Never store `Callback(...).Get()` in a
  // variable - the temporary releases the delegate object at the semicolon,
  // leaving a dangling pointer that WebView2 would invoke later.
  auto onEnvironment =
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [hwnd, onController](HRESULT hr,
                               ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(hr) || !env) {
              AlertHr(L"CreateCoreWebView2Environment", hr);
              return E_FAIL;
            }
            g_env = env;
            return env->CreateCoreWebView2Controller(hwnd, onController.Get());
          });

  // Canonical bootstrap per SDK samples: explicit (default-valued) options
  // object rather than a bare nullptr for the options slot.
  ComPtr<ICoreWebView2EnvironmentOptions> opts =
      Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();

  return CreateCoreWebView2EnvironmentWithOptions(
      nullptr /* evergreen */, g_userDataDir.c_str(), opts.Get(),
      onEnvironment.Get());
}

} // namespace

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
  g_tEntryMs = NowMs();

  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;

  // Per-monitor-v2 DPI so pointer math stays pixel-true.
  if (auto fn = reinterpret_cast<decltype(&SetProcessDpiAwarenessContext)>(
          GetProcAddress(GetModuleHandleW(L"user32.dll"),
                         "SetProcessDpiAwarenessContextW")))
    fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  // WebView2 runtime availability check (friendly message for non-dev PCs).
  LPWSTR ver = nullptr;
  if (FAILED(GetAvailableCoreWebView2BrowserVersionString(nullptr, &ver)) ||
      !ver) {
    MessageBoxW(nullptr,
                L"Microsoft Edge WebView2 runtime was not found on this PC.\n\n"
                L"Install the free Evergreen Runtime from:\n"
                L"https://go.microsoft.com/fwlink/p/?LinkId=2124703\n\n"
                L"...then run this spike again.",
                L"Sigmaxx Phase 0", MB_ICONWARNING | MB_OK);
    return 2;
  }
  g_rtvVer = ver;
  CoTaskMemFree(ver);

  // Paths: results land beside the exe when writable, else under LOCALAPPDATA.
  wchar_t exePath[MAX_PATH]{};
  GetModuleFileNameW(nullptr, exePath, MAX_PATH);
  g_exeDir = fs::path(exePath).parent_path();

  wchar_t lad[MAX_PATH]{};
  GetEnvironmentVariableW(L"LOCALAPPDATA", lad, MAX_PATH);
  g_userDataDir = fs::path(lad) / L"Sigmaxx" / L"Phase0" / L"WebView2";
  g_wwwDir      = g_userDataDir / L"www";
  DumpBenchmarkPage();

  WNDCLASSEXW wc{ sizeof(wc) };
  wc.lpfnWndProc   = WndProc;
  wc.hInstance     = hInst;
  wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  wc.lpszClassName = kWndClass;
  RegisterClassExW(&wc);

  wchar_t title[128];
  swprintf_s(title, L"Sigmaxx - Phase 0 - WebView2 Spike - EAL bridge v%u",
             (unsigned)sx_eal::kBridgeProtocolVersion);

  RECT rcDesired{ 0, 0, 1280, 840 };
  AdjustWindowRect(&rcDesired, WS_OVERLAPPEDWINDOW, FALSE);
  HWND hwnd = CreateWindowExW(0, kWndClass, title, WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT,
                              rcDesired.right - rcDesired.left,
                              rcDesired.bottom - rcDesired.top,
                              nullptr, nullptr, hInst, nullptr);
  if (!hwnd) return 3;
  g_hwnd = hwnd;

  ShowWindow(hwnd, nCmdShow);
  UpdateWindow(hwnd);

  HRESULT hrInit = InitWebView(hwnd);
  if (FAILED(hrInit)) { AlertHr(L"WebView2 initialization", hrInit); return 4; }

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  CoUninitialize();
  return (int)msg.wParam;
}
