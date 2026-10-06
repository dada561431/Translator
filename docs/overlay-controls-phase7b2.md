# Overlay Controls - Phase 7B.2

## Status and Evidence Boundaries

Baseline: `e965f06232c3f4fe4c55d35d186d8d83e9a7d477`.

**Implementation and automated acceptance PASS. Full desktop manual acceptance pending.**
Release and Debug build successfully with Qt 6.11.2, MinGW 13.1, C++17 and Ninja.
Each runs **16/16 CTest suites PASS**, retaining the previous 15 suites.
This is not a claim that every requested native mouse/tray/foreground scenario passed.
No subsequent phase, installer, packaging or license work is started.

Evidence is separated below:

| Check | Result | Evidence |
| --- | --- | --- |
| Lock persistence, manipulation guards, toolbar operability | PASS | Injected move/resize boundary and widget events |
| Hotkey validation, normalization, Cancel, transactional Apply | PASS | Fake registry and real SettingsDialog |
| Native custom-key conflict / rollback / release / restart | PASS | Opt-in RegisterHotKey fixture on another thread |
| Defaults on this actual desktop | PARTIAL | Ctrl+Alt+T/R registered; Ctrl+Alt+S already occupied, error 1409 |
| Tray menu/state routing, unavailable tray, recovery | PASS | QAction model with injected availability; not physical notification-area clicks |
| Notification-area availability | AVAILABLE | Actual QSystemTrayIcon availability in desktop QA |
| Lock/Unlock visible and clickable | PASS | Computer Use observed toolbar and clicked Lock, which became Unlock |
| Native locked/unlocked dragging and edge resizing | PENDING | Drag input interrupted by user-input detection; not relabeled PASS |
| New hotkeys with a different application foreground | PENDING | No complete input-delivery acceptance this round |
| Actual notification-area menu / double-click | PENDING | Model/activation routing tests do not substitute for these physical inputs |
| Hidden Region cancellation / capture failure restoration | PASS | Actual selector lifecycle and coordinator under offscreen Qt |
| Hidden Region successful screen selection/crop | PENDING | Shared restoration path implemented; no new complete desktop crop acceptance |
| Real PP-OCRv6 Small, hidden/locked overlay, Stop/Start | PASS | Local private-crop replay through unchanged real helper and realtime pipeline |
| Latest hidden subtitle on Show | PASS | Widget tests plus real OCR replay; not a new webpage-capture claim |
| Real DeepL in this round | BLOCKED / NOT PASS | Two direct requests timed out, HTTP 0; combined replay produced no translation results |
| Offline translator/realtime regressions | PASS | Existing suites, including mocked provider responses |
| Capture exclusion / appearance / geometry regression | PASS (automated) | Existing 7A/7B.1 tests, plus real OCR appearance replay |
| Physical capture-exclusion desktop regression | PENDING this round | Earlier owner's 7B.1 PASS remains historical evidence |
| Exit/Close and helper cleanup | PASS (automated/probe) | Idempotent controller close/exit; real OCR probe cleanup, no observed helper residual |
| DPI 100% / 150% / multi-monitor | PENDING / NOT TESTED | No new physical environment validation |

Previous owner-reported 7A/7B.1 manual acceptance is preserved in its original documents.
It is not retroactively counted as Phase 7B.2 manual testing.

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

On this desktop Ctrl+Alt+S is occupied (Win32 1409). No owner process was stopped or
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

These paths are local invocation parameters only, not embedded project dependencies.

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

The default native-registration command is blocked by the existing Ctrl+Alt+S
conflict; the isolated variant passes. It does not override unknown registrations.
The DeepL probe issued en->zh and zh->en requests, each approximately 15 seconds,
both HTTP 0 / timed out. No production endpoint/proxy/timeout or credentials changed.

QA-only `TranslatorPhase7ControlsTests --desktop-probe <ignored-report.json>`
provides a controlled foreground fixture and real tray/native-shortcut window for
manual checks, auto-ending after 120 seconds. It uses temporary INI settings and
disables capture exclusion only for this QA window so screenshots can show it.
Its Start/Stop callbacks are UI fixtures, not production OCR results. The fixture's
menu-inspection button opens the same QAction menu but is not a taskbar click test.
Computer Use saw the genuine overlay toolbar, transparent subtitles and Lock->Unlock.
The attempted drag was interrupted by user-input detection. No physical drag/resize,
custom foreground hotkey or actual tray-menu PASS is inferred from that attempt.

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

## Manual Acceptance Still Required

1. In the production app, unlock then drag/resize; lock and retry, including toolbar
   and subtitle-edge gestures. Controls/Settings/Close must still work while locked.
2. Apply Ctrl+Shift+T/R/S, focus another application, exercise all three, and verify
   old keys no longer affect Translator. Restart and repeat with the saved keys.
3. Repeat conflict Apply with a controlled owner; old recovery remains usable and
   the inline error contains no secret or raw native code. Never steal unknown keys.
4. Use the actual notification-area icon for every action and double-click recovery.
   Test available/unavailable environments separately, not an injected test alone.
5. Start PP-OCRv6 Small on a public subtitle Region, hide, let subtitles change,
   show the latest text. Repeat with working DeepL and ClickThrough + lock.
6. While hidden, select/cancel Region and confirm it stays hidden. Choose a valid
   region and confirm crop contents/result and visibility; repeat while ClickThrough.
7. Exit via tray during real OCR; verify no Translator/helper process remains;
   restart and verify hotkeys can register. Repeat toolbar Close.
8. Recheck real capture-exclusion ON/OFF screenshots, mode switching, font/opacity/
   field controls, saved geometry, 100%/150% DPI and negative-origin multi-monitor.

Qt logical coordinates/point sizes remain in use; no new DPR multiplication or
custom screen transformation was added. No environment-specific checks are inferred
from offscreen tests. Known no-subtitle false positives and license release gates
are unchanged. Tray availability is evaluated at startup, not actively polled.

## Change Inventory and Final Report Ledger

Modified: CMakeLists.txt; README.md; src/main.cpp;
src/app/{CaptureCoordinator,GlobalShortcutManager,OverlayInteractionController}.{h,cpp};
src/config/SettingsManager.{h,cpp}; src/gui/{TranslationWindow,SettingsDialog}.{h,cpp};
tests/Phase2UiTest.cpp; tests/PaddleIntegrationProbe.cpp.

Added: src/config/GlobalHotkeyConfig.h; src/gui/OverlayControlAction.h;
src/gui/OverlayTrayController.{h,cpp}; tests/Phase7OverlayControlsTest.cpp;
docs/overlay-controls-phase7b2.md.

The requested final report items are summarized here, with evidence levels intact:

1. Implementation complete; full manual Phase 7B.2 acceptance still pending.
2. Baseline `e965f06232c3f4fe4c55d35d186d8d83e9a7d477`.
3. Modified files listed above.
4. New files listed above.
5. Lock owned by TranslationWindow; display-only dedicated setting/signal.
6. Default false.
7. QSettings persistence; draft Settings Apply, immediate toolbar/tray.
8. Drag guard PASS (injected events); physical test pending.
9. Resize guard PASS (injected events); physical test pending.
10. Locked toolbar operable; geometry operations only are blocked.
11. ClickThrough/Lock independent and restored without losing lock.
12. Three `hotkeys/*` keys listed above.
13. Ctrl+Alt+T/R/S defaults retained; actual S conflict recorded.
14. QKeySequence::PortableText.
15. Single chord, modifier, supported key; no empty/multi/unknown input.
16. Duplicate normalized chords rejected before registration.
17. Stage new/reuse old/swap/persist/release obsolete transaction.
18. Failure rolls back only new IDs and preserves old set/storage.
19. Stored toggle failure tries default; all failure forces Interactive.
20. Malformed stored values use runtime defaults, not storage overwrites.
21. Custom foreground physical-input test pending.
22. Old native ID dispatch and OS release verified; physical old-key test pending.
23. PortableText fresh-settings and native reinitialization tests PASS; full GUI restart pending.
24. Controlled native conflict fixture PASS; manual foreground recovery pending.
25. Actual tray availability true in desktop QA; unavailable injected case PASS.
26. Seven tray commands implemented.
27. Show/Hide routing PASS; actual taskbar click test pending.
28. Interaction action uses same controller; restoration tests PASS.
29. Lock command synchronized to window/Settings/toolbar.
30. Region uses the existing coordinator; hidden restoration tests PASS.
31. Start/Stop reads the actual realtime state; no copied running flag in tray.
32. Hidden Settings remains independent and interactive (widget test).
33. Exit routes normal close/quit and is idempotent.
34. Tray recovery without a registered toggle tested via activation/action model.
35. Hidden real OCR replay continues and preserves session.
36. Show displays the latest hidden text; real OCR plus widget tests.
37. Existing 7A regression suite retained and PASS; native S occupancy not hidden.
38. Existing Region suite retained; hidden cancel/failure restored.
39. Existing realtime suite PASS; scheduler code unchanged.
40. Real PP-OCRv6 Small replay PASS; MKL-DNN remains disabled.
41. Mocked translation suites PASS; current real DeepL timeout, not PASS.
42. Automated affinity/handle tests PASS; physical recheck pending this round.
43. Existing appearance suite and real OCR appearance replay PASS.
44. Existing geometry tests PASS; minimum width includes the new control.
45. Controller Close/Exit and real helper cleanup probes PASS; full native scenarios pending.
46. Tray hides on quit/destruction; owned hotkeys cleaned once.
47. Existing logical DPI handling retained; real 100%/150% pending.
48. Multi-monitor pending / not tested.
49. Release build PASS.
50. Debug build PASS.
51. 16/16 CTest suites PASS in each configuration.
52. New controls suite, native/manual opt-in modes and real OCR controls replay option.
53. Production OCR unchanged.
54. Helper/protocol/runtime/models unchanged.
55. Production translation backends unchanged.
56. Packaging unchanged; no ZIP/binaries committed or generated.
57. Pre-existing untracked licenses/ and scripts/prepare_qt_licenses.py untouched/unstaged.
58. README updated with precise implementation/manual status.
59. This document contains architecture, evidence, procedures and limitations.
60. Intended Git state: main synchronized; only the two pre-existing untracked license items.
61. Commit hash supplied in final response (cannot embed a commit's own hash here).
62. Actual push outcome supplied in final response.
63. LunaTranslator reference untouched; status checked separately before commit.
64. Phase 8 not started.
65. Remaining manual checks, native S conflict, current DeepL network timeout,
    DPI/multi-monitor and earlier OCR/release limitations explicitly preserved.
66. Future recommendations only: finish these environmental acceptance checks;
    select any further overlay/product work under a separate scoped request.

All stage/commit paths are explicit; no `git add .`. Private crops, QA screenshots,
reports, runtime binaries, model weights, virtualenvs, logs and credentials are not
part of this commit. No change was made to the original LunaTranslator repository.
