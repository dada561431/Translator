# Offline Clean Windows Acceptance

Status (2026-10-06): **Technical portable acceptance PASS; Clean Windows acceptance
PASS; Physically offline OCR acceptance PASS.** Release redistribution remains
**blocked by OWNER REVIEW REQUIRED** for Qt and VC runtime, plus outstanding license
review. No public release ZIP/SHA256 is authorized or generated.

## Owner-reported Manual Acceptance

Evidence is the owner's manual test report, not a new Sandbox run by the documentation
agent. A clean Windows Sandbox used **Networking=Disable**. The same portable candidate
was first copied into the guest's local filesystem, then executed there.

| Check | Owner-reported Result |
|---|---|
| GUI / PP-OCRv6 Small startup | PASS |
| Manual Region -> Start OCR | PASS |
| Translation Provider | None; no online translation acceptance claimed |
| Self-check | 20/20 PASS |
| Helper PID | 5964, unchanged throughout self-check |
| First request including helper startup | 7332 ms |
| Warm median / mean | 155 ms / 160 ms |
| Warm >300 ms | 0 |
| Stop -> wait -> Start | PASS; helper stays warm, no new cold start |
| Close cleanup | PASS; no Translator.exe or python/helper remains |
| Real network disconnection | PASS; Sandbox Networking=Disable |
| Model downloads | None reported |

Sandbox Windows build, exact guest path, separate READY timing, per-tool inventory,
individual recognized bilingual strings and security-tool observations were not
provided and are not invented. Additional guest Unicode/ACL/hash tests are not
claimed. Prior developer-host isolation tests remain distinct historical evidence.
Only documentation is changed for this record; no SDK/runtime installation, system
policy, production source, helper protocol or timeout change is performed.

## Non-blocking Mapped-folder Limitation

Direct execution from the read-only host-mapped `PortableInput` folder once produced
an approximately 30-second helper startup timeout. After copying the **same candidate**
to the Sandbox local filesystem, GUI, OCR and 20/20 self-check passed. Classification:
**Non-blocking Windows Sandbox host-mapped-folder limitation**. It is not fixed;
root cause is not established, and production timeout is unchanged. This does not
block the accepted local-copy execution flow or prove a cause for separately recorded
developer-host startup failures. Use host mapping for transfer, then run locally.

The procedure below is retained for repeatable future testing, not a claim that
every optional step was performed during this owner's acceptance.

## Prepare the Sandbox

Only map the technical candidate, never the repository, benchmark inputs, venv,
credentials, private screenshots or QA probes. The candidate remains license-review
blocked and is for controlled local technical acceptance, not public redistribution.

On a host where Windows Sandbox is already available, generate a local configuration:

```powershell
$package = (Resolve-Path -LiteralPath '.\dist\TranslatorPortable').Path
$escaped = [Security.SecurityElement]::Escape($package)
$template = Get-Content -LiteralPath '.\scripts\windows-sandbox-offline.wsb.in' -Raw
New-Item -ItemType Directory -Path '.\.cache\sandbox' -Force | Out-Null
$template.Replace('@ABSOLUTE_PORTABLE_FOLDER@', $escaped) |
    Set-Content -LiteralPath '.\.cache\sandbox\Translator-offline.wsb' -Encoding utf8
```

Open the generated .wsb normally. Networking is **Disable**, and the host mapping
is read-only. Do not enable network later to repair a dependency or model error.
If Sandbox cannot launch, record the error and leave all clean/offline gates pending.

## Manual Test

1. Record Sandbox Windows version (`winver`). Confirm no Qt SDK, Python, Paddle,
   MinGW, Tesseract, Visual Studio or source checkout was installed. Install nothing.
2. Copy Desktop/PortableInput to Desktop/Portable Test/TranslatorPortable. This
   copies only the candidate into the disposable guest; do not run in a developer tree.
3. Double-click Translator.exe. Record GUI appearance, Settings opening and
   PP-OCRv6 Small availability. Do not bypass a security block. Cancel Settings
   unless changing the OCR engine is necessary. Leave Translation Provider **None**
   so OCR acceptance makes no provider/network request.
4. Open Notepad and enter `你好世界`, then `Hello Portable OCR`. With Region, select
   each actual rendered line; record visible text and OCR output. Use Start and Stop
   to exercise the real production capture path. No video or synthetic benchmark
   download is needed, and no extra runtime may be installed.
5. From the guest's normal PowerShell, run 20 production-engine requests:

   ```powershell
   $app = "$env:USERPROFILE\Desktop\Portable Test\TranslatorPortable\Translator.exe"
   $report = "$env:USERPROFILE\Desktop\portable-self-check.json"
   & $app --self-check --report $report
   Get-Content -LiteralPath $report -Raw
   ```

   Check 20 successes, one helper PID, warm median/mean and >300ms count. This
   self-check is synthetic OCR through the production engine, not the manual Region
   test. For separate READY/first measurements, preserve helper diagnostics to the
   guest user-cache and explicitly record unavailable metrics rather than inventing
   them; the combined first-request self-check time includes helper startup.
6. In the GUI, Start, obtain OCR, Stop, wait, and Start again. Observe the same
   python.exe/helper PID in Task Manager Details; verify no cold reload and reasonable
   warm OCR latency. Close using the toolbar and confirm both app and helper disappear.
7. Repeat after copying into `Desktop\Portable Test\翻译工具\TranslatorPortable`.
   Check app/plugins/models, no install-directory pyc, and unchanged six model hashes.
8. Optional host-mapped direct execution is a separate diagnostic, not the required
   local-copy acceptance path: the startup timeout above is a known non-blocking
   limitation. Do not change host ACLs/system policy or production timeout to pass.
   User Cache/Temp/QSettings writes are permitted; record any optional test separately.
9. Record whether Defender/SmartScreen intervened. Unsigned release engineering
   limitations must remain visible; do not disable security tools to pass.

Keep evidence in the guest/user-local ignored cache, not Git. Networking=Disable
must be confirmed in the launched configuration, with no guest network route.
This is actual network isolation, unlike HF offline flags alone. Verify no attempted
model download from diagnostics; note that absence of log messages is not packet
capture proof. A launch/OCR failure in the required local-copy flow must stop that
acceptance gate; optional host-mapped failures are recorded separately.

## Record and Release Decision

Record Windows version, pristine-environment checks, offline configuration, text
outputs, 20 request report, helper PID, Stop/Start and Close, path tests, security
events and evidence location. Each unexecuted item stays **PENDING**. Only after
the required clean/offline flow passes and owner/license release gates are cleared
may a public release ZIP and SHA256 be generated. Technical/clean/offline acceptance
now passes on the owner-reported evidence; Qt and VC redistribution remain
OWNER REVIEW REQUIRED. Do not declare public-release ready, implement an Installer,
or start Phase 7 or Audio/ASR.
