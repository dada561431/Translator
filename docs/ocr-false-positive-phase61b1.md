# Phase 6.1B.1: No-subtitle False Positives

## Status

**Production filtering deferred by user direction.** No production suppression
policy has been enabled. Non-behavioral instrumentation and the independent
analysis tool are retained with Phase 6.1B. No-subtitle false positives are a
Known Limitation and no longer block the integration commit.
This document records preliminary evidence, not a claim that the issue is fixed.

## Current Path and Failure Mechanism

The helper sets `text_rec_score_thresh=0.0`, returns every recognition box,
and `reading_order()` concatenates the results. `PaddleOcrEngine` validates the
protocol/schema and score range but does not interpret scores as quality gates.
`RealtimePipelineCoordinator` currently accepts valid nonempty OCR text and then
uses `TextDeduplicator` before translation. Background detections can therefore
reach Original and DeepL. Empty debounce cannot suppress a nonempty false result.
The reported subtitle-free failure still needs reproduction on labelled negatives.

`TextDeduplicator` remains unchanged and only normalizes/deduplicates accepted
text. No new queues, temporal candidates, production thresholds or settings
parameters were added in this instrumentation/analysis step.

## Actual Metadata

PaddleOCR 3.7.0 / PaddleX 3.7.2 currently expose `rec_texts`, `rec_scores` and
`rec_polys` through the OCR result path. The installed PaddleX OCR pipeline and
result serializer do not propagate detection confidence. Status is explicitly
`not exposed by current result path`; recognition score is not detection score.

Protocol v1 keeps existing fields and adds optional ordered `box_texts` and
`detection_score_status`. `OcrResult` carries these plus helper request identity.
Old helpers without optional metadata still work. Native tests cover missing,
valid, wrong-count and wrong-type box texts. Helper stdout remains framed IPC;
runtime diagnostics remain on stderr. Debug summaries print counts/sizes, not
complete captions. The opt-in probe writes full box text/score/polygon and
absolute/relative width, height, polygon area and center to ignored local JSON.
MKL-DNN remains explicitly disabled.

## Preliminary Positive Regression

The production native probe reran the 10 distinct user-confirmed Phase 6.1A
Chinese crops from Bilibili episode ep743046. Each had two IPC requests; all 20
completed using helper PID 23396. There were no errors. Raw Exact remains 9/10,
CER 2.7027%, with 10 accepted frames and 12 detected boxes.

| Statistic | All boxes in positive frames |
| --- | --- |
| Recognition score min / median / mean / max | .949125 / .999153 / .988905 / .999959 |
| Relative width min / median / max | .099065 / .275234 / .921495 |
| Relative height min / median / max | .783505 / .850515 / .958763 |
| Relative polygon area min / median / max | .094980 / .245592 / .721996 |

These frame-level distributions include noise boxes. Specifically sample 010
has a correct short subtitle and an extra false `0` with recognition score
**.9491254687**, relative area .094980 and relative center x .946729. Its presence
is confirmed against the earlier user-verified ground truth, not inferred from
score. This is a mixed-box false detection, not a subtitle-free negative frame.

Sweeping 0, .2, .3, .4, .5, .6, .7, .8 and .9 gives the same Exact 9/10,
CER 2.7027% and zero rejected positive frames at every candidate. None removes
that high-confidence false box. Therefore a generic .5 or .8 threshold is not
justified as a solution. Choosing >.949 or a large area gate from this one box
would also be unjustified without low-score, single-character and negative data.

The manual tool also emits experimental relative-area sweeps. They are not
production rules. No negative score/geometry distribution or FP frame rate is
available yet: **validated negative count is zero**, so those values are null,
not 0% FP. Additional browser captures are provisional until label/region review.

## Evaluation Tool and Tests

See `tools/ocr_false_positive_analysis/README.md`. It reuses the real native
engine/parser/helper; images, private manifests and raw output remain ignored.
Unit tests cover mixed-box filtering, all-filtered empty output, high-confidence
single-character words, two-line reading order, missing metadata, error counting,
positive loss in sweeps and exact bucket boundaries. These are analysis tests,
not evidence that production filtering or real short-video captions passed.
The Release CTest run includes 11 suites, including all previous phase suites.

## Deferred Research and Unverified Coverage

If filtering research resumes, at least 15 visually confirmed positives/negatives
would be required, followed
by 20 positive and 20 negative checks, 10 subtitle-empty-new transitions and
5-10 minutes of real pipeline video playback. These checks have not been
completed and are not required for the current Phase 6.1B integration commit
under the user's revised scope. No further samples are being collected now.

No final A/B/C/D decision is made. The observed high-score false box shows that
the current confidence sweep alone is insufficient for this mixed-box example.
It does not prove a geometry or temporal policy works. Temporal experiments
require ordered video transitions, not shuffled still crops. Production candidate
lifecycle, rejected-noise DeepL request reduction and production empty-after-filter
behavior are not implemented or accepted yet. Existing debounce/queue behavior
must be retained when a data-supported policy is subsequently integrated.

Remaining real coverage: short single-character captions, low-confidence genuine
captions, two-line real samples, colored/artistic/tiny/low-contrast text, multiple
videos and subtitle-free dynamic backgrounds. No claim of universal suppression
is made. No production confidence, geometry or temporal filter is included in
the Phase 6.1B commit. Integration proceeds with this Known Limitation explicitly
documented rather than claiming suppression.
LunaTranslator remains read-only; Phase 6.1C has not started.
