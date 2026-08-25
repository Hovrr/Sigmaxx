# Sigmaxx

Hybrid vector + raster graphic design suite. Currently executing **Phase 0**
(see [`BLUEPRINT.md`](BLUEPRINT.md)): three shell spikes, benchmark-gated.
This repository contains **Spike A — WebView2**.

## Layout

```
.github/workflows/build-windows.yml   CI: MSVC v143 build + artifact upload
apps/spike-webview2/                  Spike A: native host + benchmark page
  src/main.cpp                        Win32 host, WebView2 bootstrapping,
                                      SendInput injection driver, results writer
  src/spike_page.hpp                  Embedded HTML5/WebGL2 benchmark harness
libs/sx_eal/                          Engine Abstraction Layer contract (v0 stub)
cmake/WebView2SDK.cmake               Pinned NuGet fetch of the WebView2 SDK
docs/qa/                              Product-owner test protocols
vcpkg.json                            Dependency manifest (FlatBuffers)
CMakePresets.json                     win-release preset (Ninja + MSVC)
```

## Publishing a build (Product Owner flow)

1. Create an empty repo named `Sigmaxx` on github.com (no README init).
2. Upload this folder's contents (or run):
     git init && git add . && git commit -m "P0 scaffold"
     git branch -M main
     git remote add origin https://github.com/<you>/Sigmaxx.git
     git push -u origin main
3. Open the **Actions** tab — the `build-windows` workflow starts automatically
   (or press *Run workflow*). Green run ⇒ scroll to **Artifacts** ⇒
   download **`Sigmaxx-Phase0-WebView2-Spike`**.
4. Test per [`docs/qa/phase0-webview2-checklist.md`](docs/qa/phase0-webview2-checklist.md).

## Notes

* The WebView2 SDK is pinned via NuGet (`cmake/WebView2SDK.cmake`) for bit-stable CI.
* `vcpkg.json`'s baseline placeholder is resolved by CI against the runner's vcpkg
  commit — do not hand-edit it.
* The exe is unsigned; SmartScreen will warn on first run (expected, see QA doc).
