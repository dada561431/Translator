# Offline Clean Windows Acceptance

Status: **PENDING / NOT EXECUTED**. The current Windows 11 developer host has no
discoverable WindowsSandbox.exe or accessible VM/second PC. No optional feature,
security policy, Defender or SmartScreen setting was changed. Local isolation
does not count as clean Windows acceptance. This procedure is for an owner-provided
Sandbox-capable host, not permission to enable virtualization or install SDKs.

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
8. Optionally run from the read-only host mapping to exercise deny-write behavior.
   Do not change host ACLs or system policy. Record limitations if guest read-only
   mapping cannot support this test. User Cache/Temp/QSettings writes are permitted.
9. Record whether Defender/SmartScreen intervened. Unsigned release engineering
   limitations must remain visible; do not disable security tools to pass.

Keep evidence in the guest/user-local ignored cache, not Git. Networking=Disable
must be confirmed in the launched configuration, with no guest network route.
This is actual network isolation, unlike HF offline flags alone. Verify no attempted
model download from diagnostics; note that absence of log messages is not packet
capture proof. A launch/OCR failure must stop the corresponding acceptance gate.

## Record and Release Decision

Record Windows version, pristine-environment checks, offline configuration, text
outputs, 20 request report, helper PID, Stop/Start and Close, path tests, security
events and evidence location. Each unexecuted item stays **PENDING**. Only after
all clean/offline tests pass and owner/license release gates are cleared may a
release ZIP and SHA256 be generated. Do not install an Installer or start Phase 7.
