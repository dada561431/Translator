# Phase 4.1 OCR Accuracy Report

## Scope and method

This report records local Tesseract 5.4 tests performed on 2026-09-27. Test
images and browser crops stayed under the ignored `build/phase41` directory and
are not committed or uploaded. Each before/after row uses the same local image:

- Before: original RGB image passed to Tesseract, PSM 6 or 7 as stated.
- After: automatic Qt preprocessing followed by Tesseract.
- Automatic preprocessing: conditional smooth upscale, grayscale, 1%/99%
  contrast stretch, and shape-based PSM 6/7 selection.

The browser test exercised publicly visible video pixels only. It did not
download videos, crawl pages, log in, bypass a challenge, or test HTML titles,
buttons, comments, or danmaku as OCR targets.

## Fixed samples

All fixed samples were generated locally with Qt and passed through the real
Tesseract C API. Times below exclude the one-time engine initialization unless
explicitly noted.

| Sample | Font / scene | Input -> processed | Before | After automatic | Time after | Result |
| --- | --- | --- | --- | --- | --- | --- |
| `Hello Phase 4` | Segoe UI, black/white | 520x80 -> 1040x160 | exact | exact, PSM 7 | 2ms pre + 7ms OCR = 9ms | PASS |
| `Qt Screen OCR Test` | Segoe UI, white/black | 520x80 -> 1040x160 | exact | exact, PSM 7 | 1ms + 15ms = 17ms | PASS |
| `ABCDEFG 987654` | Consolas | 520x70 -> 1040x140 | exact | exact, PSM 7 | 1ms + 8ms = 10ms | PASS |
| `你好世界` | Microsoft YaHei | 520x90 -> 1040x180 | exact | exact, PSM 7 | about 11ms | PASS |
| `你好世界` | SimSun | 520x90 -> 1040x180 | exact | exact, PSM 7 | about 11ms | PASS |
| `你好世界` | SimHei | 520x90 -> 1040x180 | `你好世开` | `你好世青` | about 11ms | PARTIAL; no accuracy gain |
| `你好世界` | KaiTi | 520x90 -> 1040x180 | PSM 6 inserted a space; PSM 7 exact | exact with tuned automatic PSM 7 | about 11ms | PASS |
| `这是中文OCR测试` | YaHei, white/black | 620x90 -> 1240x180 | exact | exact, PSM 7 | 2ms + 25ms = 27ms | PASS |
| `实时屏幕翻译` | YaHei, normal size | 620x90 -> 1240x180 | exact | exact, PSM 7 | 2ms + 9ms = 12ms | PASS |
| `实时屏幕翻译` | YaHei, small | 300x42 -> 900x126 | empty | exact, PSM 7 | 1ms + 10ms = 12ms | PASS; clear improvement |
| `白字黑描边字幕` | YaHei outline, blue background | 620x90 -> 1240x180 | PSM 7 partial | empty | about 4ms | FAIL; preprocessing regression |
| blank | white region | 520x90 -> 1040x180 | empty | empty | about 2ms | PASS; no crash/stale text |

Thresholding was deliberately not added. The outline failure demonstrates why
one destructive global conversion is unsafe for all subtitle styles.

## Bilibili video subtitle tests

Platform: Bilibili. Public videos tested were `BV1PEhi6REfA`,
`BV1keJBzGE7a`, and `BV1LW411B7XA`. Danmaku was disabled where the player
allowed it. Five representative, non-HTML subtitle crops are counted below.

| # | Scene | Visible text | OCR before | OCR after | Input -> processed | Preprocess / PSM | Elapsed after | Result / notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | yellow artistic text, two lines | `当我截肢后` / `我的爸爸...` | empty with PSM 6 and 7 | `当我哉肢后|` | 200x80 -> 400x160 | upscale2x+grayscale+contrast / 6 | 14ms | PARTIAL; one wrong character and second line missing |
| 2 | small yellow outlined text | `这不是你头像吗?` | empty | `=这不是你头像吗?` | 180x100 -> 360x200 | upscale2x+grayscale+contrast / 6 | 23ms | PARTIAL; readable with one extra symbol |
| 3 | multiple colored artistic overlays | legible lines include `植物人，确信` and `哈哈哈` | mostly unrelated garbage | mostly unrelated garbage | 190x310 -> 190x310 | grayscale+contrast / 6 | 80ms | FAIL; mixed styles and layout |
| 4 | small white text on a moving dark background | `我遗失钥匙` / `找上帝帮的忙...` | `我遗失铀是...` plus unrelated characters | `找遗失钥是...` plus unrelated characters | 205x65 -> 410x130 | upscale2x+grayscale+contrast / 6 | 60ms | PARTIAL; key phrase readable but inaccurate |
| 5 | white outlined two-line subtitle on complex background | `到底在搞什么` / `你不能就这样冲出来` | severe garbage | sparse incorrect fragments | 300x82 -> 600x164 | upscale2x+grayscale+contrast / 6 | 25ms | FAIL |

Bilibili totals:

- Tested: 5
- Completely correct: 0
- Minor/readable errors: 3
- Severe failures: 2
- Empty optimized result: 0
- Exact accuracy: 0 / 5
- Usable accuracy: 3 / 5

This intentionally includes failures rather than selecting only successful
frames. PSM 7 was clearly better for clean wide single-line samples, but it
returned empty text on the tested two-line/complex crops. PSM 6 remains the
multi-line fallback. The Region should be tight: excess background materially
reduces Tesseract accuracy.

## Douyin access result

Douyin's public recommendation and selection pages loaded, but opening a video
presented a mandatory login dialog. Testing stopped there. No login, QR scan,
captcha, or bypass was attempted. Consequently:

- Douyin video subtitle samples: 0
- Douyin OCR statistics: not available due to login requirement

## Same-frame before/after conclusions

The same local input was reused for every comparison. Three especially clear
comparisons are:

1. Bilibili yellow two-line: empty -> partially readable `当我哉肢后|`.
2. Bilibili small yellow outline: empty -> nearly exact with one leading symbol.
3. Small synthetic Chinese: empty -> exact `实时屏幕翻译` in about 12ms.

Counterexamples are equally important: the synthetic white/black outline became
empty after preprocessing, and complex Bilibili backgrounds remained severe
failures. The optimization improves small clean text, not scene text in general.

## Style assessment and decision

| Style | Assessment |
| --- | --- |
| ordinary Chinese subtitles | Partial: good when clean, unreliable on real moving backgrounds |
| white text with black outline | Partial/Poor: sensitive to crop and background; one synthetic regression |
| small text | Good on clean generated text; Partial on real video |
| two-line subtitles | Poor in tested video crops; PSM 6 is necessary but not sufficient |
| dynamic complex background | Poor |
| artistic fonts / multiple overlays | Poor |
| colored subtitles | Partial to Poor |

Tesseract remains useful as the current Phase 5/6 backend for clean ordinary
English and Simplified Chinese and for preserving the working OCR contract. It
is not sufficient as the long-term sole backend for general Bilibili/Douyin or
game subtitle recognition. A later phase should evaluate PaddleOCR or another
scene-text OCR backend specifically because of outlined text, artistic fonts,
small video captions, multi-line layouts, and complex moving backgrounds. No
PaddleOCR code or model is included in Phase 4.1.
