# Phase 6.1A Local OCR Benchmark

This is evaluation tooling, not a new Translator backend. It calls the unchanged
production Tesseract engine and runs official PP-OCRv6 small/medium detector plus
recognizer in isolated, CPU-only Python workers. Nothing imports Paddle into the
application. Each model stays loaded throughout its batch; this is not a
per-frame production process design.

## Private Inputs

Put local subtitle crops and `manifest.csv` in `private/`. CSV must contain exactly
`filename,language,ground_truth,category`; UTF-8 (with or without BOM) is accepted.
Supported language hints are zh/en/ja/ko, but installed language resources and
candidate language support must be verified separately. Confirm labels visually,
not using OCR output. All manifest images are evaluated, including failures.
Use 15-30 different frames if possible; ten is a limited pilot, not a broad benchmark.
Do not count duplicates twice. The example manifest contains fictional labels,
not measured data. Never upload private crops or results to an OCR service.

Debug Translator's existing Region action can overwrite one local
`%TEMP%/Translator/last_capture.png` after capture. Archive selected frames manually
in `private/` before the next capture. Release and realtime mode do not dump frames.
Disable online translation providers during private acquisition if transmission
is not desired. No production acquisition behavior was changed for this benchmark.

For screenshots rather than crops, `prepare_crops.py private/acquisition.json`
accepts `{"samples": [{"source": "<absolute local PNG>", "filename": "001.png",
"crop": [left, top, right, bottom], "language": "zh", "ground_truth": "<visually confirmed text>",
"category": "outline"}]}`. Crop bounds use actual image pixels, not desktop DPI
coordinates. This explicit utility records source hash, original dimensions,
crop bounds and exact-byte duplicate exclusions. Inspect every resulting crop.
It does not resize, enhance, fetch videos or infer labels. Existing private
manifest/provenance files are replaced when this utility is explicitly run.

## Environment And Build

Use Python 3.12+ for the isolated tooling and the standard Qt/MinGW build environment:

```powershell
python -m venv benchmarks/ocr_phase61a/.venv
& ./benchmarks/ocr_phase61a/.venv/Scripts/python.exe -m pip install --cache-dir benchmarks/ocr_phase61a/.cache/pip -r benchmarks/ocr_phase61a/requirements.txt
& ./benchmarks/ocr_phase61a/.venv/Scripts/python.exe tools/ocr_benchmark/prepare_models.py
cmake -S . -B build/phase61a -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="<Qt prefix>" -DPython3_EXECUTABLE="<absolute benchmark venv python>"
cmake --build build/phase61a --parallel 6
ctest --test-dir build/phase61a --output-on-failure
```

Qt and MinGW `bin` must be on PATH; no machine paths are embedded in CMake.
Tesseract 5's DLL and chi_sim data must be discoverable using production rules.
The downloader only requests four official model archives, never images. Models,
venv, cache and generated data are ignored. Archive SHA256 and extracted file
sizes/checksums are written to `models/index.json`; hashes identify downloaded
artifacts, not an independent upstream-signature check. Pinned top-level packages
are provided; save a local `pip freeze` for the complete resolved environment.

## Run

```powershell
& ./benchmarks/ocr_phase61a/.venv/Scripts/python.exe tools/ocr_benchmark/run_benchmark.py --tesseract-exe build/phase61a/TranslatorTesseractBatch.exe --ground-truth-confirmed --no-mkldnn
```

The successful Windows evaluation used `--no-mkldnn`: with MKL-DNN enabled,
Paddle 3.3.1 raised an unsupported PIR ArrayAttribute error in both models.
This option is explicit, not a hidden accuracy-dependent retry. Without it, the
official CPU acceleration path is attempted and any errors remain in the report.
`--ground-truth-confirmed` means the user has actually confirmed every label.
Without confirmation, status remains pending. `--synthetic` marks smoke inputs
and cannot establish scene-text quality. Three warm repeats are required by
default; 20 distinct samples are required for cross-sample P95. Repeats do not
turn ten frames into thirty independent samples.

Outputs: `results/latest.md`, `latest.csv`, `latest.json`, and `raw/` worker JSON
and logs. All are local and ignored. CSV/JSON include raw and normalized text,
input hashes, language, dimensions, Tesseract preprocessing/PSM, timings,
confidence, boxes and failures. Re-running overwrites latest; use `--output`
with a new local ignored directory to preserve runs. Failure cases are never
dropped from quality denominators. ERROR and valid EMPTY are distinct. CER may
exceed 1 for hallucinated text. Normalization only trims, standardizes line
endings and collapses ordinary spaces; it does not delete punctuation, translate,
simplify Chinese or remove internal line breaks.

Paddle's three optional document/orientation models are disabled for upright
subtitle regions. Recognition confidence threshold is zero; no custom confidence
filter is applied. Boxes are grouped into horizontal lines and sorted top-to-bottom,
left-to-right; separate boxes keep a space. Raw box texts and polygons are retained.
Warm time includes production preprocessing / full Paddle detection-recognition
and output assembly, but not file decoding or IPC. Model constructor time and
first usable-result time are separate; Tesseract lazily initializes, so its ready
measurement necessarily includes one inference. Python imports are reported
separately. Working-set snapshots are not peak memory measurements.

See `../../docs/ocr-scene-text-evaluation-phase61a.md` for measured findings,
coverage limits and proposed deployment choices. No Phase 6.1B is implemented.
