# No-subtitle OCR Analysis

Manual, local-only analysis of human-labelled video subtitle crops. This tool
does not implement another Paddle inference pipeline: it invokes
`TranslatorPaddleProbe --batch`, which uses the production `OcrCoordinator`,
`PaddleOcrEngine`, persistent helper and C++ result parser. Reading order is
reused from that same production helper.

## Inputs

Store screenshots and the manifest under the ignored directory
`benchmarks/ocr_false_positive/private/`. Required UTF-8 CSV columns:

```csv
filename,type,ground_truth
p001.png,positive,<visually verified subtitle>
n001.png,negative,
```

Use distinct real video frames, not synthetic text or repeated frames. Negative
crops must have no subtitles, player UI, watermarks, danmaku or other visible
text. Keep the same actual subtitle region during subtitle-free moments; do not
turn positive frames into negatives by cropping away their text. Extra columns
such as source, timestamp, category and review status are retained. Labels must
not come from OCR. Escape paths and duplicate image contents are rejected.

## Run

Build the opt-in native probe with `BUILD_TESTING=ON`. Its runtime requires the
same local Python environment/models and Qt DLL search path as the application.

```powershell
python tools/ocr_false_positive_analysis/run_analysis.py `
  --manifest benchmarks/ocr_false_positive/private/manifest.csv `
  --images benchmarks/ocr_false_positive/private `
  --probe build/phase61b/TranslatorPaddleProbe.exe `
  --output benchmarks/ocr_false_positive/results/run1 `
  --labels-reviewed
```

`--labels-reviewed` is a manual attestation, not an automated visual check.
At least 15 positives and 15 negatives are required. `--allow-incomplete`
explicitly marks smaller/unreviewed runs preliminary. It cannot make them
acceptance evidence. No DeepL call, upload or video download is performed.

## Outputs

Only Git-ignored output directories are accepted. Native raw responses/logs,
analysis JSON/Markdown and confidence/geometry sweep CSVs remain local.
Each input gets two requests to the same persistent helper; the second is used
for quality analysis, while both responses and helper PIDs remain in native JSON.
Input hashes are verified again after inference to enforce same-input comparison.

Confidence candidates are 0, .2, .3, .4, .5, .6, .7, .8 and .9. Experimental
geometry candidates are minimum relative polygon areas .001, .003, .01 and .03.
No candidate automatically becomes a production default. Filtering is per box,
never an average score or minimum text length. Missing per-box text prevents a
fair sweep and raises an explicit error, while old helper compatibility remains
supported in the production engine.

Positive-frame box distributions include all boxes, including possible noise
next to real subtitles; they are not distributions of proven true-positive
boxes. Exact/CER use the existing benchmark whitespace normalization; Minor
means nonempty output with CER <=20%, Severe means any other non-exact output.
Inference errors remain in positive CER and are separately reported. Negative
FP rate uses valid responses only; zero valid negatives gives null, not zero.
Still-frame sweeps cannot evaluate temporal confirmation or continuous playback.
