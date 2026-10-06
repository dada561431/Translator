# Overlay Capture and Appearance - Phase 7B.1

## Status and Scope

Baseline: `8465098f5b8932181d1d4f83504be33887c2e535`.
Implementation commit: `02c63bded97d1bd911155cc2a81277b3262a9880`.

**Core acceptance PASS. Remaining environment-specific checks: DPI / multi-monitor.**
The owner subsequently supplied the real Windows desktop manual results recorded
below. These are owner-reported evidence, not a new agent-run screenshot/probe test.
This acceptance follow-up changes only this document and README, not production code.

- Basic Appearance: **PASS (automated UI, painting, persistence and lifecycle tests)**.
- Capture Exclusion API Integration: **PASS (real Win32 calls/readback plus injected tests)**.
- Windows Real Capture Exclusion Manual Acceptance: **PASS (owner-reported)**.
- Capture Exclusion ON/OFF Real Behavior: **PASS (owner-reported)**.
- Live OCR/Translation Appearance Regression: **PASS (owner-reported)**.
- Release and Debug builds PASS; all **15/15 CTest suites PASS** in each build.
- Agent-run PP-OCRv6 Small / DeepL replay regression PASS; later owner-reported live
  desktop regression PASS is separate evidence.
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

## Historical Agent-Run Production Capture Evidence

The following failed environment-specific attempt is retained verbatim as historical
evidence. It is not the current core acceptance status and is not retroactively PASS.

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

This attempt did **not** prove that QScreen ignores display affinity or that exclusion
works. No security workaround or capture-backend change was attempted. The agent-run
QScreen probe remains inconclusive; its all-black images are not positive evidence.
The later owner-reported manual results below close the core desktop acceptance gap,
without rewriting those probe results or claiming universal capture immunity.

## Owner-Reported Windows Manual Acceptance

The owner verified the following on a real Windows desktop after the implementation:

| Manual check | Owner-reported result |
| --- | --- |
| Capture Exclusion OFF | PASS: Windows screenshots include Translator overlay |
| Capture Exclusion ON | PASS: Windows screenshots exclude Translator overlay |
| Physical overlay visibility | PASS: the user still sees the overlay normally |
| Interactive / ClickThrough with exclusion | PASS: both modes work with exclusion |
| Overlay overlapping Region | PASS: no obvious self-capture / OCR feedback observed |
| Translation font size | PASS: adjustment works normally |
| Original font size | PASS: adjustment works normally |
| Background opacity | PASS: adjustment works; text stays clear |
| Show Original / Show Translation | PASS: both visibility controls work |
| At least one visible field | PASS: at least one field stays displayed |
| Live OCR after appearance changes | PASS: realtime OCR continues normally |
| Live DeepL after appearance changes | PASS: translation continues normally |
| Ctrl+Alt+T / R / S | PASS: no regression reported |
| Actual 100% / 150% DPI | Pending / not tested |
| Multi-monitor | Pending / not tested |

The screenshot API, screenshots, native HWND recreation and exact switch count were
not supplied with this manual report; no new numerical/probe result is inferred.
No obvious self-capture in the tested setup is a bounded observation, not a universal
fix for OCR feedback. Capture exclusion remains best effort and OS/capture API dependent.
The prior invalid agent baseline no longer blocks core acceptance given this later
manual evidence; the agent probe's historical outcome remains unchanged.

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

The owner subsequently confirmed live OCR and DeepL continue after appearance changes,
and Ctrl+Alt+T/R/S have no regression: **live appearance regression PASS**. These are
real-desktop owner results, separate from the agent's replay tests and historical
inconclusive capture attempt. No new agent-run foreground-input test is claimed.

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

For this documentation-only acceptance follow-up, existing Release/Debug executable
outputs and CMake build types were checked. Each latest complete LastTest.log contains
15 passed tests and zero failed tests (Release 2026-10-06 17:17, Debug 17:18, local time).
No build or CTest rerun was needed; no production source changed. An older
LastTestsFailed.log from an intermediate failed run is not the latest full-suite result.

## Repeatable Manual Procedure

Core manual checks are accepted above. This procedure is retained for future regression
testing, not as a list of current core blockers. Actual DPI / multi-monitor checks
remain pending. Additional restart/Cancel/probe details are not newly claimed as manual
PASS; existing automated coverage remains the evidence for them.

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

1. Core acceptance PASS; remaining environment-specific checks: DPI / multi-monitor.
2. Initial commit: `8465098f5b8932181d1d4f83504be33887c2e535`.
3. Modified: CMakeLists.txt, README.md, SettingsManager.*, SettingsDialog.*, TranslationWindow.*,
   tests/PaddleIntegrationProbe.cpp.
4. Added: OverlayAppearance.h, WindowCaptureExclusion.*, Phase7CaptureAppearanceTest.cpp,
   CaptureExclusionProbe.cpp, this document.
5. Capture architecture: small injectable platform boundary owned by TranslationWindow.
6. Windows API: SetWindowDisplayAffinity; RtlGetVersion for support detection.
7. Actual WDA_EXCLUDEFROMCAPTURE calls/readback succeeded.
8. API support verified; actual Windows capture ON/OFF behavior manually accepted by owner.
9. Native recreation not observed in 20 switches; fake/new-handle path tested.
10. Reapply on show, WinIdChange, preference and interaction-mode changes.
11. Default ON on Windows; OFF/unsupported on other platforms.
12. Capture preference persistence/new-window ON/OFF tested.
13. ON manual PASS: Windows screenshots exclude overlay; historical agent images stay inconclusive.
14. OFF manual PASS: Windows screenshots include overlay; historical agent images stay inconclusive.
15. Interactive + exclusion manual PASS (owner-reported).
16. ClickThrough + exclusion manual PASS (owner-reported).
17. Historical 20-switch API readback PASS; no new manual switch count or probe-image PASS inferred.
18. Owner reports no obvious self-capture / OCR feedback with overlay covering Region.
19. Best-effort behavior remains OS/capture API dependent; no universal feedback fix claimed.
20. Translation font: independent 10-72 pt, original default formula retained.
21. Original font: independent 10-72 pt, original default formula retained.
22. Background opacity: 0-100%, container alpha only, default transparent.
23. No setWindowOpacity call added.
24. Show Translation implemented, display-only.
25. Show Original implemented, display-only.
26. Last enabled UI checkbox protected; storage double-false restores Translation.
27. Apply/OK persist immediately; Cancel discards unapplied overlay drafts.
28. New SettingsManager/TranslationWindow restores appearance and affinity preferences.
29. Real Paddle batch/replay PASS; live OCR after appearance changes manual PASS (owner-reported).
30. DeepL smoke/replay PASS; live translation after appearance changes manual PASS (owner-reported).
31. Ctrl+Alt+T automated routing and owner-reported no-regression PASS.
32. Ctrl+Alt+R automated routing and owner-reported no-regression PASS.
33. Ctrl+Alt+S automated routing and owner-reported no-regression PASS.
34. Region logic/Escape automated PASS; owner-reported overlay-over-Region and R no-regression PASS.
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
48. Implementation commit: `02c63bded97d1bd911155cc2a81277b3262a9880`; doc follow-up hash in final response.
49. Implementation pushed to origin/main; doc follow-up push result supplied in the response.
50. LunaTranslator working tree clean and untouched.
51. Phase 7B.2 not started.
52. Remaining environment-specific checks: DPI / multi-monitor. Historical agent probe remains
   inconclusive; known no-subtitle false positives and public license gate are unchanged.
53. Phase 7B.2 candidates only: later overlay UX under a separate request; repeat environment-specific
   checks when available. No further feature started here.
