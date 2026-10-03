# Phase 5.1: Provider / Model / Credential Settings

## Completion boundary

Implementation and offline verification are complete. Real DeepL GUI-key
acceptance and real OpenAI-compatible translation remain pending: no legitimate
provider credentials were configured on this machine. No Phase 6 was started.
One Region still produces one capture, one OCR, and one optional translation.

Initial Git: clean main tracking origin/main, HEAD
a0e0cbb2cedf90074f01c33f573e097d93fe22ba (completed Phase 5). Single-agent work;
no subagents, additional worktrees, resets, or force pushes.

## Provider and backend architecture

TranslationProviderRegistry is a static table with ID, display name, key,
endpoint, model, auto-source, default endpoint, and plan capabilities. Settings
validation and combo population use this table. It is not a plugin system.

| Provider | ID | Key | Model | Configuration |
| --- | --- | --- | --- | --- |
| None | none | no | no | none |
| DeepL | deepl | yes | no | Free/Pro plan |
| OpenAI Compatible | openai_compatible | yes | yes | API base URL and arbitrary model ID |

TranslatorFactory resolves the current provider configuration and secure key,
then constructs a DeepLTranslator or OpenAiCompatibleTranslator. None returns
nullptr without reading credentials or sending translation requests. Backend
constructors receive configuration snapshots; they know neither SettingsDialog
nor Win32 APIs. ITranslator and the shared TranslationResult remain the contracts;
the result adds optional model metadata.

TranslationCoordinator accepts a factory callback and owns the current backend.
Effective provider, endpoint, model, language, plan, or credential changes
invalidate its active ID and recreate the backend. The monotonic request counter
does not reset. Old replies cannot change the current UI. Existing single-backend
injection is retained for the Phase 5 FakeTranslator regression suite. Coordinator
contains no DeepL-specific language-code or HTTP logic.

```text
SettingsDialog -> Registry metadata -> provider-specific fields
SettingsDialog -> SettingsManager (non-secret config) -> translationSettingsChanged
SettingsDialog -> ICredentialStore -> WindowsCredentialStore
translationSettingsChanged -> invalidate -> TranslatorFactory -> ITranslator

Region -> CaptureResult -> OcrCoordinator -> IOcrEngine
 -> OCR preprocessor -> Tesseract -> OcrResult
 -> Original QLabel + TranslationCoordinator -> ITranslator
 -> DeepL / OpenAI Compatible -> TranslationResult
 -> latest-ID validation -> TranslationWindow state / Translated QLabel
```

LunaTranslator reference read-only: gui/setting/translate.py separates
is_gpt_like translation providers and populates visible providers with stable
IDs and dynamic names. This motivated separating provider capabilities from
model IDs; no Python files were copied, ported line by line, or modified.

## Settings and credential lifecycle

The compact form contains source/target language, OCR engine, Translation
Provider, conditional Plan/Base URL/Model, API-key status/actions, and privacy
notice. None hides all provider fields. DeepL shows Plan and key actions but no
model. OpenAI Compatible shows Base URL, Model, and key actions but no DeepL Plan.

The existing source/target/provider selection behavior remains immediate;
DeepL Plan changes are also immediate. Base URL/Model drafts and API-key changes
apply on Apply/OK. Cancel does not undo previously immediate selections, but
does discard endpoint/model drafts and all unsaved key replacement/removal.
This intentionally preserves existing settings interactions rather than silently
changing the whole dialog to a transaction.

Existing credentials are never placed into an input widget. Status says Configured,
Not configured, or Environment fallback. Configure / Replace opens an empty
Password editor; Show reveals only the user's new input temporarily. Changing
provider, applying, or cancelling restores masking. Provider-specific key drafts
remain in memory until Apply/OK. An empty editor means unchanged, not delete.
Remove stages deletion; Cancel cancels it, Apply/OK commits it. Store failures
are displayed as sanitized errors. Multiple provider drafts are committed per
provider; the OS store does not offer a cross-provider atomic transaction.

QSettings contains only these translation configuration keys:

```text
translator/provider
translator/deepl/plan
translator/openaiCompatible/baseUrl
translator/openaiCompatible/model
```

Existing language, OCR, region, and geometry settings remain. Migration copies
legacy translator/engine if translator/provider is absent, then removes the old
key. The new key wins if both exist; invalid IDs fall back to None. Public
translator()/setTranslator() methods remain for calling-code compatibility.
No API-key field is added to SettingsManager or QSettings.

## Windows secure storage

ICredentialStore exposes saveSecret(provider, secret), loadSecret(provider), and
removeSecret(provider), with optional sanitized error output. Provider ID scopes
the account in this phase; multiple profiles per provider are not implemented.

WindowsCredentialStore uses official CredWriteW, CredReadW, CredDeleteW, and
CredFree. Generic credentials use UTF-8 blobs, the documented size limit, and
CRED_PERSIST_LOCAL_MACHINE (persistence for this Windows user on this machine).
Targets are stable:

```text
Translator/Translation/deepl
Translator/Translation/openai_compatible
```

Native blobs are zeroed before release where available. Qt QString copies and
the active HTTP backend still hold keys in process memory; this is not a claim
that every memory copy is wiped or inaccessible to another process under the
same Windows account. There is no homemade encryption, Base64/XOR storage,
credential export, or plaintext fallback. Windows APIs appear only in this adapter;
advapi32 is linked only under WIN32. Other platforms fail secure storage
operations explicitly; implementations for their secure stores are future work.

Credential priority is GUI store -> DEEPL_API_KEY -> missing. A store-read error
is surfaced rather than silently bypassed. OpenAI Compatible has no implicit
environment key fallback. Removing a GUI DeepL key still leaves an independently
configured environment fallback active; remove that variable too to disable it.

Before a GUI plan is saved, DEEPL_API_URL retains Phase 5 behavior. After applying
a GUI plan, the selected official Free/Pro endpoint takes precedence. DeepL HTTP
format, HTTPS checks, language mapping, TLS, timeout, and parser are preserved.

## OpenAI-Compatible protocol and security

The implemented profile is non-streaming Chat Completions. User configures the
API base URL (including a path prefix such as /v1), an API key, and a model ID.
No default commercial host/model, model list discovery, or unofficial website
API is used. The backend appends /chat/completions and sends JSON model/messages/
stream=false with Bearer authorization. It reads one assistant message's string
content and requires finish_reason=stop. Missing content, empty text, invalid JSON,
provider errors, refusal, truncation, and tool-call endings fail explicitly.
An arbitrary compatible service/model may not implement this exact profile.

The fixed system message specifies source/target languages and faithful translation
only, with no explanations, and says user content is literal translation input.
OCR text is a separate user message, never concatenated into system instructions.
Output cleanup is trimmed() only, not speculative regex rewriting. Optional
temperature/top_p/token knobs are omitted because compatible models differ and
some reject temperature; stable deterministic output is not guaranteed. Services
requiring extra mandatory vendor-specific fields are unsupported in this profile.

HTTPS remote URLs are accepted; HTTP only for literal localhost, 127.0.0.1, and
::1. Remote HTTP, userinfo, query, fragments, and a mistakenly supplied full
completion URL are rejected before credentials are sent. Redirects are not
followed. TLS validation is enabled. Transfer timeout plus a 15-second absolute
per-reply timer bounds requests. DNS/connection/TLS failures, HTTP authentication/
rate-limit/other errors, invalid JSON, and empty outputs become shared result
errors without logging authorization or raw response bodies. There is no retry.

Only recognized text and translation metadata are sent. Images, region pixels,
last_capture.png, and credentials in request JSON are never sent. Credentials
are sent only in the authorization header to the explicitly configured endpoint.
Each provider has its own privacy policy. None makes no translation network call.

Protocol sources checked before implementation:

- [Official OpenAI Chat Completions reference](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create)
- [Microsoft CredWriteW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-credwritew)
- [Microsoft CredReadW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-credreadw)
- [Microsoft CREDENTIALW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/ns-wincred-credentialw)

## Verification on 2026-10-03

Configure and build succeeded with Qt 6.11.2, MinGW 13.1, CMake, Ninja, C++17.
The optional Vulkan header warning is unrelated to this Widgets application.

```powershell
$env:PATH='D:\QT\Tools\mingw1310_64\bin;D:\QT\6.11.2\mingw_64\bin;'+$env:PATH
& 'D:\QT\Tools\CMake_64\bin\cmake.exe' -S . -B build/phase51 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=D:/QT/6.11.2/mingw_64 -DCMAKE_CXX_COMPILER=D:/QT/Tools/mingw1310_64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=D:/QT/Tools/Ninja/ninja.exe
& 'D:\QT\Tools\CMake_64\bin\cmake.exe' --build build/phase51 --parallel 4
& 'D:\QT\Tools\CMake_64\bin\ctest.exe' --test-dir build/phase51 --output-on-failure
& '.\build\phase51\TranslatorCredentialProbe.exe' --self-test
& '.\build\phase51\TranslatorCredentialProbe.exe' --status
& '.\build\phase51\TranslatorDeepLProbe.exe'
& '.\build\phase51\TranslatorDeepLProbe.exe' --stored-credentials
```

Paths above are local commands, not hardcoded Qt/compiler locations in CMake.

All six CTest suites pass: phase51_provider_and_credentials, phase5_translation,
phase2_ui_and_settings, phase3_capture_logic, phase4_ocr_logic, phase41_ocr_tuning.
No tests were removed. Phase 2's persisted-key assertion now checks the migrated
translator/provider and absence of the old key; Phase 5's combo description now
says None/DeepL are retained, rather than implying they are the only providers.
All behavioral assertions remain. The previous NOMINMAX macro redefinition
warning was fixed with a guard, not suppressed compiler diagnostics.

New tests cover Registry metadata, DeepL/None migration and new-key precedence,
memory save/load/replace/remove, environment fallback and GUI priority, dynamic
field visibility, default masking and explicit reveal, Cancel, empty input,
Apply replacement/removal, store errors, invalid URL rejection before saving,
model/URL persistence, and absence of keys/secrets in QSettings values. Generated
test secrets are also checked against captured Qt logs without printing them.

Mock HTTP checks URL, method, authorization, arbitrary model, source/target prompt,
auto source, separate user text containing an injection instruction, unified
results, HTTP/redirect/TLS/JSON/empty/truncated failures, timeout, missing config,
same-language local return, and HTTPS/loopback safety. Provider/model/endpoint/
credential changes invalidate delayed responses. Phase 5 still checks B-before-A,
duplicate response rejection, None, missing key, and responsive event processing.

Injection test is structural: it verifies separate system/user messages and
propagation of a fixture that translates the malicious sentence. It does not
measure a real model's resistance or guarantee that an arbitrary model cannot
follow malicious OCR content.

Ordinary CTest never writes Windows Credential Manager: MemoryCredentialStore
is test-only. The separately invoked manual credential probe uses a UUID-based
Translator/Translation/SelfTest namespace, never replaces production targets,
and removes its synthetic credential. Result: saved=1, loaded=1, replaced=1,
removed=1, absent=1. Production status check: deepl stored=0, openai_compatible
stored=0, both readError=0. No credential export or secret content was printed.

Real DeepL environment test: SKIPPED - DEEPL_API_KEY not configured.
Real DeepL GUI-only test: SKIPPED - no GUI DeepL credential configured.
Real OpenAI-compatible provider: SKIPPED - no user provider credentials configured.
No real EN->ZH / ZH->EN result or provider latency is claimed.

Production Translator launched and its process reported Responding=True. Native
UI tooling returned empty black captures of the transparent window and twice
failed activation with "failed to activate captured window"; desktop Settings
interaction and Region-to-translation acceptance therefore remain unverified.
Offscreen Qt Settings screenshot was rendered with a locally supplied test font
and visually inspected for field placement and clipping. It uses an in-memory
Configured fixture, not a real saved key. Image is only in the system temp directory,
not Git. No key appears in it. Automated visibility and lifecycle checks passed.

## Files and requested completion report

Modified: CMakeLists.txt, README.md, docs/architecture.md,
src/app/TranslationCoordinator.h/.cpp, src/config/SettingsManager.h/.cpp,
src/gui/SettingsDialog.h/.cpp, src/gui/TranslationWindow.h/.cpp, src/main.cpp,
src/translator/DeepLTranslator.h/.cpp, src/translator/TranslationTypes.h,
tests/DeepLProbe.cpp, tests/Phase2UiTest.cpp, tests/Phase5TranslationTest.cpp.

Added: src/credentials/ICredentialStore.h, WindowsCredentialStore.h/.cpp;
src/translator/TranslationProviderRegistry.h/.cpp, TranslatorFactory.h/.cpp,
OpenAiCompatibleTranslator.h/.cpp; tests/MemoryCredentialStore.h,
Phase51ProviderTest.cpp, CredentialProbe.cpp; this document.

1. Phase 5.1 implementation/offline tests complete; real-provider acceptance pending.
2. Modified files listed above.
3. Added files listed above.
4. Registry is one capability table used for UI and ID validation.
5. Providers: None, DeepL, OpenAI Compatible.
6. Compact form with conditional fields and key actions described above.
7. DeepL and OpenAI Compatible require a key; None does not.
8. Only OpenAI Compatible has a user-configurable model.
9. GUI keys are saved in Windows Credential Manager, not QSettings.
10. Stable production target names listed under secure storage.
11. Four non-secret translator QSettings keys listed above.
12. Key absence in QSettings is checked automatically.
13. DEEPL_API_KEY fallback preserved.
14. Stored GUI key wins; read errors are surfaced.
15. DeepL receives resolved config from TranslatorFactory.
16. Compatible config is Base URL / API Key / Model, without guessed defaults.
17. Remote HTTPS / literal loopback HTTP, no userinfo/query/fragment/redirect.
18. Model is a free-text QLineEdit persisted only on Apply/OK.
19. Concise translation-only system message; literal user message.
20. Injection message-boundary test passes; real-model resistance unverified.
21. Provider switching rejects late old-provider replies.
22. Model switching rejects late old-model replies.
23. Legacy engine key migrates without resetting unrelated settings.
24. Memory lifecycle, UI and credential-priority tests pass.
25. Manual isolated Windows API lifecycle passes; test credential removed.
26. Phase 5 DeepL regression suite passes unchanged behavior.
27. Mock compatible HTTP/error/deadline tests pass without Internet.
28. Real DeepL GUI credentials: SKIPPED (no configured GUI credential or env key).
29. Real compatible provider: SKIPPED (no endpoint/key/model supplied for acceptance).
30. No real key is present in source. Phase 5.1 test keys are generated at runtime;
    the historical Phase 5 non-secret placeholder fixture is retained for regression.
31. Keys absent from captured test logs; production never logs authorization/raw bodies.
32. No real key or credential export is included in the commit; staging is reviewed.
33. CTest: 6 total, 6 passed, 0 failed.
34. Configure succeeded.
35. Build succeeded.
36. README now documents ordinary GUI use, secure storage, fallback, models and privacy.
37. Architecture now documents Registry / Factory / ICredentialStore boundaries.
38. This document: docs/translation-provider-phase51.md.
39. Commit hash is reported in the final response, not self-referenced here.
40. Push result is checked and reported in the final response.
41. Original LunaTranslator remained clean and read-only.
42. Complete translation flow is recorded at the start of this document.
43. Phase 6 would add triggering/lifecycle above existing coordinators, preserving
    latest-ID behavior and configuration invalidation; it is not implemented here.
44. Explicitly stopped before Phase 6 / real-time pipeline.
