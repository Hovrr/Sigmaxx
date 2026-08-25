# Sigmaxx — Architectural Blueprint & Feasibility Study

| | |
|---|---|
| **Project** | Sigmaxx — hybrid Vector + Raster desktop graphic design suite (.exe) |
| **Phase** | 0 — Research & Planning (this document) |
| **Status** | Approved for implementation planning; no code/build executed yet |
| **Date** | 2026-08-25 |
| **Authors** | Architecture working session (ox-alpha) |

---

## Decision Record (inputs locked before this blueprint)

| # | Question | Decision | Consequence |
|---|---|---|---|
| D1 | Relationship to upstream GPL code (Inkscape core = GPLv3+, GIMP app layer = GPLv3+) | **Undecided — keep modular** | Hard Engine Abstraction Layer (EAL) across a process boundary; GPL-derived engines must remain swappable/isolatable drop-ins |
| D2 | Dominant constraint for the .exe shell | **Benchmark-gated hybrid** | Native host + web UI; WebView2 vs CEF vs WASM-in-page decided by Phase-0 spikes, not opinion |
| D3 | Meaning of "Penpot-based" UI | **UX patterns only** | Rebuild Penpot-inspired UX in TypeScript; borrow design tokens/layout logic/WASM-renderer concepts — not their ClojureScript codebase |
| D4 | Team size & horizon | **Solo / 2–3 devs, open-ended** | Aggressively minimal MVP path: vector persona first, raster persona deferred behind vector completion |

### Feasibility verdict

**Feasible**, subject to two hard conditions:

1. A strict **Engine Abstraction Layer (EAL)** between UI and engines so GPL code stays swappable and isolatable.
2. Building raster on **GEGL + babl only** (LGPL-3+) rather than GIMP's GPLv3 application layer.

Note: PhotoGIMP is a settings/icon patch over stock GIMP, not reusable source. "PhotoGIMP-based" therefore translates to *GIMP-style UX over GEGL* — which conveniently aligns with the licensing posture above.

---

## 1. Software Architecture (Tech Stack)

### 1.1 Process topology

```
┌────────────────────────── sigmaxx.exe ──────────────────────────┐
│  Host Process (C++20)                                           │
│  ├─ App Shell: lifecycle, updater, Crashpad, licensing gate     │
│  ├─ Compositor (owns THE GPU device + swapchain)                │
│  │    └─ DirectComposition tree: [canvas visual][UI visual]     │
│  ├─ Input Router (pointer/wacom/keys → engine sessions)         │
│  ├─ Document Service: scene graph root, unified undo journal,   │
│  │   autosave WAL, .sxdoc serializer                            │
│  ├─ Persona Manager (prewarms both engines)                     │
│  └─ Plugin Broker (WASM sandbox via wasmtime + JS ext world)    │
│            │ Shared-memory SPSC rings + FlatBuffers             │
│      ┌─────────┴──────────┐                                     │
│  ┌───▼──────────┐   ┌──────▼──────────┐                          │
│  │ VectorCore   │   │ RasterCore      │   ← sandboxed workers,  │
│  │ worker proc  │   │ worker proc     │     NO GPU access       │
│  │ (pluggable)  │   │ GEGL+babl only  │                         │
│  └───┬──────────┘   └──────┬──────────┘                          │
│        │ Skia DisplayLists  │ pixel tiles (shm)                  │
│  ┌───▼────────────────────▼────┐                               │
│  │ GPU thread: replays DLs into│   Engines NEVER touch the GPU │
│  │ shared D3D11 texture pool   │   (single-device rule, §4.3)  │
│  └─────────────────────────────┘                               │
│  UI Renderer (WebView2/CEF): TS/React UI, WebGL preview only    │
└──────────────────────────────────────────────────────────────────┘
```

Key structural rules:

- Engines run as **sandboxed worker processes** with no GPU device access.
- All engine/host traffic is **FlatBuffers over shared-memory SPSC rings** — never serialized strings.
- The UI layer renders chrome/DOM only; the document canvas is native GPU output composited *under* a transparent UI surface.

### 1.2 Shell decision matrix

| Criterion | A: Native host + WebView2 | B: Native host + CEF (OSR) | C: Electron/Tauri |
|---|---|---|---|
| Canvas-under-DOM GPU compositing | ✅ D3D11 texture + DirectComposition visual interleaving | ✅ `OnAcceleratedPaint` shared DXGI handle; we own final composite | ❌ No supported way to put native GPU content under DOM → forces WASM-in-renderer (Figma model) |
| Hot-path latency | ✅ Zero-copy shared textures | ✅ Same | ⚠️ IPC serialization per frame unless engine runs WASM-in-page |
| Binary/memory | ✅ Uses system Edge runtime (~0 MB bundled) | ⚠️ +~120 MB Chromium | ❌ +150–200 MB, high RAM |
| Dev velocity / ecosystem | ⚠️ Win-only APIs; Tauri-v2 variant gives Rust host + same WebView2 | ✅ Mature C++ embed story (Steam, Spotify, Epic) | ✅ Best |
| Cross-platform path | Port = swap to CEF behind `IWebHost` interface | ✅ Identical everywhere | ✅ |
| Fit for two heavy C++ cores | ✅ Out-of-process engines natural | ✅ Same | ⚠️ N-API addons in-process = crash-coupling |

**Recommendation:** build the host against an `IWebHost` abstraction.

- **Primary candidate: WebView2** — Windows-optimal, honors ".exe optimized for Windows."
- **Fallback: CEF OSR shared-texture** — identical composition semantics, fully portable.
- Electron survives only as the "engine-as-WASM" contingency, which stays alive anyway because it doubles as the future browser/Sigmaxx-web deployment target.

### 1.3 Phase-0 benchmark gates (decides WebView2 vs CEF empirically)

Harness measures, on a synthetic 10k-object scene:

| Metric | Gate |
|---|---|
| Pointer-move → photon-present p95 | ≤ 16 ms (target ≤ 8 ms) |
| Frame pacing jitter during pan/zoom @120 Hz | no visible hitching |
| Cold start | ≤ 3 s |
| Idle working set | ≤ 700 MB |
| Sustained edit throughput across bridge | ≥ 5k commands/s |

### 1.4 Stack summary & licensing map

| Layer | Choice | License | Linkage posture |
|---|---|---|---|
| UI | TypeScript + React (Penpot design tokens / layout patterns ported) | ours | clean-room |
| Shell / UI host | WebView2 (primary) / CEF (portable fallback) | MIT / BSD | OK closed-source |
| Canvas rasterizer | Skia, GPU via ANGLE(D3D11) or Vulkan | BSD-3 | OK closed-source |
| Vector math kernel | **lib2geom (verified MPL-1.1)** + Clipper2 | MPL-1.1 / Apache-2.0 | OK closed-source (disclose modifications to MPL files only) |
| Raster engine | **GEGL + babl only** (LGPL-3+), lcms2 | LGPL-3+ / MIT | dynamic link, keep patches contributed or isolated |
| Text | HarfBuzz + FreeType | MIT / FTL | OK closed-source |
| Bridge | FlatBuffers schemas + shm SPSC rings | Apache-2.0 | OK closed-source |
| Plugins | Wasmtime Component Model + sandboxed JS extension world | Apache-2.0 / MIT | OK closed-source |
| Packaging | MSIX + NSIS, Azure Signing, Crashpad | — | — |

**Explicitly excluded from the dependency graph:** GIMP application core (GPLv3+), Inkscape application core (GPLv3+), potrace (GPL), MyPaint brushlib (GPLv2+). These may only enter as optional EAL-compliant modules behind the process boundary, pending legal review.

**Modular/GPL firewall (per D1):** all engines speak only the EAL — a frozen C ABI + FlatBuffers contract — across a process boundary. Consequences:

1. Today's permissive VectorCore can be replaced tomorrow by an Inkscape-derived module without re-architecting.
2. If a GPL module ever ships, it lives in a separate process with source published — the legally cleanest isolation pattern available.
3. Formal IP-counsel sign-off is a Phase-4 ship gate, not a Phase-0 blocker.

---

## 2. Vector Networks Mechanism

### 2.1 Canonical data model (internal truth — SVG becomes a projection)

```cpp
using NodeId = uint64_t;

struct VNVertex { NodeId id; Vec2 pos; };                 // corner | bezier | mirror variants live on segments

struct VNSegment {
    NodeId a, b;                                          // directed half-edge pair generated lazily
    Vec2 cpA[2];                                          // control points leaving a
    Vec2 cpB[2];                                          // control points entering b (independent → true VN)
};

struct VNRegion {                                         // a fillable FACE, not a subpath
    std::vector<HalfEdgeRef> loop;                        // CCW outer loops, CW holes
    PaintRef paint; FillRule rule;
};

struct VectorNetwork {
    PersistentMap<NodeId, VNVertex> verts;                // structural sharing → O(1) undo snapshots
    std::vector<VNSegment> segs;
    std::vector<VNRegion> regions;                        // recomputed lazily per dirty vertex set
};
```

Invariants:

- Vertices are **shared** — it is a graph, not chains of subpaths.
- Segments may cross; crossing resolution is an explicit edit op, never implicit.
- Regions are derived state, cached with a quadtree keyed by dirty bbox.
- Every segment stores independent control points per direction (this is what makes it a network rather than a path).

### 2.2 Face extraction (half-edge left-turn sweep)

```
function buildFaces(net):
    for each vertex v: sort outgoing edges by angle          # O(E log E)
    H ← halfedges(net)                                       # each undirected seg → 2 half-edges
    visited ← {}
    for each h in H where h ∉ visited:
        face ← [ ]
        repeat:
            face.push(h); visited.add(h)
            h' ← twin(h)
            h  ← prev_ccw(h', h'.origin)                     # rotate to predecessor in angular order
        until h == start
        area ← signedArea(face)
        classify(face, area)                                 # area > 0 outer, area < 0 hole;
                                                             # nest via winding point-in-face test
    return regions
```

Hit-testing uses ear-clipped triangulation per face (incremental CDT on edits — only affected faces retessellate).

### 2.3 Bridging to standard SVG without losing graph logic

Strategy: **derived projection + lossless sidecar**.

1. **Canonical storage.** `.sxdoc` keeps the `VectorNetwork` as ground truth. Every vertex/segment/region carries a stable ULID.
2. **SVG export.** Each region serializes as a closed `<path>` loop (de Casteljau flattening at device tolerance); fill-rule applied per region-tree. Output is standard SVG that renders anywhere.
3. **Losslessness.** The same element embeds namespaced metadata:

```xml
<path d="M ... Z" fill="#3477EB"/>
<metadata><sx:network version="1">
  <v id="u1" x="120.5" y="88"/> ...
  <s id="e1" a="u1" b="u2" cpa="12,4 30,-9" cpb="-8,10 -22,3"/>
  <r id="f1" loop="e1 e3 e5" fill="p1"/>
</sx:network></metadata>
```

On reload the graph is restored verbatim; the projected `d` string is treated as disposable cache.

4. **Plain SVG import** (no sidecar present):

```
function importPathToNetwork(d):
    subpaths ← parse(d)
    weld(subpaths): spatial-hash endpoints,
                    merge within ε = 0.01 px → shared vertices      # chains become graph
    net.segs ← welded segments (control points preserved per direction)
    net.regions ← buildFaces(net)                                   # fills recovered from winding/evenodd classification
```

5. **Inkscape-compatibility mode.** Expose the whole object as a single `SPPath` whose displayed geometry is regenerated by an LPE-style effect ("spx-vector-network") reading the sidecar — mirroring how we would graft onto Inkscape's Live Path Effect pipeline if the GPL backend is ever slotted in.

### 2.4 Edit-time consistency

Every op — vertex move, segment split via `deCasteljau(seg, t)`, vertex weld, face fill — mutates only local topology:

- Recompute faces only inside the dirty vertex's star + adjacent faces.
- Push delta command `{op, inverse}` onto the host journal.
- Structural-sharing maps make snapshot-based undo cheap; additionally journal every N ops for crash recovery.

---

## 3. GPU Rendering Pipeline

### 3.1 Frame lifecycle (edit frame)

```
1. Pointer event (OS) → WebView2 input sink                     ~0.5 ms
2. Router coalesces (latest-wins mailbox) → VectorCore shm ring ~0.3 ms
3. Core mutates graph, emits {delta, damageRects, DisplayList}  ~1–4 ms
4. Host GPU thread records DL → Skia → ANGLE/Vulkan →
   shared D3D11 texture pool (tiles 256², dirty-only redraw)    ~2–4 ms
5. DirectComposition commits; UI DOM overlays unchanged         ~1 ms
```

Frame budget @120 Hz: total ≤ 8.33 ms. Overruns present last-good tiles while the edit completes; predictive interpolation on pointer drags hides residual latency.

### 3.2 Figma-grade optimizations to replicate

- **Tile cache** (256² px tiles) with LRU GPU-residency manager; zoom-aware mip levels.
- **Curve flattening cache** keyed `(segVersion, transformHash, tolerance)` — pan/zoom hits cache; only scale changes retessellate.
- **Selection/pixel-grid/outline passes** rendered as cheap overlays; they never invalidate art tiles.
- **Three channels:** control (reliable, ordered), bulk pixels (mailbox latest-wins), telemetry (lossy). Never stringify payloads; FlatBuffers over shm only.

### 3.3 WASM dual-path (deliberately kept alive)

The same VectorCore compiles via Emscripten (SIMD128 + threads with COOP/COEP headers served from a registered app scheme) to run *inside* the page — the Figma model. This is:

1. The Electron contingency for the shell decision, and
2. The future browser/Sigmaxx-web target (strategic synergy with the Penpot heritage).

The Phase-0 harness benchmarks native-bridge vs WASM-in-page so the shell gate decision is data, not opinion.

---

## 4. Persona Integration & Memory Management

### 4.1 Unified document model

One scene graph; every layer carries a payload variant:

- `VectorPayload(network)`
- `RasterPayload(buffer)`
- `Mixed(children)`

…plus a **GEGL operation graph** attached per layer. Adjustment layers and masks are nodes in this graph (`mask buffer → op chain`), evaluated lazily and cached by GEGL's tile machinery. Nothing is baked at edit time; quality loss is structurally impossible because sources are always re-rendered from origin graphs.

### 4.2 Handover protocols

**Vector → Raster (entering Persona 2):**

1. Persona Manager freezes the edit stream and drains rings (≤ 8 ms; both sessions prewarmed so the switch feels instant).
2. Host rasterizes selected vector subtrees at `scale × DPI` into pooled textures; pixels exported as **scene-linear RGBA half-float**, color-managed via lcms2 profile tags (never sRGB-u8 internally).
3. Tiles cross via shared memory; RasterCore wraps them as a **read-only GeglBuffer provider** — zero-copy, no re-quantization step exists.
4. First raster frame composites; perceived switch < 50 ms.

**Raster → Vector (returning):**

- The raster result embeds as a linked image anchor referencing the *same* buffer plus its GEGL graph — a **link, not a flatten**. Original vectors stay intact underneath; re-entering raster re-evaluates the graph at any resolution. Exports always re-render from masters.

### 4.3 Single-device rule (prevents GPU synchronization hell)

Only the **host GPU thread** creates/submits GPU work. Workers emit serializable Skia DisplayLists (bytes) or raw tiles; the host replays them. No cross-process D3D synchronization, no keyed mutexes, deterministic frame ordering.

### 4.4 Unified undo

Host-level transaction journal tags each entry with its persona; each engine implements `apply/invert` over its subgraph; cross-persona transactions compose atomically. Autosave = fsync'd redo log (database-style WAL) enabling crash recovery to the exact last transaction.

---

## 5. Execution Roadmap (calibrated for solo / 2–3 devs, vector-first)

| Phase | Window | Deliverables | Exit criteria (hard gates) |
|---|---|---|---|
| **P0 Foundations & Spikes** | wk 1–6 | Monorepo (CMake + vcpkg, MSVC v143, clang-tidy), CI, licensing audit memo, 3 shell spikes (WebView2 / CEF-OSR / WASM-in-page) on the §1.3 harness, EAL header v0 frozen | ≥ 1 shell meets gate metrics; written ADR choosing it; EAL contract reviewed |
| **P1 Canvas Core** | wk 7–20 | Permissive VectorCore (lib2geom + Clipper2), input router, Skia tiled renderer, select/move/pen/node tools on true VN semantics, SVG import/export + golden-corpus tests, single-level undo | Round-trip fidelity 100% on corpus; 60 fps pan/zoom @ 5k objects |
| **P2 Vector Persona Complete** | wk 21–36 | Faces/fills, booleans, text v1 (HarfBuzz), styles/align/transforms, Figma keymap layer + command palette, autosave WAL, plugin SDK v0 (wasmtime + JS world) | Beta-quality vector workflow; perf CI asserts green; third-party demo plugin runs sandboxed |
| **P3 Raster Persona** | wk 37–52 | Headless GEGL integration, layer stacks, non-destructive adjustments + masks, custom stamp brush engine (Windows Ink pressure), persona switch + handoff, unified undo, PSD import subset, Photoshop keymap layer | Persona switch < 50 ms p95; zero-loss round-trip verified by pixel-diff harness |
| **P4 Ship** | wk 53–68 | `.sxdoc` OADF container, crash recovery, MSIX + NSIS installers, code-signing, updater, Crashpad telemetry, a11y/i18n pass, private beta | Signed `.exe`; crash-free sessions > 99%; beta feedback loop running |

**Solo-mode adjustment:** P3 splits — raster slips behind P2 completion; EAL scaffolding still lands in P0/P1 so nothing re-architects later.

---

## 6. Risk Analysis

### R1 — GPL contamination under an undecided licensing stance *(severity: existential)*

Inkscape's app layer and GIMP's app layer are GPLv3+; naive linking permanently poisons a closed-source .exe.

*Mitigation:* the EAL process boundary (§1.4) as a non-negotiable architectural invariant; permissive-default engines (lib2geom/Clipper2, GEGL/babl — licenses verified); potrace / MyPaint-brushlib / GIMP-core excluded from the dependency graph; formal IP-counsel review as a P4 ship gate before any GPL-derived module could ship.

### R2 — Achieving Figma-class responsiveness with 2 devs *(severity: high)*

Figma's performance comes from years of tile-cache, immutable-graph, and profiling discipline.

*Mitigation:* measure-first culture — the P0 harness becomes permanent perf CI (regression = red build); adopt their proven patterns wholesale (tiles, dirty rects, display lists, structural sharing) instead of inventing; explicit degradation ladder (drop to 30 Hz tiles → static frame) so misses degrade gracefully instead of freezing.

### R3 — VN ↔ sequential-SVG semantic impedance (round-trip data loss) *(severity: high)*

Face-derived SVG paths destroy graph identity on naive import/export cycles.

*Mitigation:* canonical-network-with-sidecar (§2.3) makes loss structurally impossible for our own files; ε-weld importer + property-based golden corpus (import→export→import byte-stability on graph identity); versioned schema with forward migrations; fuzz the parser against malformed SVG.

### Honorable mentions

- **Keymap collisions** between Figma and Photoshop schemes (same keys, different meanings) → resolve via persona-scoped keymap layers + user remap file.
- **GEGL's glib assumptions** inside a non-GTK host → isolate in its own worker process pumping its own glib main context.
- **Bus factor of a tiny team** → monorepo discipline, ADRs for every major decision, no hero-knowledge modules.

---

## Next Step

Upon approval, begin P0: monorepo layout, CMake/vcpkg manifests, the EAL FlatBuffers contract, and the three-spike benchmark harness. No build execution has been performed as of this document.
