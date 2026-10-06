# Overlay Controls - Phase 7B.2

## Status and Evidence Boundaries

Baseline: `e965f06232c3f4fe4c55d35d186d8d83e9a7d477`.

Implementation commit: `5461574ac6e196a96895f868c63e50697452db95`.

**Phase 7B.2 Core Acceptance: PASS.** The owner subsequently completed real Windows
desktop manual testing of the core controls, as recorded below. Remaining checks:
**DeepL regression, DPI 100%/150%, and multi-monitor pending / not tested**.
Existing Release and Debug builds PASS with Qt 6.11.2, MinGW 13.1, C++17 and Ninja.
Each previously ran **16/16 CTest suites PASS**, retaining the previous 15 suites.
This closeout changes documentation only, reuses those results without rerunning
builds/tests, and does not present owner-reported checks as new agent-run tests.
No subsequent phase, installer, packaging or license work is started.

Evidence is separated below:

| Check | Result | Evidence |
| --- | --- | --- |
| Lock persistence, manipulation guards, toolbar operability | PASS | Injected move/resize boundary and widget events |
| Hotkey validation, normalization, Cancel, transactional Apply | PASS | Fake registry and real SettingsDialog |
| Native custom-key conflict / rollback / release / restart | PASS | Opt-in RegisterHotKey fixture on another thread |
| Defaults in earlier agent-run desktop test | PARTIAL (historical) | Ctrl+Alt+T/R registered; Ctrl+Alt+S was occupied, error 1409; not a new core acceptance blocker |
| Tray menu/state routing, unavailable tray, recovery | PASS | QAction model with injected availability; not physical notification-area clicks |
| Notification-area availability | PASS | Owner confirmed actual system tray display; earlier desktop QA also reported availability |
| Lock/Unlock visible and clickable | PASS | Computer Use observed toolbar and clicked Lock, which became Unlock |
| Native locked/unlocked dragging and resizing | PASS (owner-reported) | Lock blocks drag/resize; Unlock restores both; locked toolbar remains usable |
| ClickThrough and Drag Lock independence | PASS (owner-reported) | Interaction mode and position lock remain independent |
| New hotkeys with a different application foreground | PASS (owner-reported) | Custom global shortcuts remain effective with another application foreground |
| Custom shortcut persistence | PASS (owner-reported) | Shortcut settings persist |
| Actual notification-area commands and Interactive recovery | PASS (owner-reported) | All tray commands work; ClickThrough can be restored solely through tray; specific double-click routing retains automated evidence only |
| Hidden Region cancellation / capture failure restoration | PASS | Actual selector lifecycle and coordinator under offscreen Qt |
| Tray Region command | PASS (owner-reported) | Owner confirmed Tray Region works; no separate new crop-pixel comparison is claimed |
| Real PP-OCRv6 Small, hidden/locked overlay, Stop/Start | PASS | Local private-crop replay through unchanged real helper and realtime pipeline |
| Hidden-overlay realtime processing and latest subtitle on Show | PASS (owner-reported and automated) | Pipeline continues while hidden; Show displays latest subtitles; earlier widget tests and OCR replay also pass |
| Real DeepL regression in this round | PENDING / NOT PASS | Two requests timed out, HTTP 0; no confirmed Phase 7B.2 regression; backend unchanged and historical DeepL success recorded |
| Offline translator/realtime regressions | PASS | Existing suites, including mocked provider responses |
| Capture exclusion / appearance / geometry regression | PASS (automated) | Existing 7A/7B.1 tests, plus real OCR appearance replay |
| Physical capture-exclusion / appearance desktop regression | PASS (owner-reported) | Owner found no Phase 7B.1 capture-exclusion or appearance regression |
| Exit/Close and helper cleanup | PASS (owner-reported and automated/probe) | Tray Exit works; no observed Translator/helper residual after exit; earlier controller and real helper probes pass |
| DPI 100% / 150% / multi-monitor | PENDING / NOT TESTED | No new physical environment validation |

Previous owner-reported 7A/7B.1 manual acceptance is preserved in its original documents.
It is not retroactively counted as Phase 7B.2 manual testing. The new Phase 7B.2
owner report independently supplies the manual PASS evidence above.

## Ownership and Action Routing

`OverlayControlAction` is a small UI-command enum. Toolbar commands, registered
global shortcuts and tray QAction callbacks all route to
`OverlayInteractionController::routeControl()`. The controller has existing
Region/Start/Stop callbacks and reads actual `isSelecting()` / `isRunning()` state.
It does not own another realtime scheduler or duplicate business state.

Application wiring in `main.cpp` calls `CaptureCoordinator::beginSelection()`
directly from that callback. The production coordinator disables its legacy
window-signal connection, avoiding recursive Region signal routing. The default
legacy connection remains for standalone widgets/probes. Start/Stop callbacks
invoke the existing realtime coordinator. Settings uses the existing dialog.

`TranslationWindow` owns mode, drag lock, UI, and native Qt geometry operations.
Standalone use without an installed router retains the original Region/Start/Stop
signals. Its router is cleared when the controller is destroyed.
Close emits the normal stop/closed signals and saves geometry; the controller's
idempotent Exit route closes the window and invokes `QApplication::quit()` in main.
`quitOnLastWindowClosed(false)` permits hidden-overlay Settings to close without
exiting the app. Toolbar Close and tray Exit remain actual exit actions.

### Drag Lock

`overlay/dragLocked` defaults false and is persisted by SettingsManager with a
dedicated signal, not `translationSettingsChanged`. Settings edits are drafts until
Apply/OK; toolbar and tray commands update the preference immediately.

`setDragLocked()` / `dragLocked()` are independent of input mode. The checkable
toolbar button reads Lock/Unlock, uses a stable width, and remains functional while
locked. Minimum overlay width includes all toolbar controls (at least 420 logical
pixels); the old exact-width assertion was updated, not removed.

`geometryInteractionAllowed()` is false for either Lock or ClickThrough. The guard
runs after hover feedback but before cursor/move/resize paths. Lock clears a stale
resize cursor. A small injectable GeometryBackend is the boundary to
`QWindow::startSystemMove()` / `startSystemResize()`; tests count both operations
without relying on platform movement. ClickThrough switches never alter lock.
This is a user-gesture lock, not a ban on programmatic layout/screen restoration.

### Global Hotkeys

| Setting | Default | Action |
| --- | --- | --- |
| `hotkeys/toggleInteraction` | `Ctrl+Alt+T` | Interactive / ClickThrough |
| `hotkeys/region` | `Ctrl+Alt+R` | Region |
| `hotkeys/startStop` | `Ctrl+Alt+S` | Realtime Start / Stop |

QKeySequenceEdit provides editing. `GlobalHotkeyConfig.h` validates exactly one
nonempty chord, at least one Ctrl/Alt/Shift/Meta (Win) modifier, and a supported
letter/digit/function/navigation key. Bare keys, modifier-only input, unknown keys,
multi-chord sequences and duplicates after normalization are rejected inline.
PortableText is the storage format; Qt NativeText is not stored. No lock hotkey is added.
Reserved/unavailable OS combinations may pass syntax validation but fail registration.

Replacement is a synchronous transaction on the GUI thread:

1. Validate the entire proposed set before any OS operation.
2. Reuse registrations with matching chords, including action permutations.
3. Register genuinely new chords under distinct bounded application IDs while
   all old registrations remain owned. Production adds MOD_NOREPEAT.
4. If any registration fails, unregister only successful new staged IDs, keep
   the old action mapping/preferences, and display a human-readable inline error.
   Numeric Win32 diagnostics remain in logs.
5. On success swap the action mapping, persist canonical shortcuts, then unregister
   only obsolete old IDs. The native filter remains installed throughout replacement.

The injectable modifier-aware backend retains compatibility with the earlier
two-function default-chord backend. Startup/default IDs remain compatible with
existing tests; replacement IDs are obtained through `registeredId()` rather than
assuming static T/R/S IDs. WM_HOTKEY validates the current owned ID, actual key,
actual modifiers, and thread message ownership. Dispatch uses the current action map.
Cleanup unregisters only owned successful registrations and is idempotent.

Startup reads stored settings without repairing storage. Empty/malformed/duplicate/
multi-chord sets use runtime defaults and a diagnostic. A conflicting stored toggle
tries default Ctrl+Alt+T; failure there forces Interactive regardless of saved mode,
without deleting the saved preference. Other unavailable actions are reported.
An unchanged Settings Apply does not retry unavailable shortcuts or materialize
default keys, so an unrelated appearance Apply remains usable after a startup conflict.
Applying shortcuts during selection is refused. Cancel neither saves nor registers.

In the earlier agent-run desktop test Ctrl+Alt+S was occupied (Win32 1409). No owner process was stopped or
shortcut stolen. The controlled native fixture used Ctrl+Alt+F9/F10/F11 as its
initial independent set, then changed to Ctrl+Shift+T/R/S. A second thread deliberately
occupied Ctrl+Shift+R. Apply failed and retained the baseline; another thread could
register staged Ctrl+Shift+T after rollback. Releasing the fixture allowed Apply to
succeed; another thread could then register the obsolete baseline recovery key.
Reinitialization and a fresh manager successfully reacquired the saved custom set.
These API checks are not foreground physical key-press acceptance.

### System Tray and Visibility

`OverlayTrayController` owns QSystemTrayIcon/QMenu/QActions, with a Qt stock icon
fallback when the application has no icon. No additional dependency/download is used.
Menu commands: Show/Hide Overlay; Enable ClickThrough/Switch to Interactive;
Lock/Unlock Position; Region; Start/Stop; Settings; Exit. Labels read actual window,
mode, lock and coordinator state. Window state signals, realtime runningChanged,
selectionStarted/Finished, and menu opening trigger refresh; no polling timer is added.

If the notification area is unavailable at startup, the icon is not shown; a
diagnostic is logged. Existing UI/shortcuts remain usable and Hide is refused so no
last recovery surface is lost. Startup still requires the recovery shortcut to
restore a saved ClickThrough preference. When the tray is available, its Interactive
command and double-click recovery work independently of the toggle hotkey.
Double-click deliberately performs Show + Interactive and saves Interactive mode.

Hide only hides TranslationWindow. It does not call Stop, destroy the OCR engine,
invalidate translations or discard subtitle updates. Show restores the same state,
geometry, fonts and capture affinity. Settings opens its own interactive QDialog
without showing a hidden overlay. Selection temporarily disables state-mutating
commands except Exit; the prior mode/lock remain intact. CaptureCoordinator now
snapshots visibility and restores it on selection success, Escape and failed capture,
including when selection originated from a hidden overlay.

aboutToQuit hides the tray and unregisters global shortcuts. Stack destruction
then follows existing worker/helper cleanup. No change to OCR/helper/translation
production lifecycle or timeout is made.

## Verification Commands

These are the earlier implementation verification commands, not rerun for this
documentation-only closeout. Paths are local invocation parameters only, not
embedded project dependencies.

```powershell
$env:PATH = 'D:/QT/Tools/mingw1310_64/bin;D:/QT/6.11.2/mingw_64/bin;' + $env:PATH
& D:/QT/Tools/CMake_64/bin/cmake.exe --build build/phase61b -j 4
& D:/QT/Tools/CMake_64/bin/ctest.exe --test-dir build/phase61b --output-on-failure
& D:/QT/Tools/CMake_64/bin/cmake.exe --build build/phase61b-debug -j 4
& D:/QT/Tools/CMake_64/bin/ctest.exe --test-dir build/phase61b-debug --output-on-failure
& ./build/phase61b/TranslatorPhase7ControlsTests.exe --native-registration
& ./build/phase61b/TranslatorPhase7ControlsTests.exe --native-registration-isolated
& ./build/phase61b/TranslatorDeepLProbe.exe --stored-credentials
```

The earlier default native-registration command was blocked by the Ctrl+Alt+S
conflict; the isolated variant passed. It did not override unknown registrations.
The DeepL probe issued en->zh and zh->en requests, each approximately 15 seconds,
both HTTP 0 / timed out. No production endpoint/proxy/timeout or credentials changed.
DeepL regression remains pending, not a confirmed Phase 7B.2 regression. This phase
did not change the translation backend, and historical successful DeepL tests exist.

QA-only `TranslatorPhase7ControlsTests --desktop-probe <ignored-report.json>`
provides a controlled foreground fixture and real tray/native-shortcut window for
manual checks, auto-ending after 120 seconds. It uses temporary INI settings and
disables capture exclusion only for this QA window so screenshots can show it.
Its Start/Stop callbacks are UI fixtures, not production OCR results. The fixture's
menu-inspection button opens the same QAction menu but is not a taskbar click test.
Computer Use saw the genuine overlay toolbar, transparent subtitles and Lock->Unlock.
The attempted drag was interrupted by user-input detection. No physical drag/resize,
custom foreground hotkey or actual tray-menu PASS is inferred from that attempt.
The subsequent owner's manual PASS is separate evidence, not a reclassification
of that interrupted agent-run attempt.

QA-only `TranslatorPaddleProbe --replay <ignored-report.json> <local-crop...>
--overlay-regression --controls-regression --restart` uses the real helper and
unchanged realtime pipeline with deterministic local image input. It checks hidden
OCR samples, session preservation, latest subtitle, warm restart and the tray Exit
action route. Models, screenshots and reports stay in ignored local directories.
It is not a normal webpage/Region capture or physically offline Sandbox acceptance.
Final replay used four valid OCR requests with helper PID 2360 throughout: cold
3595 ms, subsequent 178/210/214 ms. Three OCR samples arrived while hidden;
controls/appearance retained the same active session. Stop/Start reused the helper
with first post-restart OCR 214 ms. Show's original text matched the last result.
The tray Exit QAction stopped the pipeline and closed the overlay, and PID 2360
was absent after the probe returned. Exit was routed programmatically, not clicked
through the native notification area. Detailed private JSON/PNG remains only at
`.cache/phase7b2/paddle-controls-exit.json[.png]`, excluded from Git.
Adding `--deepl` attempts the real configured backend; this round it did not complete
translations and is not marked PASS. Previous live DeepL PASS is historical only.

## Owner-Reported Windows Manual Acceptance

The owner tested the production application on a real Windows desktop and reported
no issues with the following core behaviors. These are manual observations, not
new automated test runs or claims about untested environments.

- Drag Lock works; Locked prevents overlay dragging and resizing.
- Toolbar controls remain operable while Locked; Unlock restores drag and resize.
- ClickThrough and Drag Lock states remain independent.
- Custom global shortcuts work with another application foreground and persist.
- System Tray displays normally.
- Tray Show/Hide Overlay works.
- Tray Interactive/ClickThrough works, including recovery solely via Tray.
- Tray Lock/Unlock works.
- Tray Region works.
- Tray Start/Stop works.
- Tray Settings works.
- Tray Exit works.
- Hiding the overlay leaves the realtime pipeline running.
- Show Overlay displays the latest subtitles.
- No Translator/helper residual was observed after exit.
- Phase 7B.1 capture exclusion and appearance show no observed regression.

### Remaining Checks

- **DeepL regression: PENDING / NOT PASS.** Two network timeouts this round do not
  establish a confirmed Phase 7B.2 regression. The translation backend was not
  changed, and historical successful DeepL evidence remains valid for its own round.
- **DPI 100% / 150%: PENDING / NOT TESTED.** No physical validation supplied.
- **Multi-monitor: PENDING / NOT TESTED.** No physical validation supplied.

Qt logical coordinates/point sizes remain in use; no new DPR multiplication or
custom screen transformation was added. No environment-specific checks are inferred
from offscreen tests. Known no-subtitle false positives and license release gates
are unchanged. Tray availability is evaluated at startup, not actively polled.

## Change Inventory and Final Report Ledger

The following source inventory describes implementation commit `5461574`, not this
manual acceptance closeout. This follow-up changes only README.md and this document.

Modified: CMakeLists.txt; README.md; src/main.cpp;
src/app/{CaptureCoordinator,GlobalShortcutManager,OverlayInteractionController}.{h,cpp};
src/config/SettingsManager.{h,cpp}; src/gui/{TranslationWindow,SettingsDialog}.{h,cpp};
tests/Phase2UiTest.cpp; tests/PaddleIntegrationProbe.cpp.

Added: src/config/GlobalHotkeyConfig.h; src/gui/OverlayControlAction.h;
src/gui/OverlayTrayController.{h,cpp}; tests/Phase7OverlayControlsTest.cpp;
docs/overlay-controls-phase7b2.md.

The requested final report items are summarized here, with evidence levels intact:

1. Phase 7B.2 Core Acceptance PASS, including owner-reported Windows manual tests.
2. Baseline `e965f06232c3f4fe4c55d35d186d8d83e9a7d477`.
3. Modified files listed above.
4. New files listed above.
5. Lock owned by TranslationWindow; display-only dedicated setting/signal.
6. Default false.
7. QSettings persistence; draft Settings Apply, immediate toolbar/tray.
8. Drag guard PASS in tests and owner-reported physical lock/unlock testing.
9. Resize guard PASS in tests and owner-reported physical lock/unlock testing.
10. Locked toolbar operable, confirmed by owner; geometry operations only are blocked.
11. ClickThrough/Lock independent, confirmed by owner.
12. Three `hotkeys/*` keys listed above.
13. Ctrl+Alt+T/R/S defaults retained; actual S conflict recorded.
14. QKeySequence::PortableText.
15. Single chord, modifier, supported key; no empty/multi/unknown input.
16. Duplicate normalized chords rejected before registration.
17. Stage new/reuse old/swap/persist/release obsolete transaction.
18. Failure rolls back only new IDs and preserves old set/storage.
19. Stored toggle failure tries default; all failure forces Interactive.
20. Malformed stored values use runtime defaults, not storage overwrites.
21. Custom foreground global shortcuts PASS in owner-reported physical testing.
22. Old native ID dispatch and OS release verified by existing tests; no separate owner old-key test claimed.
23. Shortcut persistence PASS in owner report; fresh-settings/native reinitialization tests PASS.
24. Controlled native conflict fixture PASS; no separate new manual conflict experiment claimed.
25. Actual tray display PASS in owner report; unavailable injected case PASS.
26. Seven tray commands implemented.
27. Tray Show/Hide PASS in owner report and routing tests.
28. Tray Interactive/ClickThrough PASS in owner report and restoration tests.
29. Tray Lock/Unlock PASS in owner report; synchronized to window/Settings/toolbar.
30. Tray Region PASS in owner report; existing hidden restoration tests PASS.
31. Tray Start/Stop PASS in owner report; reads actual realtime state.
32. Tray Settings PASS in owner report; hidden Settings widget tests PASS.
33. Tray Exit PASS in owner report; normal close/quit route is idempotent.
34. Tray-only recovery from ClickThrough PASS in owner report; specific double-click has model evidence only.
35. Hidden realtime continues in owner report; earlier real OCR replay preserves session.
36. Show displays latest hidden subtitles, confirmed by owner and earlier replay/widget tests.
37. Existing 7A regression suite retained and PASS; native S occupancy not hidden.
38. Existing Region suite retained; hidden cancel/failure restored.
39. Existing realtime suite PASS; scheduler code unchanged.
40. Real PP-OCRv6 Small replay PASS; MKL-DNN remains disabled.
41. Mocked translation suites PASS; real DeepL regression pending after timeouts, not confirmed regression.
42. Capture exclusion regression PASS in owner report; affinity/handle tests PASS.
43. Appearance regression PASS in owner report and existing suite/real OCR replay.
44. Existing geometry tests PASS; minimum width includes the new control.
45. Exit cleanup PASS in owner report, with no observed Translator/helper residual; earlier probes PASS.
46. Tray hides on quit/destruction; owned hotkeys cleaned once.
47. Existing logical DPI handling retained; real 100%/150% pending.
48. Multi-monitor pending / not tested.
49. Existing Release build PASS; not rerun for this documentation-only closeout.
50. Existing Debug build PASS; not rerun for this documentation-only closeout.
51. Existing 16/16 CTest suites PASS in each configuration; not rerun here.
52. New controls suite, native/manual opt-in modes and real OCR controls replay option.
53. Production OCR unchanged.
54. Helper/protocol/runtime/models unchanged.
55. Production translation backends unchanged.
56. Packaging unchanged; no ZIP/binaries committed or generated.
57. Pre-existing untracked licenses/ and scripts/prepare_qt_licenses.py untouched/unstaged.
58. README updated with Core Acceptance PASS and the remaining three checks.
59. This document contains architecture, evidence, procedures and limitations.
60. Intended Git state: main synchronized; only the two pre-existing untracked license items.
61. Commit hash supplied in final response (cannot embed a commit's own hash here).
62. Actual push outcome supplied in final response.
63. LunaTranslator reference untouched; status checked separately before commit.
64. Phase 8 not started.
65. DeepL regression, DPI/multi-monitor remain pending; historical native S conflict,
    DeepL timeouts and earlier OCR/release limitations remain accurately recorded.
66. Future recommendations only: finish these environmental acceptance checks;
    select any further overlay/product work under a separate scoped request.

All stage/commit paths are explicit; no `git add .`. Private crops, QA screenshots,
reports, runtime binaries, model weights, virtualenvs, logs and credentials are not
part of this commit. No change was made to the original LunaTranslator repository.
