# Phase 6.1A: Scene-text OCR Evaluation

Evaluation date: 2026-10-05 (Asia/Shanghai). **Decision A for small, with limited scene coverage.**
The evaluation infrastructure and ten-frame local real-video A/B are complete.
This is not full production, deployment, or continuous-video acceptance.
No Paddle backend or Phase 6.1B code is implemented.

## Problem And Existing Architecture

The Phase 6 realtime scheduler is 300ms, with frame comparison, single-flight OCR,
one latest pending image, text deduplication, session IDs and stale-result rejection.
Incorrect OCR feeds incorrect text into DeepL; changing translation prompts cannot
restore missing source text. In this dataset the current OCR baseline has 70.27%
micro CER and 7/10 severe-or-empty results, making OCR the observed bottleneck.

Reviewed source: `src/ocr/IOcrEngine.h`, `OcrTypes.h`,
`TesseractOcrEngine.{h,cpp}`, `OcrImagePreprocessor.{h,cpp}`;
`src/app/OcrCoordinator.{h,cpp}`, `RealtimePipelineCoordinator.{h,cpp}`,
`CaptureCoordinator.cpp`; `src/capture/ScreenCaptureService.cpp`;
`src/config/SettingsManager.{h,cpp}`; Phase4, Phase41 and Phase6 tests;
`docs/ocr-accuracy-phase41.md`, `docs/realtime-pipeline-phase6.md`,
README, architecture and CMake. These production files were not changed.

The standalone `TranslatorTesseractBatch` links the actual production engine and
calls `TesseractOcrEngine::recognize`, not a replacement CLI approximation.
For all ten 1070x97 inputs it selects chi_sim, automatic 2x smooth upscale,
Grayscale8, 1st/99th percentile contrast stretching and PSM 7. Processed size is
2140x194. Low-range contrast (<32) is left unchanged by the existing algorithm.
Production PSM 6 applies to non-single-line regions; it was not forcibly selected
or extensively tuned in this evaluation. Baseline threads retain production
defaults; Paddle CPU threads are explicitly four, not claimed thread-equivalent.

Installed Tesseract CLI reports 5.4.0.20240606 (same local installation as the DLL).
chi_sim data is 2,469,156 bytes, SHA256
`a5fcb6f0db1e1d6d8522f39db4e848f05984669172e584e8d76b6b3141e1f730`.
The local libtesseract-5.dll SHA256 is
`279488c41bd341edaa03dcd097faa33604ba330e6254ac97bcaf2229d8d7ac1a`.

## Data And Ground Truth

The user supplied eleven full-resolution Bilibili video screenshots.
Exact source-file SHA256 established that screenshots 1 and 8 are duplicates.
Ten distinct original frames were retained, in supplied order, with no exclusions
based on OCR output. Every unique Chinese subtitle was transcribed independently
from the visible frame; the user explicitly confirmed all ten labels including
internal spaces before evaluation. OCR output was never used to create labels.

All originals are 2559x1599. The same explicit crop [740,1345,1810,1442) produces
1070x97 pixels for every engine. Every resulting crop was visually inspected:
Chinese line complete, no Japanese line, watermark, player buttons, or webpage
title/comment/danmaku text in the crop. No artificial downsampling or enhancement
was applied before the common input. Original screenshot hashes, coordinates and
deduplication provenance are local in `private/provenance.json`.

Source is Bilibili as supplied by the user, not successful agent-controlled
browser playback or an actual new Translator Region UI acceptance. Computer Use
could list the video window but was blocked on browser-URL verification; there was
no bypass, scraping or video download. Douyin was not tested in this phase.
The video is one source, not ten independent productions. Ten frames meet the
minimum, but fall below the recommended 15-30 and are insufficient for broad
generalization.

The primary manifest has 2 outline_bright_complex and 8 outline_single_line
samples. All are normal white Simplified Chinese with black outlines, in different
animated backgrounds. They are paused frames: movement/temporal robustness was
not tested. They are not genuinely small glyphs (large fullscreen subtitles);
the 97px crop height must not be mistaken for tiny text.

| Scene | Evidence and result |
| --- | --- |
| Ordinary Chinese subtitles / black outlines | All 10; small 9 exact, medium 8 exact; Tesseract 1 exact |
| Bright/complex illustrated backgrounds | 2; both Paddle tiers 2 exact, Tesseract 1 severe + 1 empty |
| Other outlined single-line backgrounds | 8; small 7 exact + 1 severe, medium 6 exact + 2 minor; Tesseract 1 exact + 2 minor + 4 severe + 1 empty |
| Isolated shadow effect | Not separately sampled; no quality claim |
| Small font | Not covered; Japanese lower line deliberately excluded from primary Chinese test |
| Two-line Chinese / bilingual OCR | Not covered; ordering code is unit tested, not video-validated |
| Dynamic background / fast changing text / motion blur | Paused-frame complexity only, no temporal acceptance |
| Artistic, yellow or colored text, low contrast | Not covered; no claim of Good or Poor |

## Harness And Measurement

`tools/ocr_benchmark/run_benchmark.py` validates the complete manifest first,
hashes inputs, sends identical paths to one baseline process and two isolated
Python model workers, validates one output per sample, hashes inputs again,
then writes local Markdown, CSV and JSON. Ground truth is used only by metrics,
not sent to the OCR workers. All manifest rows remain in every engine denominator.

`paddle_worker.py` uses detection plus recognition. It disables document
orientation, unwarping and text-line orientation for upright subtitle crops.
Results are sorted by horizontal line, then left-to-right; line breaks and spaces
between separate boxes remain. Boxes, raw texts, scores and ordered output are
retained. Recognition threshold is zero; no custom low-score suppression.
Small's extra false-positive box is intentionally retained in the failure case.

The same image is decoded once per engine outside timing; each image has one
warmup, then three measured calls. Full production preprocessing and full Paddle
pipeline/output assembly are timed. Screen capture, disk decode, child-process
startup, IPC and translation are excluded from warm latency. Each engine remains
alive for all ten images. Model tier order is baseline/small/medium; the evaluation
does not claim randomized order, thermal control or statistical significance.

Normalization: trim, CRLF/CR to LF, trim line boundaries, collapse consecutive
ordinary ASCII spaces. Preserve punctuation, internal spaces, Chinese variants
and line breaks. Exact match uses normalized text; CER is Unicode character
Levenshtein edits divided by GT length. Micro CER weights by total GT characters.
Character accuracy is max(0,1-CER). MINOR is nonempty CER <=0.2; SEVERE is >0.2.
EMPTY is a successful blank result; ERROR is failed inference and scored empty.
CER may exceed 1 for hallucinations. A short two-character string with an extra
space/digit is severe, not arbitrarily relabeled usable.

## Environment And Runtime Compatibility

Windows 11 Pro 10.0.26100, AMD Ryzen 7 7840H, 8 cores / 16 logical CPUs.
Qt 6.11.2, MinGW GCC 13.1, CMake + Ninja, Release native build.
Isolated local Python 3.12.14, paddleocr 3.7.0, paddlepaddle CPU 3.3.1,
paddlex 3.7.2, psutil 7.2.2, numpy 2.3.5, opencv-contrib-python 4.10.0.84.
No global Python installation or production Qt dependencies were changed.
CPU only; no CUDA/GPU result is inferred.

Both small and medium initially failed on all ten inputs with MKL-DNN enabled:
`ConvertPirAttribute2RuntimeAttribute not support [pir::ArrayAttribute<pir::DoubleAttribute>]`.
This is an execution incompatibility, not evidence of OCR quality.
The full failed run is preserved locally under `results/mkldnn/`.
The successful explicitly configured run uses `--no-mkldnn`, same versions,
same models, same ten crops. A second successful pass reproduced all text results;
timing varies, including an occasional small call just over 300ms.
Final numbers below correspond to the last successful `results/latest.json`.

Model host checks are disabled only because local model directories are explicit;
this is not disabling TLS or bypassing website security. Hugging Face offline mode
and disabled telemetry are set for the workers. Model preparation separately
downloads official weights, never user images. No OCR SaaS or translation API
was called by the benchmark.

Current official [OCR pipeline documentation](https://www.paddleocr.ai/main/en/version3.x/pipeline_usage/OCR.html)
provides PP-OCRv6 tiny/small/medium and local detector/recognizer selection.
Actual evaluated names are PP-OCRv6_small_det + PP-OCRv6_small_rec and
PP-OCRv6_medium_det + PP-OCRv6_medium_rec, not a v5 or recognition-only substitute.
Official headline accuracies are not results on this local subtitle set.

## Accuracy Results

| Engine | Exact | Minor | Severe | Empty | Runtime error | Micro CER | Character accuracy |
| --- | --- | --- | --- | --- | --- | --- | --- |
| production_tesseract | 1/10 | 2 | 5 | 2 | 0 | 70.27% | 29.73% |
| PP-OCRv6_small | 9/10 | 0 | 1 | 0 | 0 | 2.70% | 97.30% |
| PP-OCRv6_medium | 8/10 | 2 | 0 | 0 | 0 | 2.70% | 97.30% |

74 total normalized GT characters: Tesseract 52 edits, each Paddle tier 2 edits.
Small improves exact match by 80 percentage points, medium by 70; both lower CER
by 67.57 points. This is a substantial descriptive improvement on all ten frames,
not a claim of universal scene-text accuracy or statistical significance.
Medium misses a space in two longer phrases; those remain MINOR, not PASS.
Small has one severe hallucination on a very short subtitle. Both have zero
empty and zero runtime errors in the successful non-MKL-DNN configuration.

## Latency, Cold Start And Memory

Warm median below is the median of the ten per-image medians. Mean is over all
30 measured warm calls, not first inference. Raw JSON also retains the mean of
per-image medians and all 30 timings for reproducibility.

| Engine | Warm median ms | All warm calls mean ms | Warm min-max ms | Calls >300ms |
| --- | --- | --- | --- | --- |
| production_tesseract | 38.73 | 40.96 | 9.27-69.55 | 0/30 |
| PP-OCRv6_small | 193.54 | 206.99 | 158.52-308.96 | 1/30 |
| PP-OCRv6_medium | 812.38 | 843.74 | 767.18-960.67 | 30/30 |

P95 is deliberately N/A: only ten distinct samples and three repeats per image.
Repeated timings are correlated and not presented as 30 independent frames.
Small is usually 100-300ms but has 1/30 calls above 300ms; it is viable for a
bounded single-flight scheduler, not guaranteed 300ms end-to-end translation.
Medium has 30/30 calls above 300ms and is unsuitable as the current CPU default.
Capture/IPC/translation will consume additional latency budget.

| Engine | Python imports ms | Constructor ms | Constructor + first inference ms | Before MiB | Ready MiB | After all MiB |
| --- | --- | --- | --- | --- | --- | --- |
| production_tesseract | 0.00 | 0.0037 | 186.82 | 12.21 | 68.35 | 71.00 |
| PP-OCRv6_small | 1232.56 | 1486.1240 | 1726.16 | 22.38 | 344.20 | 355.72 |
| PP-OCRv6_medium | 1280.76 | 1607.4492 | 2595.08 | 22.39 | 465.11 | 443.02 |

Tesseract initializes lazily. Its tiny constructor is NOT a model-ready time;
the ready figure includes DLL/language initialization and first preprocessing/OCR.
No pure Tesseract init timing is exposed by the production API; it was not
invented by subtracting unrelated timers. Paddle constructor is measured directly;
first-inference caches are separately included in ready. Including Python imports,
small is about 2.96s to first result and medium 3.88s (not total process launch).
These are fresh-engine starts with OS files potentially cached, not reboot-cold
or statistically sampled startup measurements. Memory is working-set snapshots,
not peak/allocation profiling; it includes Python/runtime for Paddle and Qt for
Tesseract. Snapshot variation does not prove or exclude memory leaks.

## Models, Size And Provenance

Four archives are downloaded only from the official Paddle model storage:
`https://paddle-model-ecology.bj.bcebos.com/paddlex/official_inference_model/paddle3.0.0/`.
Extracted package sizes include inference metadata/dictionary, not only tensors.
No orientation models are installed by this benchmark.

| Model | Extracted bytes | Archive SHA256 |
| --- | --- | --- |
| PP-OCRv6_small_det | 10,048,080 | bfb7c1e59f0faa6b540ebdca93aea3f4b1f2477805b389fbee117820d68fe9f5 |
| PP-OCRv6_small_rec | 21,433,201 | da460f968ce9f88325ac3a34fa302077d6e9b0dcefb16ba3137cd7796f879d06 |
| PP-OCRv6_medium_det | 62,273,512 | 144d0621e059566e5086e228829171591c144c2deb07b2dad4962214fbabfcf7 |
| PP-OCRv6_medium_rec | 76,837,481 | 4eecc1c6a4623765042e6fc15446da0da110b7d875b6b72b2d351d2b2dbd4da6 |

Small total: 31,481,281 bytes (30.02 MiB); medium: 139,110,993 (132.67 MiB).
Per-file size/SHA256 and dictionary-containing inference.yml are indexed locally
in `models/index.json`. No archive or weight is tracked by Git. Archive copies
and extracted copies both occupy disk; sizes above are extracted deployment assets,
not installed Python environment size.

## Representative Failures (Anonymized)

Actual GT, raw predictions, scores, boxes and timings for **every** image are in
the ignored local `results/latest.md/.csv/.json`. No frames, real manifest or
raw user subtitle corpus are published in this document. Cases below identify
the same local rows by their ordered selection, but anonymize subtitle content.

| Case | Production Tesseract | Small | Medium |
| --- | --- | --- | --- |
| Case 1 (13 GT chars) | 6 edits; CER 0.462; 68.93ms | 0 edits; CER 0.000; 205.23ms | 0 edits; CER 0.000; 902.40ms |
| Case 2 (3 GT chars) | 6 edits; CER 2.000; 66.76ms | 0 edits; CER 0.000; 170.83ms | 0 edits; CER 0.000; 787.16ms |
| Case 3 (13 GT chars) | 6 edits; CER 0.462; 38.36ms | 0 edits; CER 0.000; 205.60ms | 1 edits; CER 0.077; 903.61ms |
| Case 4 (2 GT chars) | 8 edits; CER 4.000; 53.50ms | 0 edits; CER 0.000; 178.19ms | 0 edits; CER 0.000; 815.67ms |
| Case 5 (2 GT chars) | 5 edits; CER 2.500; 37.15ms | 2 edits; CER 1.000; 267.95ms | 0 edits; CER 0.000; 802.96ms |

Cases 1-3 illustrate three Tesseract severe failures: a long sentence with multiple
substitutions, a three-character proper name with much extra text, and a phrase
whose prefix is lost and noise appended. Cases 4-5 are two-character phrases
where Tesseract mostly reads background noise. Two additional baseline empties
also remain in the aggregate and complete private report, not removed.

Small's Case 5 emits correct two-character text plus a space and spurious digit:
2 boxes/2 edits, CER 1.0. This is FAIL, not PASS, despite the intended words being
present. Medium's failures are omitted spaces in two other phrases; 1 edit each,
CER 0.077 and 0.083. They are PARTIAL, not exact. No confidence-based filter was
added to disguise the false positive, and no GT or normalization was adjusted
after seeing these results.

## Windows / MinGW Deployment Matrix

The following are researched feasibility/recommendations, **not implemented or
benchmarked integrations**. No direct MinGW build of Paddle/ORT was verified.

| Route | Compatibility / packaging | Maintenance / risk | Recommendation |
| --- | --- | --- | --- |
| Native Paddle C++ inside Qt | Official Windows route uses VS2022/MSVC + Paddle Inference + OpenCV; not an official MinGW recipe | C++ symbols, STL, exceptions, CRT allocation and import libraries cannot be presumed ABI-compatible | Do not directly link MSVC C++ interfaces into the existing MinGW executable |
| Persistent Python helper | Process boundary preserves MinGW UI; can reproduce validated official Python detection/postprocessing | Python, Paddle, NumPy/OpenCV, DLLs and models enlarge distribution; startup, readiness, timeout, worker restart and security need handling | Recommended first Phase 6.1B route with small, explicit no-MKL-DNN until compatibility fixed |
| Persistent MSVC helper | Official native dependencies isolated in a separately built exe; Qt stays MinGW | Two build toolchains, native postprocessing/parity and redistributables to maintain | Viable later packaging alternative; no helper built this phase |
| ONNX Runtime C ABI | Official v6 ONNX artifacts exist; ORT C entry point could be loaded from a compatible x64 DLL | Need detector resize/normalize/DB postprocessing, polygon crop, recognizer dynamic widths/dictionary/CTC plus parity tests; DLL C ABI and allocator ownership still require verification | Promising lighter long-term route, not a proven drop-in or measured speedup |
| Other lightweight runtime | Official inference-engine documentation lists engine-specific support; ONNX CPU is the most concrete alternative reviewed here | No verified current v6 full-pipeline Windows-MinGW recipe for ncnn/Paddle Lite/FastDeploy found in consulted official sources | Do not claim compatibility or choose an older runtime based on v2 examples |

[Official Windows Paddle C++ deployment](https://www.paddleocr.ai/main/en/version3.x/inference_deployment/local_inference/cpp/OCR_windows.html)
uses VS2022, CMake and prebuilt/source Paddle Inference plus OpenCV.
MSVC/MinGW separation is an engineering inference from the different toolchains
and C++ ABI surfaces, not a claim that all C APIs or all DLLs are incompatible.
A carefully restricted C wrapper might be possible, but is not validated here.

[ONNX preparation](https://www.paddleocr.ai/main/en/version3.x/inference_deployment/others/obtaining_onnx_models.html)
documents PaddleX/Paddle2ONNX conversion and Windows development-version caveats.
Official [small detector ONNX](https://huggingface.co/PaddlePaddle/PP-OCRv6_small_det_onnx)
and [small recognizer ONNX](https://huggingface.co/PaddlePaddle/PP-OCRv6_small_rec_onnx)
also exist, avoiding mandatory local conversion in an exploratory route.
These artifacts were researched, not downloaded/executed; dynamic-shape coverage
and numerical/output parity with the successful Paddle baseline remain unverified.
[ORT C API](https://onnxruntime.ai/docs/api/c/) exposes a C interface; the
[Windows ORT build guide](https://onnxruntime.ai/docs/build/inferencing.html)
uses VS2022 rather than proving a MinGW C++ build. Loading through the C API could
avoid C++ STL crossing the boundary, but this is a proposed bridge, not acceptance.

For future helper IPC: keep one resident model, send binary RGB pixels plus width,
height,stride and request/session IDs using framed pipes/local sockets or shared
memory. Return text, boxes/scores, elapsed time and request IDs; preserve the
existing stale-result rules. Never spawn/load/write-PNG per realtime frame.
1070x97 RGB is 311,370 bytes (~304 KiB); raw local copy/framing overhead is estimated
1-10ms on an ordinary desktop, **not measured**. PNG encode/decode, congestion or
process scheduling can exceed that. This is not an actual IPC benchmark or included
in OCR numbers. One bounded pending frame and timeout/restart must be explicitly
accepted in Phase 6.1B; no such production protocol was written now.

## Licenses

[PaddleOCR](https://github.com/PaddlePaddle/PaddleOCR/blob/main/LICENSE) and
[PaddlePaddle](https://github.com/PaddlePaddle/Paddle/blob/develop/LICENSE) use Apache-2.0.
All four official cards identify Apache-2.0:
[small det](https://huggingface.co/PaddlePaddle/PP-OCRv6_small_det),
[small rec](https://huggingface.co/PaddlePaddle/PP-OCRv6_small_rec),
[medium det](https://huggingface.co/PaddlePaddle/PP-OCRv6_medium_det),
[medium rec](https://huggingface.co/PaddlePaddle/PP-OCRv6_medium_rec).
[ONNX Runtime](https://github.com/microsoft/onnxruntime/blob/main/LICENSE) is MIT.
Retain applicable license/copyright/NOTICE material, mark modifications as needed,
and audit transitive runtime/binary notices before redistribution. These licenses
can support commercial/open-source use under their conditions, not a blanket
legal guarantee for the entire packaged product. Model licenses do not grant
redistribution rights to user screenshot content. Qt/Tesseract existing distribution
obligations are unchanged. No screenshot or ground-truth corpus is published.

## Build, Tests, Changes And Scope

Initial Git was clean main at `5cfe81b51e080e272f4977fa4ed0ea2a71c01895`;
Phase 6 remains in history. Configure and full Release build succeeded.
Missing optional Vulkan headers only produced a nonfatal CMake warning.
Commands actually run (machine paths are command-line only):

```powershell
$env:PATH='D:\QT\Tools\mingw1310_64\bin;D:\QT\6.11.2\mingw_64\bin;'+$env:PATH
& 'D:\QT\Tools\CMake_64\bin\cmake.exe' -S . -B build/phase61a -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=D:/QT/6.11.2/mingw_64 -DCMAKE_CXX_COMPILER=D:/QT/Tools/mingw1310_64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=D:/QT/Tools/Ninja/ninja.exe -DPython3_EXECUTABLE=D:/workspace/Project/Usst/Translator/benchmarks/ocr_phase61a/.venv/Scripts/python.exe
& 'D:\QT\Tools\CMake_64\bin\cmake.exe' --build build/phase61a --parallel 6
& 'D:\QT\Tools\CMake_64\bin\ctest.exe' --test-dir build/phase61a --output-on-failure
& './benchmarks/ocr_phase61a/.venv/Scripts/python.exe' tools/ocr_benchmark/run_benchmark.py --tesseract-exe build/phase61a/TranslatorTesseractBatch.exe --ground-truth-confirmed --no-mkldnn
```

CTest: **8/8 pass**: Phase2,3,4,4.1,5,5.1,6 and the benchmark suite.
Benchmark suite has 18 deterministic stdlib tests covering manifest Unicode,
missing data/files, empty labels, paths/duplicates, normalization/punctuation,
CER/failure denominator, blank-vs-error, invalid timing, serialization, reading
order/confidence retention, engine startup failure and stale-response rejection.
Tests do not require OCR models or network. No production factory, settings,
capture/pipeline algorithm, translator or UI code changed.

Changed tracked files: .gitignore, CMakeLists.txt, README.md, docs/architecture.md.
New: this report; benchmarks/ocr_phase61a README, example CSV, requirements;
tools/ocr_benchmark TesseractBatch.cpp, metrics.py, paddle_worker.py,
prepare_models.py, prepare_crops.py, run_benchmark.py, test_metrics.py.
Private acquisition/manifest/provenance/images, models/index/weights, results/logs,
venv, cache and build are ignored, not committed. The reference LunaTranslator
repository remains clean and unchanged. Commit/push identity is reported after
the final verification, not prefilled as success in this document.

## Decision And Remaining Work

**A: PP-OCRv6 small clearly improves this ten-frame Chinese subtitle set and is
recommended for a separately authorized Phase 6.1B integration.** Prefer a
persistent Python helper initially, because it reproduces the measured pipeline
and avoids MinGW/MSVC C++ ABI coupling. Keep Tesseract as a fallback.
Medium is a B-like option: improved accuracy but too slow for the current CPU
scheduler, with no net CER gain on these frames; not the realtime default.

Phase 6.1B should first reproduce local small results in a resident helper,
validate pixel/stride/protocol handling and text parity, enforce readiness,
bounded single-flight/session IDs/timeout/restart, then test a clean-machine
bundle and real continuous-video sessions. Expand to 15-30 diverse Chinese
crops, small text, two Chinese lines, outlines/shadows/colors/artistic fonts,
motion blur and different videos; resolve the false-positive background box,
MKL-DNN incompatibility and startup/packaging costs without cherry-picking.
ONNX is a subsequent parity/packaging investigation, not automatic speedup.

The current local A/B milestone is complete with coverage caveats, not a claim
of universal accuracy, pure lazy-init timing, peak memory or production readiness.
No PaddleOcrEngine, factory, Settings OCR selector, production Python runtime,
realtime algorithm modification, Audio/ASR or Phase 7 work has been introduced.
Stop after Phase 6.1A.
