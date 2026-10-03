# Phase 5: Translation Backend

## Scope and implementation

Phase 5 implementation and offline verification are complete. Real DeepL
integration and manual Region-to-translation acceptance remain pending because
`DEEPL_API_KEY` was not configured. No Phase 6 work was started.

```text
RegionSelector -> ScreenCaptureService -> CaptureResult.image
 -> OcrCoordinator -> IOcrEngine -> OCR preprocessing -> Tesseract
 -> OcrResult -> original subtitle + TranslationCoordinator
 -> ITranslator -> DeepLTranslator -> TranslationResult
 -> latest-request validation -> translated subtitle
```

`main.cpp` assembles services and connects the OCR signal to the coordinator.
OCR runs on its existing worker thread; translation uses the GUI event loop's
asynchronous QtNetwork callbacks, with no blocking/nested event loop in production.
Tests may use a bounded local event loop to await their fixtures.

The coordinator owns one backend, rejects empty/error OCR, creates monotonically
increasing IDs, and accepts only the outstanding ID. Completion retires that ID,
also rejecting duplicates. A new capture, new OCR, or changed source/target/provider
invalidates pending translation. Old network work is allowed to complete; its
response cannot update subtitles. It may still consume provider quota. There is
no cache, retry, queue, or continuous screenshot loop.

TranslationRequest carries requestId, sourceText, sourceLanguage, targetLanguage.
TranslationResult additionally carries translatedText, success, error, provider,
httpStatus, elapsedMs. ITranslator is a QObject with asynchronous translate(request),
id(), and resultReady(result). It accepts text, never QImage.

## Official DeepL protocol

The implementation was checked against current official documentation:

- [Translate request](https://developers.deepl.com/api-reference/translate/request-translation)
- [Authentication](https://developers.deepl.com/docs/getting-started/auth)
- [Supported languages](https://developers.deepl.com/docs/getting-started/supported-languages)
- [Official OpenAPI specification](https://github.com/DeepLcom/openapi)

POST JSON to the official Free or Pro `/v2/translate` endpoint. Authorization
uses `DeepL-Auth-Key` with the environment credential. Body contains text as a
single-element array, target_lang, and optional source_lang. Success reads
translations[0].text, not a guessed translated_text field. Request bodies exceeding
128 KiB are rejected locally. Only official HTTPS hosts/path/port are accepted;
userinfo, query, fragments, and automatic redirects are prohibited.

| App ID | DeepL source | DeepL target |
| --- | --- | --- |
| auto | omit source_lang | invalid |
| en | EN | EN-US |
| zh | ZH | ZH-HANS |
| ja | JA | JA |
| ko | KO | KO |

Translation auto uses provider detection; OCR auto remains the existing eng
fallback. Equal supported source and target IDs return the source text locally
with a normal result and requestId. None disables translation even for this case.

## Configuration and privacy

Default translator/engine is none; Settings offers only None and DeepL. Existing
QSettings writes remain immediate. SettingsManager now emits a notification when
translation-related values actually change. FakeTranslator is test-only.

Set DEEPL_API_KEY in the launching process environment or Qt Creator's run
environment. Restart the application after environment changes. No API-key UI,
QSettings credential entry, or credential file is created. Future credential UI
should use a secure store. Never put a key in source, screenshots, logs, commits,
or this document. Only presence was checked during verification; no key was printed.

DEEPL_API_URL defaults to https://api-free.deepl.com/v2/translate. Pro users set
https://api.deepl.com/v2/translate; the key's string format is not used to guess
subscription type. Test injection uses an in-memory network manager and a clearly
non-secret placeholder; it never contacts a server.

Selecting DeepL sends recognized text to DeepL. OCR remains local; the complete
online workflow is not wholly local. No image, screenshot, last_capture.png, or
region pixel is uploaded. None sends no translation request. Debug logging includes
IDs, source/target IDs, character counts, status, elapsed times, and sanitized
errors; it does not print authorization, raw provider responses, or translation
text. Existing OCR Debug diagnostics still follow Phase 4.1 behavior.

## Errors and UI

Idle restores the translation placeholder; Pending shows 翻译中…; Success replaces
it with current translated text; Error shows 翻译失败 and preserves the original.
Translation stays above Original with a larger font. QLabel PlainText prevents
OCR/provider strings being interpreted as markup. Drag/hover/geometry behavior
is unchanged. Start/Stop still controls UI state, not periodic capture.

Local errors include missing key, unsupported language/empty target, empty text,
invalid endpoint, malformed key header, and oversize request. Backend handles DNS,
connection, TLS, timeout, HTTP 4xx/5xx, 401/403 authentication, 429 rate limit,
456 quota, invalid JSON, missing/empty translations/text, and provider errors.
Each request uses transfer timeout plus a 15-second absolute per-reply timer that
aborts the reply. No SSL errors are ignored. No raw error response is logged.

## Verification: 2026-10-03

Initial state: clean main tracking origin/main, HEAD
1d2f34d739f1e146e0f536fff861ee66d3b3bbda, containing completed Phase 4.1.
Original LunaTranslator reference remained clean and was not edited.

Commands executed in D:\workspace\Project\Usst\Translator:

```powershell
$env:PATH='D:\QT\Tools\mingw1310_64\bin;D:\QT\6.11.2\mingw_64\bin;'+$env:PATH
& 'D:\QT\Tools\CMake_64\bin\cmake.exe' -S . -B build/phase5 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=D:/QT/6.11.2/mingw_64 -DCMAKE_CXX_COMPILER=D:/QT/Tools/mingw1310_64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=D:/QT/Tools/Ninja/ninja.exe
& 'D:\QT\Tools\CMake_64\bin\cmake.exe' --build build/phase5 --parallel 4
& 'D:\QT\Tools\CMake_64\bin\ctest.exe' --test-dir build/phase5 --output-on-failure
& '.\build\phase5\TranslatorDeepLProbe.exe'
& '.\build\phase5\TranslatorOcrBenchmark.exe' '.\build\phase41\ocr-samples\english-hello.png' en
& '.\build\phase5\TranslatorOcrBenchmark.exe' '.\build\phase41\ocr-samples\zh-yahei.png' zh
```

Local paths above are verification commands only, not hardcoded into CMake.
Configure succeeded (optional Vulkan-header warning, not a required dependency).
Build succeeded including Translator.exe, tests, and manual probes.
First CTest run: 4/5 passed; the old Phase 2 assertion expected a now-obsolete
"Backend is not implemented yet" Start status. Updated that expectation to
"Started." while retaining Start/Stop state assertions. Final run: 5/5 passed,
0 failed, 3.21 seconds. No test was removed.

| CTest | Result |
| --- | --- |
| phase5_translation | PASS |
| phase2_ui_and_settings | PASS |
| phase3_capture_logic | PASS |
| phase4_ocr_logic | PASS |
| phase41_ocr_tuning | PASS |

Phase 5 offline checks cover blank/error OCR, increasing IDs, request metadata,
success, B-before-A order, duplicate replies, backend error, invalidation on new
input/None/language changes, same-language local result, source-settings mismatch,
None/DeepL persistence, missing-key backend-to-UI behavior, async return, official
POST/JSON/auth mapping, auto omission, UI event processing while a network reply
is pending, timeout and redirect policy, HTTP errors,
DNS/connection/TLS errors, malformed/missing/empty JSON, deadline abort, and unsafe
endpoint rejection. UI tests are offscreen, not a manual desktop Region test.

Fixed English OCR sample: Hello Phase 4 (520x80 -> 1040x160, PSM 7,
automatic preprocessing, total 9 ms after warmup). Fixed Chinese sample:
你好世界 (520x90 -> 1040x180, PSM 7, total 11 ms after warmup). Baseline first
calls including initialization were 77 ms and 129 ms respectively. No OCR
preprocessor or PSM selection algorithm changed in Phase 5.

Production executable startup smoke: process remained alive after two seconds
and was terminated by the test. This establishes startup only, not manual
drag/close interaction. Existing offscreen UI tests cover those UI contracts;
native desktop/manual Region-to-translator testing was not performed this run.

Real provider test: **SKIPPED - DEEPL_API_KEY not configured**.
EN -> ZH and ZH -> EN manual tests remain pending; no real API elapsed measurement
exists. Debug instrumentation records OCR completion UTC, translation start UTC,
reply elapsedMs, and UI update UTC for a later legitimate-key run. Synthetic HTTP
fixtures validate deadline behavior but are not real-network latency measurements.

## Manual acceptance still required

With a legitimate configured key, select DeepL/en/zh and Region over
"Hello, this is a translation test." in Notepad. Verify immediate original,
Pending, then reasonable Chinese above it while dragging/clicking stays responsive.
Repeat zh/en with 你好世界. Check blank OCR invalidates the previous translation,
None preserves OCR without network, missing key shows lightweight failure, and
two rapid Regions cannot let an older response overwrite the newest. Stable
response disorder is already enforced by FakeTranslator tests.

## Changed files and completion report index

Modified: .gitignore, CMakeLists.txt, README.md, docs/architecture.md,
src/app/OcrCoordinator.cpp, src/config/SettingsManager.h/.cpp,
src/gui/SettingsDialog.cpp, src/gui/TranslationWindow.h/.cpp, src/main.cpp,
src/ocr/OcrTypes.h, tests/Phase2UiTest.cpp.

Added: src/app/TranslationCoordinator.h/.cpp, src/translator/TranslationTypes.h,
ITranslator.h, TranslationLanguageMapper.h/.cpp, DeepLTranslator.h/.cpp,
tests/Phase5TranslationTest.cpp, tests/DeepLProbe.cpp, docs/translation-phase5.md.

The requested completion report items are covered as follows:

1. Implementation/offline complete; real-provider acceptance pending, as above.
2. Initial clean main and Phase 4.1 commit recorded under Verification.
3. Modified files listed above.
4. Added files listed above.
5. TranslationRequest structure described under implementation.
6. TranslationResult structure described under implementation.
7. ITranslator API described under implementation.
8. Coordinator responsibilities and ownership described under implementation.
9. Actual OCR connection is the resultReady lambda in main.cpp.
10. Only real provider: DeepL.
11. Official API documentation links and protocol recorded above.
12. QtNetwork uses POST and finished callback.
13. No production blocking/nested network event loop.
14. Key read from DEEPL_API_KEY only.
15. No credentials committed; local credential/build/capture rules in .gitignore.
16. Official Free default and Pro environment URL documented above.
17. Mapping table above includes all five source IDs/four target IDs.
18. Auto omits source_lang, independent of OCR fallback.
19. Same language returns source locally with normal request identity.
20. Blank OCR invalidates prior work and never invokes backend.
21. OCR errors retain existing feedback and do not invoke backend.
22. Only active ID accepted; settings/input/completion invalidate it.
23. Fake B then A and duplicate B checks passed.
24. Fake backend-error check passed; original preserved.
25. Missing-key checks passed, including UI integration with zero network calls.
26. HTTP/JSON/transport fixtures passed; explicit sanitized errors.
27. Transfer timeout plus absolute deadline; hanging fixture aborted successfully.
28. Normal TLS validation; no ignoreSslErrors; no automatic redirects.
29. Pending displays 翻译中… and removes old translated content.
30. Success displays current result above original.
31. Error displays 翻译失败 without replacing original.
32. None preserves original and shows placeholder without translation calls.
33. Real EN -> ZH: SKIPPED (no key).
34. Real ZH -> EN: SKIPPED (no key).
35. Key absence is the explicit reason, not a fabricated provider success.
36. Real API latency unavailable; instrumentation present.
37. Existing OCR regression and English/Chinese fixed samples passed.
38. Final CTest: 5 total, 5 passed, 0 failed.
39. Configure succeeded.
40. Build succeeded.
41. README documents Phase 5, credentials, mapping, privacy, and one-shot behavior.
42. Architecture retains OCR backend and adds independent translation backend.
43. This report: docs/translation-phase5.md.
44. Final repository status is checked after commit/push and reported in the response.
45. Commit hash is reported in the final response (cannot self-reference its hash).
46. Push result is verified and reported in the final response.
47. Reference LunaTranslator status was clean; no files edited there.
48. Complete data flow is recorded at the start of this document.
49. Phase 6 may add trigger/lifecycle scheduling above capture/OCR/translation
    coordinators, preserving latest-request protection; no such work was done.
50. Explicitly stopped at Phase 5; no realtime video translation implemented.
