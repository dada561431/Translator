# Streaming ASR Accuracy And Clean Loopback - Phase 8D.1A

Date: 2026-10-07. Initial commit: `0b3c3878f03b065c651342a74c00cc5e92726918`.
Single agent, existing checkout only. LunaTranslator is read-only.

## Scope And Baseline

This is standalone QA, not production integration. Phase 8D.1 demonstrated
incremental microphone text but remained PARTIAL: accuracy was weak, controlled
loopback was missing, and speech-boundary latency was not precisely annotated.
Its ~600 ms update cadence and ~1.27 s first output were measured from audio
start, not a universally applicable speech-start latency.

Production input/controller, overlay, settings, tray, audio capture, Whisper,
OCR, endpoint detector, translation and packaging are unchanged. The Probe links
neither translation providers nor the production pipeline. DeepL/OpenAI requests
are zero. No correction, denoising, echo cancellation or new VAD is used.

## Fixed Candidates

sherpa-onnx remains **v1.13.8 / `11afbd009a7f8c08f4bcf2fc1b265d0df4670fbf`**.
ORT remains app-local CPU **1.28.2**, public C API, telemetry disabled.
The previously prepared runtime is reused; no dependency upgrade was performed.

- A: `sherpa-onnx-streaming-paraformer-bilingual-zh-en`, original int8 files,
  HF revision `8e40c43232a1c5c66c82111efc5820d3accca11b`.
- B: `sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20`, streaming
  Transducer, official int8 recipe: encoder/joiner int8, decoder FP32, HF revision
  `98590b7ed6443e77b714204da2757d75e1a642f4`.

[Official online Transducer documentation](https://k2-fsa.github.io/sherpa/onnx/pretrained_models/online-transducer/zipformer-transducer-models.html)
lists B as Chinese/English streaming. The
[pinned publisher model card](https://huggingface.co/csukuangfj/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/tree/98590b7ed6443e77b714204da2757d75e1a642f4)
declares Apache-2.0 and links the original model/training code. No Zipformer2 CTC
substitution, beam search, third candidate, cloud ASR or Whisper comparison is
used. This QA does not approve model/runtime redistribution or provide legal advice.

| Model/file | Bytes | SHA256 |
| --- | ---: | --- |
| A encoder.int8.onnx | 165462184 | `81a70226a8934e6ed92aa1d4fc486b428b5398e2f2619ed4897b7294cab90e9a` |
| A decoder.int8.onnx | 71664561 | `f3cca9f77bb9d93c8fcbfb63ae617b6b1ee96818df3aa3b151c40658fe38594f` |
| A tokens.txt | 75756 | `59aba8873a2ed1e122c25fee421e25f283b63290efbde85c1f01a853d83cb6e6` |
| B encoder-epoch-99-avg-1.int8.onnx | 181895032 | `8fa764187a261844f859d7143ebaa563af5d10adfece4c18a8f414c88cba2a9b` |
| B decoder-epoch-99-avg-1.onnx | 13876452 | `2e3b5ec371f8899ee6acd829fd753ba45772df57a91bdf37cde3136354e7db7d` |
| B joiner-epoch-99-avg-1.int8.onnx | 3228404 | `1ed689c5ed19dbaa725d9d191bb4822b5f4855a39e1ffd28cbc1f340d25b2ee0` |
| B tokens.txt | 56317 | `a8e0e4ec53810e433789b54a5c0134a7eaa2ffca595a6334d54c00da858841d3` |

A total: **237202501 bytes / 226.21 MiB**. B total: **199056205 bytes / 189.83 MiB**.
Downloaded B ONNX hashes match the pinned publisher LFS object hashes.
All weights/runtime/audio/JSONL are ignored local `.cache/` inputs, never committed.

A preliminary B run used `decoder-epoch-99-avg-1.int8.onnx` (13091040 bytes,
SHA256 `1a70c593d71e53f023f5f55b0b4cfff5055abb786ee3992e5f63dc2e273cc4fa`).
It produced English WER mean 9.88%, Chinese CER mean 21.87%, with repeated Chinese
characters. On reviewing the official int8 example, the decoder was corrected
to its recommended FP32 file. The preliminary trace remains local and is not
silently replaced or represented as the official recipe. This is the same B
model identity, not a third streaming candidate or a sherpa upgrade.

## Corpus And Evaluation Method

The tracked `tools/streaming_asr/corpus-phase8d1a.json` fixes **10 English and
10 Chinese** references, covering short phrases, ordinary/long sentences,
numbers and Qt/streaming terminology. References precede model inference.
`scripts/prepare_streaming_asr_corpus.ps1` explicitly prepares local Windows
offline TTS using Zira Desktop / Huihui Desktop at 16k mono PCM16LE.
This is a controlled synthetic-source test, **not human accuracy evidence**.
Real microphone QA is separate and its recorded PCM is reused for fair A/B.
Qt pronunciation/acronym spelling can affect the literal score; no dictionary
normalization is applied to conceal that difference.

Both models receive identical PCM, 20 ms chunks, real-time pacing, CPU provider,
2 threads, greedy search, endpoint rules 2.4/1.2/20 s, and 300 ms Stop-only
synthetic tail. Each corpus run loads the model once; endpoint resets reuse it.
There is no offline-fast latency ranking. Inter-utterance gaps allow Final to
be associated with independently fixed windows, not ASR-derived references.

Speech boundaries are **waveform-assisted**, 5 ms RMS frames: threshold 0.0005
for clean TTS; initial microphone annotation used 0.002, the confirmed retest
uses 0.005 after independently inspecting quiet noise and waveform clusters.
The manifest stores exact
frame/sample-relative start/end values, not file-end or cue-onset shortcuts.
These thresholds annotate QA files only; they do not gate recognizer input,
change production endpointing, or represent a validated neural VAD. Waveform
boundaries retain threshold/framing uncertainty; they are not phonetic alignment.
Both models use the same manifest. Live capture requires a saved local PCM
manifest and verified silent gaps before precise boundary latency can be scored.

Normalization lowercases, removes Unicode punctuation and collapses spaces.
English WER is Levenshtein edit distance on word tokens / reference word count;
Chinese CER uses Unicode code points with spaces removed / reference characters.
Symbols are not silently discarded. No pinyin, semantics, spellcheck or LLM
correction replaces the score. Means/medians are per-utterance, not pooled corpus
WER/CER. Empty Final counts as deletion, unknown latency stays null.

First/middle/last pre-flush raw Partial and Final are retained per utterance.
Stable prefix is the existing two-revision common-prefix hypothesis, not model
truth. Raw model text alone is scored. Time to first raw and first stable prefix
are separate. Rewrite characters = previous suffix beyond longest common prefix;
emitted characters = new suffix length; rate = summed rewritten / summed emitted.
Resets do not count as text rewrites. A stable conflict remains visible, not repaired.

RTF is measured worker accept+decode wall compute / real input duration,
including explicit Stop flush compute but not its synthetic audio as denominator.
CPU uses Windows process times, average/250 ms peak single-core-equivalent percent.
Memory is sampled working set before/load/stream/unload, not guaranteed peak allocation.
`audio_ms` is worker-accepted PCM progress; it is **not exact decoder/token
alignment**. `progress` additionally records decode count, IsReady drained and
queue backlog. Queue high-water, zero drops and lack of sustained wall/input lag
must support throughput conclusions; RTF alone is insufficient.

## Reproduction

Use the Phase 8D.1 opt-in SDK/runtime CMake setup. No CMake/application download
is permitted. Model paths appear only in local commands.

```powershell
./scripts/prepare_streaming_asr_corpus.ps1
python scripts/benchmark_streaming_asr.py prepare --corpus tools/streaming_asr/corpus-phase8d1a.json --wav-dir .cache/phase8d1a/tts --output .cache/phase8d1a/clean.wav
./build/phase8d1-sherpa/TranslatorStreamingAsrProbe.exe --model paraformer --model-dir .cache/models/sherpa --wav .cache/phase8d1a/clean.wav --endpoint --threads 2 --chunk-ms 20
./build/phase8d1-sherpa/TranslatorStreamingAsrProbe.exe --model zipformer --model-dir .cache/models/zipformer --wav .cache/phase8d1a/clean.wav --endpoint --threads 2 --chunk-ms 20
python scripts/benchmark_streaming_asr.py evaluate --manifest .cache/phase8d1a/clean.manifest.json --log <local-log.jsonl> --output <new-local-result.json>
```

Live loopback uses `--kind loopback --device <explicit-render-id> --seconds 80
--play-wav .cache/phase8d1a/loopback-source.wav --record-wav <new-local-wav>`
with the same model/thread/endpoint options; no microphone source is opened.
`annotate-loopback --manifest <source-manifest> --recorded <capture-wav>
--log <live-jsonl> --output <new-capture-manifest>` checks quiet margins.
Replay the captured WAV with each `--model` and evaluate with that **same**
capture manifest; live outputs and replay outputs remain separately labeled.

Microphone QA uses `--kind microphone --device <explicit-Realtek-id> --seconds
124 --cue-interval-ms 12000 --cue <reference>` (repeat the cue option ten times)
and explicitly authorized `--record-wav`. Owner-confirmed repetitions/extra
speech are entered in a private independent reference/window JSON. Use
`annotate-recording --recorded <capture> --windows-json <confirmed-windows>
--annotation-rms 0.005 --output <new-manifest>`; optional waveform-reviewed
speech_start_ms/speech_end_ms must lie within the window. Do not choose these
boundaries to improve WER or omit any raw Final. A/B uses identical saved PCM
and manifest; evaluator `--semantic-review <manual-bools-json>` adds independent
yes/no judgments without changing text or scores. Outputs refuse overwrite.

Explicit `--record-wav` is live-only, requires a new file below current checkout
`.cache/phase8d1a/`, refuses overwrite, streams PCM to disk and finalizes its
WAV header on normal Stop. It is opt-in only; nothing is recorded by default.
Do not record unrelated audio or enable it in production. Probe WAV limit is
300 s to support a multi-utterance session with one model load.

## Acceptance Results

**Phase 8D.1A: PARTIAL for microphone accuracy / production readiness.**
The measurement task is complete: fixed 20-reference WAV, clean live loopback,
and owner-confirmed microphone same-PCM A/B are available. Clean loopback PASS
does not conceal weak microphone English. Phase 8D.1 and Phase 8D remain PARTIAL.
No production integration or production accuracy PASS is asserted.

### Same-Input Clean WAV

`clean.wav`: 139780 ms, SHA256
`8f02a1200b1ad70640feb63348fc2eb929465ff2557b1a3bc85437d43b9e5643`.
Both final runs use that exact file and identical manifest. This is a **clean
synthetic WAV** result, not a clean live loopback result.

| Metric | Paraformer A | Official-recipe Zipformer B |
| --- | ---: | ---: |
| English WER mean / median, n=10 | 15.19% / 0% | 8.97% / 0% |
| Chinese CER mean / median, n=10 | 0% / 0% | 1.54% / 0% |
| English normalized exact | 6/10 | 7/10 |
| Chinese normalized exact | 10/10 | 9/10 |
| Speech start -> first raw median | 788.57 ms | 516.06 ms |
| First raw p90, nearest rank n=20 | 888.17 ms | 725.42 ms |
| Within-utterance update median | 600.62 ms | 320.49 ms |
| Speech start -> first stable median | 1353.56 ms | 939.16 ms |
| Speech end -> matching endpoint median | 1496.34 ms | 1432.10 ms |
| Speech end -> Final median | 1496.39 ms | 1432.15 ms |
| Rewrite chars / emitted chars | 0 / positive (rate 0) | 0 / positive (rate 0) |
| RTF, accept+decode compute / 139780 ms | 0.05984 | 0.05235 |
| Average CPU, single-core-equivalent | 23.15% | 37.86% |
| Sampled peak CPU, same 250 ms method | 81.58% | 88.21% |
| Working set before model | 21610496 bytes | 21618688 bytes |
| Working set after load | 318492672 bytes | 274649088 bytes |
| Sampled max streaming working set | 329895936 bytes | 290246656 bytes |
| Working set after unload | 58327040 bytes | 56578048 bytes |
| Model load time / count | 1205 ms / 1 | 1543 ms / 1 |
| Queue high-water | 40 ms | 20 ms |
| Drops / final backlog | 0 / 0 ms | 0 / 0 ms |
| Max feed lateness | 24 ms | 2 ms |
| Join + unload | 60 ms | 43 ms |

The machine has 16 logical CPUs; average process CPU normalized to the full
machine is approximately 1.45% / 2.37%. Peak samples depend on scheduler/process
time granularity. RTF denominator includes inter-utterance silence; this is the
same paced corpus for both candidates, not a speech-only RTF. Queue/progress
returns to zero, IsReady is drained, and accepted PCM progresses with wall time;
no growing backlog was observed. Loaded memory was measured in separate
processes, never by loading both candidates simultaneously. Residual working
set after unload includes runtime/Qt allocations, not proof of a model leak.

Below, S is an independent conservative **agent manual semantic judgment** of
reference versus raw Final, not owner-confirmed microphone accuracy. Orthographic
differences still count in WER; they are not repaired. All 20 literal references
are in the tracked corpus JSON; every raw Final and first/middle/last Partial is
in the ignored `clean-paraformer-reviewed-v3.json` /
`clean-zipformer-reviewed-v3.json` below `.cache/phase8d1a/`.

| ID | A WER/CER | B WER/CER | A S | B S |
| --- | ---: | ---: | --- | --- |
| en01 | 0% | 0% | yes | yes |
| en02 | 0% | 0% | yes | yes |
| en03 | 0% | 0% | yes | yes |
| en04 | 42.86% | 28.57% | no | yes |
| en05 | 0% | 11.11% | yes | no |
| en06 | 0% | 0% | yes | yes |
| en07 | 16.67% | 0% | no | yes |
| en08 | 0% | 0% | yes | yes |
| en09 | 83.33% | 50% | no | no |
| en10 | 9.09% | 0% | no | yes |
| zh01 | 0% | 0% | yes | yes |
| zh02 | 0% | 0% | yes | yes |
| zh03 | 0% | 0% | yes | yes |
| zh04 | 0% | 0% | yes | yes |
| zh05 | 0% | 0% | yes | yes |
| zh06 | 0% | 15.38% | yes | no |
| zh07 | 0% | 0% | yes | yes |
| zh08 | 0% | 0% | yes | yes |
| zh09 | 0% | 0% | yes | yes |
| zh10 | 0% | 0% | yes | yes |

A truncates `recognition test` / `recognition` / `playing` to missing/incomplete
words in several English Finals. B preserves those endings but has `QUID` for
`quick`, `CUTE` for `Qt`, and Chinese `刘士` for `流式`. `real time` versus the
fixed `realtime`, and `across platform` versus `cross-platform`, affect literal
WER substantially. These effects are disclosed, not normalized away. Manual
semantic counts: A English 6/10, Chinese 10/10; B English 8/10, Chinese 9/10.

Representative raw/stable sequence, en04:

```text
A raw: this -> this is a real time speech -> this is a real time speech recognition
A last stable: this is a real time speech
A Final: this is a real time speech recognition
B raw: THIS IS -> THIS IS A REAL TIME SPEECH -> THIS IS A REAL TIME SPEECH RECOGNITION TEST
B last stable: THIS IS A REAL TIME SPEECH RECOGNITION
B Final: THIS IS A REAL TIME SPEECH RECOGNITION TEST
```

zh05 A: `系` -> `系统应该在音频播放` -> full reference; B starts at `系统` and
reaches the same full reference. Both last stable prefixes end at `持续显示`,
not the final `文字`. Stabilization delays first display and must not replace raw
model accuracy. No real rewrite was seen here; fake tests cover rewrites and
stable conflicts, so zero rewrite on these voices is not a universal guarantee.

Both candidates produce >=2 pre-speech-end updates for longer sentences.
The 440 ms `Hello` clip completes before either first Partial; do not advertise
all short utterances as incremental. Source start -> first raw of the first
utterance is 1877.90 ms / 1714.72 ms, versus its waveform speech onset at 1095 ms.
This illustrates why audio-start and speech-start latency must remain separate.

### Initial Live Microphone: Invalid Input (Historical Attempt)

The owner authorized explicit local QA capture and one reading of five English
and five Chinese prompts. The Probe used **麦克风 (Realtek(R) Audio)**, stable ID
`7b302e302e312e30303030303030307d2e7b31353538303264652d623365632d343338352d623039372d3161303163353138616331327d`.
Native: 48000 Hz / stereo float32 LE; unified PCM unchanged at 16k mono PCM16LE.
Prompts: en03/en04/en06/en08/en10/zh03/zh04/zh05/zh07/zh10, every 12 s, 124 s run.

Actual trace: initial peak 0.005676 before cues, then decaying noise; essentially
zero PCM during cue windows. All Finals empty. Drop count 0, load count 1,
queue high-water 20 ms, RTF 0.04863. Waveform annotation correctly rejects the
recording because it has no speech activity. **Do not score this as WER/CER 100%
for spoken ground truth or infer a model defect.** No verified spoken utterance
from this round is available for fair A/B replay. Recording/trace stay ignored.

That initial attempt required owner confirmation and was not scored. The owner
subsequently selected this default Realtek device, unmuted it, and confirmed the
valid retest below. No capture backend bug is confirmed; capture code is unchanged.

### Live Loopback: Contaminated Input, Not Clean PASS

Both 80 s runs used **耳机 (Realtek(R) Audio)**, stable render ID
`7b302e302e302e30303030303030307d2e7b35643632303235362d333163662d343262612d626332622d6561613835616437643833367d`.
Native 48000 Hz / stereo float32 LE, unified 16k mono PCM16LE. Selected backend
is SystemLoopback; **no microphone backend was started**. Local source is the
same 73660 ms fixed-corpus WAV, SHA256
`9e8a459d71737b3484c82bd19823ade7aede98be18cd96cdee3aa844f3dcf568`.
It contains the same 5 English + 5 Chinese references as the microphone cues.

The owner was asked to pause other playback, but actual captures still contain
unrelated Chinese chat: A includes `你们看比赛去吧` / `我有啥办法呀`; B includes
game/chat language unrelated to any reference. Background energy extends through
the intended silent margins. Do not trust waveform speech boundaries in those
mixtures. The annotator now rejects missing silent margins rather than presenting
them as precise clean-speech measurements. This is **QA annotation validation**,
not an audio filter, ASR text filter or production VAD change.

Both runs have genuine online activity, but that is not sufficient for clean
loopback acceptance. The mixed recordings are not used to rank accuracy or report
valid clean-loopback WER/CER. They are also separate physical capture sessions,
not identical PCM A/B. Same-input isolated captured-PCM replay is now completed
below. Preliminary mixed-input JSON scores remain invalid for acceptance.

A/B diagnostic RTF: 0.05136 / 0.04558; queue high-water 120 / 40 ms; drops 0 / 0;
end backlog 0 / 0; each has one initial capture discontinuity and one model load.
Initial discontinuity resets the stream and is not swallowed. No microphone
pickup, forced application closing/muting or fabricated isolation confirmation.
The initial attempts were invalid; they are retained only as diagnostic history.

### Isolated Loopback Retest

After the owner replied that retesting could begin, a new 80 s Zipformer run
used the same explicit Realtek render ID and source. The stored capture
`loopback-retest.wav` passes the independent waveform quiet-margin check in all
ten utterance windows. The ten raw Finals match the fixed references exactly:
English 5/5, WER mean/median 0%; Chinese 5/5, CER mean/median 0%. No unrelated
chat text appears in this trace. The microphone backend was not opened.

Speech-start -> first raw median 541.52 ms, p90 701.36 ms; update median 319.93 ms;
first stable median 962.80 ms; speech-end -> endpoint/Final medians
1411.40/1411.45 ms. Speech boundaries come from captured PCM, with 5 ms waveform
framing plus live capture/wall-clock alignment uncertainty, not token timestamps.
All utterances have a pre-end Partial; longer sentences have 6-9 pre-end updates.

RTF 0.04603, average process CPU 32.12% single-core-equivalent, queue high-water
60 ms, drops 0, end backlog 0. One initial discontinuity resets the stream before
speech; model load count 1, load 1668 ms, sampled streaming working set
294084608 bytes, joined/unloaded in 43 ms, exit 0. **Zipformer clean live
loopback incremental acceptance PASS.** Same-input captured-PCM replay through
both candidates is now complete; results follow. This live result alone is not
used to declare microphone accuracy PASS.

A further 80 s live Paraformer retest was attempted after fair replay. It ended
normally (one load, zero drops, zero final backlog, join/unload 51 ms) but included
unrelated Chinese speech at the start (`那我没有人能往前走而且垃点太`). The silent-margin
validator rejected it. This additional attempt is **not clean live A PASS**, is
not scored for accuracy/latency, and does not replace the valid isolated B capture
or its identical-PCM A/B replay. No application was forcibly muted or closed.

### Same Captured Loopback PCM A/B

Both candidates replayed `loopback-retest.wav` (79980 ms) with identical pacing,
threads, endpoint settings and captured-waveform manifest. SHA256:
`d400ca4c354caf3e744be401e46e2a3c81d59e8b7bb7067ad2e30d5859c5f79b`.
This is captured system output, not microphone pickup; separate live runs
are not misrepresented as identical physical captures.

| Metric | A | B |
| --- | ---: | ---: |
| English WER mean / median, 5 prompts | 10.71% / 0% | 0% / 0% |
| Chinese CER mean / median, 5 prompts | 0% / 0% | 0% / 0% |
| Normalized exact | 8/10 | 10/10 |
| First raw median / p90 | 759.75 / 932.93 ms | 544.42 / 706.10 ms |
| Update / first stable median | 599.82 / 1293.59 ms | 320.13 / 965.60 ms |
| Speech end -> endpoint / Final | 1582.19 / 1582.23 ms | 1414.10 / 1414.15 ms |
| Rewrite rate | 0 | 0 |
| RTF | 0.04870 | 0.04624 |
| Average / sampled peak CPU, single-core-equivalent | 19.50% / 81.25% | 33.07% / 74.40% |
| Loaded / max / unloaded working set, bytes | 316006400 / 327462912 / 55451648 | 271585280 / 288112640 / 54472704 |
| Load ms / count | 1262 / 1 | 1640 / 1 |
| Queue high-water / drops / end backlog | 40 ms / 0 / 0 | 20 ms / 0 / 0 |

A misses `today` in en03 (25% WER) and writes `real time` instead of `realtime`
in en04 (28.57%). All remaining eight rows are exact. B is exact on all ten.
Independent agent semantic judgment: A English 4/5, Chinese 5/5; B 5/5 + 5/5.
Longer sentences have 3-5 pre-end updates for A and 6-9 for B; no growing backlog.

### Confirmed Real Microphone Retest And Fair Replay

The owner confirmed the default **麦克风 (Realtek(R) Audio)** was unmuted.
Device ID and native/unified formats are the same as the initial attempt above.
One 124 s capture, `microphone-retest.wav`, was replayed sequentially through A
and B; neither needs a second human reading. SHA256:
`92a91a9ded328ba7d29833c7429084c7853233d9d0d0ac01e3f6912cd5d0ec26`.
The owner explicitly confirmed no omissions: first `How are you today?` was
read twice, other nine cues once unchanged, and **Black** was additionally spoken
after `今天天气很好`. Both repetitions and Black are independently included in
ground truth, not model duplication/insertion errors. No ASR output supplies
the reference and no hypothesis is corrected or filtered.

There are five English prompt windows (first contains two spoken repetitions),
five Chinese prompt windows, plus a separately scored extra English Black window.
Thus primary prompt accuracy is 5+5; the all-spoken-window English score is n=6.
These different denominators must not be conflated to make B look better.

Waveform clusters at 4.125-5.150 s and 14.190-15.310 s identify the first two
readings; next prompt activity starts 17.350 s. The corrected window boundary
17.200 s is in that quiet gap, before the next activity. Chinese activity spans
64.055-65.465 s and 67.945-68.285 s; extra Black spans 71.235-71.525 s, with
window boundary 70.900 s in a quiet gap. The zh04 speech cluster ends 79.270 s;
later isolated waveform clicks at 80.855/83.030 s are excluded from its **speech
boundary only**, not from PCM, Partial or Final scoring. Raw A's extra `这` stays
an insertion. Both models use the same confirmed-v2 manifest; this is waveform
assistance (5 ms framing/noise uncertainty), not phonetic timing or production VAD.

| Metric | A | B |
| --- | ---: | ---: |
| English primary 5 prompt WER mean / median | 23.34% / 36.36% | 28.55% / 27.27% |
| English all 6 windows incl. Black mean / median | 36.12% / 36.93% | 23.79% / 26.14% |
| Chinese CER mean / median, n=5 | 8.33% / 0% | 0% / 0% |
| Primary English / Chinese normalized exact | 2/5 / 3/5 | 1/5 / 5/5 |
| Extra Black WER | 100% (`bl`) | 0% (`BLACK`) |
| First raw median / p90, n=11 | 746.33 / 839.68 ms | 706.07 / 938.85 ms |
| Update / first stable median | 599.96 / 1334.71 ms | 320.21 / 1076.40 ms |
| Speech end -> endpoint / Final median | 1640.67 / 1640.72 ms | 1661.33 / 1661.37 ms |
| Rewrite rate | 0 | 0 |
| RTF | 0.04841 | 0.04821 |
| Average / sampled peak CPU, single-core-equivalent | 19.16% / 69.30% | 34.04% / 81.25% |
| Loaded / max / unloaded working set, bytes | 317833216 / 329179136 / 57405440 | 273453056 / 289607680 / 56766464 |
| Load ms / count | 1360 / 1 | 1558 / 1 |
| Queue high-water / drops / end backlog | 40 ms / 0 / 0 | 20 ms / 0 / 0 |

All raw Finals below; casing/spacing retained. en03 reference is the question
twice; remaining prompt references are fixed in the corpus JSON.

| ID | A Final | B Final | A / B WER or CER |
| --- | --- | --- | --- |
| en03 | how are you today how are you today | HOW ARE YOU TODAY HOW ARE YOU TODAY | 0% / 0% |
| en04 | this is a real times speech recognition | THIS IS A REAL TIMES BEACH REGNATION TEST | 42.86% / 57.14% |
| en06 | please translate this sentence into chinese | PLEASE TRANSLATE THIS SENTENCE IN TWO CHINESE | 0% / 33.33% |
| en08 | the meeting tstarts as thirty tomorrow morning | THE MAY MEETING START AT NINE THIRTY TOMORROW MORNING | 37.50% / 25% |
| en10 | the system should display test where the audiis still playing | THE SYSTEM SHOULD DISPLAY TEST WHERE THE AUDIO IS DEAL PLAYING | 36.36% / 27.27% |
| zh03 | 今天天气预 很 | 今天天气 很好 | 33.33% / 0% |
| Black | bl | BLACK | 100% / 0% |
| zh04 | 这是一个实时语音识别测试 这 | 这是一个实时语音识别测试 | 8.33% / 0% |
| zh05 | 系统应该在音频播放时持续显示文字 | 系统应该在音频播放时持续显示文字 | 0% / 0% |
| zh07 | 明天上午九点半开始会议 | 明天上午九点半开始会议 | 0% / 0% |
| zh10 | 我们需要比较识别速度和准确率 | 我们需要比较识别速度和准确率 | 0% / 0% |

Agent conservative semantic review (not owner accuracy approval): A primary
English 2/5, Chinese 3/5; B English 2/5, Chinese 5/5. B en08 retains intended
meeting time despite grammatical/additional-word errors; en06 `IN TWO` is not
silently normalized to `into`. Extra Black is A no / B yes. Long prompts have
multiple pre-end updates in both runs, but the ~290 ms Black finishes before
first Partial. The first repeated window has an intentional long silence and
is not a single uninterrupted utterance for latency purposes.

The live B microphone run ended normally (join/unload 46 ms, exit 0); models
remain loaded once per replay, drops and final backlog are zero. Full raw/stable
examples, per-window metrics and separate agent semantic judgments are in ignored
`microphone-retest-*-reviewed-v3.json`.
This establishes effective human PCM, **not microphone English accuracy PASS**.
Clean source/loopback accuracy is good while microphone English is worse for
both models: inspect audio quality, pronunciation/device path separately rather
than blaming the model alone or claiming the capture implementation is broken.
No denoise, echo cancellation, model upgrade or correction was implemented.

## Regression, Working Tree And Decision

Builds completed with Qt6.11.2/MinGW13.1/CMake/Ninja:

| Configuration | Build | CTest |
| --- | --- | --- |
| `build/phase61b` Release | PASS | 25/25 PASS |
| `build/phase61b-debug` Debug | PASS | 25/25 PASS |
| `build/phase8b-whisper` Release | PASS | 26/26 PASS |
| `build/phase8b-whisper-debug` Debug | PASS | 26/26 PASS |
| `build/phase8d1-sherpa` Release | Probe/core PASS | contract 1/1 PASS |
| `build/phase8d1-sherpa-debug` Debug | Probe/core PASS | contract 1/1 PASS |

Build command: `cmake --build <build> -j4` (opt-in builds target Probe and its
contract tests); test command: `ctest --test-dir <build> --output-on-failure`
(opt-in `-R phase8d1`). Python deterministic analysis is exercised by the existing
package-validator CTest, with explicit reruns after evaluator changes. WER/CER,
Unicode, missing output, serialization, rewrites/stability, waveform annotation,
empty-silence endpoint association and contaminated-margin rejection need no
model, network, microphone or speaker. A real Debug Zipformer load/WAV/Stop smoke
also passed: load 1568 ms, one load, zero drops, RTF 0.05333, exit 0. Its fixture
has uncertain ground truth and is not an accuracy score.

Final rerun: all four ordinary/Whisper suites passed at the counts above;
standalone streaming Python tests **12/12 PASS**, including explicit repeated
reference windows and manual-boundary preservation. The package validator was
rerun in all four configurations after the final evaluator edit and passed.
All six build checks succeeded (final invocation had no pending Ninja work).

Modified: four QA C++ files only. Added: benchmark/evaluator tests, explicit
offline corpus preparation script, fixed text corpus and this report. README
links the measured accuracy limitation. CMake/production `src/`/`helpers/`/packaging/licenses
are unchanged. Pre-existing `licenses/`, `mic-test.txt`,
`scripts/prepare_qt_licenses.py` are untouched and excluded. Private weights,
WAV, DLL, logs and reports remain ignored; no credentials are read or committed.
No subagent/worktree, dual model memory experiment, Whisper accuracy correction,
Phase 8D.2 or Phase 9 was started.

| Gate | Result |
| --- | --- |
| Fixed 10 en / 10 zh clean synthetic references | PASS, limitation disclosed |
| Same-input paced candidate A/B | PASS for fixed WAV, captured loopback and real microphone |
| WER/CER, stability, rewrite, CPU/memory/RTF/load measured | PASS for fixed WAV |
| Waveform-assisted speech start/end latency | PASS for fixed WAV, 5 ms framing uncertainty |
| Longer sentence pre-end incremental updates | PASS for fixed WAV |
| Growing backlog / RTF >=1 | Not observed in measured runs |
| Clean live loopback | PASS for isolated Zipformer retest, historical mixed captures excluded |
| Verified real microphone A/B measurement | PASS, owner-confirmed references and identical PCM |
| Microphone English production accuracy | PARTIAL; substantial errors, no approval implied |
| Overall Phase 8D.1A | PARTIAL accuracy gate; measurement task complete |
| Phase 8D.1 / Phase 8D | Remain PARTIAL |

**Recommended streaming model: Neither for unrestricted production yet.**
**Zipformer is the preferred next experimental Original-Partial candidate**:
clean-loopback 10/10, faster ~320 ms cadence, lower memory, strong microphone
Chinese, no backlog, at higher CPU. It is not uniformly more accurate: its five
primary microphone English prompts are worse in mean WER than A. That limitation
prevents a blanket production-readiness or Phase 8D.1 PASS claim. Do not replace
Whisper Final. A future explicitly authorized experiment may use Zipformer fast
Original Partial -> existing Whisper Final correction -> DeepL Final-only, but
this task makes no such wiring and does not begin Phase 8D.2.

Exact next step: diagnose/repeat microphone English with independently confirmed
same-input audio under controlled device/reading conditions, retaining all errors.
No additional streaming model search or production change is made here.
Optional Whisper Final reference was not run; its WER/CER is NOT MEASURED and
no claim that it corrects this particular recording is made. Phase 8D remains
PARTIAL regardless of the successful clean-loopback gate.

## Requested Final Report Checklist

This ledger covers all 72 requested items; detailed tables/provenance above
remain authoritative. Git hash/push verification is reported in the delivery
message because a commit cannot contain its own hash.

1. Phase 8D.1A: PARTIAL accuracy gate; measurement task completed.
2. Phase 8D.1: stays PARTIAL; clean-loopback blocker resolved, English mic remains weak.
3. Initial commit: `0b3c3878f03b065c651342a74c00cc5e92726918`.
4. Added: this document; `scripts/benchmark_streaming_asr.py`,
   `scripts/test_streaming_asr_benchmark.py`, `scripts/prepare_streaming_asr_corpus.ps1`,
   `tools/streaming_asr/corpus-phase8d1a.json`.
5. Modified: README, feasibility follow-up note, four existing QA files:
   `SherpaOnlineRecognizer.h/.cpp`, `StreamingAsrProbe.cpp`, `StreamingProbeCore.cpp`.
6. sherpa: v1.13.8, exact commit in Fixed Candidates; ORT CPU 1.28.2 unchanged.
7. Paraformer: original bilingual streaming int8, pinned identity above.
8. Paraformer hashes: full file table above; 237202501 bytes / 226.21 MiB.
9. Zipformer: official bilingual streaming Transducer 2023-02-20.
10. Zipformer hashes: full table above; 199056205 bytes / 189.83 MiB,
    encoder/joiner int8, official FP32 decoder (not concealed).
11. Decoding: greedy_search for both; no beam comparison.
12. Threads: 2 each, sequential process runs.
13. Fixed corpus: 20 utterances; live subsets 5 en + 5 zh; mic repetition/extra disclosed.
14. English corpus: en01-en10 in tracked corpus; short/long/numbers/Qt included.
15. Chinese corpus: zh01-zh10 in tracked corpus; short/long/numbers/Qt included.
16. WER: lowercase, punctuation removal, whitespace collapse, word edit distance.
17. CER: same punctuation rule, no spaces, Unicode code-point edit distance.
18. A clean English WER mean/median: 15.19% / 0%.
19. B clean English WER mean/median: 8.97% / 0%.
20. A clean Chinese CER mean/median: 0% / 0%.
21. B clean Chinese CER mean/median: 1.54% / 0%.
22. A clean first raw speech-start median: 788.57 ms.
23. B clean first raw speech-start median: 516.06 ms.
24. A clean within-utterance update median: 600.62 ms.
25. B clean within-utterance update median: 320.49 ms.
26. A rewrite rate: 0 in measured runs; fake tests cover real rewrites.
27. B rewrite rate: 0 in measured runs; not a universal guarantee.
28. A clean RTF: 0.05984, silence-inclusive denominator disclosed.
29. B clean RTF: 0.05235, same method.
30. Backlog: no sustained growth; fair replay drops 0, final backlog 0 for both.
31. CPU: clean A 23.15% average / 81.58% sampled peak; B 37.86% / 88.21%,
    single-core-equivalent; live/captured comparisons in separate tables.
32. Memory: clean loaded A 318492672 / B 274649088 bytes; pre/stream/unload table above.
33. Load: clean A 1205 / B 1543 ms; count 1 for each multi-utterance session.
34. Clean WAV: fixed identical 139780 ms synthetic input, full 20-row scores disclosed.
35. Clean live loopback device: Realtek headphones, explicit stable render ID above.
36. Clean live B loopback: PASS, 10/10 exact, pre-end updates; additional live A
    attempt contaminated and rejected, not counted PASS. Fair A replay 8/10, B 10/10.
37. Microphone during loopback: not opened; selected backend SystemLoopback only.
38. Microphone: default Realtek, stable ID above, 48k stereo Float32LE -> 16k mono S16LE.
39. Microphone: 5 English + 5 Chinese prompts; first repeated, extra Black scored separately.
40. A mic: primary English WER 23.34% / 36.36%; Chinese CER 8.33% / 0%.
41. B mic: primary English WER 28.55% / 27.27%; Chinese CER 0% / 0%.
42. Mic first raw: A 746.33 / B 706.07 ms median over 11 annotated windows;
    waveform uncertainty, repeated-window silence and short Black disclosed.
43. A raw example en04: `this` -> `this is a real time speech` ->
    `this is a real time speech recognition`; no fabricated completion.
44. B raw example en04: `THIS IS` -> `THIS IS A REAL TIME SPEECH` -> full TEST.
45. Stable clean first-prefix median: A 1353.56 / B 939.16 ms; raw text scored separately.
46. English agent semantic review: clean A 6/10, B 8/10; loopback replay 4/5, 5/5;
    mic primary 2/5, 2/5; not owner accuracy approval.
47. Chinese agent semantic review: clean A 10/10, B 9/10; loopback 5/5 each;
    mic A 3/5, B 5/5.
48. Optional Whisper Final accuracy: NOT MEASURED; production Whisper unchanged.
49. Recommended streaming model: Neither for unrestricted production;
    Zipformer preferred next experimental Original-Partial candidate.
50. Rationale: faster cadence/lower memory, clean 10/10 and mic Chinese; higher CPU,
    primary mic English not uniformly better, so retain accuracy limitation.
51. Production integration ready: not approved; no all-input accuracy PASS.
52. DeepL / OpenAI: 0 requests.
53. Production UI: unchanged; separate opt-in QA preview only.
54. Audio backends: unchanged.
55. Whisper backend: unchanged, not replaced.
56. OCR/Paddle/helper/protocol: unchanged.
57. Standard Release build: PASS.
58. Standard Debug build: PASS.
59. Standard CTest: Release 25/25, Debug 25/25 PASS.
60. Whisper Release build: PASS.
61. Whisper Debug build: PASS.
62. Whisper CTest: Release 26/26, Debug 26/26 PASS.
63. sherpa Probe: Release/Debug build PASS, each contract 1/1 PASS.
64. Real models: both load/stream/reset/Stop/unload; valid WAV/captured/mic A/B;
    invalid mixed captures disclosed; no model/hardware dependency in ordinary CTest.
65. Docs: this report, feasibility cross-reference, README status.
66. Pre-existing untracked files: untouched, excluded from staging.
67. Git: task files only; private `.cache`, weights, WAV, DLL, logs and temporary
    reports excluded. Original untracked items remain, not a globally clean worktree.
68. Commit: `Evaluate streaming ASR accuracy and loopback`; exact hash in delivery.
69. Push: normal `origin main`, never force; verified outcome in delivery.
70. LunaTranslator: read-only, `git status --short` empty.
71. Phase 8D.2 / Phase 9: not started.
72. Next step: controlled same-PCM English microphone diagnosis/accuracy recheck;
    future Original-Partial integration requires separate explicit authorization.
