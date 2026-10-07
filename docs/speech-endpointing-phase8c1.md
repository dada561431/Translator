# Speech Endpointing and Latency - Phase 8C.1

## Acceptance and Scope

Baseline: `c77cac13c06a822f764410daa821392efd19de34`. Date: 2026-10-07.
Work is confined to Translator, single agent, no new worktree. The initial
untracked `licenses/`, `mic-test.txt`, `scripts/prepare_qt_licenses.py` are
untouched and excluded from the commit. LunaTranslator remains read-only.

**Phase 8C.1: PASS for the tested environment and utterance set.**
**Phase 8C Core Acceptance: PASS on the explicitly tested Realtek microphone
and system loopback**, superseding its historical PARTIAL microphone acceptance.
This is not universal microphone/model accuracy, complete audio UX, or packaging
acceptance. Ordinary Translator startup remains OCR-only, with no audio acquisition.
Phase 8D is not started.

The owner previously reported inaccurate English recognition and long waits after
speaking; fixed non-overlapping 4-second cuts could split syllables, mix long silence
with short words, and send silent windows to Whisper. This task changes segmentation
before ASR, not the recognition model or textual output. No text hallucination filter,
confidence filter, denoise, echo cancellation or third-party/neural VAD is added.

## Architecture and State Machine

```text
Unmodified AudioInputCoordinator / microphone or WASAPI
  -> Audio::PcmChunk (16 kHz mono signed int16 LE, 20 ms, 640 bytes)
  -> independent SpeechEndpointDetector (Qt Core types only)
       Idle -> MaybeSpeech -> Speech -> TrailingSilence
                | failure     ^              |
                v             | active       | sustained silence
               Idle           +--------------+ -> sealed PCM utterance
       continuous speech at safety cap -> forced seal -> next Speech buffer
  -> AudioTranslationCoordinator: active + latest pending utterance
  -> unmodified AsrCoordinator / whisper.cpp CPU
  -> Final -> Original -> unmodified TranslationCoordinator -> DeepL -> Translation
```

The detector is in `src/audio/SpeechEndpointDetector.*`, built into the independent
`TranslatorAudioPipeline` processing library, not the capture backend library.
It includes no ASR, translator, screen capture or UI interfaces. Configuration,
explicit states, observations and sealed utterances are independently testable.
`configure` rejects invalid values and leaves the previous valid configuration intact.

RMS is `sqrt(sum((littleEndianInt16 / 32768.0)^2) / 320)`; Peak is the maximum
absolute normalized sample. Original bytes, session, sequence, timestamp and
discontinuity are forwarded without gain changes, resampling or synthetic padding.

The idle-only noise estimate is a slow EMA with alpha 0.01, minimum floor 0.00001.
Upward innovations are clipped to the current end threshold; possible speech freezes
adaptation. Start RMS = min(0.08, max(0.002, noiseFloor * 3)). End/hold threshold
is start RMS * 0.6. Consecutive start-active chunks confirm onset; lower hold energy
prevents rapid state oscillation. Abrupt sustained noise/music is not classified
as non-speech: this is **energy endpointing, not a speech-classification VAD**.

| Selected baseline parameter | Value |
| --- | --- |
| Pre-roll | 300 ms / at most 15 chunks |
| Consecutive start confirmation | 100 ms / 5 chunks |
| Minimum active speech | 250 ms, rounded up to 260 ms by 20 ms chunks |
| Trailing silence | 600 ms / 30 consecutive low-energy chunks |
| Maximum utterance including pre-roll/trailing PCM | 12000 ms / 600 chunks |
| Minimum start RMS | 0.002, selected after real Realtek QA |
| Noise ratio / end multiplier | 3 / 0.6 |
| Noise minimum / EMA / maximum threshold | 0.00001 / 0.01 / 0.08 |

The pre-roll ring is frozen during MaybeSpeech; confirmed onset concatenates all
prior pre-roll AND all confirmation chunks, preserving the first syllable. A failed
candidate returns to the bounded ring. Minimum speech counts active audio, not
pre-roll or trailing silence. At maximum duration a new buffer continues immediately
without overlap or dropping the next chunk. Insufficient short noise is discarded
before ASR, without inspecting any recognized text.

Stop cancels unsealed speech, clears pre-roll/candidate/current/pending/active state,
invalidates identities and translation, and keeps the ASR model loaded. Start resets
to Idle. Session changes reset detector buffers; discontinuity within one session
continues received audio and preserves its marker for ASR. No missing PCM is inserted.
The existing Final-only, duplicate/stale/out-of-order and bounded backpressure rules
remain: one active inference, one latest pending segment, no cancellation of an
active Final merely because new speech ends. Partial triggers zero translations.

## Clocks, No-Packet Intervals and Bounds

Speech end is estimated from the last hold-active chunk end, not the seal timer.
Probe reports `speech_end_detection_delay_ms`, `speech_end_to_asr_final_ms`,
`speech_end_to_translation_ms`, ASR processing and translation time separately.
These are monotonic-clock estimates, not annotated human/acoustic ground-truth times.
Result time is immediately before synchronous UI signal delivery, not pixel-paint time.
Queueing, device buffers, 20 ms quantization and arrival-clock uncertainty remain.

The Phase 8A sample clock can exclude wall-clock gaps when WASAPI supplies no
packets. Detector delivery time re-anchors the estimated clock at first arrival
or after a >200 ms packet gap; raw PCM timestamps are not changed. Estimated chunk
end is clamped to its receive time. A 20 ms owner timer checks missing-packet
silence: at least 40 ms without delivery and the trailing-silence deadline must
both have elapsed. It seals existing speech only, never creates a silence utterance.
Idle pre-roll and unconfirmed candidates expire across long packet gaps.
This is a baseline: a stalled producer can look like silence, and buffering affects
clock accuracy. No hardware ADC/QPC or sample-exact wall-clock claim is made.

RMS work is O(320) per chunk; no inference/network/disk recording is done there.
Pre-roll is 9600 PCM bytes; a default current or pending utterance is at most
384000 PCM bytes plus metadata. ASR retains its existing bounded snapshots/float
conversion. No full-session recording is retained. Legacy QA alone also keeps a
bounded diagnostic detector alongside its fixed buffer. Slow inference can still
drop older pending utterances; this is not lossless transcription.

## Real Microphone QA

Explicit device: `麦克风 (Realtek(R) Audio)`, not default Camo or a virtual input.
Stable ID (hex):
`7b302e302e312e30303030303030307d2e7b31353538303264652d623365632d343338352d623039372d3161303163353138616331327d`.
Native source: 48000 Hz / 2 channels / float32 LE. Unified PCM: 16000 Hz / mono /
int16 LE / 20 ms / 640 bytes. Existing multilingual ggml-base, pinned whisper.cpp
`48f628a84833905ee4a0658ee6d4a5c915ce1997`, 4 CPU threads, en -> zh, real DeepL.
No model upgrade/download or audio recording.

Two initial exploratory runs used minimum RMS 0.0002. The owner reports repeated
reading in those runs; they are not the standard single-utterance A/B. Energy onset
could be falsely triggered by low-level noise, and background tails around
0.0003-0.0008 merged phrases. One pre-prompt `[BLANK_AUDIO]` Final occurred.
This failed setting is recorded, not hidden or removed with a text filter.
Speech RMS reached approximately 0.05; minimum RMS 0.002 was then tested explicitly.

The tuned energy run preceded the tuned legacy run; each lasted 55 seconds with
the same six QA cues, spaced 8 seconds apart. The owner explicitly confirmed:
**each phrase read once in both runs; first (energy) run completely correct;
second (legacy) run had recognition problems**. QA prompts are instructions,
not ASR input or fabricated results. Text below comes from actual local inference.
This is live repeated-speech A/B, not replay of the same recording; timing/pronunciation
differences and the small sample size limit generalization.

Latencies below are milliseconds from estimated energy speech end to Final.
For split legacy phrases, all relevant outputs are shown; the early cut and the
later last-active segment are distinguished. A later erroneous Final is not a
successful recognition latency. No-active estimates are null, not zero.

| Spoken (owner confirmed) | Legacy 4s ASR | Endpoint ASR | Legacy latency | Endpoint latency |
| --- | --- | --- | --- | --- |
| Hello | Hello. | Hello. | 2437 | 1454 |
| Translator | [inaudible]; Thank you for listening. | Translator | 875 (cut); 4394 (later wrong Final) | 1395 |
| Testing | ST; [BLANK_AUDIO] | testing | 798 (cut); 4783 (later wrong Final) | 1379 |
| Good morning | Good morning. | Good morning. | 1129 | 1420 |
| How are you today? | How are you today?; See you. | How are you today? | 809 (cut); 3848 (later wrong Final) | 1417 |
| This is a microphone recognition test. | This is a macro from the donation test. | This is a microphone recognition test. | 2024 | 1413 |

Endpoint: 6 independent utterances, 6 Finals, 6 real translation requests, 6 successful
Chinese translations, 12 visible-label checks true, zero drops, one model load (70 ms).
All six source phrases match ignoring punctuation/case; short words 3/3 versus
legacy 1/3. Legacy: 13 Finals/requests/translations, including five `[BLANK_AUDIO]`
and other incorrect pieces, despite only six spoken phrases. No outputs were filtered.

| Spoken | Endpoint Chinese subtitle | Detection delay | ASR processing | Translation | End -> translated subtitle delivery |
| --- | --- | --- | --- | --- | --- |
| Hello | 你好。 | 600 | 854 | 885 | 2341 |
| Translator | 译者 | 599 | 795 | 340 | 1736 |
| Testing | 测试 | 600 | 778 | 328 | 1708 |
| Good morning | 早上好。 | 600 | 819 | 323 | 1744 |
| How are you today? | 你今天好吗？ | 600 | 817 | 321 | 1739 |
| This is a microphone recognition test. | 这是一项麦克风识别测试。 | 600 | 813 | 340 | 1754 |

Energy end -> Final median 1415 ms, mean 1413 ms, range 1379-1454;
ASR median 815 ms / mean 812.67 ms; translation median 334 ms / mean 422.83 ms;
end -> translation median 1741.5 ms / mean 1837 ms.
Using the legacy segment containing each phrase's last active audio (including
unsuccessful Finals), end -> Final median 3142.5 ms / mean 3102.5 ms, and end ->
translation median 3496 ms / mean 3464 ms. This compares boundary waits, NOT
time-to-correct-text for legacy errors. Some early Finals occurred before completion
of a phrase. Legacy residual waits were 1609, 3488, 3989, 369, 2989, 1209 ms;
energy used approximately 600 ms. Good morning was faster in legacy in this run.
The improvement is bounded natural-end latency and complete utterances, not every
individual sentence being faster. The microphone semantic/E2E gate is accepted
for this explicit device and these six independently spoken samples.

## Real Loopback A/B and Lifecycle

Real WASAPI output capture from `耳机 (Realtek(R) Audio)`, 48000 Hz stereo float32,
using normal QMediaPlayer playback of the existing official short `jfk.wav` fixture.
No injection directly into ASR, generated speech, new download or captured recording.
System audio is distinct from microphone capture.

Expected speech: "And so my fellow Americans, ask not what your country can do
for you, ask what you can do for your country."

| 25-second live playback run | Legacy 4s | Energy |
| --- | --- | --- |
| Finals / real translations completed | 5 / 5 | 1 / 1 |
| Speech result | Three fragments: And so my fellow Americans ask / not what your country can do for you. / Ask what you can do for your country. | Complete expected sentence |
| Silent tail Finals | 2: you / you (another silent boundary was cancelled at Stop) | 0 |
| Final sentence-tail end -> Final | 1793 ms | 1537 ms |
| Final sentence-tail end -> translation | 2196 ms | 2588 ms |
| Detection delay for sentence tail | 919 ms | 579 ms (sample/arrival timing estimate) |

The first two legacy fragments' 848/870 ms latencies are partial fixed cuts during
ongoing speech, not sentence-end latency. Loopback energy preserved semantics and
eliminated idle periodic ASR, but translation was slower in this particular run
because the DeepL request took 1050 ms versus 403 ms. No network/backend tuning
was applied. The final clock-clamp refinement is validated by the subsequent warm
pause runs (599-600 ms confirmation) and automated tests.

Six further energy cycles: each 17 seconds, explicit pause at 11000 ms, 500 ms
Stop-wait-Start. All six complete JFK sentences recognized, six successful DeepL
translations, six pauses, no post-speech silent Finals or drops, **one model load
in the entire process (67 ms)**. Expected punctuation differences do not change
semantics. Pause tails then warm restart exercise the actual loopback path.

Close QA additionally exercises exit with inference pending and a real translation
request pending; capture stops, ASR is cancelled/joined and no Probe remains after
exit. No Python helper is introduced by ASR. OCR helper lifecycle is unchanged.

## Tests and Reproduction

All old tests remain. `phase8c1_speech_endpoint` covers 10 s zero silence, 40 ms
click, minimum-active rejection, 300 ms short word, full pre-roll/confirmation
PCM, 580/599/600 ms trailing boundary, exactly-once sealing, two 12 s forced
utterances and continuous successor, reset, session change, discontinuity,
low/rising higher background, relative energy, hysteresis, no-packet timeout,
wall-clock re-anchor, expiring stale pre-roll/candidates and config validation.

`phase8c_audio_translation` retains the entire fixed-mode routing/stale/error/
backpressure suite and adds energy routing: 10 s zero input -> zero ASR calls and
zero translation requests while Running; one ended burst -> one Final request;
Partial and duplicate Final -> zero extra requests; Stop during speech -> zero new
inference; restart -> empty detector and one model load. Hardware/model/DeepL tests
are explicit probes, not dependencies of CTest. Synthetic active audio is not a
claim to prove speech recognition or music classification.

| Configuration | Configure / build | CTest |
| --- | --- | --- |
| build/phase61b (Release) | PASS | 23/23 PASS |
| build/phase61b-debug (Debug) | PASS | 23/23 PASS |
| build/phase8b-whisper (Release) | PASS | 24/24 PASS |
| build/phase8b-whisper-debug (Debug) | PASS | 24/24 PASS |

Commands use existing caches (Qt/MinGW/Ninja paths are local shell settings, not
new hard-coded CMake dependencies):

```powershell
$env:PATH = 'D:/QT/Tools/mingw1310_64/bin;D:/QT/6.11.2/mingw_64/bin;D:/QT/Tools/Ninja;' + $env:PATH
# Repeated for each build directory in the table:
& D:/QT/Tools/CMake_64/bin/cmake.exe -S . -B build/phase61b
& D:/QT/Tools/CMake_64/bin/cmake.exe --build build/phase61b -j4
& D:/QT/Tools/CMake_64/bin/ctest.exe --test-dir build/phase61b --output-on-failure
& ./build/phase61b/TranslatorAudioCaptureProbe.exe --list
& ./build/phase8b-whisper/TranslatorAudioTranslationProbe.exe --kind microphone --device <explicit-hex-ID> --model .cache/models/ggml-base.bin --provider deepl --seconds 55 --phrase-cues --endpointing energy --minimum-rms 0.002
# Repeat with --endpointing legacy-fixed --segment-seconds 4.
& ./build/phase8b-whisper/TranslatorAudioTranslationProbe.exe --kind loopback --model .cache/models/ggml-base.bin --provider deepl --seconds 25 --play .cache/third_party/whisper.cpp/samples/jfk.wav --play-once --endpointing energy
# Repeat with legacy-fixed. For warm pause: --seconds 17 --cycles 6
# --pause-after-ms 11000 (without --play-once).
```

Probe supports duration/RMS/ratio tuning, legacy-fixed comparison, explicit device,
QA-only timed cues and optional `--verbose-endpointing`. Normal output is lifecycle/
boundary/result/timing only; per-chunk RMS appears only with the verbose flag.
All logs/transcripts remain under ignored `.cache/phase8c1`, never committed.
The transient Release linker permission error occurred while its Probe executable
was running; the completed capture was not terminated. Relinking after normal
exit succeeded. Optional Vulkan-header discovery warning does not prevent builds.

## Limitations and Next Step

- This is energy detection, not a neural VAD. Sudden loud background, sustained
  keyboard/microphone noise and music can still create utterances/hallucinations.
- Learned quiet noise and tested gain are not representative of every device;
  low-gain microphones may need a different validated RMS floor. No tuning UI yet.
- Natural within-sentence pauses >=600 ms can split a sentence; max duration can
  still cut a long word. No overlap or context stitching is added.
- Timing is an energy/sample-arrival estimate, not hardware speech-end annotation.
- Whisper base quality and network latency remain separate limitations. No universal
  low-latency accuracy/product completeness claim, no audio packaging.
- Recommended next step: independently collect a larger owner-annotated natural
  microphone corpus across gain/background conditions and repeat controlled
  endpoint/audio-quality comparison. Evaluate model alternatives only as a separate
  authorized task if errors persist; do not switch the model in this phase.

## Requested Closeout Ledger

| Item | Answer / evidence |
| --- | --- |
| 1 | Phase 8C.1 PASS for tested device/set, with limitations above |
| 2 | Phase 8C Core moves PARTIAL -> PASS for accepted microphone + loopback |
| 3 | Initial commit c77cac13c06a822f764410daa821392efd19de34 |
| 4 | Modified CMakeLists, AudioTranslationCoordinator h/cpp, AudioTranslationProbe/Test, README, Phase 8C doc |
| 5 | Added SpeechEndpointDetector h/cpp, SpeechEndpointDetectorTest, this document |
| 6 | Independent PCM boundary processor before existing ASR |
| 7 | Idle / MaybeSpeech / Speech / TrailingSilence |
| 8 | RMS normalized LE int16 / 32768, 320 samples |
| 9 | Idle-only clipped slow EMA, minimum floor |
| 10 | max(minimum RMS, noise * ratio), capped maximum |
| 11 | 100 ms confirmation |
| 12 | 300 ms pre-roll, bounded and first syllable retained |
| 13 | 250 ms configured / 260 ms active chunks |
| 14 | 600 ms silence |
| 15 | 12 s forced boundary, continuous successor |
| 16 | Selected configuration table; RMS 0.002 tested on Realtek |
| 17 | Default fixed-4s audio boundaries removed |
| 18 | legacy-fixed QA retained |
| 19 | 10 s zero PCM: 0 ASR calls |
| 20 | 10 s zero PCM: 0 translation requests |
| 21 | 40 ms click rejected; real exploratory noise failure recorded |
| 22 | 300 ms sustained synthetic burst accepted |
| 23 | Full pre-roll and candidate PCM verified |
| 24 | Hysteresis test PASS |
| 25 | Forced 12 s bounds test PASS |
| 26 | Explicit Realtek internal microphone, stable ID above |
| 27 | 48000 Hz stereo float32 LE |
| 28 | Hello -> Hello. |
| 29 | Translator -> Translator |
| 30 | Testing -> testing |
| 31 | Good morning -> Good morning. |
| 32 | How are you today? -> How are you today? |
| 33 | This is a microphone recognition test. -> exact phrase |
| 34 | Six-row live A/B table, exploratory failures retained |
| 35 | Legacy last-active segments: median 3142.5 ms, errors not successful latency |
| 36 | Energy end -> Final median 1415 ms |
| 37 | Microphone confirmation 599-600 ms |
| 38 | ASR median 815 ms |
| 39 | Translation median 334 ms; end -> translation median 1741.5 ms |
| 40 | Real microphone semantics accepted by owner for six phrases |
| 41 | Six real DeepL requests/successes |
| 42 | Six Original + six Translation visible label checks; owner confirmation |
| 43 | Live loopback complete sentence PASS |
| 44 | Energy playback/paused tail: zero extra silent Finals |
| 45 | JFK complete expected sentence, no lost semantics |
| 46 | Zero PCM does not reach ASR; nonzero noise can still do so |
| 47 | No hardcoded text filter |
| 48 | Partial translation count 0 |
| 49 | Exactly one request per accepted nonempty Final; six in standard energy mic run |
| 50 | One model load per process; six warm cycles retained one |
| 51 | Five real Stop/Start transitions PASS |
| 52 | Existing stale/out-of-order/duplicate tests PASS |
| 53 | Stop during speech cancels, no forced network submission |
| 54 | Restart Idle/empty; model warm |
| 55 | O(320) per chunk; small synthetic suite ~0.1 s Release including startup; no isolated CPU percentage claim |
| 56 | Bounded 15-chunk ring + 600-chunk utterances; bounded ASR/pending |
| 57 | Normal Release PASS |
| 58 | Normal Debug PASS |
| 59 | Normal CTest 23/23 each |
| 60 | Whisper Release PASS |
| 61 | Whisper Debug PASS |
| 62 | Whisper CTest 24/24 each |
| 63 | Phase 8A backend/converter files unchanged; new processing files only |
| 64 | Phase 8B ASR core/backend/pin/model unchanged |
| 65 | OCR unchanged |
| 66 | DeepL unchanged |
| 67 | Translation coordinator/providers/factory unchanged |
| 68 | Packaging unchanged |
| 69 | No production Audio UI; six-prompt window is QA-only |
| 70 | No VAD dependency |
| 71 | README updated only after real acceptance |
| 72 | This document contains architecture, parameters, A/B, tests and limits |
| 73 | Final tracked changes committed; preexisting untracked retained; transport/status reported in task closeout |
| 74 | Existing untracked items preserved and excluded |
| 75 | Exact commit hash in task closeout / git log; message Improve audio speech endpointing and latency |
| 76 | Non-force origin/main push; final result in task closeout |
| 77 | LunaTranslator read-only, status checked at closeout |
| 78 | Phase 8D not started |
| 79 | Energy/noise, gain, pauses, sample timing, base model, network and incomplete UX limitations |
| 80 | Larger annotated controlled audio-quality/endpoint comparison; no automatic next phase/model switch |
