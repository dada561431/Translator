# True Streaming ASR Feasibility - Phase 8D.1

Date: 2026-10-07. Initial commit: `89f02314cdd473e89c035eb37ea37082b4652b01`.
Single agent, no worktrees. All work is confined to Translator.

**Phase 8D.1: PARTIAL. Do not integrate this candidate into production yet.**
The native C ABI, multilingual model, bounded worker, incremental WAV results,
and user-observed live microphone streaming work. Controlled loopback acceptance
and precise independently annotated speech-boundary latency remain incomplete.
Recognition errors and the 1.27 s first Partial in two fixtures are significant.
Phase 8D remains **PARTIAL**; no Phase 8D.2 or Phase 9 work is authorized here.

## Motivation And Scope

The existing Whisper Final-only pipeline waits for endpointing. The previous
speech-end-to-Original median was about 1415 ms and translation about 1741.5 ms,
including roughly 600 ms trailing silence and 0.8-1.0 s CPU Final inference.
Reducing the silence alone cannot produce words while a person is speaking.

This independent executable is a feasibility test, not a replacement backend.
`TranslatorStreamingAsrProbe` does not link TranslatorProviders,
TranslatorAudioPipeline, TranslationCoordinator, or TranslationWindow.
It makes **zero translation requests**, reads no credentials, records no audio,
and does not download anything. Its optional QA window is not the overlay.

## Pinned Local Dependencies

| Component | Pin / preparation | License evidence |
| --- | --- | --- |
| sherpa-onnx | [k2-fsa/sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx/tree/v1.13.8), v1.13.8, `11afbd009a7f8c08f4bcf2fc1b265d0df4670fbf` | [Apache-2.0](https://github.com/k2-fsa/sherpa-onnx/blob/v1.13.8/LICENSE) |
| Windows C API | Official x64 shared MT Release no-TTS library asset; headers from the same commit | No internal sherpa C++ types cross the boundary |
| ONNX Runtime | Official Microsoft CPU x64 v1.28.2, C API 28, dynamic DLL | MIT plus official archive ThirdPartyNotices and Privacy documents |
| Model | `sherpa-onnx-streaming-paraformer-bilingual-zh-en`, int8; HF revision `8e40c43232a1c5c66c82111efc5820d3accca11b` | [Publisher model card](https://huggingface.co/csukuangfj/sherpa-onnx-streaming-paraformer-bilingual-zh-en) declares Apache-2.0 |

The [official Paraformer documentation](https://k2-fsa.github.io/sherpa/onnx/pretrained_models/online-paraformer/paraformer-models.html)
identifies the original ModelScope online Paraformer model and conversion origin.
This model does not supply token timestamps. Speech onset must not be inferred
from token timestamps that do not exist. This is local technical QA only:
**public redistribution is not approved by this task**, and this is not legal advice.
Existing license files and packaging are unchanged.

Downloads were explicitly authorized by the owner in this conversation.
Only prepared local files are used by CMake and the executable; there is no
FetchContent, checkout fetch, model downloader, or application network request.

Archive SHA256:

- sherpa library asset `sherpa-onnx-v1.13.8-win-x64-shared-MT-Release-no-tts-lib.tar.bz2`:
  `a1253e665c4f236119c443c8932a8acfca32a546c78c05962d483c9a0eae21b7`.
- Official Microsoft `onnxruntime-win-x64-1.28.2.zip`:
  `c4eedd29489d5feca21866d054638416f3655bf6b18851b3b6b85c8313e95c35`.
Both match the release asset digest. No System32 DLL was copied.

| Local model input | Bytes | SHA256 |
| --- | ---: | --- |
| encoder.int8.onnx | 165462184 | `81a70226a8934e6ed92aa1d4fc486b428b5398e2f2619ed4897b7294cab90e9a` |
| decoder.int8.onnx | 71664561 | `f3cca9f77bb9d93c8fcbfb63ae617b6b1ee96818df3aa3b151c40658fe38594f` |
| tokens.txt | 75756 | `59aba8873a2ed1e122c25fee421e25f283b63290efbde85c1f01a853d83cb6e6` |

Total: **237202501 bytes / 226.21 MiB**. No model, token file, WAV, DLL,
archive, or third-party checkout is tracked by Git. All are below `.cache/`.

## MinGW Gate A And Runtime Search Order

MinGW 13.1 compiled and linked the official MSVC-format import libraries using
the C ABI. No MSVC installation or sherpa source build was required. This proves
the tested prebuilt path, **not** a MinGW source-build claim.

Initial version-only test succeeded, but the first recognizer creation failed:

```text
The requested API version [28] is not available, only API versions [1, 17]
are supported in this build. Current ORT Version is: 1.17.1
exit: -1073741819 (0xC0000005)
```

Investigation found System32 `onnxruntime.dll` taking precedence over PATH.
The sherpa archive's own DLL is 1.28.2; this was **not an incompatible upstream
archive**. Earlier tentative suspicion of that archive was corrected.
The prepared QA runtime is copied beside the Probe executable, never beside
production Translator. The adapter checks sherpa version/commit and the **actual**
ORT `OrtGetApiBase()->GetVersionString()` before model creation. Wrong ORT inputs
return an explicit error instead of intentionally requesting an unavailable API.
ORT telemetry is disabled through its documented C API before creating sherpa.
No packet capture or clean/offline runtime acceptance is claimed for this Probe.

## Build And Usage

Prepared SDK layout:

```text
.cache/phase8d1/sdk/
  include/sherpa-onnx/c-api/c-api.h  # pinned upstream header
  include/onnxruntime_c_api.h       # and companion headers, official ORT 1.28.2
  lib/sherpa-onnx-c-api.lib
  lib/onnxruntime.lib
.cache/phase8d1/runtime/
  sherpa-onnx-c-api.dll
  onnxruntime.dll
  onnxruntime_providers_shared.dll
.cache/models/sherpa/
  encoder.int8.onnx
  decoder.int8.onnx
  tokens.txt
```

Qt/MinGW/Ninja PATH is supplied by the local shell, not hardcoded into CMake:

```powershell
cmake -S . -B build/phase8d1-sherpa -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="<Qt6 prefix>" `
  -DTRANSLATOR_SHERPA_ONNX_DIR="<absolute prepared SDK>" `
  -DTRANSLATOR_SHERPA_RUNTIME_DIR="<absolute prepared DLL folder>"
cmake --build build/phase8d1-sherpa --target TranslatorStreamingAsrProbe TranslatorStreamingProbeTests -j4
build/phase8d1-sherpa/TranslatorStreamingAsrProbe.exe --model-dir .cache/models/sherpa --wav <local.wav> --chunk-ms 20 --threads 2
build/phase8d1-sherpa/TranslatorStreamingAsrProbe.exe --list-devices
build/phase8d1-sherpa/TranslatorStreamingAsrProbe.exe --model-dir .cache/models/sherpa --kind microphone --seconds 50 --endpoint --preview
build/phase8d1-sherpa/TranslatorStreamingAsrProbe.exe --model-dir .cache/models/sherpa --kind loopback --seconds 40 --endpoint --play-wav <local.wav>
```

`--device` accepts the enumerated hex ID, not a guessed display name.
`--play-wav` can be repeated for a local playlist with three-second gaps; it is
allowed only with loopback. Playback uses QMediaPlayer/default output, while
capture uses the existing WASAPI backend, never a microphone substitute.
Use the default loopback endpoint when testing this default-output playback.
`--cue` can be repeated for live microphone QA (first at 3 s, then every 15 s).
A cue timestamp is **not** an actual speech timestamp.

The normal project has no sherpa requirement. With an empty opt-in path, no
sherpa library/runtime is searched or linked. Only the dependency-free test core
is available. New build-tree inputs require reconfiguration, not production edits.

## PCM, Worker, Endpoint And Stop

```text
WAV wall-clock batches 20/40/100/200 ms
  OR unchanged AudioInputCoordinator -> 20 ms PcmChunk
  -> bounded mailbox (32000 bytes = 1 second)
  -> one independent worker
  -> existing AsrAudioBuffer::toFloat (16k mono S16LE -> float [-1,1])
  -> AcceptWaveform (16000 Hz, no resampling)
  -> while IsReady: DecodeOnlineStream -> changed-text Partial
  -> optional sherpa Endpoint -> Final-like snapshot -> Reset stream
```

One recognizer is loaded per process; stream reset does not reload it. Capture
callbacks only enqueue bounded copied PCM and produce low-frequency level QA;
decode and recognizer destruction never run in the capture/GUI callback.
No SpeechEndpointDetector or production coordinator is involved.

Mailbox overflow rejects new chunks and is counted; the executable fails QA
instead of hiding loss. Capture discontinuity is logged and resets stream state
on the worker without stitching text across an audio gap. Initial WASAPI
discontinuity can occur before playback and is recorded separately from drops.
Queue sampling runs at 4 Hz. Input duration is bounded: WAV <=60 s, live <=300 s.
Partial log/preview work is only on text change; preview polling is <=10 Hz.

Stop stops timers, player and audio input, closes the mailbox, drains queued
PCM, adds explicitly logged zero tail, calls InputFinished, drains IsReady,
prints Final, joins the worker, then destroys stream and recognizer. No helper
process or TerminateThread is used. Load cannot be safely force-cancelled inside
an upstream native API; no hard native-load timeout guarantee is claimed.

Endpoint mode uses the sherpa baseline rules: 2.4 s initial/trailing silence,
1.2 s trailing silence after speech, or 20 s maximum utterance. Endpoint->Final
measures snapshot overhead **after** detection, not total speech-end latency.
Continuous/manual-stop mode uses the same loaded model with endpoint disabled.

## Partial And Stability Semantics

Every changed text increments revision; identical snapshots are not repeated.
JSONL contains wall/audio time, segment, previous/current text, Unicode character
length, common prefix, added and removed/replaced characters, queue/drop metrics.
Raw text is never corrected, translated, dictionary-completed, or rewritten by QA.

Experimental two-update local agreement makes the first hypothesis wholly
unstable; the next common prefix can become stable. Stable only grows until
endpoint/reset. A later rewrite across stable is an explicit `stable_conflict`:
the stable hypothesis is retained, and the **entire raw new text** is returned as
unstable. In this case stable+unstable is not a faithful concatenated display;
the QA preview always shows raw text, avoiding fabricated recognition.
Real Paraformer runs here were append-only (rewrite/conflict counts 0). Fake
tests exercise both English/Chinese rewrites and committed-prefix conflicts.
Agreement does not mean that the text is correct.

## Fixed Speech Fixtures And Truth

- English: pinned local whisper.cpp `samples/jfk.wav`, 11.00 s, real JFK speech.
  Independent reference: JFK Library's [public-domain inaugural address](https://www.jfklibrary.org/learn/about-jfk/historic-speeches/inaugural-address).
  Expected excerpt: "And so my fellow Americans ask not what your country can do
  for you ask what you can do for your country."
- Chinese/mixed official `test_wavs/1.wav`, 5.10 s. Owner independently listened
  and supplied: "这是第一种，第二种叫 呃 与 always alwaays是什么意思啊？".
  The supplied English spelling is retained, not silently normalized to ASR.
- Chinese/mixed `test_wavs/2.wav`, 4.69 s. Owner supplied: "这个是平凡的啊，这个是记下来，frequently，平凡的";
  then explicitly corrected 平凡 to 频繁 as a typing mistake. The middle wording
  remains uncertain; do not claim a verified exact-match benchmark for it.
- `test_wavs/3.wav`, 8.83 s, is used only for endpoint/Stop behavior; it has no
  independent transcript here and is not assigned an accuracy PASS.

Fixture hashes: 1.wav `8bfb42c963e623ebab31b81ff4404867d07d3102507c87ac14577c4c61663b8c`;
2.wav `20805bcc696b9b65f3357a2508a419d4f33b81006d47e1312e219b0fad934d1d`;
JFK `59dfb9a4acb36fe2a2affc14bacbee2920ff435cb13cc314a08c13f66ba7860e`.
Raw WAV and JSONL stay local under ignored cache; none are uploaded.

### Chunk A/B

Same fixture, real wall-clock pacing; 2 ORT threads, CPU, manual Stop, 300 ms
Stop-only zero tail. First latency below is **audio-start**, not speech-start.

| Input | Feed ms | First Partial ms | Changed Partials | Interval median ms | Compute RTF | Queue high water ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| English JFK | 20 | 1273 | 13 | 600.37 | 0.0511 | 20 |
| English JFK | 40 | 1274 | 13 | 599.95 | 0.0509 | 40 |
| English JFK | 100 | 1333 | 13 | 600.72 | 0.0509 | 100 |
| English JFK | 200 | 1439 | 13 | 601.02 | 0.0512 | 200 |
| Chinese 1 | 20 | 1273 | 7 | 601.06 | 0.0529 | 40 |
| Chinese 1 | 40 | 1271 | 7 | 601.28 | 0.0500 | 40 |
| Chinese 1 | 100 | 1334 | 7 | 600.27 | 0.0517 | 100 |
| Chinese 1 | 200 | 1435 | 7 | 600.06 | 0.0520 | 200 |

All eight runs: model load count 1, drops 0, final backlog 0. Sampled backlog
returns to zero rather than growing. Model load 1178-1288 ms; teardown 32-70 ms.
Feed lateness up to 26 ms is logged, not disguised as perfect timer delivery.
IsReady produces roughly 600 ms model steps regardless of a 20 ms input feed;
large feeds wait longer at boundaries. First latency is not improved to 300 ms
merely by using small feed buffers. These samples do not meet the 800 ms goal.

Chinese 2: first 679 ms in the original 20 ms run (676 ms with extended Stop
tail); incremental prefixes `这个是 -> 这个是频繁 -> 这个是频繁的啊不 -> ...frequently`.
This meets the audio-start target on one clip, not on all clips.

English sequence at approximately 1.27/1.87/2.47/3.67/4.27/6.07 s:
`and so -> and so h my go -> and so h my god americ -> ...americans -> ...ask -> ...not what your`.
It continues to the last phrase at 10.87 s, proving incremental words instead
of one sentence-only output. English Final contains `h my god` and `as what`
errors; normalized WER **13.64%**, CER **8.43%** on this one reference excerpt.
Normalization is lowercase, Unicode punctuation removal and collapsed spaces;
CER also removes whitespace. This is not a broad English benchmark.

Chinese 1 sequence at approximately 1.27/1.87/2.47/3.07/3.67/4.27/4.89 s:
`这这是 -> 这这是第一种第 -> 这这是第一种第二种 -> ...叫 -> ...呃与 -> ...always -> ...always o s 什`.
This is phrase/character-group growth, not one-character-per-frame. Default
Final truncates the tail; CER **40.00%** against the verbatim supplied transcript
is provisional because the supplied mixed-English spelling is not standardized.
It is not reported as an accuracy PASS.

### Stop Tail A/B

Upstream's example uses 300 ms zero tail. The same 1.wav with explicitly chosen
`--flush-ms 1200` completes `...always o s 什么意思啊`, while remaining inaccurate.
Stop-flush-to-Final is 59.29 ms; extra Final-tail output is flagged `during_flush` and
**excluded** from streaming-before-end/update statistics. It does not improve
first Partial. Default stays 300 ms; no production endpoint or timeout is changed.
Chinese 2 extended-tail Final is `这个是频繁的啊不认识接下来 frequently 频繁的`.

### Speech Boundary Measurement Limits

Exact speech-start/end annotations were not independently timestamped. They
remain unknown (`-1`/null), not replaced by file duration, cue time, ASR output,
or artificial trailing padding. Logs support explicit manually annotated spans
through `--speech-start-ms`/`--speech-end-ms`; analysis supports the same inputs.

Independent waveform inspection with 20 ms RMS >0.002 gives **activity proxies**:
JFK 60-11000 ms, Chinese 1 0-5040 ms, Chinese 2 0-4600 ms. These imply roughly
1213/1273/679 ms to first Partial, but RMS activity is not a verified linguistic
boundary and is not promoted to an exact speech latency PASS. Prefix progression
occurs while later spoken content remains; exact boundary timing is still pending.

## Live Microphone

Real `麦克风 (Realtek(R) Audio)`, default microphone, native 48000 Hz / 2 channels /
float32 LE. Existing converter delivers 16000 Hz mono int16 LE 20 ms / 640 bytes.
Three QA cues (read once each; the owner confirmed no omissions/repetitions):

1. Hello, how are you today? This is a realtime speech recognition test.
2. 今天天气很好，这是一个实时语音识别测试，希望说话的时候能看到字幕。
3. The translator should display words while I am speaking, before the sentence has finished.

Owner explicitly confirmed that text appeared **before finishing speaking**.
Live microphone incremental UX: **PASS**, not an accuracy PASS.
There were 34 raw changed Partials, seven nonempty endpoint Finals, 0 dropped
chunks, 0 discontinuities, 40 ms queue high-water and zero final backlog.
Recognizer loaded once; 50 s capture; Stop/join/unload 39 ms.

Examples: `真 -> 真的 how -> 真的 how are you -> 真的 how are you today`;
`今天 -> 今天天气 -> 今天天气真`;
`这 -> 这是一 -> 这是一个实 -> 这是一个实施与 -> ...识别测试`;
`the -> the transl -> the translate should -> ...be more th rely and m in`.
Accuracy is clearly insufficient in several segments. Intended three sentences
are not scored as seven separate exact-match tests. Natural pauses can cause
internal endpoint splits; it is not evidence of a production regression.

First Partial was 14472 ms after capture start, **not** 14.47 s speech latency:
the user began speaking much later than the first cue. 500 ms diagnostic RMS
bins place first voice activity in 14000-14500 ms, implying an uncertain roughly
0-472 ms onset-to-first range. Later Chinese activity begins within 21500-22000 ms
and the first Partial at 22273 ms implies 273-773 ms. Third-cue activity within
36500-37000 ms, first Partial 37271 ms implies 271-771 ms. These are coarse
activity proxies, not exact manually marked speech timings. The user-confirmed
before-end display is the acceptance evidence, not the proxy threshold.

## Live Loopback

Default `耳机 (Realtek(R) Audio)` WASAPI output endpoint; 48000 Hz stereo float32
native format. Microphone was never opened by this loopback Probe.
Three local fixture playbacks were attempted in one 40-second capture.
Continuous Partial generation and lifecycle worked, but unrelated active app
speech was mixed into the system-wide audio (recognition includes unrelated
Chinese conversation before/after the fixtures). **Controlled three-clip accuracy
and latency acceptance: PENDING**, not PASS. Owner was asked to pause other
playback before a repeat. No app was forcibly closed or muted.

The contaminated run: 46 changed Partials, load once, compute RTF 0.0535, queue
high-water 80 ms, drops 0, final backlog 0, Stop 59 ms. One initial WASAPI
discontinuity at 17 ms, before the first playback at 1000 ms, reset the stream.
Its first Partial at 1281 ms must not be assigned to the intended fixture onset:
unrelated content was already audible. This trace proves capture/decode activity,
not isolated speech-recognition accuracy or a clean live-loopback gate.

## Resources And Endpoints

WAV compute RTF = measured AcceptWaveform + Decode elapsed compute / real audio
duration (synthetic Stop tail excluded from denominator). This is wall-clock
compute time, not CPU thread-time. Also recorded separately: Windows process CPU
time (user+kernel), typically 20-24% of one logical core across the WAV tests,
about 1.3-1.5% if normalized over this machine's 16 logical CPUs. Thread count 2.
No dual-ASR contention experiment was performed; Whisper stayed untouched.

Loaded working set about 313 MB decimal (299 MiB); WAV sampled live working set
about 325 MB (310 MiB), microphone max 343744512 bytes (327.8 MiB), contaminated
loopback max 333307904 bytes (317.9 MiB). These are sampled process working sets,
not system memory, private allocation totals, or a proven full lifetime peak.

3.wav endpoint-mode comparison: first 1282 ms, 9 growing updates; endpoint at
7269.75 ms / audio 7240 ms, Final snapshot overhead 0.051 ms; following stream
continues on the same recognizer. Stop Final overhead 29.70 ms, join 76 ms,
RTF 0.0651, no drops. Manual mode on the same file emitted Final only at Stop.
The later `stop_request_to_final_ms` instrumentation also includes worker queue
drain; earlier traces reported flush-to-Final only. Do not conflate the two.
Trailing-silence detection is still roughly 1.2 s plus chunk alignment; the tiny
snapshot number does **not** mean sub-millisecond speech-end latency.

## Automated Checks And Protection

Model-independent CTest covers English growth/rewrite, Chinese growth/rewrite,
surrogate boundaries, committed-prefix conflict, PCM range/odd-byte rejection,
queue capacity/drop/drain/gap tagging, fake online worker incremental results,
endpoint resets, single model load, off-callback execution, load-error wakeup,
Stop-before-input, repeated Stop and destruction. Python analysis tests exclude
flush updates/cues from streaming evidence and do not substitute ASR for truth.

Final source checks used the following existing caches (no sherpa opt-in), with
`cmake -S . -B <cache>`, `cmake --build <cache> -j4`, and
`ctest --test-dir <cache> --output-on-failure`:

| Build cache | Configure/build | CTest |
| --- | --- | --- |
| build/phase61b (Release) | PASS | 25/25 PASS |
| build/phase61b-debug (Debug) | PASS | 25/25 PASS |
| build/phase8b-whisper (Release) | PASS | 26/26 PASS |
| build/phase8b-whisper-debug (Debug) | PASS | 26/26 PASS |

Opt-in Release and Debug Probe builds and `ctest -R phase8d1`: each 1/1 PASS.
Debug real-model sample 2 smoke: load once in 1426 ms, first Partial 674 ms,
RTF 0.0650, no drops/backlog, worker Stop request->Final 35.31 ms, join/unload
75 ms; process exited successfully. No Probe process remained afterwards.
The three Python
analysis unit cases also pass via the existing script-test discovery.
The optional Vulkan-header warning is unrelated to this Qt Widgets Probe.
Real model, microphone and loopback QA are
not added to ordinary CTest. No model/mic/network input is needed by the fake tests.

Production source files are unchanged: no modifications to `src/`, existing
capture, endpointing, ASR/Whisper, translation, OCR, Settings, Toolbar/Tray, or
overlay. Packaging/license files and original LunaTranslator are untouched.
Pre-existing `licenses/`, `mic-test.txt`, `scripts/prepare_qt_licenses.py` remain
untracked and are excluded from this task's commit.

## Gate Decision And Recommendation

| Gate | Result |
| --- | --- |
| A: MinGW C ABI/model load | PASS, prepared app-local v1.28.2 runtime |
| B: English/Chinese incremental WAV | Observed; exact speech-boundary timing pending |
| C: >=2 pre-end updates, longer zh/en | Incremental prefixes observed; precise timed annotation pending |
| D: live microphone | PASS for owner-observed incremental UX, accuracy PARTIAL |
| D: isolated three-clip loopback | PENDING, first run contaminated by other playback |
| E: bounded queue/realtime throughput/Stop | PASS in measured runs; no sustained backlog growth |
| Overall Phase 8D.1 | PARTIAL; do not claim the all-items feasibility PASS |

Do not directly integrate this Paraformer candidate into production. It establishes
that true C-ABI streaming is possible on the current toolchain, but cadence is
about 600 ms, first latency varies, and bilingual/English errors are substantial.
Finish controlled loopback and independently timestamped speech verification;
then compare a streaming Zipformer candidate or explicitly measured Whisper
Final correction in a separately authorized task. No model search, dual-ASR,
Whisper rolling window or production integration was implemented here.

## Requested Report Ledger

1. Phase 8D.1 status: PARTIAL; see gate table.
2. Initial commit: `89f02314cdd473e89c035eb37ea37082b4652b01`.
3. Added: five `tools/streaming_asr/` files, `tests/StreamingProbeTest.cpp`, two analysis scripts, this document.
4. Modified: `CMakeLists.txt`; README gets a QA-only status link.
5. sherpa repository: k2-fsa/sherpa-onnx, linked above.
6. Pin: v1.13.8 / `11afbd009a7f8c08f4bcf2fc1b265d0df4670fbf`.
7. sherpa license: Apache-2.0.
8. ORT strategy: official Microsoft CPU 1.28.2 shared DLL / API 28; app-local QA only.
9. MinGW result: compiled, import-library linked, DLL/model loaded; no source-build claim.
10. API: public sherpa C API plus ORT public version/telemetry C API, no internal C++ ABI.
11. Model: streaming Paraformer bilingual zh/en int8 only.
12. Inputs: encoder.int8.onnx, decoder.int8.onnx, tokens.txt.
13. Total model bytes: 237202501.
14. Model source/license: pinned publisher HF files and ModelScope conversion origin, declared Apache-2.0.
15. Model committed: no.
16. Automatic download: none in build/application; explicit owner-authorized preparation only.
17. PCM: 16 kHz mono S16LE -> existing float adapter; no second resample.
18. Tested feeds: 20, 40, 100, 200 ms for same English/Chinese files.
19. Readiness: while IsReady, roughly 600 ms decode cadence, not 20 ms output cadence.
20. Worker: one independent std::thread owns recognizer and teardown.
21. Queue: 32000 bytes / one second; rejects and counts overflow.
22. Load: A/B 1178-1288 ms.
23. Load count: one per process, unchanged across endpoints.
24. English truth: independent JFK reference excerpt, not recognizer text.
25. English sequence: listed in fixed-fixture section; 13 revisions.
26. English first: 1273 ms audio-start at 20 ms feed; exact speech-start timing pending.
27. English update count: 13.
28. English median interval: 600.37 ms.
29. English Final: errors including `h my god`/`as what`, normalized WER 13.64%.
30. Chinese truth: owner's independent sample 1/2 listening, ambiguity explicitly retained.
31. Chinese sequence: listed in fixed-fixture section, no invented character-by-character frames.
32. Chinese first: sample 1 1273 ms; sample 2 about 679 ms audio-start.
33. Chinese update count: sample 1 seven streamed updates; sample 2 seven before Stop flush.
34. Chinese interval: sample 1 601.06 ms median.
35. Chinese Final: errors/truncation documented; 1200 ms Stop tail improves completeness, not correctness.
36. Chinese visual behavior: grouped character-prefix growth, also confirmed live by owner.
37. Rewrites: zero in real logs; fake cases cover rewrites.
38. Stability: two-update common prefix; conflicts reported without altering text.
39. Microphone: default Realtek microphone, 48k stereo float native.
40. Live mic: incremental UX PASS by owner; accuracy not PASS.
41. Mic first: 14472 ms capture-start, coarse speech proxy range 0-472 ms; exact onset pending.
42. Mic sequences: three cues and multiple resulting utterance segments listed above.
43. Loopback: default Realtek headphones WASAPI endpoint, not microphone pickup.
44. Live loopback: activity works; isolated controlled acceptance pending.
45. Loopback first latency: contaminated first cannot be attributed to fixture speech onset.
46. Loopback sequences: contaminated trace retained privately, not relabeled as known fixture text.
47. Endpoint: sherpa 2.4/1.2/20 s rules, versus disabled/manual Stop.
48. Endpoint latency: snapshot ~0.05 ms, not 1.2 s trailing-silence-inclusive speech latency.
49. RTF: WAV ~0.05-0.065, microphone 0.0547, contaminated loopback 0.0535.
50. Backlog: finite bursts return to zero; high-water 20-200 ms in WAV A/B.
51. Drops: zero in measured successful runs.
52. CPU: measured Windows process CPU; typically 20-24% single-core-equivalent WAV.
53. Memory: sampled load ~299 MiB, mic ~327.8 MiB.
54. Stop: stopped capture/feed, closed/drained queue, InputFinished, joined/destroyed.
55. Orphans: no helper child process created; Probe exit and joined worker verified.
56. Standard build: no new sherpa dependency on Translator.
57. Ordinary CTest: Release/Debug 25/25; Whisper Release/Debug 26/26; opt-in contract 1/1.
58. Fake tests: independent of model, microphone, network; details above.
59. Production Translator source modified: no.
60. TranslationWindow modified: no.
61. Audio backend modified: no.
62. Whisper backend modified: no.
63. SpeechEndpointDetector modified: no.
64. Translation backend modified: no.
65. DeepL requests: zero by independent target linkage and trace.
66. Dual-ASR: not performed.
67. Dual CPU/memory: not measured; no fabricated comparison.
68. Whisper Final correction: not evaluated here.
69. Document: this file; analysis commands use `scripts/analyze_streaming_asr_probe.py`.
70. Pre-existing untracked: preserved and excluded from staging.
71. Task commit: identify with `git log -1 --format=%H` after task commit; final chat reports hash.
72. Push: final chat records actual non-force origin/main outcome.
73. LunaTranslator: no files changed by this task; read-only reference.
74. Phase 8D: still PARTIAL, status not promoted.
75. Recommendation: do not integrate yet; complete missing evidence, then authorized candidate/correction comparison.
