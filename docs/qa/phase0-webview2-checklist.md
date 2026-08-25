# QA Protocol — Phase 0 · Spike A (WebView2)

You need **no development tools**. Just a Windows 10/11 PC with internet and
roughly 10 minutes.

---

## Step 1 — Get the app

1. Go to your GitHub repo → **Actions** tab → click the newest green run
   (`build-windows`).
2. Scroll to the bottom **Artifacts** box → click
   **`Sigmaxx-Phase0-WebView2-Spike`** (downloads a ZIP).
3. Right-click the ZIP → **Extract All…** → open the extracted folder.

> ⚠️ Run the exe from inside the extracted folder, not from inside the ZIP.

## Step 2 — First-run security prompts (expected, safe here)

* If Windows shows *"Windows protected your PC"* (blue SmartScreen box):
  click **More info** → **Run anyway**. The app is unsigned because we have
  not set up code-signing yet — that is planned before Phase 4.
* If antivirus asks, choose **Allow**.

## Step 3 — Run and observe

Double-click **`SigmaxxPhase0-WebView2.exe`**. A dark window (~1280×840) opens
titled *“Sigmaxx — Phase 0 · WebView2 Spike · EAL bridge v0”*.

The test then runs **by itself** (total ≈ 40 seconds):

| Time | What you should see |
|---|---|
| 0–3 s | A colorful field of ~10,000 glowing shapes drifting/rotating behind a “benchmark starting” card. Top bar shows phase + progress bar. |
| Phase A (~6 s) | Camera slowly pans and zooms across the scene. Motion must look **buttery smooth**. |
| Phase B (~15 s) | ⚠️ **Your mouse cursor moves by itself** in a smooth weaving pattern over the window — hands off! This measures real input latency. Afterwards your cursor returns to where it was. |
| Phase C (~5 s) | Progress continues; scene keeps animating. |
| Phase D | A **results panel** appears with six PASS/FAIL rows and a big green **VERDICT: PASS** or red **VERDICT: FAIL**, plus the path where a results JSON was saved. |

## Step 4 — Pass / fail decision

| # | Check | Pass looks like |
|---|---|---|
| 1 | Final verdict banner | Green **PASS** |
| 2 | Animation quality | Smooth pan/zoom; no visible stutter/freezes longer than a blink |
| 3 | Phase B behaviour | Cursor weaves automatically, then returns to its original spot |
| 4 | Responsiveness while running | Window can be resized without going white/frozen |
| 5 | Results persistence | Bottom of the panel shows “Results saved: …” with a real path |

**Anything red, frozen, crashed, or blank = FAIL.**

## Step 5 — Report back (only if something fails)

1. Screenshot the results panel (or the frozen/blank window).
2. Click **Copy results JSON** and paste it into the issue.
3. Paste the GitHub Actions run URL and the last ~20 lines of its log if the
   workflow itself failed.

## Known-good notes (not failures)

* Mouse moving on its own during Phase B — that is the test itself.
* A one-time SmartScreen prompt — unsigned binary, expected until P4.
* FPS number varies with your monitor (60 Hz screens gate at ≥50 fps,
  144 Hz at ≥129) — the app adapts automatically.
