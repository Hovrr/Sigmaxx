# Spike B — CEF-OSR Shared-Texture Shell (plan)

Goal: give ADR-0001 its apples-to-apples counterpart. Same harness, same six
gates, different host: CEF in **off-screen rendering (OSR)** mode with
`OnAcceleratedPaint` shared-texture capture, composited natively under the UI
layer via DirectComposition — the exact path BLUEPRINT §1.2 column B proposes.

## Parity rules (non-negotiable)

- `apps/spike-webview2/src/spike_page.hpp` is consumed **unmodified**
  (served to CEF via `file://` from the same generated `www/index.html`).
- Same six gates, same thresholds, same verdict JSON schema, same
  `inject-stats` counters.
- Input injection reuses the round-7 method (`WM_MOUSEMOVE` →
  `Chrome_RenderWidgetHostHWND` child lookup) — identical code shape.

## Build strategy

- `cmake/CefSdk.cmake`: at configure time, download the official cef-builds
  `index.json`, resolve the newest **stable windows64 minimal** distribution
  via `string(JSON …)`, download + `ARCHIVE_EXTRACT` it into
  `build/_ext/cef/`, and expose imported target `sx::cef`
  (`Release/libcef.lib` + include dir) plus a DLL-copy helper
  (`libcef.dll`, `chrome_elf.dll`, `d3dcompiler_47.dll`, `libEGL.dll`,
  `libGLESv2.dll`, `vk_swiftshader.dll` + `icudtl.dat`, `resources.pak`,
  `snapshot_blob.bin`, `v8_context_snapshot.bin`).
- Entire spike compiles only when `-DSX_ENABLE_CEF_SPIKE=ON`; default builds
  (and the green CI lane) are untouched until we enable it deliberately.

## Implementation steps (ordered)

1. **Boot:** `CefMainArgs`/`CefSettings` (`windowless_rendering_enabled=true`,
   `no_sandbox=true`, multi-threaded-message-loop off; pump via
   `CefDoMessageLoopWork()` on a 4 ms Win32 timer).
2. **Client plumbing:** `CefClient` → `CefRenderHandler` with
   `GetViewRect` = client size; `OnPaint` (software path first) blitting into
   a staging buffer; assert frame cadence before optimizing.
3. **Shared-texture upgrade:** switch handler to `OnAcceleratedPaint`
   (shared D3D11 texture handle + `DXGI_SHARED_HANDLE` in
   `CefAcceleratedPaintInfo`), open it in our device, wrap as
   DirectComposition visual layered UNDER the (future transparent) UI surface.
4. **Harness wiring:** navigate to the dumped `index.html`, wire
   `CefMessageRouterBrowserSide` ↔ `HandleWebMessage` equivalent of the
   WebView2 handlers (`ready/hostinfo/mem/inject-stats/save`).
5. **Packaging:** extend the CI job to stage the CEF runtime set; watch the
   artifact size jump (~+80–120 MB) — acceptable for a spike.
6. **Run both monitors, collect JSONs, draft ADR-0002.**

## Risks / known unknowns

- `OnAcceleratedPaint` availability & stability on the chosen stable build;
  software `OnPaint` fallback must not silently become the shipped path
  (assert hardware path in logs).
- CEF child HWND class names may drift vs Chromium versions — re-use the same
  `EnumChildWindows` class-name search, assert found, else fail loudly.
- Message-pump starvation during long JS tasks: keep benchmark page identical;
  if jank appears here but not in WebView2, that IS the data.

## Exit criteria

Green board on the same gates on both displays, or a documented loss with
numbers — either outcome completes the D2 decision record (ADR-0002).
