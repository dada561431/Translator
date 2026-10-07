# Local ASR Backend - Phase 8B

## Scope and Baseline

Baseline: `14f0f5d9e9be20e0053f954294fcc9b78e1de31e` (Phase 8A Core Acceptance PASS).
Work occurs only in `D:/workspace/Project/Usst/Translator`; LunaTranslator remains
read-only. Initial `git status` had three untracked items: `licenses/`,
`mic-test.txt`, `scripts/prepare_qt_licenses.py`. All remain untouched and unstaged.
Single agent, no additional worktree, no reset and no force push.

**Phase 8B overall status: PASS. Core Acceptance: PASS.** Independent architecture,
real MinGW whisper build, model load, real speech Partial/Final, model reuse,
cancel/stale protection, teardown and all automated regressions have passed.

This phase adds independent local ASR infrastructure and a WAV QA Probe. It does
not connect AudioInputCoordinator, TranslationCoordinator, DeepL, the OpenAI-compatible
provider, TranslationWindow, Settings or the OCR realtime pipeline. No capture,
model load, extra startup latency or audio/ASR dependency is added to Translator.exe.
No VAD, silence boundary detection, hallucination filter, denoising, ASR cloud API,
model downloader, packaging or installer. Phase 8C is not started.

## Architecture and Ownership

```text
Local QA WAV (not an audio device)
  -> simulated 20 ms Audio::PcmChunk on Probe/owner thread
  -> AsrCoordinator -> AsrAudioBuffer (30 seconds maximum)
                        |
               current job + latest pending snapshot
                        |
               single QThread / AsrWorker
                        |
                   IAsrBackend
                        |
               WhisperCppAsrBackend
                        |
                  whisper.cpp CPU
                        |
             Partial / Final source text + metadata
                        |
            owner-thread generation/deadline validation
```

`TranslatorAsrCore` links only Qt Core. It consumes the header-only Phase 8A PCM
type, not TranslatorAudio's capture implementation. `TranslatorWhisperCppBackend`
is optional. Both libraries are separate from the production executable's link graph.

`IAsrBackend` is a synchronous, non-QObject worker-only interface: loadModel,
unloadModel and recognize. The injected factory constructs the backend lazily on
the worker. All context creation, inference, unload and backend destruction occur
on that same thread. The coordinator remains a QObject on its owner thread and
delivers public signals there. Call coordinator APIs on that owner thread; use
deleteLater rather than deleting it inside its own signal stack.

AsrCoordinator owns one worker, one deadline timer, one input buffer, one in-flight
operation and at most one pending inference snapshot. Model changes are a separate
single latest desired-path command, prioritized over inference. There is no queued
Qt task per PCM chunk. Only one operation is posted to the worker until its completion
returns; model commands do not create an unbounded worker event queue.

Public API: loadModel, unloadModel, beginUtterance, pushPcm, requestPartial,
finalizeUtterance, cancelUtterance and stop. `isBusy()` reports physical worker work,
not merely whether an utterance was logically cancelled. State is Unloaded, Loading,
Ready, Recognizing or Error. Model loading never blocks the owner/GUI thread.

Reviewed Phase 8A source and Audio tests, main.cpp, CMake, OcrCoordinator,
RealtimePipelineCoordinator, TranslationCoordinator and translation interfaces/factory,
plus Phase 8A and 7B.2 docs. The request-ID/generation idea is retained; no translation
controller is invoked by ASR. Existing production implementations are not changed.

## Model Lifecycle and Dependency

Upstream: [ggml-org/whisper.cpp](https://github.com/ggml-org/whisper.cpp).
QA checkout: tag v1.8.7, exact commit
`48f628a84833905ee4a0658ee6d4a5c915ce1997`, at
`.cache/third_party/whisper.cpp/`. No third-party source is committed.
The [pinned C API](https://github.com/ggml-org/whisper.cpp/blob/48f628a84833905ee4a0658ee6d4a5c915ce1997/include/whisper.h)
provides custom model loaders, encoder-begin and inference abort callbacks.

Load once, infer many times, unload at Stop/exit. Repeating the same normalized
model path while loading/ready is a no-op. A changed path invalidates the utterance,
cancels the old task, waits for the serial worker, unloads the old context before
loading a new context, and suppresses obsolete completion. No two large models are
intentionally resident together. unloadModel/stop invalidate immediately and schedule
worker-side release; actual cleanup completes asynchronously (`isBusy()` false).
Destruction cancels, quits, joins, then destroys/releases the worker context.

Qt QFile implements the model loader, avoiding MinGW std::ifstream's narrow Windows
path limitation. Chinese characters and spaces are tested with the real model.
The loader follows upstream's EOF-after-read contract, not QFile::atEnd() before
the tensor-header EOF probe. Basic ggml header validation and short-read errors
reject tested missing, locked/unreadable and corrupted/truncated models safely.
This is not a security-hardened parser for arbitrary hostile model files: use
trusted upstream models. A 1 MiB minimum and known Whisper header shape checks apply.

QA model: **multilingual ggml-base.bin**, 147951465 bytes (approximately 141.10 MiB).
SHA256: `60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe`.
Local location: `.cache/models/ggml-base.bin` (ignored).
The upstream [model preparation reference](https://github.com/ggml-org/whisper.cpp/blob/48f628a84833905ee4a0658ee6d4a5c915ce1997/models/README.md)
points to the converted-model repository. The one-time QA preparation used
`https://huggingface.co/ggerganov/whisper.cpp/resolve/5359861c739e955e79d9a303bcbc70fb988958b1/ggml-base.bin`.
The measured hash matches the repository's linked artifact SHA256. Neither CMake,
Translator nor the Probe downloads anything. No *.en-only model is used for acceptance.

## PCM, Utterances and Results

Input is exclusively Phase 8A Audio::PcmChunk: 16000 Hz, mono, signed int16 LE,
320 samples / 20 ms / 640 bytes, session, sequence, monotonic timestamp and
discontinuity. PCM format is a type contract; fields do not carry a guessed sample
rate. Wrong size/negative or non-increasing timestamp is InvalidAudio. Duplicate
or older sequence is rejected without altering buffered audio. A changed audio
session is rejected, not concatenated: caller must cancel/begin before pushing it.
Sequence/timestamp gaps or an explicit discontinuity mark the utterance/result;
continue on received samples, without inventing replacement audio.

Buffer maximum: 30 seconds / 480000 samples / **960000 bytes**. An extra chunk
returns UtteranceTooLong, preserves existing audio and requires finalize/cancel/new
utterance before more input. No silent oldest-audio drop. One accumulating buffer
plus current/pending immutable snapshots is at most three logical PCM copies
(2.88 MB), often shared via QByteArray copy-on-write; worker float conversion is at
most 1.92 MB, excluding model/ggml allocations. No unlimited snapshots or recordings.

Conversion is separately tested little-endian int16 / 32768.0f: -32768 -> -1.0,
32767 -> 0.999969482421875, zero -> zero. No reinterpret_cast and no second resample.

beginUtterance explicitly starts a new generation/utterance. requestPartial takes
the current snapshot and increments revision; text can increase, change or retract.
There is no token-level streaming promise. The Probe requests partials every 1000 ms,
not every 20 ms. During an in-flight partial, only the latest pending snapshot
survives. Revisions can skip, never go backwards within a delivered utterance.

finalizeUtterance replaces a pending partial with the complete snapshot, closes PCM
input immediately, and cooperatively aborts/suppresses the in-flight partial so the
Final can run next. No backlog of partials precedes Final. Successful Final clears
the input buffer; another beginUtterance is required. Empty finalize returns the
explicit EmptyAudio error, leaving the caller free to push PCM or cancel. Empty
backend text is a legal Final. No level-based automatic final or VAD is present.

Result: trimmed QString UTF-8 source text, Partial/Final kind, ASR session,
utterance ID, revision, detectedLanguage, processingMs, snapshot audioDurationMs
and discontinuity. Different ASR generations invalidate old partial/final results;
audio-session identity is independently checked while buffering.

Supported options: auto/en/zh/ja/ko; invalid code is UnsupportedLanguage, not silent
English fallback. CPU threads default 4 (1-64). Greedy decoding, no previous-text
context, no timestamps/token print; `translate=false`, `vad=false`, GPU and flash
attention off. English-only models explicitly reject non-English fixed language.
Only English speech is actually recognized in this QA; zh/ja/ko option validation
does not imply recognition accuracy acceptance in those languages.

## Cancellation, Deadlines and Cleanup

Cancel invalidates generation, clears PCM and pending partial/final snapshots,
sets an atomic token and leaves the loaded model available for the next utterance.
No cancelled result is delivered. An obsolete job finishing after a new generation
is discarded; this also covers a backend ignoring cancellation in fake tests.

whisper's abort callback plus encoder-begin callback consult the token and an
absolute steady-clock deadline. Default inference deadline: **60000 ms** from
dispatch. Model operation deadline: **120000 ms**. Pending jobs do not consume an
inference deadline until dispatched. The owner timer emits Timeout, invalidates
the result and cancels; completion also checks the absolute deadline, covering an
owner event loop that was temporarily delayed. Timeout recovery uses the existing
model after worker drain; there is no automatic model reload.

Cancellation is cooperative, not instantaneous. ggml scheduling/model allocations,
blocked file/OS calls and callbacks determine how quickly work returns. No hard
kill, TerminateThread, detached thread or universal teardown-time guarantee.
Serial pending Final/reload waits for the actual current job to drain; the queue
stays bounded even for an uncooperative backend. Model loading checks cancellation
on reads, but allocation/initialization is not forcibly interrupted.

Errors: ModelNotLoaded, ModelLoadFailed, InvalidAudio, UnsupportedLanguage,
UtteranceTooLong, Cancelled, Timeout, BackendFailure and EmptyAudio. Backend exceptions
become errors. Input-validation errors preserve the accepted buffer; inference errors
close the utterance, and a new begin may recover using the still-loaded model.
Logging uses translator.asr, with upstream verbose logs at debug level (off by
default), explicit errors as warnings and lifecycle at info. Expected cooperative
abort can produce upstream encode/decode/encoder-begin warnings, not a delivered
ASR failure. The Probe prints source text only during explicit local QA.

## Builds and Tests

Ordinary configurations keep `TRANSLATOR_WHISPER_CPP_DIR` empty. No FetchContent,
clone, source fetch or model download occurs in configure. An explicitly invalid
checkout path gives a configure error; empty path builds the core/fake tests normally.
whisper is statically linked into the optional QA backend/Probe, **not Qt**: Qt stays
dynamically linked, and production linking is unchanged. No runtime packaging changes.

MinGW 13.1 initially failed in upstream ggml-cpu with missing
THREAD_POWER_THROTTLING_STATE and two macro constants. Its winbase.h already
declares SetThreadInformation and ThreadPowerThrottling. WhisperMingwCompat.h adds
only the missing guarded layout/constants, force-included only on ggml-cpu.
Layout checked against Microsoft's
[THREAD_POWER_THROTTLING_STATE reference](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-thread_power_throttling_state).
No upstream file was edited; external checkout status remains clean. A remaining
upstream `%ld` format warning is non-blocking and documented, not suppressed.

CPU-only QA disables GPU backends, BLAS, OpenMP, backend dynamic loading, native
CPU detection, curl, server, upstream tests/examples and ccache. Upstream x64 CPU
defaults still compile SSE4.2/F16C/FMA/BMI2/AVX/AVX2; verified on Ryzen 7 7840H,
8 cores / 16 logical processors. This is not a packaging claim for older CPUs.

Actual configuration/build locations:

| Configuration | Directory | Build | CTest |
| --- | --- | --- | --- |
| Normal Release, no whisper checkout | build/phase61b | PASS | 21/21 PASS |
| Normal Debug, no whisper checkout | build/phase61b-debug | PASS | 21/21 PASS |
| whisper-enabled Release | build/phase8b-whisper | PASS (all targets) | 22/22 PASS |
| whisper-enabled Debug | build/phase8b-whisper-debug | PASS (all targets) | 22/22 PASS |

All old 19 suites are retained. New tests: AsrBufferTest (conversion/full-scale,
random PCM, sequence/timestamps/session/gap, immutable snapshots, 30-second limit),
AsrCoordinatorTest (worker affinity, failure/recovery, same-model load, supported/
invalid languages, empty audio, partial revisions/coalescing/latest snapshot, final
priority/input closure, cancel/stale/fresh utterance, timeout/late delivery/owner-loop
delay, exceptions, max buffer, reload while inferring, Stop and destructor join), and
optional WhisperBackendContractTest (real API without model: unloaded/missing,
directory/unreadable, Windows exclusive-lock failure, corrupt/truncated header,
idempotent unload). Standard CTest requires no model, network, microphone or speaker.

Machine-specific paths below are QA commands, not hard-coded project configuration:

```powershell
$env:PATH = 'D:/QT/Tools/mingw1310_64/bin;D:/QT/6.11.2/mingw_64/bin;D:/QT/Tools/Ninja;' + $env:PATH
$cmake = 'D:/QT/Tools/CMake_64/bin/cmake.exe'
$ctest = 'D:/QT/Tools/CMake_64/bin/ctest.exe'
& $cmake -S . -B build/phase61b -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=D:/QT/6.11.2/mingw_64 -DTRANSLATOR_WHISPER_CPP_DIR=
& $cmake --build build/phase61b -j 4
& $ctest --test-dir build/phase61b --output-on-failure
# Same commands for phase61b-debug, CMAKE_BUILD_TYPE=Debug.
& $cmake -S . -B build/phase8b-whisper -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=D:/QT/6.11.2/mingw_64 -DCMAKE_C_COMPILER=D:/QT/Tools/mingw1310_64/bin/gcc.exe -DCMAKE_CXX_COMPILER=D:/QT/Tools/mingw1310_64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=D:/QT/Tools/Ninja/ninja.exe -DTRANSLATOR_WHISPER_CPP_DIR=D:/workspace/Project/Usst/Translator/.cache/third_party/whisper.cpp -DPython3_EXECUTABLE=D:/workspace/Project/Usst/Translator/benchmarks/ocr_phase61a/.venv/Scripts/python.exe
& $cmake --build build/phase8b-whisper -j 4
& $ctest --test-dir build/phase8b-whisper --output-on-failure
# Same commands for phase8b-whisper-debug, CMAKE_BUILD_TYPE=Debug.
```

Python is only the existing OCR/package test runner, not an ASR runtime. The first
standalone upstream compatibility attempt used build/whisper-cpu-check and failed
with the exact missing SDK declarations described above; the integrated build fixes
that via the scoped compatibility header, without changing compiler/toolchain.

## Real QA

Date: 2026-10-07, real Windows development host, CPU, MinGW 13.1 / Qt 6.11.2.
Input: the pinned external checkout's `samples/jfk.wav`, actual English speech,
11.000 seconds, 16 kHz mono PCM16. No audio device is opened, no user recording
collected, no speech file copied into tracked source. Probe parses RIFF chunks,
accepts only this unified WAV format (<=30s), and feeds one 640-byte chunk every
20 ms. At most 19.9375 ms zero padding is used for a non-aligned WAV tail, explicitly
reported; JFK has zero padding. No resampling/format guessing. This strict QA reader
is not a general media decoder. Wrong formats/oversized/empty WAV are explicit errors.

```powershell
$env:QT_FORCE_STDERR_LOGGING = '1'
./build/phase8b-whisper/TranslatorAsrProbe.exe --model .cache/models/ggml-base.bin --wav .cache/third_party/whisper.cpp/samples/jfk.wav --language auto --partial-interval-ms 1000 --repeat 5
./build/phase8b-whisper/TranslatorAsrProbe.exe --model '.cache/phase8b/模型 空格/ggml-base.bin' --wav .cache/third_party/whisper.cpp/samples/jfk.wav --language en --cancel-after-ms 100 --repeat 1
./build/phase8b-whisper/TranslatorAsrProbe.exe --model .cache/models/ggml-base.bin --wav .cache/third_party/whisper.cpp/samples/jfk.wav --language en --exit-after-ms 100
./build/phase8b-whisper/TranslatorAsrProbe.exe --model .cache/models/ggml-base.bin --silence --language en
```

Real auto-language run detected en; non-empty early Partial `And so` grows into
the speech. All five Final texts semantically match:

> And so my fellow Americans, ask not what your country can do for you, ask what you can do for your country.

Partial is unstable: one intermediate snapshot read `what you are coming`; this is
not represented as Final correctness or a stable transcript guarantee. Revision
skips visibly exercise latest-only coalescing when CPU inference exceeds the 1s
partial interval. Final is explicit, not triggered by silence. Model load count
is one for all five utterances; repeating loadModel in the Probe also does not reload.

Final recorded auto/repeat run (`auto-final-repeat.jsonl`, latest Probe with correct
snapshot-duration metadata): 27 non-empty Partials, five correct Finals, one load.
Model load: 145 ms; first actual inference (1s Partial): 1810 ms; first complete
11s Final: 1920 ms. Subsequent whole-utterance Finals: 1982, 1836, 1754, 1798 ms.
Warm whole-utterance median: **1817 ms**, mean: **1842.5 ms**. Warm Final RTF median:
0.16518; mean: 0.1675. Final RTF range: 0.15945-0.18018. These do not include audio
arrival time or earlier Partial work, and are not end-to-end live-translation latency.

| Utterance | Final ms | Working set bytes at Final |
| --- | --- | --- |
| 1 | 1920 | 372236288 |
| 2 | 1982 | 371511296 |
| 3 | 1836 | 370765824 |
| 4 | 1754 | 371122176 |
| 5 | 1798 | 371650560 |

Post-load working set: 222662656 bytes; inference working set is roughly 354-355 MiB,
with no observed linear growth across the five utterances. After exit: 15945728 bytes.
Normal join/unload: **20 ms**, total simulated streaming run: 67063 ms. Working-set
samples are coarse observations, not leak proof. Earlier repeat evidence is retained
locally but not substituted for this run; initial iterations overlapped QA/build activity.

The Unicode-path en cancellation run loaded once (76 ms), cancelled session 2,
emitted no result for that cancelled session, drained, then emitted 9 non-empty
Partials and a correct Final for the new session 4. Final took 960 ms (RTF 0.08727),
normal join/unload took 21 ms. Recovery uses the same loaded context, not a cold reload.
Exit while inference was busy emitted no Partial/Final and joined/unloaded in 761 ms.
These measurements were made with concurrent build activity and are observations,
not guaranteed cancellation latency. No ASR helper process or detached worker exists.

Missing/directory/corrupt/locked inputs are model-free automated negative checks.
A real official model truncated to 1 MiB returned ModelLoadFailed with short-read
detail at byte 1048576, no crash, no model-loaded event, no results, clean 0 ms join.
Empty utterance returns EmptyAudio in the core tests. A real **two-second zero PCM**
test completed Partial and Final normally, but both said `you`: **known Whisper
silence hallucination**, not empty speech acceptance or a filtering fix. No heuristic
is added. Chinese/Japanese/Korean speech accuracy remains not tested.

Metadata/text reports and prepared models remain ignored under `.cache/phase8b/`
and `.cache/models/`; no WAV, PCM, model, logs, third-party checkout, binaries,
credentials or pre-existing untracked item is committed.

## Remaining Boundary

Partial snapshot decoding is not true incremental token streaming. Silence may
hallucinate and short/unfinished snapshots may misrecognize; no filtering/VAD claim.
Performance and cancellation latency depend on hardware, CPU contention and upstream
callback opportunities; no hard realtime deadline is promised. Model loading is not
a hostile-file security boundary. Other CPUs, OSes and non-English speech have not
been physically validated. The earlier Phase 8A physical unplug/default-switch/
privacy-denial pending checks remain in that phase, not relabeled by this work.

Exact future Phase 8C contract: owner-thread bounded Audio::PcmChunk handoff,
explicit utterance boundary policy supplied by that phase, cancel on capture-session
change, Final-only source-text consumption by default, independent translation request
IDs/stale protection. A bounded mailbox is required if producer/consumer threads
differ; do not introduce an unbounded queued PCM signal. ASR callbacks must never
execute on capture callbacks. None of this live integration is implemented here.

## File Inventory and Requested Report Ledger

Modified tracked files: CMakeLists.txt, README.md. Added:
src/asr/AsrTypes.h, IAsrBackend.h, AsrAudioBuffer.h/.cpp, AsrCoordinator.h/.cpp,
WhisperCppAsrBackend.h/.cpp, WhisperMingwCompat.h; tests/AsrBufferTest.cpp,
AsrCoordinatorTest.cpp, WhisperBackendContractTest.cpp, AsrProbe.cpp;
docs/asr-backend-phase8b.md. Existing audio, app, translator, UI, OCR, helper,
packaging, .gitignore and license files are unchanged.

1. Phase 8B overall PASS.
2. Core Acceptance PASS on architecture plus real speech/lifecycle evidence.
3. Initial commit 14f0f5d9e9be20e0053f954294fcc9b78e1de31e.
4. Modified CMakeLists.txt and README.md only outside new files.
5. Added files are enumerated immediately above.
6. Independent bounded PCM -> coordinator -> single worker -> local backend -> results.
7. IAsrBackend is synchronous and worker-only; load/unload/recognize.
8. AsrCoordinator owns scheduling, PCM, utterance/generation, deadlines and validation.
9. One QThread, no parallel context inference or detached threads.
10. Unloaded, Loading, Ready, Recognizing, Error.
11. Partial/Final source-text results with timing and input-discontinuity metadata.
12. Monotonic ASR generation invalidates obsolete work; audio session checked separately.
13. Explicit begin/finish/cancel, monotonically increasing utterance ID.
14. Requested revisions increase; coalescing may skip revisions, not reorder them.
15. Phase 8A 16 kHz mono S16LE, 20 ms / 320 samples / 640 bytes.
16. Signed little-endian decode divided by 32768.0f, independently tested.
17. No second resampling.
18. Maximum utterance 30 seconds.
19. 960000-byte input bound, current/latest snapshots bounded as documented.
20. Explicit/gap discontinuity flagged; no invented replacement audio.
21. Partial is a best-effort immutable snapshot, not stable incremental tokens.
22. QA partial requests every 1000 ms, PCM delivery every 20 ms.
23. In-flight plus latest pending partial only; fake verifies latest snapshot size.
24. Final explicitly snapshots complete audio and closes input.
25. Pending partial replaced; in-flight partial aborted/suppressed before Final.
26. Cancel invalidates, clears input/pending and requests cooperative abort.
27. Pinned upstream provides abort and encoder-begin callbacks; both used.
28. Default inference 60s / model operation 120s; late completion also checks deadline.
29. Generation/utterance/deadline gates suppress stale partial and final results.
30. Explicit typed errors, including EmptyAudio; backend exceptions converted.
31. auto/en/zh/ja/ko; invalid code is an error, no silent fallback.
32. Whisper translation-to-English mode disabled.
33. https://github.com/ggml-org/whisper.cpp.
34. 48f628a84833905ee4a0658ee6d4a5c915ce1997, v1.8.7.
35. Explicit TRANSLATOR_WHISPER_CPP_DIR; no configure network work.
36. MinGW 13.1 PASS with scoped missing-SDK declaration shim.
37. CPU only, four threads; no GPU/VAD/BLAS/OpenMP.
38. Multilingual ggml-base.bin.
39. 147951465 bytes, approximately 141.10 MiB.
40. SHA256 60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe.
41. Ignored .cache/models/ggml-base.bin, Unicode-path QA copy also ignored.
42. Model not committed.
43. No application/Probe/configure automatic model download; one-time explicit QA preparation.
44. Primary repeat model load 145 ms; other observed loads 68-156 ms.
45. One load for five utterances, repeat same path is a no-op.
46. Pinned upstream samples/jfk.wav used externally; not added to project source.
47. Actual speech duration 11.000s.
48. English speech, with auto and explicit en paths tested.
49. Auto detection en, consistent with sample.
50. First non-empty Partial PASS (And so).
51. Primary repeat run 27 non-empty Partials.
52. Five Final results PASS.
53. All five Finals semantically match JFK speech/keywords.
54. First actual Partial 1810 ms, first whole-utterance Final 1920 ms.
55. Warm Finals 1982/1836/1754/1798 ms; median 1817, mean 1842.5.
56. Warm Final median RTF 0.16518, mean 0.1675; not end-to-end latency.
57. Five completed utterances in one process.
58. Same loaded context reused; model count remains one.
59. Real 100ms cancellation and fake ignoring-cancel tests PASS.
60. No Partial/Final from cancelled session emitted afterward.
61. Real next utterance produces correct Partial/Final without model reload.
62. Fake timeout, late result and delayed-owner-loop checks PASS.
63. Missing/directory/corrupt/exclusively locked automated tests; real 1MiB-truncated model safe.
64. Real 2s silence lifecycle PASS, but hallucinated you: Known Limitation, no filter.
65. Empty utterance explicit EmptyAudio, tested.
66. Real Chinese/spaces model path load and recognition PASS through QFile loader.
67. Coarse five-Final memory samples show no linear growth; not a leak-proof claim.
68. Primary normal join/unload 20 ms; observed active-inference exit 761 ms.
69. No external ASR helper; worker joined, backend destroyed on worker; Probe exits.
70. Two normal core suites, one optional real-API/model-free contract suite.
71. Standard Release all-target build PASS.
72. Standard Debug all-target build PASS.
73. Normal Release/Debug each 21/21 PASS, retaining original 19 suites.
74. whisper-enabled Release/Debug all-target build PASS; each 22/22 PASS.
75. No real model required by any standard CTest.
76. Phase 8A source/test files not modified.
77. OCR not modified.
78. Paddle/helper/protocol not modified.
79. Realtime OCR not modified.
80. Translation backend not modified.
81. TranslationCoordinator not integrated or modified.
82. TranslationWindow not integrated or modified.
83. No microphone live integration.
84. No loopback live integration.
85. No VAD or silence segmentation.
86. Packaging not modified; no portable ASR runtime or model added.
87. License/release files and pre-existing untracked license work untouched.
88. README documents Local ASR Backend, not Realtime Audio Translation.
89. This document records architecture, pinned dependency, hash, QA and limitations.
90. Final tracked-worktree status reported after commit/push; initial untracked items retained.
91. All three pre-existing untracked items preserved and excluded from staging.
92. Actual commit hash is supplied in the final response (not embedded in its own commit).
93. Actual non-force origin/main push outcome is supplied in the final response.
94. Original LunaTranslator status checked clean; no source changes.
95. Phase 8C not started.
96. Silence hallucination, unstable partials, cooperative cancellation and validation limits above.
97. Future 8C only: bounded capture handoff, explicit boundary policy, Final-only translation and independent stale guards.
