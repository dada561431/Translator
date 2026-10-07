# Realtime Audio Translation Pipeline - Phase 8C

## Phase 8C.1 Follow-Up

Current Core Acceptance: **PASS for the explicitly tested Realtek microphone and
system loopback** after owner-confirmed single-reading microphone A/B and actual
loopback/DeepL/warm-lifecycle tests. Default audio segmentation now uses independent
RMS endpointing, not fixed 4-second windows. See
[Phase 8C.1 architecture, live A/B and limitations](speech-endpointing-phase8c1.md).
Ordinary startup remains OCR-only; complete Audio UX/packaging and Phase 8D are not
started. The following Phase 8C record is historical: its original PARTIAL status,
fixed cuts, inaccurate microphone outputs and old test counts are intentionally
retained, not relabeled as successful evidence.

## Historical Phase 8C Acceptance and Scope

Baseline: `95ef8a0e864eda71aec8c24257c218ad83c8d09a`; the requested Phase 8A
acceptance commit is in history. Work is confined to Translator. No subagents,
worktrees, downloads, recordings, packaging or license changes were used.

**Phase 8C overall / Core Acceptance: PARTIAL.** Production-component integration,
automated routing/lifecycle tests and actual loopback -> Whisper -> DeepL -> visible
subtitle labels are verified. A separate real Realtek microphone run also completed
the full processing chain, but the owner explicitly reported: spoke English,
recognition was inaccurate. Microphone semantic acceptance is **NOT ACCEPTED /
PENDING**, not inferred from nonempty output, translations or earlier Phase 8A tests.
No full production E2E / polished audio-mode claim is made.

| Gate | Result |
| --- | --- |
| Independent audio coordinator and Final-only routing | PASS |
| Ordinary Release / Debug | PASS; 22/22 CTest each |
| Whisper-enabled Release / Debug | PASS; 23/23 CTest each |
| Real loopback capture, ASR, DeepL and subtitle delivery | PASS (integration); recognition quality has limitations |
| Real microphone capture, ASR, DeepL and subtitle delivery | Processing path verified; owner confirms real English speech but inaccurate ASR |
| Microphone semantic E2E acceptance | PENDING / NOT ACCEPTED |
| Complete Phase 8C Core Acceptance | PARTIAL, not PASS |
| Model warm after five restarts, clean exit | PASS |
| No-provider live transcription | PASS |
| Full mode UI / tray / shortcut audio UX | Deferred; normal app remains OCR-only |

## Architecture and Entry

```text
explicit QA source/model selection
  -> InputPipelineController (mode arbitration only)
  -> AudioTranslationCoordinator
       AudioInputCoordinator -> real Qt microphone / WASAPI loopback
       bounded PCM segments -> AsrCoordinator -> pinned whisper.cpp CPU
       Final -> originalTextReady -> existing TranslationWindow
             -> TranslationCoordinator::acceptText
             -> existing TranslatorFactory / DeepL / OpenAI Compatible
             -> identity-checked translatedTextReady -> same TranslationWindow
```

`TranslatorAudioPipeline` is an independently reusable library. It borrows audio,
ASR and translation coordinators, which must outlive it, on the same QObject owner
thread. No backend inference or HTTP logic is duplicated. Its destructor stops the
pipeline before the borrowed ASR worker is destroyed and joined.

The permitted minimal production-composition entry is the optional
`TranslatorAudioTranslationProbe`, built only with testing and the explicit existing
whisper checkout. It instantiates the **real production capture / ASR / translator /
subtitle components**, not a mock translation mode. Its source selection routes
through `InputPipelineController`. It has no OCR source and cannot run an OCR
scheduler in the same process. `Translator.exe`, main.cpp and the normal OCR
shortcut/tray semantics remain unchanged; normal startup never opens an audio
device or loads an ASR model. Audio is not added to ordinary Settings or packaging.

The controller has ScreenOcr, AudioMicrophone and AudioSystemLoopback modes. In a
combined runtime, callers must route all source starts through it: choosing a new
mode stops both old sources, Audio Start first stops OCR, and OCR Start first stops
Audio. The injected OCR callbacks are tested with controlled fake lifecycle state;
the dedicated audio Probe uses absent-OCR no-op actions. There is no new production
combined-mode UI or cross-process exclusion against another Translator executable.
This boundary must not be misreported as a manually validated combined-mode GUI.

Unmodified: `src/audio/*`, `src/asr/*`, OCR, Paddle/helper/protocol,
RealtimePipelineCoordinator, DeepLTranslator, OpenAiCompatibleTranslator,
TranslatorFactory, TranslationWindow, global shortcuts/tray, packaging and licenses.

## Translation Contract Extension

The only existing coordinator extension is `acceptText(text, source, target)` and
`requestStarted(request)`, emitted before invoking even a synchronous translator.
This permits identity registration before a same-language local or fake synchronous
result. `acceptOcr` keeps its original validity/provider/source checks and one
invalidation, then uses the shared private submission path. Latest-request handling,
factory recreation and result validation are preserved for OCR. Existing Phase 5,
5.1, 6 and 7 tests still pass. Providers themselves are unchanged.

ASR and translation languages are explicit configuration snapshots. `auto` remains
auto; detected language never rewrites SettingsManager. Credentials are obtained
only through the existing factory/store/env fallback. Probe arguments never accept
or print a key. Only nonsecret provider configuration is copied into temporary QA
settings; the user's persistent preferences are not modified by QA controls.

## Segmentation, Bounds and Threading

Default fixed, **non-overlapping 4000 ms** windows. No VAD, silence detector,
overlap, prompt/context chaining, denoise or hallucination filter. Timer expiry
requests a boundary over the PCM actually received: duration = chunk count * 20 ms,
not an assumption that a timer produces exactly 200 chunks. Empty capture windows
do not infer. Each 640-byte chunk carries the original sequence/timestamp and current
AudioInputCoordinator session; wrong sessions/sizes are rejected. Discontinuity
metadata reaches ASR unchanged and does not restart capture.

Start validates model readiness and configuration, creates a new pipeline session,
starts the requested audio device, and opens the first ASR utterance. Received PCM
is staged for the current window; at its boundary the complete batch is forwarded
and finalized. After that, beginUtterance is used only when the previous physical
operation has drained: calling it during an active Final would cancel that Final
under the existing Phase 8B contract. No ASR source changes were required.

Inference runs serially on the existing ASR worker while capture continues. There
is one current capture segment and **one pending complete segment**. A new boundary
replaces an older unsubmitted pending segment, records a drop, and gives lightweight
feedback. It never cancels the active Final merely because another boundary arrived.
Current and pending segments are each capped at 1500 chunks / 960,000 PCM bytes
(30 seconds). ASR adds its existing bounded snapshot buffer; upstream capture uses
its existing bounded mailboxes. No extra unbounded queued PCM signal delivery is
introduced. Owner-thread chunk work is cheap; no inference/network/wait occurs there.
The dispatch timer only retries bounded dispatch after worker cancellation drains.

Dropped windows are **not lossless transcription**. Fixed boundaries can cut words,
and no packets on idle loopback are not synthetic silence. These are documented
limitations, not silently solved by timing assumptions or filters.

## Identity, Ordering and Errors

- Pipeline session increments on Start and Stop. Audio capture session is separate.
- Monotonically increasing pipeline utterance IDs map to active ASR session/utterance.
- Partial results never update formal Original and never create translation requests.
  The baseline does not schedule Partial; Phase 8B still supports diagnostic Partial.
- A valid Final consumes its active identity once. Duplicate/old Final callbacks
  cannot create more requests. Identical text in different utterances is not deduped.
- A trimmed empty Final preserves both subtitle fields and submits no request.
- A nonempty Final updates Original immediately, before any translation completes.
  It invalidates any previous pending translation even if provider is None.
- requestStarted binds request ID to pipeline session and displayed utterance before
  backend invocation. Only that identity can update Translation. Old responses cannot
  overwrite a newer Original, even before the newer translation finishes.
- Stop invalidates session first, stops timers/capture, cooperatively cancels ASR,
  clears PCM/identities and invalidates translation without clearing the subtitles.
  Ordinary Stop never calls AsrCoordinator::stop/unloadModel.
- Translation failure is per-request feedback, never stops capture/ASR.
- ASR BackendFailure/Timeout releases the failed segment and retries the next segment
  once physical work drains. Missing/failed/reloaded/unloaded model stops the pipeline.
- Audio error/unexpected stop stops the pipeline, cancels ASR and invalidates responses.
  It does not generate endless empty utterances.
- Network requests have no new cancellation API. Existing timeouts remain; stale
  suppression prevents delivery and provider destruction cleans up on application exit.

## Actual Windows Evidence (2026-10-07)

Existing Phase 8B CPU backend: pinned whisper.cpp `48f628a`, multilingual
`.cache/models/ggml-base.bin`, 4 inference threads, ASR en, translation en -> zh,
4-second windows. No model/source upgrade or download. Model/sample provenance and
hashes remain documented in `asr-backend-phase8b.md`. Only JSON text/metadata is
saved locally under ignored `.cache/phase8c`; no microphone recording is saved.

### Independent Loopback

Real WASAPI default render endpoint: **headphones (Realtek(R) Audio)**, native
48000 Hz / stereo / float32 LE. The Probe plays the existing upstream 11-second
JFK speech sample through normal Qt system playback, repeatedly. No microphone
backend is constructed or active in these runs. Unified input is 16000 Hz / mono /
int16 LE / 20 ms / 640 bytes.

Continuous 32-second run: **7 Finals, 7 requests, 7 successful DeepL replies**,
one model load (135 ms), no segment drops, normal exit. Actual examples:

| Final | DeepL result |
| --- | --- |
| Not what your country can do for you. | 不是你的国家能为你做些什么。 |
| ask what you can do for your country. | 问问自己能为国家做些什么。 |
| my fellow Americans. Ask not. | 我的美国同胞们。不要问。 |

The first Final was `And so my fellow America asked`: a recognition error, not an
exact transcription claim. Later warm/restart tests also produced an incorrect
`They jammed it.` fragment. No recognition failures are concealed or filtered.

Six 6-second capture cycles / five Stop-wait-Start restarts: **6 Finals / 6 requests /
6 translated results**, model loaded once (75 ms), zero drops. Original and translated
QLabel text equality and actual label visibility were recorded as true after every
delivery in the same TranslationWindow. A final repeat after integration cleanup
also checks the production-composition entry (see local final-warm.jsonl).

Provider=None independent run: **3 Finals, 3 visible Original updates, 0 requests,
0 translations**, one model load (80 ms), continued capture and clean Stop.

### Independent Microphone

Initial default device was **Camo**, not the expected Realtek. Its 60-second run
produced 14 `you` Finals with translations. This is a **negative/unusable speech
test**, not microphone voice acceptance. No text suppression was added.

The subsequent explicit **microphone (Realtek(R) Audio)** run used 48000 Hz / stereo /
float32 LE and the same unified PCM format. At least 3 continuous windows were
processed without capture restart: **14 Finals, 14 requests, 14 successful DeepL
replies**, model loaded once (84 ms), no drops, clean Stop/exit. All 14 Original and
14 translated labels were visible and matched delivered text programmatically.
Examples: `Hello.` -> `你好。`, `Bye.` -> `再见。`, `All I need.` ->
`这就是我需要的全部。`. It also produced `[BLANK_AUDIO]`, `you`, `[inaudible]`
and dubious fragments; these are kept as nonempty Final text under the no-filter
requirement, not counted as correctly recognized voice.

**Owner's actual manual feedback: spoke English, but recognition was inaccurate.**
No exact spoken ground truth or satisfactory 3-segment semantic/UI confirmation was
provided. Thus microphone processing/real provider delivery is verified but semantic
acceptance remains PENDING / NOT ACCEPTED, and complete Phase 8C remains PARTIAL.
We do not substitute loopback evidence or fake routing tests for microphone accuracy.

### Latency

Actual received segments were roughly 3980-4060 ms, not always 4000 ms. Timings below
are milliseconds; collection duration is **separate** from post-boundary latency.

| Run | ASR median / mean | Translation median / mean | Boundary to translation delivery median / mean |
| --- | --- | --- | --- |
| Continuous loopback (7) | 897 / 894.57 | 393 / 514.14 | 1284 / 1409.14 |
| Realtek microphone (14, includes bad/silent output) | 903 / 916.50 | 380 / 417.64 | 1323 / 1335.71 |
| Warm loopback cycles (6) | 1058.5 / 1023.83 | 428 / 508.33 | 1470 / 1533.33 |

Original updates in the owner-thread Final callback; translation updates directly
after the identity guard. Post-boundary timing includes ASR scheduling/processing
and network delay, not the preceding 4-second capture interval or physical display
scan-out. The Probe checks visible QLabel text after each update, not GPU frame timing.
No microphone recognition accuracy statistic is inferred from these latency samples.

### Shutdown and Network

DeepL actually returned Chinese translations in both independent input paths; this
run is not offline/fake translation acceptance. No current network timeout was
observed in the completed delivery runs. The microphone deficiency is ASR quality,
not a DeepL confirmed regression. OpenAI Compatible was not manually retested.

`--exit-on-translation` closes the actual window 50 ms after starting the first real
DeepL request: 1 Final / 1 request / 0 delivered translations, stopped audio and normal
application exit. `--exit-on-inference` closes 100 ms after the first boundary: 0 Final /
0 requests, cooperative cancellation, joined worker and normal exit. The upstream
`failed to encode` warning during expected cancellation is not a delivered error or
an assertion of uncancellable failure. No Probe process remained after exit. ASR is
an in-process worker, not a new Python helper. Existing Paddle helper is untouched.

## Build and Automated Regression

No standard CTest opens a microphone, plays audio, loads a real model, contacts the
network or requires credentials. One new suite `phase8c_audio_translation` uses real
coordinators, injectable fake input/ASR/provider and the existing offscreen subtitle
window. It covers initial privacy/stopped state, model readiness, microphone/loopback
routing, duplicate Start/Stop, stale audio sessions, PCM metadata, timer boundaries,
active matching Partial callbacks (zero requests/Original updates), Final once only,
duplicate Final, empty Final, immediate Original/UI binding, successful Translation/UI
binding, old translation before/after newer reply, failure recovery, Stop/restart
stale ASR/network results, warm model reuse, bounded latest-pending replacement,
ASR timeout/backend-failure recovery, None, mode switching, device error, and
destructor cleanup during active inference. Previous suites are all retained.

Existing local configurations:

| Build directory | Configuration | Build / CTest |
| --- | --- | --- |
| build/phase61b | ordinary Release, no whisper checkout | PASS / 22 of 22 |
| build/phase61b-debug | ordinary Debug, no whisper checkout | PASS / 22 of 22 |
| build/phase8b-whisper | opt-in whisper Release | PASS / 23 of 23 |
| build/phase8b-whisper-debug | opt-in whisper Debug | PASS / 23 of 23 |

For each, configure, build and CTest were executed, retaining local compiler/Qt/Ninja
cache paths, with no machine paths inserted into CMakeLists.txt:

```powershell
$env:PATH = 'D:\QT\Tools\mingw1310_64\bin;D:\QT\6.11.2\mingw_64\bin;D:\QT\Tools\Ninja;' + $env:PATH
& D:\QT\Tools\CMake_64\bin\cmake.exe -S . -B build/phase61b
& D:\QT\Tools\CMake_64\bin\cmake.exe --build build/phase61b -j 4
& D:\QT\Tools\CMake_64\bin\ctest.exe --test-dir build/phase61b --output-on-failure
# Repeat with each existing directory above. Whisper directories retain their
# explicit TRANSLATOR_WHISPER_CPP_DIR; ordinary caches keep it empty.
```

Example explicit QA (never a normal automatic microphone listener):

```powershell
$env:QT_FORCE_STDERR_LOGGING = '1'
& ./build/phase8b-whisper/TranslatorAudioTranslationProbe.exe `
  --kind loopback --model .cache/models/ggml-base.bin `
  --language en --source en --target zh --provider deepl `
  --segment-seconds 4 --seconds 32 --play .cache/third_party/whisper.cpp/samples/jfk.wav
# microphone: --kind microphone, omit --play. Use --device <stable hex ID>
# from TranslatorAudioCaptureProbe --list when default selects a virtual source.
# --provider none verifies transcription without requests.
# --cycles 6 --seconds 6 verifies five warm restarts.
```

## File and Git Boundary

Added: AudioTranslationCoordinator.h/.cpp, InputPipelineController.h,
tests/AudioTranslationTest.cpp, tests/AudioTranslationProbe.cpp and this document.
Modified: CMakeLists.txt, TranslationCoordinator.h/.cpp, README.md. No other production
source files changed. No model, WAV/PCM, logs, binary, runtime, credential or cache file
is staged. Pre-existing untracked `licenses/`, `mic-test.txt`,
`scripts/prepare_qt_licenses.py` remain untouched/untracked. Use the implementation
commit `Implement Phase 8C realtime audio translation pipeline` and its task closeout
for the final hash/push result; no self-referential commit hash is embedded here.

## Remaining Limits / Phase 8D Recommendation (Not Started)

1. First obtain satisfactory microphone semantic acceptance with a pinned actual
   device and owner-verified spoken text. Current owner says recognition is inaccurate;
   exact accuracy/root cause is not established. Do not call the complete pipeline ready.
2. Fixed four-second boundaries lose word/context continuity and include silence;
   ASR can hallucinate `you` and special markers, and those can reach real translation.
   No short-text, token, confidence or silence filtering was added.
3. Local QA has mode/device/model command-line controls only. Formal audio source/device/
   model UI, combined tray/shortcut UX and ASR runtime packaging remain deferred.
4. Later, separately evaluate segment/window/model choices and VAD/preview policies,
   with matched speech samples and independent microphone/loopback semantic checks.
   Preserve Final identity/stale guards. No such optimization is implemented here.
5. Physical unplug/output switching/privacy denial, non-English speech accuracy,
   general audio device/model performance, and new full desktop OCR/DPI/multi-monitor
   manual acceptance are not asserted by this phase. Existing automated regressions pass.

No Phase 8D, Installer, Audio packaging, TTS or future phase is started.

## Requested Closeout Ledger

The requested 90 reporting items are indexed below. Git commit hash and final push
result are supplied in the task closeout, rather than embedded into their own commit.

| # | Item | Outcome |
| --- | --- | --- |
| 1 | Overall | PARTIAL |
| 2 | Core acceptance | Not full PASS; microphone semantic acceptance remains pending |
| 3 | Initial commit | 95ef8a0e864eda71aec8c24257c218ad83c8d09a |
| 4 | Modified files | CMakeLists.txt, README.md, TranslationCoordinator.h/.cpp |
| 5 | Added files | AudioTranslationCoordinator.h/.cpp, InputPipelineController.h, two tests/Probe files, this document |
| 6 | Coordinator | Independent borrowed dependencies and UI-facing signals |
| 7 | OCR separation | No OCR scheduler changes; ordinary app stays OCR-only |
| 8 | Mode | ScreenOcr / AudioMicrophone / AudioSystemLoopback |
| 9 | Exclusion | Controller stops previous source; tested with fake OCR, actual dedicated Probe has no OCR source |
| 10 | Segmentation | Fixed non-overlapping received-PCM windows |
| 11 | Default window | 4000 ms |
| 12 | Partial | Diagnostic only, baseline does not schedule Partial |
| 13 | Partial requests | 0; active matching Partial-only automated test |
| 14 | Final | Consume matching active identity once; immediate Original, then translation |
| 15 | Final requests | At most one per nonempty identity; 7/7 loopback and 14/14 Realtek in initial runs |
| 16 | Empty Final | No request, keep subtitles |
| 17 | ASR mapping | Active ASR session/utterance -> pipeline session/utterance |
| 18 | Utterance mapping | Monotonic pipeline ID per sealed segment |
| 19 | Request mapping | requestStarted before backend invocation binds request ID/session/utterance |
| 20 | Stale ASR | Old session/utterance and duplicate Final ignored |
| 21 | Stale translation | Stop/new session/current displayed identity guard |
| 22 | Ordering | Old result cannot overwrite newer Original, before or after new translation |
| 23 | Stop | Invalidate, timers stop, capture stop, ASR cancel, buffers clear, preserve subtitles |
| 24 | Restart | New session/capture; same model/context |
| 25 | Model loads | One per real Probe process |
| 26 | Warm | Six cycles / five restarts, load count one |
| 27 | Microphone backend | Qt QAudioSource; explicit Realtek; initial default Camo negative test retained |
| 28 | Microphone E2E | Processing chain verified; semantic acceptance pending after owner feedback |
| 29 | Microphone segments | 14 completed continuous Realtek Finals |
| 30 | Microphone examples | Hello.; Bye.; All I need.; also bad/silence markers |
| 31 | Microphone translation | 14 successful real DeepL replies; not proof of source accuracy |
| 32 | Microphone subtitles | 14 Original + 14 Translation visible QLabel matches; manual semantic confirmation absent |
| 33 | Loopback backend | Windows WASAPI real headphone render endpoint |
| 34 | Loopback E2E | Integration PASS; not exact ASR accuracy claim |
| 35 | Loopback segments | 7 initial continuous Finals; plus 6 warm-cycle Finals |
| 36 | Loopback examples | Not what your country can do for you.; ask what you can do for your country. |
| 37 | Loopback translation | Real DeepL 7/7 initial and 6/6 warm |
| 38 | Loopback subtitles | Existing window; warm-cycle visible field matches |
| 39 | Independent testing | Yes, never both capture backends active in the Probe |
| 40 | Provider | DeepL and None; fake only in CTest |
| 41 | Network | Real DeepL success; no completed-run timeout observed |
| 42 | None | Actual 3 Finals, 0 requests, live Original |
| 43 | Translation failure | Nonfatal, next Final can submit; fake regression PASS |
| 44 | ASR timeout | Release failed segment, wait physical drain, recover next window; PASS |
| 45 | Device failure | Stop/cancel/invalidate; fake device-unavailable PASS |
| 46 | Discontinuity | Original chunk metadata retained; actual-duration and discontinuity assertions PASS |
| 47 | Audio-ASR latency | Window duration separate; no claim about hardware ADC latency |
| 48 | ASR latency | Initial loopback median 897 ms; Realtek median 903 ms |
| 49 | Translation latency | Initial loopback median 393 ms; Realtek median 380 ms |
| 50 | Post-boundary latency | Initial loopback median 1284 ms; Realtek median 1323 ms |
| 51 | Start/Stop | Six real cycles / five restarts and automated warm reuse PASS |
| 52 | Clean exit | Real window Close during inference/network pending and normal Stop PASS |
| 53 | Orphans | No Probe process remained; joined in-process ASR worker; no new helper |
| 54 | Added tests | phase8c_audio_translation, independent non-CTest real Probe |
| 55 | Partial test | Active matching hel/hello Partial callbacks, zero request/Original |
| 56 | Ordering test | Older pending result before/after newer translated result |
| 57 | Stop stale test | Late Final/provider callbacks cannot change subtitle |
| 58 | Restart stale test | Previous session translation suppressed |
| 59 | Mode test | Both switch directions, fake OCR lifecycle |
| 60 | Normal Release | PASS |
| 61 | Normal Debug | PASS |
| 62 | Normal CTest | 22/22 each; all previous 21 retained |
| 63 | Whisper Release | PASS |
| 64 | Whisper Debug | PASS |
| 65 | Whisper CTest | 23/23 each; all previous 22 retained |
| 66 | Audio 8A source changed | No |
| 67 | ASR 8B core changed | No |
| 68 | OCR changed | No |
| 69 | Paddle/helper changed | No |
| 70 | Realtime OCR changed | No |
| 71 | DeepL changed | No |
| 72 | OpenAI backend changed | No |
| 73 | TranslationCoordinator changed | Minimal text ingress/requestStarted, shared private submission path |
| 74 | TranslationWindow changed | No; reused by tests/real Probe |
| 75 | VAD | Not implemented |
| 76 | Hallucination filter | Not implemented |
| 77 | Audio Settings UI | Not implemented |
| 78 | Model UI | Not implemented |
| 79 | Packaging | Unchanged |
| 80 | Licenses/release work | Unchanged; pre-existing licenses untracked untouched |
| 81 | README | Partial status, no polished/complete audio claim |
| 82 | Phase 8C document | This file |
| 83 | Git status | Intended tracked changes only; three pre-existing untracked items retained |
| 84 | Pre-existing untracked | licenses/, mic-test.txt, scripts/prepare_qt_licenses.py preserved |
| 85 | Implementation commit | See task closeout / git log -1 after commit |
| 86 | Push | Non-force origin/main; final transport result in task closeout |
| 87 | LunaTranslator | Read-only; git status clean at closeout check |
| 88 | Phase 8D | Not started |
| 89 | Limits | Microphone accuracy, fixed cuts, silent hallucinations, bounded dropping, no complete UX |
| 90 | Later recommendation | Obtain semantic acceptance first; then separately evaluate windows/model/VAD/UX/packaging |
