# Phase 6 Real-time Pipeline

## Acceptance Status

Implementation, configure/build, and all seven automated suites are complete.
**Full Phase 6 acceptance is NOT complete.** Windows Computer Use stopped this
turn because it could not confidently determine the current browser URL for
policy enforcement. No further desktop input was attempted. Browser control
also reported that Edge was not available through the browser connector.
Therefore Notepad Region/Start interaction, five consecutive Bilibili subtitles,
video Stop/restart, screenshots, physical multi-monitor/DPI checks, and a
5-10 minute actual-screen stability test remain pending. No video was downloaded,
scraped, or uploaded. Historical Phase 4.1 video rows are not Phase 6 evidence.

Offline probes use locally rendered QImages, real Tesseract, the actual scheduler,
and optional real DeepL. They are explicitly **synthetic-input**, not QScreen
or video tests. Timers run for actual wall-clock durations, not simulated time.

## Initial State And Files Read

Started on clean `main...origin/main`, HEAD
`10bfa4b25aca91bc698fb837e3668582fe46a33c`. The eight-entry history includes Phase
5 and 4.1; no reset, worktree, subagent or reference-repository edit was used.

Reviewed CaptureCoordinator, OcrCoordinator, TranslationCoordinator, RegionSelector,
ScreenCaptureService, TranslationWindow, SettingsDialog, SettingsManager, main,
OCR contracts/preprocessor/Tesseract, translator contracts/factory/backends,
CMake, existing test contracts, README and architecture/Phase 4.1/5/5.1 reports.

## Runtime Design

```text
saved single-screen logical Region
  -> Start validates saved screen and rectangle
  -> immediate capture + QTimer every 300ms
  -> ScreenCaptureService (GUI thread)
  -> FrameComparator (grayscale thumbnail, bounded size)
  -> changed frame / empty confirmation / error retry
  -> one OCR worker + at most one newest pending frame
  -> OcrResult + task ID + active session validation
  -> TextDeduplicator / empty debounce
  -> Original updates first
  -> TranslationCoordinator (existing translation request ID)
  -> configured ITranslator (None sends nothing)
  -> TranslationResult -> Translated
  -> next timer tick
```

`RealtimePipelineCoordinator` owns the lifecycle; UI only emits requests and
receives authoritative running state. `main.cpp` owns/wires all services without
embedding scheduling logic. CaptureCoordinator emits selectionStarted before
hiding the overlay; selecting a Region always stops monitoring before selector
overlays appear. Success/cancellation stays stopped. A successful selection
still performs the original single-frame capture/OCR check in OneShot mode.

### Start And Stop

- Start checks SettingsManager's current validated rectangle and screen name.
  Minimum size is 10x10 logical pixels; the entire rectangle must fit one screen.
  Missing screen/invalid region gives lightweight feedback, not a random fallback.
- Start is idempotent, rotates the session, clears frame/text references and
  immediately captures. One precise timer then checks every 300ms, about 3.33 Hz.
  This trades subtitle-detection delay (up to one tick) against unnecessary work;
  it is not a 30/60 FPS video-processing loop. Options are internal, not settings UI.
- Stop stops both timers, retires the generation, clears pending, marks stopped,
  and calls `TranslationCoordinator::invalidate(false)`. It never waits for OCR
  or network. Existing content is retained; an already displayed pending label
  can remain until new content/state arrives, but no old completion can replace it.
- The physical OCR task may finish after Stop; request ID + session + mode +
  source language must all match before any UI/status/translation effect.
  Restart can wait behind that one task while retaining one newest fresh frame.
- Close requests Stop before persisting geometry. Region selection requests Stop
  before hiding. Semantic settings rotate the generation, discard pending and
  reset frame/text references. Running continues with new snapshots next tick;
  one-shot work is retired. The provider coordinator still rebuilds its backend.

### OCR Bounds And Identity

OcrCoordinator exposes `tryRecognize` (returns zero while physically busy) and
`taskFinished(requestId, result)`. Its legacy `recognize` also retains at most
one pending input instead of posting an unlimited number of worker callbacks.
The production pipeline exclusively uses tryRecognize and owns its single
pending Frame: capture image, language, session, sequence and monotonic timestamp.
B/C/D arriving behind A overwrite pending; only A then D execute. Image pixels
are implicitly shared and released after completion/replacement. No frame list,
per-frame disk save, thread pool, or background screen-grab operation exists.

Mode is an enum: Stopped, OneShot, Realtime. One-shot results are not mistaken
for real-time sessions. The physical active request remains tracked across
Stop/restart; logical retirement does not pretend the worker is idle.

### Frame Comparator

Qt-only proportional smooth thumbnail, at most 160x90, then Grayscale8.
The reference is the last **accepted** thumbnail, not every sampled frame, so
gradual small changes accumulate. Input-size changes force changed; null input
is safely ignored. Compare only thumbnail pixels:

- pixel delta >= 18 counts as materially changed;
- changed-pixel ratio >= **0.0005 (0.05%)**, OR mean absolute delta >= **2.0**;
- same pixels and 1-2 minor noisy pixels are ignored.

These are provisional, conservative defaults, not optimal video thresholds.
Initial 0.5% ratio passed larger-block tests but missed small trailing digit
changes in the synthetic live probe. A 0.1% candidate also failed the added
8x12 full-resolution character-block test at 640x200. The 0.05% candidate passes
that regression. Real-video calibration is still pending. Moving backgrounds
can trigger OCR frequently; text dedup and strict bounds are the second layer.
Even this conservative comparator cannot guarantee every punctuation/color-only
change survives grayscale/downsampling. This is a known detection limitation.

### Text And Empty Handling

Normalize CRLF/CR to LF, trim surrounding/per-line whitespace, collapse horizontal
whitespace to a space, retain line breaks and punctuation. Exact normalized
comparison only; no edit-distance/fuzzy suppression or OCR correction. Hello,
Hello, Hello, World, World starts only two translations.

Original is emitted before TranslationCoordinator accepts the changed text.
Duplicate text neither updates UI nor starts translation. Empty text never
translates. Two consecutive valid empty OCR completions clear original and retire
translation, returning the translation field to its placeholder. An unchanged
empty frame is explicitly recognized again for confirmation; afterward it is
deduplicated normally. Errors reset the consecutive-empty count, retain content,
and permit a retry on the next tick even if pixels are unchanged.
One-shot valid blank input preserves the historical immediate-clear behavior;
there is no running timer to provide a second confirmation in that mode.

One transient capture failure is skipped; four consecutive failures stop and
show concise feedback. Successful capture resets the counter. Lost region/screen
stops immediately. A scoped guard prevents capture-tick reentry.

### Screen And Window Constraints

QScreen grabWindow remains GUI-thread-only in the current wiring. Phase 3 logical
coordinates, negative screen origins, one-screen validation and returned image
DPR are unchanged. No new manual DPR multiplication was added. Physical mixed-DPI
hardware was not tested this phase.

QScreen desktop capture includes overlapping application windows. Neither the
translation overlay nor Settings is excluded. README explicitly requires both
outside the Region. No per-tick hide/show flicker or unverified native exclusion
API was introduced. Reliable window exclusion remains future work.

## Automated Verification

Seven CTest entries: phase2_ui_and_settings, phase3_capture_logic, phase4_ocr_logic,
phase41_ocr_tuning, phase5_translation, phase51_provider_and_credentials,
phase6_realtime_pipeline. All seven pass after the final threshold change.

New coverage: invalid/valid Region, immediate capture, idempotent Start/Stop,
session rotation, timer ticks only while running, static frame skip, same/noisy/
small-character/large/size-changed/null frames, bounded A->D scheduling,
nonblocking Stop/restart, stale OCR with and without restart, settings rotation,
one-shot retirement on Start, missing saved screen, normalized text duplicates,
two translation calls for five OCR texts, stopped translation reply rejection,
stable-empty confirmation, OCR error preservation, capture-failure threshold,
and bounded legacy recognize API.

Phase 2's dummy optimistic Start test now asserts emitted requests and uses the
authoritative UI setter. Failed/no-Region Start must not claim Running. Old tests
were not removed. The initial new fixture failed because offscreen QScreen has
an empty name while CaptureResult requires a nonempty name; the synthetic capture
fixture now explicitly labels its source instead of loosening production checks.

Build commands (PowerShell, from the independent Translator repository):

```powershell
$env:PATH='D:\QT\Tools\mingw1310_64\bin;D:\QT\6.11.2\mingw_64\bin;'+$env:PATH
& 'D:\QT\Tools\CMake_64\bin\cmake.exe' -S . -B build/phase6 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=D:/QT/6.11.2/mingw_64 -DCMAKE_CXX_COMPILER=D:/QT/Tools/mingw1310_64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=D:/QT/Tools/Ninja/ninja.exe
& 'D:\QT\Tools\CMake_64\bin\cmake.exe' --build build/phase6 --parallel 4
& 'D:\QT\Tools\CMake_64\bin\ctest.exe' --test-dir build/phase6 --output-on-failure
```

Configure succeeded; missing optional Vulkan headers is nonfatal. One intermediate
full build could not relink the manual probe because its old five-minute process
was still running (Windows executable file lock). The probe was allowed to finish
before rebuilding. No permissions/security setting was changed.

## Manual Probe Method

`TranslatorRealtimeProbe` is manual-only, not CTest or production. It isolates
QSettings in a temporary directory, loads an explicitly supplied local font,
renders dynamic images in memory, and uses the production coordinator/worker and
real Tesseract. No screenshots, video, user config changes or credential dumps.
None is the default. Optional --deepl uses the existing secure factory; it never
prints keys. Without a loadable local font it fails explicitly rather than
benchmarking missing-glyph squares. An initial fontless attempt was discarded.

```powershell
$env:TRANSLATOR_TEST_FONT='C:/Windows/Fonts/segoeui.ttf'
.\build\phase6\TranslatorRealtimeProbe.exe 300000
.\build\phase6\TranslatorRealtimeProbe.exe 12000 --deepl
.\build\phase6\TranslatorCredentialProbe.exe --status
.\build\phase6\TranslatorDeepLProbe.exe --stored-credentials
```

The production project hardcodes neither this font nor local Qt/toolchain paths.
Temporary probe logs are under ignored build/phase6 and are not committed.
Performance results and the completion-report index follow below.

## Measured Results (2026-10-03)

### Synthetic Real-Tesseract / None

Final-threshold probe ran 300000 ms of actual wall-clock time. At 5.2 seconds:
18 capture-function calls, 1 OCR, 17 unchanged frames. Hello Phase 6 was recognized
exactly (75 ms including cold engine initialization, 78 ms after capture).
Without changing the region, Realtime OCR Test 123 was recognized exactly
(14 ms OCR, 15 ms after capture). These are rendered inputs, not Notepad pixels.

Five consecutive synthetic changes (not Bilibili):

| Rendered input = OCR output | OCR ms | After-capture to accepted/UI signal ms | Translation |
| --- | --- | --- | --- |
| Realtime OCR Test 5 | 14 | 15 | None |
| Realtime OCR Test 6 | 13 | 15 | None |
| Realtime OCR Test 7 | 13 | 16 | None |
| Realtime OCR Test 8 | 13 | 15 | None |
| Realtime OCR Test 9 | 13 | 14 | None |

End: 983 capture-function calls, 195 OCR starts/accepted texts, 788 unchanged
frames, 0 duplicate texts, 0 translations, 0 pending. No superseded pending
frames in this workload because recognition finished within each tick; the
controlled slow-worker automated test proves the queue bound instead.
The initial 0.5% ratio run also lasted five minutes: 983 calls but only 65 OCR,
demonstrating missed small digits. It is not presented as successful detection.

Resource samples at 15.036s and 285.165s (last sample before exit): working set
40.1875 -> 40.2734 MiB; private bytes 21.9492 -> 21.7383 MiB. Occasional working
set peaks were about 42.2 MiB, returning to about 40.3 MiB. CPU accumulated
3.3125 seconds by 285.165s, approximately 1.16% of one logical core, not a Task
Manager all-core percentage. No sustained growth/full-core load was observed
for this small synthetic region. This is not a 4K capture/network stress result.

The 50ms event-loop heartbeat fired 4799 times. The process responded during
all resource samples, but this does not prove interactive desktop UI acceptance.
Stop at mid-run held capture/OCR/UI counts unchanged for 5200ms while synthetic
content continued changing. Restart used a new session and resumed automatic
updates without reselecting Region.

Frame comparator: a 640x200 synthetic comparison averaged about 92 microseconds
in a final direct unit run; maximum observed in the five-minute probe was 695
microseconds. The synthetic capture callback maximum was 5 microseconds: **not
QScreen performance**. Real QScreen/capture-to-display timings remain unmeasured.
The accepted latency starts after capture returns and includes any pending wait;
it excludes waiting for the next sampling tick. Signal dispatch is measured,
not actual compositor presentation time.

### Real DeepL With Synthetic Input

Credential status: DeepL stored=true/readError=false, compatible stored=false.
No key was printed, exported, modified or included in a screenshot. The saved
credential probe returned HTTP 200: Hello world -> 你好，世界 (1778ms) and
你好世界 -> Hello, World (499ms). These are real provider checks, not mocks.

The 12-second pipeline probe used final frame thresholds and existing secure
factory/backends. Real outputs:

| Input/OCR | After-capture to accepted ms | DeepL elapsed ms | Translation | Translation signal time |
| --- | --- | --- | --- | --- |
| Hello Phase 6 | 86 | 1045 | 你好，第6阶段 | 1125ms from probe start |
| Realtime OCR Test 123 | 17 | 430 | 实时OCR测试123 | 5852ms from probe start |
| Realtime OCR Test 7 | 16 | 430 | 实时OCR测试7 | 11962ms from probe start |

The middle input's OCR signal was at 5420ms and translation at 5852ms, a 432ms
gap; this is not a real video end-to-end latency. Totals: 23 capture callbacks,
4 OCR starts/accepted texts, 19 unchanged frames, 4 translation attempts. One
earlier request after restart was superseded by a newer text; only the current
response was displayed. Stop's 5.2-second check had unchanged capture, OCR and
update counts. The real desktop Region-to-DeepL path and video translation are
still pending, despite the successful synthetic/backend integration.

### Bilibili And Desktop Acceptance

Attempted public video URL: `https://www.bilibili.com/video/BV1LW411B7XA/`.
The browser connector returned Edge unavailable, and subsequent Windows browser
state access ended Computer Use for URL-policy enforcement. No verified playback
or subtitle region was reached. Bilibili consecutive subtitle samples: **0**.
Visible/OCR/Translation rows, video counts, video stale-regression observation,
video Stop/restart and video latency: **NOT TESTED**. No alternate platform was
used, and historical Phase 4.1 screenshots/results were not relabeled.

Required remaining manual checks: normal public playback, subtitle-only Region,
Start, five consecutive changes recorded without cherry-picking, Stop while
playback continues for at least five seconds, restart without Region, actual
Notepad static/edit checks, and a 5-10 minute screen/video run. Move both app
windows outside Region. No security/login bypass is permitted.

## Changed And Added Files

Modified: CMakeLists.txt, README.md, docs/architecture.md,
src/app/CaptureCoordinator.h/.cpp, src/app/OcrCoordinator.h/.cpp,
src/app/TranslationCoordinator.h/.cpp, src/config/SettingsManager.cpp,
src/gui/TranslationWindow.h/.cpp, src/main.cpp, tests/Phase2UiTest.cpp.

Added: src/app/RealtimePipelineCoordinator.h/.cpp,
src/processing/FrameComparator.h/.cpp, src/processing/TextDeduplicator.h/.cpp,
tests/Phase6RealtimeTest.cpp, tests/RealtimeProbe.cpp, this report.

No new dependencies, OCR models, provider/credential redesign, screen-exclusion
API, click-through, PaddleOCR, audio/ASR/TTS, Hook or Phase 6.1/7 work.

## Completion Report Index

1. Phase 6 completion: code/offline integration complete; full manual acceptance pending.
2. Initial Git status: clean main, Phase 5.1 HEAD confirmed.
3. Modified files: listed above.
4. Added files: listed above.
5. Coordinator design: explicit service dependencies, timer, one pending Frame and mode enum.
6. Start: saved region validation, idempotent, immediate tick.
7. Stop: timer stop, generation retirement, pending clear, translation retirement, no waits.
8. Session ID: monotonic on lifecycle/semantic changes, checked with mode/source/request ID.
9. Interval: 300ms.
10. Interval rationale: roughly 3.33 checks/sec, not video-frame-rate OCR.
11. Comparator algorithm: tolerant grayscale thumbnail MAD/changed ratio.
12. Thresholds: delta 18, ratio 0.0005, mean delta 2.0.
13. Threshold evidence: synthetic live missed-digit observation and small-character regression; video calibration pending.
14. Comparison size: proportional maximum 160x90.
15. Diff performance: mean about 92us; synthetic probe maximum 695us.
16. Single flight: one physical worker, newest pending only.
17. Maximum active OCR: one.
18. Pending policy: overwrite intermediate frames.
19. Maximum pending: one in production path.
20. Identity: OCR request ID + session + frame sequence + captured language.
21. Stale OCR: ignored before all UI/status/translation effects.
22. Normalization: whitespace trim/compression, normalized LF, retained line structure.
23. Dedup: exact normalized text; punctuation changes accepted.
24. Empty: two valid empties realtime; one-shot retains immediate clear behavior.
25. OCR error: retain subtitles, no translation, retry, not an empty.
26. Capture failure: transient skipped; fourth consecutive stops; missing region stops immediately.
27. Region while running: stop before selection, remain stopped afterward, preserve one-shot check.
28. Settings invalidation: rotate generation, discard pending, reset references; next tick uses new snapshot.
29. Translation stale protection: retained existing coordinator request ID ownership.
30. Stop invalidates translation: yes, minimal invalidate(false), tested delayed replies.
31. None realtime OCR: synthetic real-Tesseract probe passed; actual desktop input pending.
32. DeepL realtime: real backend + synthetic scheduler passed; actual Region/UI path pending.
33. Bilibili scene: public-video attempt blocked before verified playback.
34. Five video rows: unavailable, zero samples; synthetic rows are clearly separate above.
35. Old subtitles reverting: deterministic stale tests pass; not observed on real video because untested.
36. Task backlog: controlled slow-worker test bound proven; no synthetic-run accumulation.
37. Static capture count: 18 synthetic callbacks over 5.2s.
38. Static OCR count: one.
39. Video capture count: unmeasured; synthetic five-minute count 983.
40. Video OCR count: unmeasured; synthetic count 195.
41. Duplicate count: zero synthetic long-run; deterministic duplicate test verifies two requests for five OCR texts.
42. Translation count: None zero; real DeepL short probe four attempts.
43. Capture-to-OCR: synthetic cold 78ms, warm samples 14-19ms; after capture, not QScreen latency.
44. Translation latency: measured DeepL 1045/430/430ms for reported samples.
45. Total subtitle latency: backend signal timestamps recorded; real video/UI presentation unmeasured.
46. Five-minute memory: 40.19 -> 40.27 MiB working set; private bytes slightly decreased.
47. CPU: about 1.16% of one core for the synthetic run, no sustained saturation.
48. Responsiveness: event-loop heartbeat and process response checked; desktop interaction pending.
49. Stop five seconds: synthetic 5.2s counts unchanged, video Stop untested.
50. Restart: automatic synthetic restart passed; stale-session regression also passed.
51. Multi-screen validation: current saved screen and full containment, missing/negative-origin unit coverage; physical test pending.
52. DPI: Phase 3 logical-coordinate capture code unchanged; no manual DPR conversion.
53. Own-window overlap: known self-capture risk; keep overlay and Settings outside Region, no flicker workaround.
54. Added tests: lifecycle/timer/frames/bounds/stale/text/empty/errors/settings/one-shot coverage above.
55. CTest suite count: seven.
56. Passed count: seven; regression runs take about 5.5s on this machine.
57. Configure: success (optional Vulkan-header warning only).
58. Build: success after Windows running-probe file lock was released.
59. README: updated current workflow, limits and self-capture warning.
60. Architecture: added real-time flow and responsibility/lifecycle boundaries.
61. Report: D:/workspace/Project/Usst/Translator/docs/realtime-pipeline-phase6.md.
62. Git status: final verified state is reported in the task response after commit/push.
63. Commit: Implement Phase 6 real-time pipeline; hash in final task response/git log (avoids self-referential hash).
64. Push: origin main result verified and reported in final task response.
65. Reference repository: clean main...origin/main on verification, no edits made.
66. Full system flow: saved Region -> timer -> capture -> frame dedup -> OCR -> session validation -> text dedup -> translator -> subtitles.
67. Main bottleneck: scene-text Tesseract accuracy, moving-background extra OCR, and online provider latency.
68. Phase 6.1 recommendation: evaluate scene-text PaddleOCR after pending Phase 6 video acceptance, based on historical outlined/artistic/multiline failures; not a claim from new video data.
69. Phase 6.1: not started; no PaddleOCR/model downloads/audio/Phase 7 implementation.
