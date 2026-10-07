# Production Streaming Original Subtitle - Phase 8D.2

Date: 2026-10-07. Initial commit: `38a973bf1efa6a702c5396576b2dcee5a3985ccb`.
Single agent, no worktree; LunaTranslator is read-only.

**Phase 8D.2 acceptance: PASS. Phase 8D overall: PARTIAL.** The production live
subtitle gates are satisfied by the real Translator.exe QA, automated regression,
and the owner's final two exit confirmations below. This is not a Phase 8 Overall
PASS or a public runtime release. Phase 8D.1A's English microphone
accuracy limitation remains; this phase implements live preview without claiming
instant or perfect recognition. Its historical Probe results are not substituted
for production Translator.exe evidence.

## Architecture

```text
Explicit Audio Start -> ProductionInputController
  -> async Whisper load -> async pinned Zipformer load (or preview fallback)
  -> one existing microphone / WASAPI capture -> 20 ms immutable PcmChunk
      +-> StreamingAsrCoordinator -> bounded queue -> independent worker
      |   -> SherpaOnnxStreamingBackend -> revision-bearing Partial mailbox
      |   -> AudioTranslationCoordinator identity / SpeechStarted gate
      |   -> TranslationWindow::setLiveOriginalText -> existing Original QLabel
      +-> unchanged SpeechEndpointDetector -> same existing Whisper worker
          -> Whisper Final -> correct Original when not older than displayed speech
          -> existing TranslationCoordinator -> DeepL / OpenAI-compatible / None
          -> identity-guarded audio translatedTextReady -> Translation QLabel
```

There is only one capture device/path. Neither ASR owns a second microphone.
Unified PCM remains 16000 Hz, mono, int16 LE, 320 samples / 640 bytes per chunk;
the existing AsrAudioBuffer float conversion feeds sherpa. Whisper continues to
receive the original chunk bytes at the unchanged energy-defined boundary.
SpeechEndpointDetector, capture backends, Whisper backend, OCR and translation
providers are not modified. Only app coordination and one subtitle setter change.

`IStreamingAsrBackend` is a synchronous **worker-only** contract. The pinned
adapter and two-revision TextStabilizer were extracted from the prior Probe into
shared code; the Probe keeps its original public aliases and model options. The
production executable does not link the Probe worker, dialog or CLI.

## Model and Runtime Identity

Production model: bilingual streaming Zipformer 2023-02-20; CPU, two threads,
greedy search. Exactly the Phase 8D.1A candidate, not a new model or decoder.
Sherpa **1.13.8**, commit `11afbd009a7f8c08f4bcf2fc1b265d0df4670fbf` (runtime
verifies version and commit prefix); app-local ORT **1.28.2**, API 28. Upstream
ORT build metadata is reported separately; it does not change the runtime pin.

| File | SHA256 |
| --- | --- |
| `encoder-epoch-99-avg-1.int8.onnx` | `8fa764187a261844f859d7143ebaa563af5d10adfece4c18a8f414c88cba2a9b` |
| `decoder-epoch-99-avg-1.onnx` | `2e3b5ec371f8899ee6acd829fd753ba45772df57a91bdf37cde3136354e7db7d` |
| `joiner-epoch-99-avg-1.int8.onnx` | `1ed689c5ed19dbaa725d9d191bb4822b5f4855a39e1ffd28cbc1f340d25b2ee0` |
| `tokens.txt` | `a8e0e4ec53810e433789b54a5c0134a7eaa2ffca595a6334d54c00da858841d3` |

Total: 199056205 bytes / approximately 189.83 MiB. Official FP32 decoder is
intentional; encoder and joiner are int8. File hashes are verified on the loading
worker. No model scanning or automatic download occurs.

Lookup is explicit `TRANSLATOR_STREAMING_MODEL_DIR` (local QA/configuration) then
application-local `models/streaming/zipformer`. Whisper still uses its existing
Settings model path then `models/ggml-base.bin`. Optional CMake switches:
`TRANSLATOR_WHISPER_CPP_DIR`, `TRANSLATOR_SHERPA_ONNX_DIR`, and local staging
`TRANSLATOR_SHERPA_RUNTIME_DIR`. No developer path is embedded in project defaults.

Sherpa and ORT are explicitly loaded with QLibrary from the application directory
on the worker, not imported at process startup. A missing DLL/symbol, wrong
version or model mismatch disables preview with feedback, not the Whisper Final
route. Plain/no-sherpa builds retain their previous behavior. Local DLL copy is
technical staging only; public packaging, license closeout and clean-machine
streaming runtime acceptance are separate remaining Phase 8D work.

## Queue, Ordering and Display

Input is a mutex-protected **50-chunk / 1000 ms / 32000-byte** FIFO, not an
unbounded Qt queued PCM signal. At overflow/invalid input, preview explicitly
degrades, queued PCM is discarded with a dropped-chunk count, stream state is
reset, and Whisper continues. No dropped middle audio is passed off as continuous
recognition. Boundary/Stop resets intentionally retire old queued preview work.
A latest-result mailbox is drained on the owner thread every 50 ms (at most
20 Hz); changed text only updates Original, duplicates do not call its setter.

Every result has pipeline session, production utterance ID and monotonically
increasing revision. A separate worker epoch retires PCM/results at reset/Stop.
SpeechStarted permits display; Idle PCM may be decoded but its text is not shown.
SpeechEnded freezes preview and resets the sherpa stream. Sherpa's internal
endpoint never triggers production Final, translation or a replacement boundary.
The existing energy policy remains 300 ms pre-roll / 100 ms start / 250 ms minimum
speech / 600 ms trailing silence / 12 s safety cap / RMS 0.002 baseline.

Stabilization is diagnostic agreement between successive hypotheses, **not a
permanent text commit**. Original displays the latest raw hypothesis, including
shortening/corrections. Stable-prefix conflicts remain visible, not hidden by
concatenating a stale prefix. Translation stays above Original; layout, transparent
background, font sizes and toolbar behavior are unchanged.

Finalized/closed utterances reject late Partial. Displayed-utterance ordering
prevents U1 Whisper Final from replacing already visible U2 live Original. U1
Final may still translate internally; Translation shows the latest accepted Final
translation, so the previous translated sentence may remain while U2 is speaking.
Request/session/Final-order guards prevent a late U1 response from replacing U2's
accepted Final translation. main.cpp's former unconditional audio translation UI
connection is replaced by the existing guarded audio-coordinator signal; Screen
continues its own result path.

Partial/stable-prefix/sherpa-endpoint translation count is always zero. Only a
nonempty valid Whisper Final enters TranslationCoordinator. Empty/error/timeout
Final retains the last preview, sends no translation and does not prevent the
next utterance. Provider None still shows Original. Streaming failures disable
only preview; fatal Whisper/model/capture failures use existing stop behavior.

## Lifecycle and Privacy

Startup restores selected mode, never capture or model loading. Start loads both
models asynchronously before capture. Missing optional preview inputs fall back
to Final-only. Ordinary Stop cancels capture/utterance/request identities, clears
preview queues and resets streams but retains both warm models. Stop during load
prevents late completion from starting capture. Mode switch stops the old input.
Hide/ClickThrough/Drag Lock do not control pipeline Running.

All sherpa create/decode/result-copy/free/reset/destroy calls belong to its worker.
QString copies result text before upstream result destruction. Close stops input,
joins the streaming worker and releases ASR state; there is no streaming helper
process. In-progress upstream native calls complete before join (no unsafe thread
termination); this is not a claim of interrupting a hung C API call.

Opt-in `TRANSLATOR_AUDIO_DIAGNOSTICS=1` records load timing, session/utterance/
revision, displayed Partial, Final, translation timing, queue/high-water/drops,
worker compute time and Windows process working-set/private bytes/CPU time.
`QT_FORCE_STDERR_LOGGING=1` is used for local GUI-process QA logs. Text is printed
only in this explicit QA mode; raw PCM, recordings and credentials are not logged.
QA logs/models/audio remain ignored in `.cache`/build, not committed or uploaded.

## Automated Acceptance

`phase8d2_streaming_original` uses fake capture, fake worker ASR and a fake
translation provider, with the real audio/application coordinators and existing
QLabel setters. It needs no model, runtime DLL, microphone or network. It checks:

- Startup privacy, async dual-model readiness before one capture, cancelled load.
- Ten Partial revisions before speech end with zero translation requests.
- Text shortening, duplicate text/revision, foreign session and old utterance.
- Closing freeze, Final correction, duplicate Final, late Partial after Final.
- U1 delayed Final while U2 Partial is visible; out-of-order translation.
- Empty/error Whisper recovery, None provider and preview-only failure fallback.
- Ten seconds synthetic silence with zero visible Partial/Final/translation.
- Stop, five warm restarts, Screen switch, shared worker mailbox identity.
- Worker-only API ownership, bounded explicit overflow and load/decode failure.
- Idempotent worker shutdown, unavailable backend and production Start fallback.

All previous CTest suites are retained. Real models are outside standard CTest.

| Configuration | Build / CTest |
| --- | --- |
| Ordinary Release | Build PASS; 26/26 CTest PASS |
| Ordinary Debug | Build PASS; 26/26 CTest PASS |
| Whisper Release | Build PASS; 27/27 CTest PASS |
| Whisper Debug | Build PASS; 27/27 CTest PASS |
| Zipformer + Whisper Release | Build PASS; 27/27 CTest PASS |
| Zipformer + Whisper Debug | Build PASS; 27/27 CTest PASS |

These are the completed regression results from the implementation run, not new
builds for the documentation closeout. The ordinary 25-suite and Whisper
26-suite baselines are retained, plus the new streaming integration suite.
The combined trees explicitly selected the local Python interpreter so all four
existing Python suites were included; the earlier 23-suite discovery is not used
as full acceptance. No full build/test rerun was required by these Markdown edits.

## Production Acceptance and Performance

Formal production QA uses `build/phase8d2-streaming/Translator.exe`, not a Probe.
No recognition recording is enabled. Microphone and loopback use explicit UI
Start and the current selected devices. The following evidence is from this
production run and owner confirmation, not historical Probe accuracy tables.

| Gate | Current evidence |
| --- | --- |
| Microphone >=5 prompts, >=2 long, owner confirms pre-end text | PASS; real Realtek microphone; owner confirmed all five read once and long-sentence live text |
| English/Chinese loopback, live text before playback end | PASS; fixed local speech playback; owner confirmed pre-end Original updates for both |
| Real Whisper correction and DeepL Final-only subtitles | PASS; real Final and translated labels, zero Partial-triggered requests; Chinese output confirmed in microphone run |
| Ten seconds real silence | PASS; zero visible Partial, Whisper Final and translation during final ten seconds after fixed WAV playback |
| Five production Stop/Start cycles, each load count one | PASS; owner confirmed five cycles; one actual load per model in the same instance |
| Hidden/ClickThrough/Tray and input switching | PASS for owner-tested Hide/Show, Tray, Ctrl+Alt+T and microphone/system-audio operation; automatic stale-mode guards PASS |
| Close during streaming | PASS; owner closed while Original updated without Stop; prior Translator process absent afterward |
| Close during Whisper inference | PASS; owner performed final manual exit test; Get-Process returned no Translator process |
| Close while DeepL request pending | PASS; owner performed final manual exit test; Get-Process returned no Translator process |
| CPU, memory, latency, bounded backlog and drops | PASS for realtime throughput; maximum backlog 220 ms, streaming/capture drops zero; measurements below |

### Microphone Evidence

Realtek(R) Audio microphone used the existing 48000 Hz stereo float32 LE capture
and unified 16000 Hz mono int16 LE PCM. The five prompts were Hello, Good morning,
How are you today?, and these two longer prompts:

- This is a realtime speech recognition test, and I can see the original subtitles while I am still speaking.
- Today we are testing live subtitles with two local models, and the final translation should appear after I finish this sentence.

The owner confirmed each prompt was read once, Original appeared before the long
sentences finished, and Chinese translation appeared after pauses. The unchanged
energy endpoint split some long prompts at pauses; five prompts are not asserted
to equal exactly five detector utterances. This is a functional live-subtitle
PASS, **not** an English microphone accuracy PASS.

One logged long-sentence segment (session 1, U9) progressed from `THEY` at 846 ms,
to `THEY WE ARE` at 1158 ms, `THEY WE ARE TESTING` at 1788 ms, and
`THEY WE ARE TESTING LIVE SUPPORT OF TWO LOCAL BORDOMS` at 5286 ms.
Whisper then returned `And there we are tasting lines of the petals with two local models.`
Other segments also had substantial recognition errors: U7's preview was
`AND I CAN SEE THE ORIGINAL`, but Whisper returned
`And I can't say that orange juice.` No errors are hidden or filtered; Final
correction means the real Whisper result replaces eligible preview, not that
Whisper always improves accuracy. Hello produced no visible preview before its
Final; Good morning produced preview and Final `Good morning`, with DeepL Chinese
output. The real Hello Final also produced the Chinese translation `你好`.

### Clean Loopback Evidence

The early run with another video playing is excluded from clean playback QA.
After the owner paused other audio and selected System Audio, the existing
WASAPI render endpoint captured two fixed local WAVs, with microphone capture
stopped. No second capture or recording was created.

On 2026-10-07 (Asia/Shanghai), English playback ran 20:13:09.115 to 20:13:13.365;
Chinese playback ran 20:13:18.387 to 20:13:22.513, followed by silence until
20:13:36.530. Session 7 U22 displayed `THE` at 519 ms, `THE SYSTEM` at 837 ms,
`THE SYSTEM SHOULD DISPLAY` at 1519 ms, and
`THE SYSTEM SHOULD DISPLAY TEXT WHILE THE AUDIO IS STILL PLAYING` at 3777 ms.
Whisper Final was `The system should display text while the audio is still playing.`
Session 7 U23 displayed `我们` at 575 ms, `我们需要` at 885 ms,
`我们需要比较` at 1577 ms, and `我们需要比较识别速度和准确率` at 3139 ms.
Whisper Final was `我们需要比较识别速度和准确率。` and updated Original.

This loopback run's actual translation target remained English: DeepL returned
the English input unchanged and translated the Chinese Final to
`We need to compare recognition speed and accuracy.` These are successful
Final-only requests, **not** evidence of Chinese output in this loopback run;
Chinese translated-subtitle acceptance comes from the microphone run.
Each of these two nonempty Finals generated one request. The automated hard gate
also verifies ten Partial revisions generate zero requests, then one Final
generates exactly one request. The last ten seconds of actual silence generated
zero visible Partial, zero Whisper Final and zero translation requests.

### Performance and Lifecycle Measurements

The measured cohort is session 1 U3-U10 microphone segments and session 7
U22-U23 fixed loopback. Nine segments had visible Partial; Hello did not and is
not silently counted as zero-latency preview. First-visible-Partial median among
those nine is **799 ms**; the median of their 56 visible update intervals is
**315.9125 ms**. These are energy-onset-to-display and changed-text intervals,
not universal recognizer timing guarantees.

| Measurement | Observed production value |
| --- | --- |
| Whisper cold load | 149 ms; actual load count 1 |
| Zipformer cold load including hash checks | 2656 ms; actual load count 1 |
| Warm Stop/Start | At least five owner-confirmed cycles; Zipformer ready notification 0 ms, no model reload |
| English fixed loopback Whisper processing / DeepL / post-boundary translation | 1563 / 931 / 2496 ms |
| Chinese fixed loopback Whisper processing / DeepL / post-boundary translation | 1684 / 432 / 2119 ms |
| Screen idle working set / private bytes | 32698368 / 8953856 at initial sample |
| Whisper-only loaded working set / private bytes | 266432512 / 870633472 |
| Both loaded working set / private bytes | 519520256 / 1116803072 |
| Later audio-running working set / private bytes | 726683648 / 1215832064 |
| Zipformer-only process memory | Not separately measured in this production instance; Whisper loads first |
| Streaming compute / decoded audio | 55682.043 / 1058220 ms; ratio about 0.0526, including idle audio |
| Process cumulative CPU | 524171.875 ms; includes both ASRs and mixed earlier audio, not isolated Zipformer CPU |
| Maximum queue depth / final queue | 11 chunks (220 ms) / 0 ms |
| Streaming dropped chunks / capture dropped chunks | 0 / 0 |

No sustained backlog growth or PCM loss was observed. Model load counts remain
one across microphone/system-audio and warm restart in the same process. Hide/Show,
Tray recovery and ClickThrough did not stop streaming; the owner confirmed the
latest subtitles remained visible on restore. Ctrl+Alt+T was manually confirmed;
Ctrl+Alt+S's existing Win32 1409 conflict remains **PENDING**, not a new PASS.
No new claim is made for untested DPI, multi-monitor or other-device scenarios.

### Final Manual Exit Closeout

On 2026-10-07 the owner explicitly reported **PASS** for Close during Whisper
inference and Close while a DeepL request was pending. After each Close,
`Get-Process Translator -ErrorAction SilentlyContinue` returned no output.
These final two results are **owner-reported real Windows acceptance**, not new
agent-operated tests or a timing inference from the earlier log. Together with
the already completed streaming exit, all three required exit scenarios PASS.
The streaming worker is in-process; no separate sherpa helper process is used.
No residual Translator.exe was found in these tests. This does not prove that an
arbitrarily hung upstream native call can be forcibly interrupted.

### Gate Decision

Phase 8D.2 is **PASS**: optional pinned backend integration, one-PCM fan-out,
real pre-end microphone and bilingual loopback display, Final correction,
Final-only translation, identity/concurrency guards, silence gating, warm
lifecycle, fallback, bounded throughput, Stop/restart/mode stale suppression,
three real exit scenarios and the six build/test configurations are satisfied.
Guard/fallback/error/None scenarios use the documented fake-backend automated
tests; they are not misrepresented as additional hardware tests. Optional
Zipformer-only memory profiling is unmeasured, not substituted with dual-model
memory. English microphone errors remain a known accuracy limitation accepted
as non-blocking for live integration, not a resolved recognition issue.

**Phase 8D overall remains PARTIAL**: portable sherpa/ORT deployment, runtime
license/redistribution closeout, streaming clean-machine acceptance, shortcut
conflict resolution and remaining environment-specific checks require separate
work. No public release readiness is inferred. Phase 8D.3 and Phase 9 are not
started by this closeout.

Known limitations: imperfect English mic hypotheses; energy is not a semantic VAD;
optional preview failure remains usable Final-only; upstream native load/decode
is not forcibly interruptible. Global shortcut conflicts, other-device/DPI/multi-
monitor checks and portable streaming dependencies/licenses require separate
acceptance. Phase 8D.3 and Phase 9 are not started.

## Local Validation Commands

```powershell
$env:PATH = 'D:\QT\Tools\mingw1310_64\bin;D:\QT\6.11.2\mingw_64\bin;D:\QT\Tools\Ninja;' + $env:PATH
$cmake = 'D:\QT\Tools\CMake_64\bin\cmake.exe'
$ctest = 'D:\QT\Tools\CMake_64\bin\ctest.exe'
& $cmake -S . -B build/phase8d2-streaming -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=D:/QT/6.11.2/mingw_64 `
  -DPython3_EXECUTABLE=C:/Users/Administrator/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe `
  -DTRANSLATOR_WHISPER_CPP_DIR=D:/workspace/Project/Usst/Translator/.cache/third_party/whisper.cpp `
  -DTRANSLATOR_SHERPA_ONNX_DIR=D:/workspace/Project/Usst/Translator/.cache/phase8d1/sdk `
  -DTRANSLATOR_SHERPA_RUNTIME_DIR=D:/workspace/Project/Usst/Translator/.cache/phase8d1/runtime
& $cmake --build build/phase8d2-streaming -j4
& $ctest --test-dir build/phase8d2-streaming --output-on-failure
# Repeat configure with Debug and build/phase8d2-streaming-debug.
# Existing ordinary / Whisper trees are rebuilt and run with full CTest.
```

## Repository Scope

New files: shared streaming interface/stabilizer/backend/coordinator, fake
production integration test and this document. Modified: CMake, small Audio /
ProductionInput controller wiring, main diagnostics/UI connections, one window
setter, Probe forwarding headers/shared-source wiring, README status. The former
Probe backend `.cpp` is moved to the shared production adapter, not discarded.
Initial untracked `licenses/`, `mic-test.txt`, `scripts/prepare_qt_licenses.py`
remain untouched and must not be included in this task's commit.

The final acceptance closeout changes only this document and the necessary README
status. Existing uncommitted implementation/test/CMake changes remain untouched
and are not staged by the documentation-only closeout; acceptance describes the
tested local working tree, not an assertion that this documentation commit alone
contains the production integration. No model, runtime binary, WAV/PCM, private
QA log or credential is included. No production source edit or full rebuild was
performed for the closeout.
