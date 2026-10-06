# Overlay Capture and Appearance - Phase 7B.1

## Status and Scope

Baseline: `8465098f5b8932181d1d4f83504be33887c2e535`.

- Basic Appearance: **PASS (automated UI, painting, persistence and lifecycle tests)**.
- Capture Exclusion API Integration: **PASS (real Win32 calls/readback plus injected tests)**.
- Production Qt Capture Exclusion: **NOT VERIFIED / acceptance blocked by invalid desktop capture**.
- Full Phase 7B.1 manual acceptance: **not complete**. Do not claim OCR self-capture solved.
- Release and Debug builds PASS; all **15/15 CTest suites PASS** in each build.
- Actual PP-OCRv6 Small / DeepL regression PASS using local image replay, not live desktop capture.
- Phase 7A owner's previously reported core manual acceptance remains PASS. This is historical
  owner evidence, not a new foreground-input test. DPI 100%/150% and multi-monitor remain pending.

No subagents, worktrees, packaging, license work, Installer, Phase 7B.2, Audio/ASR,
OCR filtering, helper/protocol, capture backend or realtime scheduler changes.
Original `D:/workspace/Project/Usst/LunaTranslator` remains untouched and clean.
Pre-existing untracked `licenses/` and `scripts/prepare_qt_licenses.py` remain untouched,
unstaged and outside this commit. QA images/results are ignored below `.cache/phase7b1/`.

## Windows Platform Boundary

`src/platform/WindowCaptureExclusion.*` owns the Win32 boundary. Its injectable Backend
has `supported()` and `apply(handle, excluded, error)`. Production uses
`SetWindowDisplayAffinity(HWND, 0x11)` (`WDA_EXCLUDEFROMCAPTURE`) when ON, `WDA_NONE`
when OFF. `RtlGetVersion` checks Windows 10 build 19041 or newer without a legacy
manifest-dependent version assumption. Other platforms return unsupported without
calling a Windows API. No third-party dependency or static Qt link was introduced.

The native handle must be this process's top-level window. `effectiveWinId()` does
not force its creation. A missing handle yields AwaitingWindow; show/WinIdChange
retries. Unsupported and Failed are explicit statuses; native diagnostics stay in
logs, while UI displays only `Capture exclusion unavailable`. The checkbox is
disabled when unsupported or the current attempt fails; a later successful apply
recovers availability. Failures never stop OCR/realtime or raise a MessageBox.

This is **best-effort Windows capture exclusion**, dependent on the OS, compositor
and capture API. It is not a security boundary or DRM guarantee. Microsoft's
[SetWindowDisplayAffinity documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)
describes the supported flag and these limitations. The Qt
[QWidget documentation](https://doc.qt.io/qt-6/qwidget.html#effectiveWinId)
documents native IDs and their lifecycle.

## Native Lifecycle and Phase 7A

Only TranslationWindow receives affinity; SettingsDialog and RegionSelector do not.
TranslationWindow reapplies on `showEvent`, `QEvent::WinIdChange`, the dedicated
setting signal, and after `setInteractionMode()` changes Qt window flags. It does
not cache success across a flag change, even if the numeric HWND is unchanged.
In the native probe, all 20 switches retained the same HWND; recreation was **not
observed** in this environment. A fake-handle change and WinIdChange tests cover
the recreation/reapply path without depending on that observation.

Mouse passthrough uses the existing Qt input flags. Affinity is a separate concern.
GlobalShortcutManager/OverlayInteractionController and their routing are unchanged.
No per-frame hide/grab/show loop was added; Region's existing temporary hide remains
unchanged. Background styling never calls setWindowOpacity or touches input flags.

## Settings and Appearance Model

| QSettings key | Default / validation |
| --- | --- |
| `overlay/excludeFromCapture` | Windows true; other platforms false, unsupported UI |
| `overlay/translationFontSize` | existing `max(16, application font + 6)` pt; 10-72 |
| `overlay/originalFontSize` | existing `max(12, application font + 2)` pt; 10-72 |
| `overlay/backgroundOpacity` | 0%; clamp 0-100 |
| `overlay/showTranslation` | true |
| `overlay/showOriginal` | true |

The lightweight OverlayAppearance value has only these five display fields. Invalid
stored fonts are clamped or fall back; non-finite values are safe. Corrupt double-false
visibility restores Translation. The UI disables the last checked visibility box.
Hidden labels continue receiving content; reshow immediately reveals their latest value.
Translation remains above Original and retains DemiBold weight. Defaults retain the
old fonts, transparent background, white/light subtitle colors and shadows.

Only the subtitle container gets fixed dark RGB (28,30,33) with percentage-derived
alpha. Labels, toolbar and status are not faded. Painting checks verify the background
alpha and opaque glyph pixels from a top-level QWidget render. Styles apply on settings
changes, not each OCR/translation update. Qt point sizes and logical geometry are used;
there is no manual DPR multiplication. Hidden labels leave the active layout, without
empty placeholder widgets. Minimum height accounts for visible font metrics; the
user's position and width are not reset on appearance changes.

Appearance/exclusion edits are drafts until Apply/OK. Cancel discards only unapplied
overlay drafts, preserving already applied changes. Existing language/provider combo
live-save behavior and credential validation are retained. Applying unchanged empty
OpenAI fields previously materialized provider keys and emitted translation settings
notifications. SettingsDialog now skips those no-change writes; the DeepL plan already
saves on its explicit combo change, so Apply no longer repeats that write. This small
UI fix prevents appearance-only Apply from invalidating the pipeline. No translation
backend or provider protocol changed.

`overlayAppearanceChanged` and `overlayCaptureExclusionChanged` are separate from
`translationSettingsChanged`. The existing coordinators subscribe only to the latter.
Tests verify Apply while the real coordinators run with controlled engines: unchanged
session, unchanged OCR engine/factory and translator instance, continued text updates,
and latest hidden content after reshow.

## Production Capture Evidence

QA-only `TranslatorCaptureExclusionProbe` is built under BUILD_TESTING, not packaged
or registered as an offscreen CTest. It creates a green lower window, overlaps it with
TranslationWindow showing `TRANSLATOR OVERLAY TEST`, and uses the unchanged production
`ScreenCaptureService -> QScreen::grabWindow`. It saves baseline, ON/OFF images,
rendered overlay and JSON metadata only to the explicitly supplied local QA directory.
The optional `--observe` adds bounded observation pauses; it changes no production code.

The baseline must contain the actual green target before any comparison can PASS.
Excluded frames must match that valid baseline; OFF frames must visibly differ.
`QWidget::grab()` is used separately to inspect the overlay's own rendering; it is
**not** substituted for a screen capture or physical-monitor observation.

2026-10-06, Windows Qt 6.11.2, QScreen `G24H1`, DPR 1:

| Stage | Readback / API | Actual production capture |
| --- | --- | --- |
| Lower window, no Translator overlay | n/a | all black; baseline invalid |
| Interactive ON | 0x11 / PASS | all black; cannot validate exclusion |
| ClickThrough ON | 0x11 / PASS | all black; cannot validate exclusion |
| After 20 input-mode switches ON | 0x11 / PASS | all black; cannot validate exclusion |
| ClickThrough OFF | 0x00 / PASS | all black; overlay not visible in capture |
| Interactive OFF | 0x00 / PASS | all black; overlay not visible in capture |

Final metadata: `baseline_valid=false`, `api_pass=true`, `on_pass=false`,
`off_pass=false`, `production_capture_pass=false`. The pre-validation first attempt
also produced black frames; its initial ON equality was **not valid evidence** and
was rejected by adding the baseline-content prerequisite. Final report is
`.cache/phase7b1/capture-final/report.json`. Native HWND `1576158` stayed unchanged
through 20 switches in that final attempt.

Computer Use also returned an all-black image of the lower QA window while its
accessibility tree contained the expected target text. Foreground activation failed;
a refresh/retry encountered an already-closed probe window. No browser, authentication
or security UI was automated. An observation run ended without a complete report;
it is not acceptance evidence. The reason for the unusable desktop capture is not
established. The user was asked to confirm an unlocked, visible desktop.

This does **not** prove that QScreen ignores display affinity, nor that exclusion
works. No security workaround or capture-backend change was attempted. Whether the
overlay is self-captured remains unknown; **overlay-over-region Known Limitation**
stays open until a valid ON/OFF desktop comparison succeeds. Do not claim full capture
exclusion PASS, a physically visible overlay observation, or a fix for OCR feedback.

## OCR and Translation Regression

Existing `TranslatorPaddleProbe` gained opt-in `--overlay-regression` and `--replay` QA
paths. Replay uses local images as the existing coordinator's injectable capture input;
it does not modify the default ScreenCaptureService and is explicitly **not live screen
capture**. Real production IOcrEngine/OcrCoordinator, FrameComparator/TextDeduplicator,
realtime coordinator, PP helper and DeepL are exercised with that input.

- Three owner-provided private subtitle crops, batch repeated twice: **6/6 valid**,
  PID **17648** throughout. No helper remained after exit. MKL-DNN stayed disabled.
- Replay + appearance changes + ClickThrough + Stop/Start: **4/4 valid OCR**, helper
  PID **24176** throughout, **4/4 successful DeepL translations**, all HTTP 200
  (985 / 408 / 359 / 384 ms). Warm restart **167 ms**.
  Appearance kept the realtime session; 3 samples arrived during ClickThrough.
  Original/Translation retained latest content, were restored visible; Stop halted capture.
  No helper remained after exit. Results remain local in `.cache/phase7b1/`.
- Independent stored-credential DeepL smoke: en -> zh and zh -> en, **HTTP 200**,
  **865 ms / 369 ms**. No key was printed, saved to the repo or changed.
- New offscreen suite exercises appearance-only Settings Apply in a running DeepL-provider
  configuration with controlled engines and proves no translator recreation/OCR restart.
- Existing Phase 7A suite retains 20 toggles, shortcut/native-event routing, selection
  guard, real CaptureCoordinator Escape restore, interactive Settings and cleanup checks.

New real desktop Region -> Start -> live PP-OCRv6 -> live DeepL while editing appearance
is pending because input capture is invalid. Native foreground Ctrl+Alt+T/R/S and true
cross-process mouse delivery were not newly accepted in this run. Historical owner 7A
PASS remains distinct from this phase's automated routing/replay evidence.

## Build and Tests

No old tests removed. New suite: `phase7b1_capture_appearance`; total grows 14 -> 15.
Coverage: defaults, ranges/non-finite input, storage/new-window persistence, ON/OFF,
unsupported/failure UI, missing HWND, fake handle change, WinIdChange reapply, 20 mode
switches, painted alpha/opaque glyphs, Apply/Cancel, last visible field guard, latest
hidden text, pure-appearance signal isolation and running coordinator lifecycle.

Local PowerShell commands (SDK paths only in commands, never CMake source):

```powershell
$env:PATH = 'D:/QT/Tools/mingw1310_64/bin;D:/QT/6.11.2/mingw_64/bin;' + $env:PATH
& D:/QT/Tools/CMake_64/bin/cmake.exe --build build/phase61b -j 4
& D:/QT/Tools/CMake_64/bin/ctest.exe --test-dir build/phase61b --output-on-failure
& D:/QT/Tools/CMake_64/bin/cmake.exe --build build/phase61b-debug -j 4
& D:/QT/Tools/CMake_64/bin/ctest.exe --test-dir build/phase61b-debug --output-on-failure
```

CMake automatically reconfigured the existing Release/Debug Ninja trees. Missing
optional Vulkan headers do not affect this Widgets build. Final checks: Release
15/15, **11.24 s**; Debug 15/15, **11.52 s**; `git diff --check` PASS.

## Remaining Manual Procedure

1. Unlock the desktop and ensure normal screen capture works. Run from the project:
   `build/phase61b/TranslatorCaptureExclusionProbe.exe .cache/phase7b1/manual --observe`.
2. Physically verify the green background and visible overlay marker. Inspect baseline,
   Interactive ON, ClickThrough ON, after-20 ON and both OFF captures. Require a valid
   nonblack baseline, no marker ON and visible marker OFF before marking PASS.
3. In Translator use Ctrl+Alt+T while a different app is foreground, repeat switches,
   then Ctrl+Alt+R / Region and Ctrl+Alt+S / Start. Confirm true mouse passthrough and
   recovery, and that Settings remains interactive and Region selection is unaffected.
4. With a real subtitle Region and PP-OCRv6 Small + configured DeepL running, Apply
   independent sizes/opacity and visibility. Confirm uninterrupted latest content.
   Reopen Settings after Cancel, then close/restart Translator to check saved values.
5. Test capture exclusion true and false after restart. Do not infer exclusion from
   API success alone. Keep overlay outside Region if the valid capture still includes it.
6. Repeat at actual 100%/150% DPI and multiple displays when available. These environments
   are pending, not PASS based on logical-coordinate/offscreen tests.

## 53-Point Delivery Ledger

1. Implementation complete; full native/manual capture acceptance not complete.
2. Initial commit: `8465098f5b8932181d1d4f83504be33887c2e535`.
3. Modified: CMakeLists.txt, README.md, SettingsManager.*, SettingsDialog.*, TranslationWindow.*,
   tests/PaddleIntegrationProbe.cpp.
4. Added: OverlayAppearance.h, WindowCaptureExclusion.*, Phase7CaptureAppearanceTest.cpp,
   CaptureExclusionProbe.cpp, this document.
5. Capture architecture: small injectable platform boundary owned by TranslationWindow.
6. Windows API: SetWindowDisplayAffinity; RtlGetVersion for support detection.
7. Actual WDA_EXCLUDEFROMCAPTURE calls/readback succeeded.
8. Current Windows supports the API; actual capture effect not accepted.
9. Native recreation not observed in 20 switches; fake/new-handle path tested.
10. Reapply on show, WinIdChange, preference and interaction-mode changes.
11. Default ON on Windows; OFF/unsupported on other platforms.
12. Capture preference persistence/new-window ON/OFF tested.
13. ON production screenshots all black; not valid exclusion evidence.
14. OFF screenshots also all black; no positive overlay control.
15. Interactive capture acceptance not verified.
16. ClickThrough capture acceptance not verified.
17. 20-switch API readback PASS; screen-image acceptance not verified.
18. Overlay self-capture cannot be determined in this environment.
19. All-black baseline blocks attribution; keep overlay-over-region limitation.
20. Translation font: independent 10-72 pt, original default formula retained.
21. Original font: independent 10-72 pt, original default formula retained.
22. Background opacity: 0-100%, container alpha only, default transparent.
23. No setWindowOpacity call added.
24. Show Translation implemented, display-only.
25. Show Original implemented, display-only.
26. Last enabled UI checkbox protected; storage double-false restores Translation.
27. Apply/OK persist immediately; Cancel discards unapplied overlay drafts.
28. New SettingsManager/TranslationWindow restores appearance and affinity preferences.
29. Real Paddle batch/replay and running controlled-coordinator regression PASS; new live capture pending.
30. Real DeepL smoke and 4/4 replay translations PASS; live desktop translation pending.
31. Ctrl+Alt+T routing/20 mode switches automated PASS; previous owner foreground PASS retained.
32. Ctrl+Alt+R routing/selection-guard tests PASS; new foreground manual pending.
33. Ctrl+Alt+S routing tests PASS; new foreground manual pending.
34. Existing Region logic/Escape restoration tests PASS; new desktop selection pending.
35. Geometry preserved in mode/appearance tests; no forced default relocation on style Apply.
36. Qt logical points/geometry; no DPR multiplication. Real 100%/150%/multi-monitor pending.
37. Release build PASS.
38. Debug build PASS.
39. 15/15 CTest suites PASS in each; old 14 retained.
40. New suite and explicit desktop capture probe; Paddle QA-only replay/style option.
41. Production OCR unchanged.
42. Helper/protocol/runtime/models unchanged.
43. Translation backends unchanged; only Settings UI no-change-write guard.
44. Packaging unchanged; no binaries, ZIP or license files included.
45. README updated with settings and precise acceptance status.
46. This report contains architecture, evidence, limitations and manual steps.
47. Intended final Git state: main synchronized; only the two pre-existing untracked license items.
48. Implementation commit hash is supplied in the final response (no self-referential commit hash).
49. Ordinary origin/main push required; final result supplied in the response.
50. LunaTranslator working tree clean and untouched.
51. Phase 7B.2 not started.
52. Remaining: valid native capture ON/OFF, new live desktop/foreground checks, DPI/multi-monitor,
   known no-subtitle false positives; public license gate not touched.
53. Phase 7B.2 candidates only: first close valid capture/manual evidence gaps; later consider
   additional overlay UX only under a separate request. No further feature started here.
