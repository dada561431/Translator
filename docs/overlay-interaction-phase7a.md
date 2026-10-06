# Overlay Interaction - Phase 7A

## Status and Scope

Baseline: `453782e662f354b791c2fda248db896ab3ed24f0`.
Implementation and automated regression are complete; **Phase 7A final Windows
manual acceptance is PENDING, not PASS**. The Computer Use service stopped before
the browser test because it could not determine the browser URL with enough
confidence to enforce its policy. No subsequent desktop input was attempted.
Notepad launch also did not expose a targetable window. Neither outcome is proof
of a Translator failure or successful mouse passthrough.

No changes to OCR, Paddle helper, RuntimeLocator, models, protocol, timeout,
translation backends, realtime lifecycle, RegionSelector or portable packaging.
The previous interrupted licensing task left untracked `licenses/` and
`scripts/prepare_qt_licenses.py`; these are preserved, excluded from this phase's
commit, and not continued. Public redistribution work remains deferred.

## Interaction State

`OverlayInteractionMode { Interactive, ClickThrough }` is owned by
`TranslationWindow`, exposed through `setInteractionMode()` / `interactionMode()`.
It is independent of `RealtimePipelineCoordinator::isRunning()`. Start/Stop never
change overlay mode. The window starts Interactive until application wiring has
registered the recovery hotkey and applied the saved preference safely.

ClickThrough uses Qt's `WindowTransparentForInput` and
`WindowDoesNotAcceptFocus`, not a Win32 style fallback. Translucency, frameless and
always-on-top flags remain unchanged. `WA_ShowWithoutActivating` avoids intentional
focus activation on show/restore. Windows input delivery and foreground retention
still require the manual checks below; no claim of verified cross-process delivery.

Qt flag changes can hide/recreate the native window. Mode switching snapshots
logical geometry, current screen, visibility and Settings visibility, reapplies
them, then shows only previously visible windows. No manual DPR conversion. Native
HWND recreation was not measured in this run. Offscreen geometry tests are not a
replacement for native Windows repeated-switch or multi-monitor testing.

## Global Shortcuts

`GlobalShortcutManager` is a QObject and QAbstractNativeEventFilter. Windows uses
thread-bound `RegisterHotKey(nullptr, id, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, key)`;
registration is independent of the overlay's HWND. No QShortcut, keyboard hook,
injection or low-level capture is used. Other platforms report unavailable.

| Default | Action |
|---|---|
| Ctrl+Alt+T | Interactive / ClickThrough |
| Ctrl+Alt+R | Existing Region request |
| Ctrl+Alt+S | Existing realtime Start / Stop |

Only successful owned IDs and matching WM_HOTKEY chords are consumed. Other native
messages and result values remain untouched. Registration reports each success or
Win32 error; failures also reach existing toolbar feedback. No message box.
The registration backend is injectable for tests without reserving CI hotkeys.
Reinitialization unregisters first; explicit aboutToQuit and destructor cleanup
are idempotent and release only successful registrations.

If T registration fails, startup forces Interactive **without changing** saved
`overlay/clickThrough=true`. Unregistered T cannot switch into an unrecoverable
mode. R/S failures leave their toolbar equivalents usable; failed registration
does not crash. A running application instance is not used to steal hotkeys from
another instance or unrelated software.

## Routing, Toolbar and Persistence

`OverlayInteractionController` routes actions through callbacks wired in main.cpp.
It reads the capture coordinator's existing selecting state and realtime's existing
running state; it does not duplicate either lifecycle. Region calls the same
`regionSelectionRequested` path as the toolbar, retaining existing hide, selection,
capture-delay and restore behavior. During selection and the capture delay, shortcut
actions are ignored, preventing mode changes/restart/reentrant selection. Entry
interaction mode therefore survives completion, cancellation or capture failure.

ClickThrough stops hover hiding timers, hides the toolbar and suppresses hover,
drag and resize handlers. Subtitle updates remain enabled. Interactive restores
the toolbar briefly (1 second), then resumes the existing enter/leave behavior.
A separate mouse-transparent label gives one-second mode feedback without replacing
OCR/translation text. Settings remains a normal independent QDialog; only the
overlay receives input-transparent flags. No new Settings editor is introduced.

User toggles persist `overlay/clickThrough` via QSettings; default false. It does
not emit translationSettingsChanged or restart/cancel OCR and translation work.
Existing geometry persistence and toolbar Close remain unchanged. ClickThrough
users must switch back with T before clicking Close; no global Quit shortcut.

## Automated Checks

New `phase7a_overlay_interaction` suite uses an offscreen Qt platform and fake
registration backend. It covers default/preference persistence, saved-mode recovery
failure without preference deletion, owned/foreign native messages, re-registration,
cleanup, action routing, selection guards, real-coordinator cancellation/visibility,
Settings isolation, hover/timers, independent subtitle updates, and **20 mode
switches with stable logical geometry, visibility, contents and flags**.
Existing 13 suites remain; total **14**. Release build **PASS**, Release CTest
**14/14 PASS** (12.55 seconds); Debug build **PASS**, Debug CTest **14/14 PASS**
(17.14 seconds). Automated routing is not a live Paddle/DeepL passthrough test.

Commands used (Qt/MinGW bin directories were prepended to this shell's PATH,
not embedded in CMake):

```powershell
cmake --build build/phase61b -j 4
ctest --test-dir build/phase61b --output-on-failure
cmake -S . -B build/phase61b-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=D:/QT/6.11.2/mingw_64 -DCMAKE_CXX_COMPILER=D:/QT/Tools/mingw1310_64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=D:/QT/Tools/Ninja/ninja.exe -DPython3_EXECUTABLE=D:/workspace/Project/Usst/Translator/benchmarks/ocr_phase61a/.venv/Scripts/python.exe
cmake --build build/phase61b-debug -j 4
ctest --test-dir build/phase61b-debug --output-on-failure
```

The first build invocation lacked the MinGW bin PATH and failed while producing
new moc compiler definitions. After restoring the local build PATH, compilation
exposed two test-only API mistakes (private selector cancel and findChild on a
dialog without Q_OBJECT); tests now use Escape event dispatch and QDialog lookup.
Both final builds/tests above passed after these repairs. Optional Vulkan headers
were not found; no Vulkan dependency is introduced or required by this change.

## Windows Manual Acceptance - Pending

Do not mark this phase fully accepted until these are observed on the real desktop:

1. Start Translator (first-run Interactive), put it over a Notepad/browser work area;
   verify toolbar, drag, edge resize, Settings and Close.
2. Focus the lower program. Press T; verify toolbar hides and the lower program
   remains focused. Click directly through visible subtitle pixels and confirm
   cursor movement or a real lower-window button response. T again restores controls.
3. With the lower program focused, use R in both modes. Complete a region and repeat
   with Escape; verify prior mode, visibility, position and size are restored.
4. Select PP-OCRv6 Small and a real changing-text region that does not intersect the
   overlay. Press S to start; toggle T, observe continuing Original updates and
   translation updates with an owner-configured provider. S stops; S restarts.
5. Switch T at least 20 times, verify native geometry/screen/contents/topmost and no
   focus stealing. Restart to verify persisted mode. Close and verify no helper
   residual process; another run must register all three shortcuts successfully.
6. Deliberately occupy T in a separate controlled registration fixture, start with
   saved clickThrough=true, confirm safe Interactive fallback; release the fixture.
7. Test 100% and 150% Windows scaling and a secondary monitor if available.

Actual T/R/S system registration, foreground hotkeys, lower-program mouse receipt,
native drag/resize, Region success, real OCR/translation while passthrough, native
20-switch geometry, DPI and secondary-monitor checks are **not yet verified**.
No GUI screenshots or credentials are committed. No security settings changed.

## Limitations and Phase 7B Candidates

Mouse passthrough does not exclude the overlay from screen capture: overlap with
the selected region can still be captured. Capture exclusion is not implemented.
No-subtitle false positives remain Known Limitation / Deferred; no new filtering.
Phase 7B candidates only: capture exclusion assessment, opacity/fonts/background
styles, drag lock, tray and editable shortcuts. None is implemented here.
No Installer, audio/ASR, packaging regeneration or public ZIP.

## Requested Acceptance Ledger

| # | Item | Result |
|---|---|---|
| 1 | Fully complete | No: native Windows manual acceptance pending |
| 2 | Initial commit | 453782e662f354b791c2fda248db896ab3ed24f0 |
| 3 | Modified files | CMakeLists.txt, README.md, src/main.cpp, src/app/CaptureCoordinator.h, src/config/SettingsManager.h/.cpp, src/gui/TranslationWindow.h/.cpp |
| 4 | New files | src/app/GlobalShortcutManager.h/.cpp, src/app/OverlayInteractionController.h/.cpp, tests/Phase7OverlayTest.cpp, this document |
| 5 | Mode | Interactive / ClickThrough enum; separate from pipeline state |
| 6 | Passthrough | Qt window flags; Win32 only for global shortcut registration |
| 7 | HWND recreation | Possible on Qt flag changes; not measured natively |
| 8 | Geometry | Snapshot/restore logical rectangle, screen and visibility |
| 9 | Topmost | Flag retained in automated tests; native pending |
| 10 | T registration | Fake backend PASS; actual Windows registration pending |
| 11 | R registration | Fake backend PASS; actual Windows registration pending |
| 12 | S registration | Fake backend PASS; actual Windows registration pending |
| 13 | Registration failure | Diagnostic, no crash, unregistered IDs ignored |
| 14 | Startup safety | Failed T forces Interactive without deleting true preference |
| 15 | Foreign foreground hotkeys | PENDING: Computer Use stopped before test |
| 16 | Lower-program mouse receipt | PENDING: not tested, not declared PASS |
| 17 | Toolbar | Offscreen hide/hover/timer checks PASS |
| 18 | Drag | Existing Interactive system-move path retained; native pending |
| 19 | Resize | Existing Interactive system-resize path retained; native pending |
| 20 | Settings | Offscreen visibility/nontransparent flag checks PASS; native pending |
| 21 | Region success | Existing path and mode guards retained; native success pending |
| 22 | Region cancel | Real coordinator + dispatched Escape offscreen PASS; native pending |
| 23 | Live pipeline + passthrough | PENDING, not replaced by fake routing tests |
| 24 | OCR updates | UI updates in ClickThrough PASS; live Paddle while passthrough pending |
| 25 | Translation updates | UI updates in ClickThrough PASS; live provider while passthrough pending |
| 26 | Stop/Start | Routing checks and existing realtime/helper suites PASS; live pending |
| 27 | 20 toggles | Offscreen geometry/content/visibility/flag checks PASS; native pending |
| 28 | DPI | 100%/150% Windows native checks pending; no DPR arithmetic added |
| 29 | Multiple monitors | Not tested; existing screen preserved in switching implementation |
| 30 | Release | Build PASS |
| 31 | Debug | Build PASS, test targets enabled |
| 32 | CTest | Release 14/14, Debug 14/14; original 13 retained |
| 33 | Production OCR | Zero source changes |
| 34 | Paddle helper | Zero changes |
| 35 | Phase 6.1C packaging | Zero changes in this phase; prior untracked licensing work excluded |
| 36 | False positives | Known Limitation / Deferred unchanged |
| 37 | Phase 7A document | This report, implementation and explicit pending manual checklist |
| 38 | README | Shortcuts, safe startup and manual-acceptance caveat added |
| 39 | Git scope | Only Phase 7A code/tests/docs staged; prior two untracked license paths preserved |
| 40 | Commit | Implementation commit hash reported in final response, not a manual-acceptance claim |
| 41 | Push | Result reported after ordinary origin/main push, never force push |
| 42 | LunaTranslator | git status empty; readonly reference untouched |
| 43 | Phase 7B | Not started |
| 44 | Limits | Native acceptance pending; overlay may enter capture; false positives deferred |
| 45 | Next candidates | Complete 7A manual gate first; later assess capture exclusion and UX customization |

## API References

- [Qt 6.11 window flags](https://doc.qt.io/qt-6.11/qt.html#WindowType-enum)
- [RegisterHotKey](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey)
- [UnregisterHotKey](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-unregisterhotkey)
