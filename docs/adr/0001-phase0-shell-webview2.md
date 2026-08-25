# ADR-0001: Phase-0 Shell Selection — WebView2

| | |
|---|---|
| **Status** | Accepted — 2026-08-25 |
| **Deciders** | Product Owner + Lead Developer |
| **Supersedes** | — |
| **Related** | BLUEPRINT.md §1.2–§1.3 (shell matrix, benchmark gates); Decision D2 ("benchmark-gated hybrid") |

## Context

BLUEPRINT §1.2 left the host-shell choice empirically open between
**WebView2**, **CEF (OSR)**, and **Electron/WASM-in-page**, to be settled by a
three-spike harness measuring the Phase-0 gates: pointer→paint p95 ≤16 ms,
FPS ≥ refresh·0.9, jank <1 %, throughput ≥5 k ops/s, cold start ≤3 s,
working set ≤700 MB.

Spike A implemented the complete measurement rig on WebView2 (Evergreen):
Win32 host + transparent-DOM benchmark page (WebGL2, 10 000 instanced objects,
pan/zoom camera stress) + automated input injection + telemetry bridge +
self-grading verdict panel. It was exercised on a **mixed-DPI dual-monitor**
setup (1080p laptop + 2K external) across eight QA iterations.

## Measured baseline (both monitors, final build)

| Gate | Target | 2K run | 1080p run | Verdict |
|---|---|---|---|---|
| Pointer→paint p95 | ≤16 ms (stretch ≤8) | **9.59 ms** (p50 5.09, p99 10.5, n=991) | 10.1 ms | PASS gate; stretch carried to P1 |
| Average FPS | ≥ refresh·0.9 | **99.94** @ ~100 Hz est. | comparable | PASS |
| Jank frames (>2× median) | <1 % | **0.00 %** | 0 % | PASS |
| Edit-command throughput | ≥5 000 ops/s | **170 M ops/s** | 247 M ops/s | PASS (upper-bound probe) |
| Cold start | ≤3 000 ms | **625 ms** | 796 ms | PASS |
| Working-set peak | ≤700 MB | **21 MB** (host process) | 23 MB | PASS |

Raw 2K report:

```json
{
  "meta": {
    "when": "2026-08-25T21:18:56.110Z",
    "screen": { "w": 1280, "h": 840, "dpr": 1 },
    "refreshEstimateHz": 100,
    "objects": 10000,
    "suite": "phase0-webview2-spike",
    "protocol": "bridge-v0",
    "injection": { "ticks": 3748, "moves": 3748 }
  },
  "metrics": {
    "inputLatencyMs": { "p50": 5.09, "p95": 9.59, "p99": 10.5, "n": 991 },
    "fpsAvg": 99.94,
    "frameMedianMs": 10,
    "jankPct": 0,
    "throughputOpsPerSec": 170352730,
    "coldStartMs": 625,
    "peakWorkingSetMb": 21
  },
  "overallPass": true
}
```

VERDICT: **PASS** on both displays.

## Decision

**WebView2 (Evergreen runtime) is adopted as the Sigmaxx shell** for Phase-0
and P1 development, behind the existing `IWebHost` abstraction so the CEF
fallback remains a drop-in.

Scope guard, honoring D2: **Spike B (CEF-OSR shared-texture) remains
scheduled** before the P2 renderer freeze; it must reproduce this harness
unchanged for apples-to-apples comparison. Electron/WASM-in-page stays alive
solely as the future web-deployment contingency (§3.3).

## Engineering record (institutional knowledge paid for in QA rounds)

1. **Shell composition:** DOM renders chrome only; document canvas is native
   GPU output composited beneath (P1 target). The spike measured the DOM-canvas
   path; expect pointer→paint to *improve* once native compositing removes a
   renderer hop — re-baseline at P1 exit.
2. **Input injection — what failed and why** (each verified by counters):
   - `SendInput` ABSOLUTE+`VIRTUALDESK` normalized against one monitor rect →
     cursor flew across all displays (virtual-desktop space ≠ monitor rect).
   - Same normalized against `SM_*VIRTUALSCREEN` → dead cursor on mixed-DPI
     Per-Monitor-V2 (physical-pixel bounds unreliable via these metrics).
   - Relative `MOUSEEVENTF_MOVE` deltas → swallowed by pointer-acceleration
     curves ("mickeys").
   - `SetCursorPos` → moves the arrow but generates **no trusted input
     frames**; Chromium never fires `pointermove` (ticks==moves==3748, n=0).
   - ✅ **Working method:** post `WM_MOUSEMOVE` (coords packed in `lParam`,
     client-space, resolved on the UI thread) directly into Chromium's internal
     **`Chrome_RenderWidgetHostHWND`** child found via `EnumChildWindows`.
     Geometry math lives on the UI thread; worker does pure math +
     `PostMessageW`. Health counters (`ticks`/`moves`) are published to the
     page and persisted in results JSON.
3. **Benchmark-clock lesson:** rAF's callback timestamp is *vsync frame start*
   and can precede event dispatch time; latency must be computed as
   `performance.now() − event.timeStamp` at consumption, never `now − t`.
   The `l ≥ 0` guard had silently discarded 100 % of samples (QA r8).
4. **Messaging contract:** host pushes arrive at the page as parsed objects
   (`PostWebMessageAsJson`); page pull-syncs state via `{type:"ready"}` after
   wiring its listener. Never `JSON.parse` an already-parsed object.
5. **SDK pins:** WebView2 SDK fetched as pinned NuGet (1.0.2210.55);
   `vcpkg` manifest baseline resolved against a genuine git clone of vcpkg
   (runner copies are `.git`-stripped).

## Consequences

- Zero-bundle install on Windows 10/11 (Evergreen runtime assumed; friendly
  fallback dialog links the runtime bootstrapper). Cross-platform port swaps
  in CEF behind `IWebHost` — unchanged public contract.
- Unsigned binaries trigger SmartScreen until the P4 signing gate.
- Stretch goal pointer→paint ≤8 ms is inherited by P1 as an explicit
  optimization target alongside the native-compositor migration.
- Harness (`spike_page.hpp` + counters + gates) is **frozen as the parity
  instrument**: Spike B must consume it unmodified.

## Verification

- Both-monitor QA runs, verdict panels PASS (screenshots in QA log).
- Results JSONs archived beside executables (`sigmaxx-phase0-webview2-results.json`)
  and quoted above; CI artifacts retained 14 days per run.
