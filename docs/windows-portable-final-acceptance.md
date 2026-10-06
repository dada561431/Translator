# Windows Portable Final Acceptance

Date: 2026-10-06. **Technical acceptance PASS; redistribution release BLOCKED.**

- Technical portable acceptance: **PASS**
- Clean Windows acceptance: **PASS**
- Physically offline OCR acceptance: **PASS**
- Release redistribution gate: **still blocked by OWNER REVIEW REQUIRED**

Phase 6.1C is not FULLY COMPLETE as a public release. The folder remains a technical
candidate, not a cleared redistributable release.
No ZIP, installer or Phase 7 work was started. No production OCR, IPC, pipeline,
translation, filtering or GUI behavior was changed in this task.

## Evidence and Sources

The Sandbox results below are **owner-reported manual acceptance evidence**, supplied
on 2026-10-06, not a Sandbox run performed by the documentation agent. This documentation
follow-up started from clean main at `04fdc818df5d8e608b03b023f09d137479f30135`.
No production source, runtime, model, protocol or timeout was changed or rebuilt for
this manual record. Historical developer-host measurements remain separate.

Initial Git: clean `main...origin/main` at
`ec11f5e339de45d03ba9defa42b3ae8e5d8e919b`. Phase 6.1B `0644b4f` and Phase 6.1A
ancestors were retained. Candidate manifest identifies that production source commit
with `source_dirty=true` because acceptance scripts/docs were uncommitted at build.
The final infrastructure commit is reported after commit/push; the report is not
an assertion that this dirty-state candidate is a final public release artifact.

Microsoft source: [official x64 VC Redist](https://aka.ms/vc14/vc_redist.x64.exe),
version **14.51.36247.0**, SHA256
`843068991daaa1f73ad9f6239bce4d0f6a07a51f18c37ea2a867e9beca71295c`.
Installer and each DLL passed Microsoft Authenticode validation. Burn CAB payload
boundaries and signed-manifest size/hash were checked before extracting DLLs as
data. No MSI installation, no System32/installed-runtime copying, no whole VS/SDK
redistribution. The build-only archive utility is not shipped. Runtime provenance
and complete extracted Microsoft terms are in candidate `licenses/microsoft-vc/`;
tracked metadata is `scripts/vc-runtime.json`.

| Actual App-local DLL | Bytes | SHA256 |
|---|---:|---|
| msvcp140.dll | 643512 | 7c26614e1d733892c2deac7e245ce115504b1d80592dd0a01b08e3e5a55f89ca |
| vcomp140.dll | 212920 | 95d4ce4a6802d1e18b5e0e1722cc30ea72ca7e033f83828f05c0b7b993fe7cbf |
| vcruntime140.dll | 178616 | d1f4225df2cd877dbf130d5668a021dce3f94118455ff5ec952061c30afc9ce7 |
| vcruntime140_1.dll | 50112 | a7146c08f89fe5b04541ab507cdb59ff7b44534d4ba3c668a426c6450a03434e |

All four are one matching version cohort. The import list selected actual standard
CRT dependencies, not a guessed collection; existing hash-renamed wheel DLLs are
retained unchanged. Real paddle/paddleocr/paddlex/cv2/pyclipper/shapely imports passed
with minimal PATH, incorrect PYTHONHOME/PYTHONPATH and a non-repository working
directory. GetModuleFileNameW confirmed all four standard CRTs were loaded from
candidate `ocr/runtime/`, not System32. The ccache discovery warning is build-cache
advice, not a missing production dependency; it is not used to waive any PE gate.

Static audit: **262 binaries, 0 missing candidates**. It includes import and delay
import tables. Windows API candidates remain OS-provided; unchanged fail-closed CRT
rules require bundled CRTs. Static candidate presence is not loader-location proof
for every DLL; true clean Windows testing is still mandatory. Manifest/hash/model
checks passed, with 15,736 payload files, zero unmanifested/prohibited artifacts and
no development paths in manifest. Six model hashes are checked independently of
the generated manifest against Phase 6.1A's tracked catalog. Required helper/runtime
and qwindows plugin checks passed. mkldnn.dll remains shipped and vcomp is supplied.

Model evidence: fixed official det revision `106c97591b235f607453300d9fc8c1cad1b25488`
and rec revision `bd619643acac4b9650c040234da8d944476ee3f1`; both cards declare
Apache-2.0 and their LFS weight hashes equal the already verified assets. Card and
license hashes are in `scripts/runtime-notices.json`. This is independent model
evidence, not an assumption from PaddleOCR's package license. No new weights were
downloaded. Model/native redistribution review remains required.

## Local Results (Not Clean-machine Acceptance)

| Test | Measured Result |
|---|---|
| Native candidate self-check | 20/20; helper PID 39968; first combined startup/OCR 6550 ms; warm median 142 ms, mean 141 ms, >300 ms 0 |
| Final validator rerun | 20/20; PID 44084; first combined 6899 ms; warm median 133 ms, mean 135.05 ms; >300 ms 0; manifest and 262/0 audit passed again |
| Direct helper IPC smoke | READY 6486.40 ms; first OCR after READY 180.77 ms; 20/20 on PID 13424; warm median 153.87 ms, mean 154.53 ms; >300 ms 0 |
| Moved Unicode/spaces helper smoke | READY 8013.23 ms; first OCR 228.96 ms; 20/20 on PID 45156; warm median 152.79 ms; mean 154.27 ms; >300 ms 0 |
| Read-only native self-check rerun | 20/20 on PID 34244; first combined call 6568 ms; warm median 152 ms; mean 152.37 ms; >300 ms 0 |
| Final QA harness rerun | 20/20 on PID 40204; median 140 ms, mean 144.05 ms, >300 ms 0; same-PID Stop/Start on 44780, restart 124 ms; immutable/no-pyc/no-orphan checks passed |
| Read-only real capture pipeline probe | Actual QScreen capture of locally rendered Hello Portable OCR; 57 captures, 55 unchanged frames; original subtitle visible |
| Stop/Start on read-only moved copy | PID 35684 retained; first combined call 6393 ms; restart OCR 144 ms (recognition 143 ms); Stop halted capture |
| Close cleanup | Native self-check and live probe helper PIDs gone after owner exit; normal GUI toolbar Close removed app 42236/helper 32944 |
| Immutable install | Inherited deny-write ACL on task-owned QA copy; write attempt denied; six bundled model hashes unchanged; no pyc; original ACL restored |
| GUI | Real qwindows GUI visible, Settings opened, PP-OCRv6 Small available; Cancel/Escape without applying changes; normal Close tested |
| Translation | Provider None, no network translation requests; translation integration was not redesigned or newly live-tested |

The first QA harness invocation used an incorrect self-check flag and was terminated
on its 120-second harness deadline; this was a harness error, not an OCR pass.
After correcting the flag, an actual native self-check reported helper timeout at
30,106 ms (PID 0). Direct helper smoke and the subsequent native rerun passed using
the same copy. **The startup failure is retained; root cause is unconfirmed, no
production timeout was increased and no fix is claimed.** Successful measurements
above are distinct runs, not averaged across or substituted for failed attempts.

Isolation: child PATH was System32 only, Python/Qt paths deliberately invalid,
working directory outside source/build. This does not alter the user's environment
or pretend the developer host is clean. QA probe binary is present only in ignored
QA copies, not official `dist/TranslatorPortable`. The probe exercises real capture,
saved-region pipeline and toolbar Start/Stop with temporary settings; it is not
a fresh-machine manual Region test. No private video screenshots were used here.

## Size and Layout

Measured payload **791.37 MiB**; total including runtime-manifest.json:
**833,179,371 bytes / 794.582 MiB**, versus historical 793.64 MiB draft.
Partition is disjoint; standard VC DLLs are excluded from Python/Paddle below.

| Partition | Bytes | MiB |
|---|---:|---:|
| Qt/plugins/MinGW/config | 38000726 | 36.240 |
| Python/Paddle and fixed packages | 745908220 | 711.354 |
| Small models | 31481281 | 30.023 |
| License/provenance inventory | 12678412 | 12.091 |
| Standard VC runtime | 1085160 | 1.035 |
| App/helper/README/manifest | 4025572 | 3.839 |

Layout follows the main Phase 6.1C document, plus four app-local CRTs under
`ocr/runtime/`, `licenses/microsoft-vc/` and `licenses/supplementary/models/`.
Qt-only size is not separately attributed from MinGW/config in this partition.
No build/tests/benchmark/private inputs, venv configuration, probe executable,
provider keys, logs or archive utility are in the candidate manifest.

## Historical Infrastructure Commands Actually Executed

Paths below are local build inputs only, never baked into CMake/production code.

```powershell
benchmarks/ocr_phase61a/.venv/Scripts/python.exe -B scripts/prepare_vc_runtime.py --redist .cache/acceptance-inputs/vc/vc_redist.x64.exe --sevenzip .cache/acceptance-inputs/tools/7zip/x64/7za.exe --license-docx .cache/acceptance-inputs/vc/LICENSE.docx --audit .cache/packaging-validation/dependencies.json --output .cache/acceptance-inputs/vc-runtime-14.51.36247-cohort --powershell C:/Users/Administrator/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe
benchmarks/ocr_phase61a/.venv/Scripts/python.exe -B scripts/package_windows.py --clean-build --qt-root D:/QT/6.11.2/mingw_64 --mingw-root D:/QT/Tools/mingw1310_64 --cmake D:/QT/Tools/CMake_64/bin/cmake.exe --ninja D:/QT/Tools/Ninja/ninja.exe --python-base C:/Users/Administrator/.cache/codex-runtimes/codex-primary-runtime/dependencies/python --site-packages benchmarks/ocr_phase61a/.venv/Lib/site-packages --model-root benchmarks/ocr_phase61a/models --vc-runtime-root .cache/acceptance-inputs/vc-runtime-14.51.36247-cohort --license-root .cache/acceptance-inputs/notices --smoke
benchmarks/ocr_phase61a/.venv/Scripts/python.exe -B scripts/validate_windows_package.py dist/TranslatorPortable --audit-output .cache/packaging-validation/dependencies-final.json --smoke
benchmarks/ocr_phase61a/.venv/Scripts/python.exe -B scripts/helper_smoke.py --program dist/TranslatorPortable/ocr/runtime/python.exe --script dist/TranslatorPortable/ocr/helper/paddle_helper.py --models dist/TranslatorPortable/ocr/models --output .cache/packaging-validation/final-helper.json
./scripts/local_portable_acceptance.ps1 -Package dist/TranslatorPortable -Probe build/phase61b/TranslatorPaddleProbe.exe -Image .cache/packaging-validation/synthetic.png -ReuseQaCopy
D:/QT/Tools/CMake_64/bin/cmake.exe --build build/phase61b -j 4
D:/QT/Tools/CMake_64/bin/cmake.exe --build build/phase61b-debug -j 4
D:/QT/Tools/CMake_64/bin/ctest.exe --test-dir build/phase61b --output-on-failure
```

Builder performed fresh Release configure and all 30 compile/link steps. Existing
Release/Debug test trees rebuilt successfully. CTest 13/13 passed; the package suite
includes eight existing validator cases and five new acceptance-input cases for
Burn bounds, VC hash/version/provenance, privilege flags and independent model hashes.
These tests do not need Sandbox. Build-tree Qt/MinGW PATH prefixes were process-local.
An additional CTest invocation in the existing Debug build tree reported "No tests
were found" (that tree does not register tests); it is not counted as a test pass.
The measured 13/13 CTest result is from build/phase61b; Debug application build passed.

## Owner-reported Clean Windows Acceptance

The owner manually tested a clean **Windows Sandbox** with **Networking=Disable**.
The portable candidate was copied from the host-mapped input to the Sandbox's local
filesystem before running. PP-OCRv6 Small and GUI startup, manual Region -> Start OCR,
20-request self-check, warm Stop -> wait -> Start and Close cleanup all passed.
Translation Provider was **None**; this is OCR acceptance, not online translation
acceptance. No model download occurred. Networking=Disable provides actual disconnected
OCR acceptance, not merely offline environment flags. Clean Windows, offline PP-OCRv6
and portable local execution are **PASS** on this owner-reported evidence.

| Sandbox Local Execution Metric | Owner-reported Result |
|---|---|
| Self-check successes | 20 / 20 |
| Helper PID | 5964, unchanged throughout the 20-request run |
| First request (includes helper startup) | 7332 ms |
| Warm median | 155 ms |
| Warm mean | 160 ms |
| Warm >300 ms | 0 |
| Stop -> wait -> Start | Normal; helper remained warm, no new cold start |
| Close | Translator.exe and python/helper gone; no residual process |
| Network | Networking=Disable; actual disconnected OCR |
| Model downloads | None reported |

Separate helper READY timing, Sandbox Windows build, exact local path, per-tool
absence inventory, per-language recognized strings and AV/SmartScreen observations
were not supplied. They are not invented or inferred from the metrics. Additional
Sandbox Unicode-path/ACL/hash tests beyond the described local-copy flow are not
claimed. The earlier developer-host tests remain historical local evidence, not
the basis of this clean Windows PASS. See [Sandbox acceptance record and repeatable
procedure](windows-sandbox-acceptance.md).

### Non-blocking Host-mapped-folder Limitation

Direct execution from the Sandbox's read-only host-mapped `PortableInput` folder
previously produced an approximately 30-second helper startup timeout. Copying the
**same candidate** into the Sandbox local filesystem gave normal GUI/OCR and 20/20
self-check PASS. Record this as **Non-blocking Windows Sandbox host-mapped-folder
limitation** for the accepted local-copy execution flow. It is not described as
fixed, its root cause is not established, and production timeout remains unchanged.
Use the Sandbox local-copy flow for acceptance; host mapping is an input-transfer
mechanism, not a claimed supported direct-execution path. This Sandbox observation
does not establish the root cause of the separate historical developer-host timeout.

## Release Redistribution Gates

Owner explicitly forbids assuming commercial Qt redistribution rights. Both Qt
and VC remain OWNER REVIEW REQUIRED; actual 78-component inventory and native/model
subcomponent gaps are in the license document. This is an inventory, not clearance.
No Defender/SmartScreen settings were changed by this task. No block was observed in
the earlier developer-host runs; Sandbox security-tool behavior was not separately
reported. Historical startup failures are retained, not claimed fixed.

No public release ZIP or public release SHA256 was generated. Technical PASS does
not grant redistribution rights or public-release readiness. Next: complete the
owner/license review, retaining this technical evidence and the mapped-folder
limitation. Only after release gates are confirmed may public artifacts be generated;
installer, Phase 7 and Audio/ASR remain out of scope.

## Documentation Follow-up Checks

Only README and the three Phase 6.1C acceptance/overview Markdown documents were
changed. No production source or build input changed, so no rebuild or new package
was produced for this manual record. Existing baseline regression was rerun:
`ctest --test-dir build/phase61b --output-on-failure` passed **13/13**, zero failures
(13.47 s). Local Markdown links resolved, documentation-only scope was checked,
and `git diff --check` passed. The manual Sandbox evidence is the owner's report;
the agent did not re-execute Sandbox or substitute these regressions for that report.

## Requested 75-point Ledger

| # | Item | Answer |
|---|---|---|
| 1 | FULLY COMPLETE | No |
| 2 | Blockers | Owner/Qt/VC/native license clearance; mapped-folder timeout is a non-blocking limitation, not fixed |
| 3 | Initial Git | Infrastructure baseline ec11f5e; this documentation follow-up started clean at 04fdc818df5d8e608b03b023f09d137479f30135 |
| 4 | Current commit | Candidate production base ec11f5e; final infrastructure commit reported after commit/push |
| 5 | msvcp source | Official Microsoft x64 Redist, extracted as data |
| 6 | msvcp version/hash | 14.51.36247.0; full SHA256 table above |
| 7 | vcomp source | Same official Microsoft x64 Redist |
| 8 | vcomp version/hash | 14.51.36247.0; full SHA256 table above |
| 9 | VC official URL | https://aka.ms/vc14/vc_redist.x64.exe |
| 10 | System32 source? | No |
| 11 | Audited PE count | 262 |
| 12 | Missing candidates | 0; static presence, not clean-host proof |
| 13 | License gate | BLOCKED, actual inventory recorded; no clearance |
| 14 | Qt | OWNER REVIEW REQUIRED, no commercial rights assumed |
| 15 | Paddle/native license | Notices collected; REVIEW REQUIRED for actual transitive DLLs |
| 16 | Model notice | Official fixed cards, Apache text and matching weight identity recorded; REVIEW REQUIRED |
| 17 | Layout | App-relative isolated Python/helper/models/Qt/plugins/CRT/licenses; see above |
| 18 | Total size | 833179371 bytes / 794.582 MiB including manifest |
| 19 | VC size | 1085160 bytes / 1.035 MiB |
| 20 | Qt size | Qt/MinGW/config combined 36.240 MiB; not separate Qt-only figure |
| 21 | Python/Paddle | 711.354 MiB excluding standard CRT cohort |
| 22 | Models | 30.023 MiB |
| 23 | Licenses | 12.091 MiB |
| 24 | Minimal PATH | System32-only local native/import/OCR tests passed |
| 25 | Environment | Invalid Python and Qt paths ignored by portable child; user environment unchanged |
| 26 | Moved directory | Separate task-owned QA copy, passed rerun; initial startup timeout retained |
| 27 | Spaces | Local QA/helper tests passed |
| 28 | Unicode | Local install/model path passed with verified user-cache staging; profile short-alias limitation retained |
| 29 | Read-only | Write denied; 20/20 rerun/Stop/Start passed; prior timeout not declared fixed |
| 30 | Local 20 OCR | Native and IPC 20/20 passing series; failed attempt recorded separately |
| 31 | Local PID | Native candidate 39968; IPC 13424; QA native 34244, each series same PID |
| 32 | Local median | Native 142 ms; IPC 153.87 ms; read-only 152 ms |
| 33 | Stop/Start | Real capture probe retained helper PID 35684 |
| 34 | Restart latency | 144 ms |
| 35 | Close | App/helper gone; no orphan in tested runs |
| 36 | Clean-machine type | Windows Sandbox, owner-reported manual acceptance; candidate copied to guest local filesystem |
| 37 | Clean Windows version | Sandbox build not supplied; no developer-host version substituted |
| 38 | Clean Python installed? | Owner reports clean Sandbox; individual installed-tool inventory not supplied |
| 39 | Clean Qt installed? | Owner reports clean Sandbox; individual installed-tool inventory not supplied |
| 40 | Clean Paddle installed? | Owner reports clean Sandbox; individual installed-tool inventory not supplied |
| 41 | Clean VC dev environment? | Owner reports clean Sandbox; individual installed-tool inventory not supplied |
| 42 | Clean app launch | PASS, GUI and PP-OCRv6 Small normal from guest local copy |
| 43 | Physically offline? | PASS, Sandbox Networking=Disable, actual disconnected OCR |
| 44 | Offline Paddle | PASS, owner-reported manual Region -> Start OCR |
| 45 | Model network requests | No model download reported; no packet trace supplied |
| 46 | Clean 20 OCR | PASS, 20/20 |
| 47 | Clean PID | 5964, unchanged throughout self-check |
| 48 | Clean cold startup | First combined request 7332 ms; separate READY not measured |
| 49 | Clean warm median | 155 ms; mean 160 ms; >300 ms count 0 |
| 50 | Clean Stop/Start | PASS, helper stayed warm, no cold reload |
| 51 | Clean Close | PASS, no Translator.exe or python/helper remaining |
| 52 | AV/SmartScreen | No changes by this task; Sandbox security observations not supplied |
| 53 | Final ZIP? | No, release gates block generation |
| 54 | ZIP name | None generated; intended Translator-windows-x64-portable.zip |
| 55 | ZIP size | Not applicable |
| 56 | ZIP SHA256 | Not applicable |
| 57 | Release | Fresh candidate configure/build passed; existing Release tests build passed |
| 58 | Debug | Build passed |
| 59 | CTest | 13/13 passed, zero failures |
| 60 | README | Technical/clean/offline PASS; redistribution blocked, no public-release claims |
| 61 | Phase 6.1C doc | Updated current status; historical evidence retained and labeled |
| 62 | License doc | Actual matrix, model/VC provenance and explicit BLOCKED/OWNER REVIEW REQUIRED |
| 63 | Git binaries | Earlier infrastructure staged 14 text files only; this follow-up stages documentation only |
| 64 | Git models | Hash metadata only; no weights staged; runtime/model/venv/cache paths confirmed ignored |
| 65 | Git keys | Staged text reviewed and credential-pattern scan passed; no provider keys/user config included |
| 66 | Commit hash | Infrastructure 04fdc818df5d8e608b03b023f09d137479f30135; documentation follow-up hash supplied in final response |
| 67 | Push | Execute origin/main non-force after staged audit; actual outcome in final response |
| 68 | Final Git | Actual post-push status supplied in final response |
| 69 | LunaTranslator | Read-only, no task writes; final status checked separately |
| 70 | False positives | Known Limitation; filtering still deferred |
| 71 | Installer | Not implemented |
| 72 | Phase 7 | Not started |
| 73 | Ordinary-user release ready? | No; controlled technical QA only, not verified redistribution |
| 74 | Why not? | Technical/clean/offline PASS does not clear Qt/VC owner redistribution rights or other license obligations |
| 75 | Next | Complete owner/license review before public ZIP/SHA256; retain non-blocking mapped-folder limitation; no Installer/Phase 7/Audio |
