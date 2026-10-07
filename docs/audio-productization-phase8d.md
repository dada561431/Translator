# Audio Productization - Phase 8D

Date: 2026-10-07. Initial commit: `87ebee3454a5bd5779a98533df1722f1ea6003a1`.
Single agent; work confined to Translator; LunaTranslator is read-only.

**Phase 8D: PARTIAL. Phase 8 Overall Core Acceptance: PARTIAL.** Implementation
and automated integration checks pass. Production microphone/loopback/DeepL
subtitle manual acceptance is pending. Prior Phase 8C.1 Probe PASS is not used
as production Translator.exe acceptance. No new phase or public release begins.

## Architecture

```text
SettingsManager input/mode (single persisted selection, default screen)
  <- Toolbar / Tray / Settings Apply
  -> ProductionInputController
       -> existing InputPipelineController (mutual exclusion, always stop on switch)
            Screen -> existing RealtimePipelineCoordinator
            Audio  -> async AsrCoordinator model load
                   -> existing AudioTranslationCoordinator
                   -> existing microphone / WASAPI AudioInputCoordinator
                   -> unchanged SpeechEndpointDetector
                   -> unchanged WhisperCppAsrBackend (single worker, warm model)
                   -> Final -> Original -> existing TranslationCoordinator
                   -> DeepL / OpenAI-compatible / None -> existing TranslationWindow
```

The small production controller owns lifecycle decisions, not capture, ASR or
translation algorithms. It borrows longer-lived coordinators. Stop and mode
switch invalidate old pipeline requests, cancel a pending model start and prevent
late load completion from starting capture. Destruction stops pipelines before
ASR worker destruction joins. Close routes through the existing overlay controller.
Region completion is accepted only while Screen remains selected.

Toolbar has a fixed-width mode combo and stable disabled Region in audio modes;
tooltip: `Region is only available in Screen mode.` Its padding is tightened to
keep the existing 760-pixel window usable, without a new audio window or panel.
Tray mode actions share the toolbar's SettingsManager-backed selection. Existing
overlay controller callbacks now target the unified production controller.
Ctrl+Alt+S toggles the selected input; Ctrl+Alt+R is a no-op in audio; Ctrl+Alt+T
still only changes overlay interaction. Drag Lock, ClickThrough and Show/Hide
remain independent from pipeline Running. Partial results are neither displayed
nor translated. Silence retains the last subtitles. Translation failures do not
stop capture; these contracts remain covered by the existing audio pipeline tests.

## Settings and Privacy

| Key | Default / representation |
| --- | --- |
| `input/mode` | `screen`; allowed `microphone`, `system-audio` |
| `audio/microphoneId` | Empty = default input; otherwise stable QByteArray ID |
| `audio/outputId` | Empty = default output; otherwise distinct render endpoint ID |
| `asr/modelPath` | Empty; path only, never model bytes |
| `asr/language` | `auto`; choices `en`, `zh`, `ja`, `ko` |

The device enumerator is injected by main.cpp; opening Settings refreshes both
lists without starting either backend. Missing saved IDs appear as `Configured
device unavailable` and are preserved. Start fails clearly instead of silently
rewriting IDs or selecting another device. Default and explicit IDs are separate
choices. Microphone and output IDs are never interchanged by the controller.

Audio/mode fields are draft-only until Apply/OK. Cancel reloads saved audio values.
ASR language is independent of translation source language. Apply does not load
a model. During Running, changed audio configuration produces `Restart audio to
apply changes.` and remains a snapshot until Stop/Start. Changing mode always
stops, including while loading; it never automatically starts the new mode.

Startup restores only selected mode, always Stopped: no model load, device open,
microphone permission request or restored Running state. Audio Start is explicit.
Existing capture permission/error logic is reused, without retry loops.

Speech Detection exposes only Automatic. The accepted baseline is unchanged:
pre-roll 300 ms; start confirmation 100 ms; minimum speech 250 ms; trailing silence
600 ms; maximum utterance 12 s; minimum RMS 0.002; noise ratio 3. No DSP parameters
or Whisper translate mode are exposed. Loading uses the existing worker with
`Loading speech model...`; Ready starts capture; errors return to stopped controls.
Ordinary Stop retains the model; a different model path reloads on next Start.

## Runtime and Build

Explicit build switch remains the existing `TRANSLATOR_WHISPER_CPP_DIR` option.
It must refer to prepared local whisper.cpp commit
`48f628a84833905ee4a0658ee6d4a5c915ce1997`; CMake now verifies the pin. No fetch or
model download is added. CPU inference, no context, four threads and endpoint
defaults are unchanged. Whisper and ggml are statically linked into Translator;
Qt stays dynamically linked. No Whisper DLL, Python ASR or source checkout is
needed at runtime. Plain builds preserve Screen and display a clear unavailable
message on Audio Start; no fake production backend is used.

Model lookup: readable regular user-configured path, then application-local
`models/ggml-base.bin`. A missing old absolute path therefore falls back locally
without rewriting preferences. No disk scan. Missing model produces `ASR model
not found. Open Settings.` or `Speech model is not installed/configured. Open
Settings.` The existing Settings button is available; no network download occurs.

The explicitly local script `scripts/prepare_audio_runtime.py` extends the existing
Phase 6.1C portable package, rather than creating a second packaging framework.
It checks the base manifest, pinned Whisper build metadata and verified model
hash/size; requires a new nonoverlapping output directory; copies runtime inputs;
uses windeployqt; updates the manifest; and performs the existing PE audit.
It never deletes inputs, cleans destinations or overwrites an existing directory.
Failure leaves a draft staging directory, not a declared release. Safety tests
cover existing output, input overlap and non-Whisper build rejection.

Actual command:

```powershell
benchmarks/ocr_phase61a/.venv/Scripts/python.exe scripts/prepare_audio_runtime.py `
  --base-package dist/TranslatorPortable --build-dir build/phase8b-whisper `
  --model .cache/models/ggml-base.bin --output-dir dist/TranslatorAudioPortable `
  --qt-root D:/QT/6.11.2/mingw_64 --mingw-root D:/QT/Tools/mingw1310_64
```

Paths are local command inputs, not hardcoded project/deployment defaults.
The staging folder and its audit report are ignored by Git.

```text
TranslatorAudioPortable/
  Translator.exe
  Qt6Core / Gui / Widgets / Network / Multimedia / Svg DLLs
  libgcc_s_seh-1.dll / libstdc++-6.dll / libwinpthread-1.dll
  platforms/  multimedia/  imageformats/  tls/  other deployed plugins
  avcodec-61.dll  avformat-61.dll  avutil-59.dll
  swresample-5.dll  swscale-8.dll
  models/ggml-base.bin
  ocr/runtime/  ocr/helper/  ocr/models/  (existing Paddle runtime)
  licenses/  qt.conf  runtime-manifest.json
```

Model: multilingual ggml-base, **147951465 bytes / approximately 141.10 MiB**.
SHA256: `60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe`.
The candidate manifest records the initial source commit with source_dirty=true:
it was built from this task's uncommitted implementation, not a public clean-tag
release. Final implementation commit is the commit containing this document.
The copied executable hash matched the actual Whisper Release build.

## Current Evidence

| Gate | Result and boundary |
| --- | --- |
| Normal Release / Debug configure and build | PASS, Qt 6.11.2 / MinGW 13.1 / Ninja |
| Normal Release / Debug CTest | 24/24 PASS each |
| Whisper Release / Debug configure and build | PASS, pinned dependency |
| Whisper Release / Debug CTest | 25/25 PASS each |
| New production control test | PASS; fake capture/ASR, real Settings/Toolbar/Tray widgets; not real voice |
| Base portable hash inventory and new manifest | PASS; local script completed |
| New candidate static PE dependency audit | PASS: zero missing candidates; not proof of all dynamic plugins or clean Windows |
| Portable startup without development PATH | PASS: PATH restricted to Windows directories, CWD TEMP, inherited Qt plugin paths cleared |
| Actual loaded Qt / MinGW / qwindows paths | All inspected modules came from the candidate folder, not build/.cache/Qt installation |
| App-local model exists/hash matches | PASS; resolver precedence/fallback tested; actual production ASR load still pending |
| Package Paddle runtime self-check | 20/20 PASS, helper PID 22320, first 9795 ms, warm median 233 ms, mean 232.05 ms, >300 ms 0, translations 0 |
| Package Tesseract | Not bundled, explicit unavailable result; development fallback remains tested; unchanged Phase 6.1C boundary |
| Production UI tree | Readable Screen mode, Start enabled / Stop disabled and both placeholders; no startup audio QA events |
| Computer Use production input | BLOCKED: failed window activation after fresh-window retry; no simulated manual PASS |
| Ctrl+Alt+S native registration | Conflict, Win32 1409; no override/system change; foreground audio shortcut pending |
| Production microphone / loopback / DeepL subtitles | PENDING: owner assistance requested; previous Probe evidence not substituted |
| Audio candidate clean Windows / physically offline audio | PENDING; old OCR-only Sandbox acceptance is separate |
| Full technical portable audio acceptance | PARTIAL; startup/deployment PASS, audio model/inference production QA pending |
| Public redistributable release | PENDING LICENSE REVIEW / OWNER REVIEW REQUIRED; no ZIP |

Final build commands, with the local Qt/MinGW/Ninja PATH:

```powershell
# Run each independently for phase61b, phase61b-debug,
# phase8b-whisper and phase8b-whisper-debug (existing prepared caches).
D:/QT/Tools/CMake_64/bin/cmake.exe -S . -B build/<tree>
D:/QT/Tools/CMake_64/bin/cmake.exe --build build/<tree> -j4
D:/QT/Tools/CMake_64/bin/ctest.exe --test-dir build/<tree> --output-on-failure
```

The new test covers restored microphone never listening, draft Cancel/Apply,
independent ASR language, missing saved device retention, async Start, model warm
reuse, changed-model reload, Stop during loading, mode exclusion, Region no-op,
tray routing, hidden pipeline continuation and unavailable-Whisper feedback.
Existing suites retain ASR cancellation/stale protection, None/translation errors,
audio error cleanup, endpointing, OCR/helper lifecycle and overlay regressions.
Two initial test failures were resolved: duplicate executable/library Qt MOC
definitions and toolbar minimum width causing offscreen geometry clamping.
No old acceptance assertion was removed. Test artifacts remain under .cache/build.

Opt-in `TRANSLATOR_AUDIO_DIAGNOSTICS=1` logs existing latency/request signals only:
session/utterance IDs, stages and durations, not audio, subtitle text or secrets.
It is off by default. Screen idle before loading: roughly 49.9 MiB working set
and 12.4 MiB private memory for this candidate; accumulated CPU 0.25 s at inspection
is **not** a sampled CPU percentage. Listening/loaded/ASR CPU and memory, production
latency medians and comparison with 8C.1 remain unmeasured pending real UI QA.

## Remaining Manual Acceptance

Use the candidate from a guest/local directory, not the Sandbox read-only mapped
folder. Do not install Qt, Python, Paddle, MinGW, Tesseract or VS in the Sandbox.
No source checkout or .cache is needed by the candidate. For offline transcription,
disable Sandbox networking and set Provider=None; DeepL requires a separate online
run, not a claim of offline translation.

1. Open Settings; leave model path empty to exercise app-local lookup, choose English,
   actual Realtek microphone and existing configured DeepL; Apply.
2. Select Microphone, Start; say Hello, Good morning and three independent sentences.
   Confirm exact/semantic Original and matching Chinese subtitles after each.
3. Keep quiet 10 seconds; require zero Final/translation requests and no subtitle jump.
4. Stop/Start; confirm one model load. Test None and translation error recovery.
5. Select System Audio; play at least three natural speech utterances; verify subtitles,
   translations and no microphone backend. Test quiet output separately.
6. Screen -> Microphone -> System Audio -> Screen: each switch stops, no automatic
   listening or stale subtitle. Region is enabled only in Screen.
7. Test default/explicit devices, Settings Cancel, model missing/changed/restored,
   restart persistence without restored Running, tray recovery and foreground hotkeys.
8. Test ClickThrough and Hide while audio runs, Show latest subtitles, Close during
   capture/ASR/network; verify process/worker/helper cleanup.
9. Run package Region -> OCR -> translation -> Stop; keep Paddle and Tesseract
   packaging boundaries separate. Repeat audio model/inference in a clean environment.

Physical microphone unplug, actual default-output switching, permission denial,
DPI 100%/150% and multi-monitor remain PENDING / NOT TESTED. No privacy or security
settings are changed for QA. Energy detection cannot distinguish music/noise from
speech; other device gains and non-English ASR accuracy need separate acceptance.

## License Boundary

No change to root `licenses/` or `scripts/prepare_qt_licenses.py`. Existing verified
official Microsoft VC runtime provenance is retained from the base package; no
System32 DLL is copied. This is a local technical candidate, not public clearance.
Required additions for eventual review: whisper.cpp/ggml MIT notices, model source
and identity, Qt Multimedia LGPLv3/source obligations and FFmpeg attribution/source.
This document is not legal advice and claims no commercial Qt license.

Primary references:
- [Pinned whisper.cpp MIT license](https://github.com/ggml-org/whisper.cpp/blob/48f628a84833905ee4a0658ee6d4a5c915ce1997/LICENSE).
- [Converted Whisper models and MIT declaration](https://huggingface.co/ggerganov/whisper.cpp).
- [Qt open-source licensing](https://doc.qt.io/qt-6/licensing.html).
- [Qt Multimedia FFmpeg 7.1.3 attribution](https://doc.qt.io/qt-6/qtmultimedia-attribution-ffmpeg.html).

Public model/runtime redistribution remains an owner review item, not reopened here.

## Final Report Ledger

The following item numbers correspond to the requested 122-point report; pending
means no new production acceptance evidence, not a confirmed core regression.

| Items | Outcome |
| --- | --- |
| 1-2 | Phase 8D PARTIAL; Phase 8 Overall Core Acceptance PARTIAL |
| 3-4 | Initial 87ebee3; final commit containing this document, hash reported in task closeout |
| 5-6 | Modified CMake, main, SettingsManager, SettingsDialog, TranslationWindow, OverlayInteractionController, OverlayTrayController, README; added AudioSettings, ProductionInputController, AudioProductizationTest, prepare_audio_runtime/test_audio_runtime and this document |
| 7-12 | Shared persisted screen/microphone/system-audio; compact toolbar; Screen unchanged; Audio Region disabled |
| 13-17 | Unified controller Start/Stop and existing shortcut router; audio Region no-op; interaction unchanged; tray modes added; native foreground audio controls pending |
| 18-20 | Mode persisted, default Screen for old users, selected mode restored with Stopped only |
| 21-26 | Separate default/stable microphone and output IDs; absent IDs preserved and flagged; actual new production selected IDs pending |
| 27-33 | Browse/local model, configured -> app-local lookup, explicit missing errors, five ASR languages, Automatic only, endpoint baseline unchanged, no startup listening |
| 34-38 | Apply/OK save audio drafts; Cancel discards; Running uses snapshot until restart; async Loading feedback |
| 39-40 | One load across warm Stop/Start in control test; real production warm acceptance pending |
| 41-47 | Production microphone, actual device, five utterances including short words, silence, DeepL and visible subtitles PENDING; owner was asked, old Probe not reused |
| 48-53 | Production loopback/output/three utterances/quiet/translation/subtitles PENDING |
| 54 | Automated OCR regressions and package self-check PASS; production Region manual smoke PENDING |
| 55-56 | Mutual exclusion and switch invalidation PASS in control/core tests; real four-mode switch and visual stale check PENDING |
| 57-60 | None/errors/stale cleanup existing automated tests PASS; new production DeepL and physical device-error QA PENDING |
| 61-63 | Unplug, real output switch, permission denial PENDING / NOT TESTED |
| 64-67 | Native Ctrl+Alt+S conflict 1409; foreground shortcuts, real tray/ClickThrough/hidden live audio PENDING; widget routing/hidden continuation tested |
| 68-71 | Existing cancellation/destruction tests PASS; new production active capture/ASR/network Close PENDING; idle candidate closure checked separately in task closeout |
| 72-78 | Production latency and sampled CPU/loaded memory PENDING; baseline 8C.1 Final median 1415 ms / translation 1741.5 ms retained only as reference; idle memory above |
| 79-86 | Pinned 48f628a, optional explicit source build, Whisper/ggml static, multilingual ggml-base, 147951465 bytes/hash above; no model Git commit or download |
| 87-92 | Resolver precedence unit PASS and local file/hash PASS; production model load pending; staging/layout/script above; no analyzed runtime source/.cache references; clean audio inference proof pending |
| 93-95 | Qt dynamic including Multimedia; FFmpeg/windows multimedia plugins and five FFmpeg DLLs deployed; three MinGW DLLs verified actual loaded package paths |
| 96-98 | Technical portable audio PARTIAL; public redistribution/license pending owner review; no public artifact |
| 99-104 | Four configure/build PASS; ordinary Release/Debug each 24/24; Whisper Release/Debug each 25/25 |
| 105-108 | All existing 8A/8B/8C/8C.1 automated suites retained and PASS |
| 109-114 | No OCR/capture/ASR/translation/endpoint implementation changes; production public UI and application wiring changed |
| 115-116 | README candidate status/ordinary usage updated; this report added |
| 117-120 | Pre-existing untracked preserved; commit/push/status reported in closeout; original LunaTranslator untouched |
| 121-122 | Limitations above; next work is completing this production acceptance checklist, not adding Phase 9 features |

No subagents, new worktrees, cloud ASR, recordings, model download, neural VAD,
text filters, denoising, installer, Phase 9 or unrelated core refactor were added.
