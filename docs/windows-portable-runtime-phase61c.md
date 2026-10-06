# Windows Portable Runtime - Phase 6.1C

## Acceptance Status

Phase 6.1C is **not fully complete**. Packaging infrastructure is implemented
and locally exercised, but the generated folder is a **technical draft**, not a
verified redistributable release. Do not distribute or label it fully portable.

Current final-acceptance evidence (2026-10-06) is in
[the 75-point release-gate report](windows-portable-final-acceptance.md).
The historical implementation evidence below is retained, not substituted for
new candidate measurements.

Outstanding gates:

- The regenerated candidate passes static candidate-presence audit: 262 PE files,
  zero missing dependencies. Microsoft 14.51.36247.0 app-local DLLs are extracted
  from the official signed EXE, hash-pinned and loaded from the candidate in real
  import tests. mkldnn.dll is retained; MKL-DNN remains disabled. This static audit
  is not a clean Windows loader proof or a redistribution clearance.
- No clean Windows VM, Sandbox or second PC was available. Minimal PATH and
  relocation on this developer machine are not clean-machine acceptance.
- Actual disconnected-network acceptance remains pending. Offline flags and
  local-model loading were tested, but network connectivity was not disabled.
- Redistribution review remains pending: Qt acquisition/applicable terms and
  VC owner entitlement remain OWNER REVIEW REQUIRED. Model-specific official
  cards/Apache text and matching weight identity are recorded; native-library
  obligations remain REVIEW REQUIRED. See the actual shipped license inventory.
- One read-only Unicode-path self-check timed out at 30,106 ms on helper startup.
  A later same-candidate run passed 20/20 and warm Stop/Start; root cause is not
  established and the failure is not declared fixed. Clean first-launch verification
  must retain this observation, not cherry-pick the passing rerun.

No installer or release zip was produced. No security controls were disabled.
The no-subtitle false positive remains a Known Limitation; filtering is deferred.

## Packaging Decision

Both alternatives were built locally from the Phase 6.1A environment, without
changing the verified Paddle versions or downloading replacement models.

| Alternative | Actual outcome | Measured folder size |
| --- | --- | --- |
| A: PyInstaller 6.19.0 one-dir helper EXE | Build succeeded in 175.9 s; helper failed before READY with a PaddleX pipeline dependency error | 640.67 MiB |
| B: app-local Python + fixed packages + helper script | Imports and 20 real requests passed; selected for this draft | See current package measurement below |

A used collect-all for paddle/paddlex/paddleocr and recursive paddleocr metadata.
PaddleX's dependency checks also inspect distribution metadata. This particular
PoC did not satisfy them; this is not a claim that PyInstaller cannot work.
B preserves the required package metadata and native libraries explicitly.
The first B experiment omitted setuptools and failed a Paddle import; it was
corrected because setuptools is an actual runtime dependency here.
Build-only pip/PyInstaller/hooks/pefile tools are excluded from the runtime.

The selected helper is `paddle_helper.py`, launched with bundled `python.exe`.
It is not a venv: no pyvenv.cfg, external base interpreter, pip, registry search,
user site or development checkout is used by its isolated `python312._pth`.
Full clean-machine proof is still pending; resolving the static CRT gate does not
replace executing the complete candidate on a clean Windows host.

## Layout and Launch

```text
dist/TranslatorPortable/
  Translator.exe
  Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll, Qt6Network.dll, Qt6Svg.dll
  libgcc_s_seh-1.dll, libstdc++-6.dll, libwinpthread-1.dll
  qt.conf
  platforms/qwindows.dll
  imageformats/, iconengines/, generic/, styles/, networkinformation/, tls/
  ocr/helper/paddle_helper.py
  ocr/runtime/python.exe, python312.dll, python3.dll, python312._pth
  ocr/runtime/DLLs/, Lib/, Lib/site-packages/
  ocr/models/PP-OCRv6_small_det/PP-OCRv6_small_det_infer/
  ocr/models/PP-OCRv6_small_rec/PP-OCRv6_small_rec_infer/
  licenses/component-matrix.json, python/, python-packages/, qt/, mingw/
  runtime-manifest.json
  README.txt
```

Each model directory contains exactly inference.yml, inference.json and
inference.pdiparams. The tracked `scripts/runtime-models.json` pins all six
Phase 6.1A file hashes and sizes (30.02 MiB total). Actual weights are ignored.

`PaddleRuntimeLocator` resolves explicit TRANSLATOR_OCR_PYTHON / HELPER / MODELS
overrides, app-relative assets, then checkout fallback only outside portable
mode. An `ocr/` directory or manifest marks portable mode even when incomplete;
missing files produce actionable errors, never a checkout/cache model search.
The current model layout validation expects the nested directories shown above;
development overrides must provide that layout too.

```text
<app>/ocr/runtime/python.exe -B -u <app>/ocr/helper/paddle_helper.py
  --models <app>/ocr/models --cpu-threads 4
```

QProcess uses a working directory in the selected runtime and a whitelisted
environment. Python pollution/provider secrets are not forwarded. stdout remains
protocol v1 framed JSON; stderr remains diagnostic. CREATE_NO_WINDOW hides the
helper console. Existing request serialization, timeouts, shutdown and restart
behavior are retained. MKL-DNN is explicitly false and logged as disabled.

`qt.conf` anchors plugins to the package. Portable startup removes inherited
Qt plugin-path overrides only within this process. windeployqt deployed the
Windows Schannel TLS backend, not an arbitrary OpenSSL installation. Its optional
Direct3D 12 compiler warning is recorded; this Widgets UI does not use D3D12.
Image plugins deployed: gif, ico, jpeg, svg (PNG is supported by QtGui).

## Unicode and Read-only Paths

Real testing found that Paddle's Windows native model JSON loader failed for
Unicode model paths, although Python imports from Unicode runtime paths worked.
The helper therefore stages only the three required local files per model into
a content-addressed **user** cache. It checks source/copy SHA256, atomically
replaces mismatches and never substitutes an unrelated cached model. ASCII model
paths bypass staging. No automatic model download is implemented.

An ASCII user-cache path, or its available ASCII Windows short alias, is required
by that native loader. A Unicode profile without such an alias yields an explicit
error; that profile configuration is a remaining limitation, not verified support.
Bundled models are immutable. Bytecode writes are disabled; QSettings stays
UserScope and credentials stay in Windows Credential Manager.

The package was copied to a different, space/Chinese-named directory. A deny-write
ACL was temporarily applied only to this task-owned QA copy, write failure was
verified, 20 production OCR calls passed and the original ACL was restored.
All six model hashes remained unchanged; no pyc files appeared in the package.

## Tesseract Strategy

Strategy B: explicitly not bundled in this draft. The installed Tesseract top-level
license was inspected, but its transitive DLL license/dependency review was not
complete. Portable mode never borrows it from PATH, Program Files or profile data.
Selecting it reports unavailable and directs the user to PP-OCRv6 Small. This is
not a working portable Tesseract fallback. Development-mode selection and existing
tests remain unchanged. No silent engine switching is added.

## Historical Implementation Test Evidence

All latency figures below are local synthetic `Hello Portable OCR` measurements,
not repeated video accuracy benchmarks or clean-machine figures. Warm statistics
exclude the first request (19 warm requests out of 20 total).

| Test | Result |
| --- | --- |
| Portable Python PoC | READY 3296.59 ms; first OCR 235.28 ms; warm median 152.03 ms, mean 153.07 ms; 20/20; PID 27120 |
| Unicode model path after fix | READY 9303.96 ms; first OCR 291.65 ms; warm median 166.08 ms, mean 166.53 ms; 20/20; PID 20964 |
| Native app self-check, read-only relocated copy | Cold first call including startup 7778 ms; warm median 159 ms, mean 160.68 ms; 20/20; PID 26016 |
| Native draft self-check after Tesseract consistency rebuild | Cold first call 6632 ms; warm median 139 ms, mean 138.84 ms; 20/20; PID 5544; hash validation passed again after exit |
| Final draft without build-only tools | Cold first call 6681 ms; warm median 144 ms, mean 144.89 ms; 20/20; PID 17300; no orphan; post-exit hash validation passed |
| Real QScreen + realtime pipeline lifecycle probe | Cold first OCR 7114 ms; Stop halted capture; Start reused PID 30412; first restarted OCR 130 ms; original visible |
| GUI draft launch | Visible subtitle placeholders and toolbar; Settings opened and Cancel worked; normal Close left no application process |
| Release / Debug | Both compiled successfully after final native source edit |
| CTest | 13/13 passed, 9.14 s on final run; old 11 retained plus locator and validator suites |
| Python helper tests | 8/8 passed, including Unicode copy/reuse/repair and immutable originals |

Every measured warm series above had zero requests over 300 ms. All helper PIDs
were reaped after their owning test/application exited. The real capture probe
used a synthetic QLabel and saved region, not manual Region dragging or DeepL.
Translation requests were zero; no keys were copied/read by self-check. Existing
provider tests passed, but portable online provider acceptance was not performed.
No claims are made that the GUI dropdown was manually exercised in this run.

Reports, QA-only probe binaries and synthetic screenshots are under ignored
`.cache/packaging-validation/`; they are not shipped, committed or uploaded.
The official folder contains no QA probe executable or private video screenshot.

## Historical Initial Draft Command

Run from the Translator checkout. Paths below are local command inputs only;
they are not hardcoded into CMake or production source. PythonBase must be a
complete, verified Python 3.12.14 base; Packages is its verified Paddle site-packages.
BuildPython is a build-only Python 3.12 interpreter with pefile 2024.8.26 for the
static audit (the verified evaluation venv was used locally). pefile is not copied
into the runtime. The plain base interpreter need not have build tools installed.

```powershell
$PythonBase = '<complete Python 3.12.14 base directory>'
$Packages = '<verified site-packages directory>'
$BuildPython = '<build-only Python interpreter with pefile>'
& "$BuildPython" -B scripts/package_windows.py `
  --qt-root D:/QT/6.11.2/mingw_64 `
  --mingw-root D:/QT/Tools/mingw1310_64 `
  --cmake D:/QT/Tools/CMake_64/bin/cmake.exe `
  --ninja D:/QT/Tools/Ninja/ninja.exe `
  --python-base "$PythonBase" --site-packages "$Packages" `
  --model-root benchmarks/ocr_phase61a/models
```

This exact build mode was run locally using explicit verified inputs. It creates
the draft and manifest, then **returns nonzero** for the two missing CRT candidates.
That failure is intentional: build success is not dependency acceptance.
`--vc-runtime-root <approved directory>` can supply hashed app-local VC DLLs and
redistribution.json with version, source_url, license_file and files(path,sha256).
The owner must approve redistribution; provenance metadata is not legal clearance.
`--license-root` accepts reviewed supplementary notices. These inputs were not
available and were not invented or filled from System32.

```powershell
# Hash/file validation plus static PE imports audit (currently fails CRT gate):
& "$BuildPython" -B scripts/validate_windows_package.py `
  dist/TranslatorPortable --audit-output .cache/packaging-validation/dependencies.json

# Separate developer-machine smoke, not a substitute for the failing audit:
& "$BuildPython" -B scripts/validate_windows_package.py `
  dist/TranslatorPortable --smoke

# Existing Release and Debug build trees were rebuilt:
$env:PATH = 'D:/QT/Tools/mingw1310_64/bin;D:/QT/6.11.2/mingw_64/bin;' + $env:PATH
& D:/QT/Tools/CMake_64/bin/cmake.exe --build build/phase61b -j 4
& D:/QT/Tools/CMake_64/bin/cmake.exe --build build/phase61b-debug -j 4
& D:/QT/Tools/CMake_64/bin/ctest.exe --test-dir build/phase61b --output-on-failure
```

The builder independently configures/builds `build/portable-release` using Ninja,
Release and BUILD_TESTING=OFF, then calls windeployqt. Only the resolved, exact
task-owned dist/TranslatorPortable output is cleaned; inputs/source are retained.
Manifest records versions, source commit/dirty state and every shipped file's
relative path, size and SHA256. It contains no developer absolute paths or secrets.
Validator rejects incomplete/modified/unmanifested files and prohibited artifacts;
its static candidate-presence audit is explicitly not a Windows loader proof.
Smoke uses minimal System32 PATH, invalid PYTHONHOME/PYTHONPATH, non-checkout cwd,
real app self-check and helper cleanup, then revalidates package immutability.

## Clean-machine Acceptance Still Required

Once licensed native inputs/notices are complete and the static audit passes:
copy the folder to a clean Windows x64 VM/PC with no Python/Qt/Paddle/dev CRTs.
Disconnect its network and test launch, 20 requests, Region/Start/Stop/Start,
Close cleanup, spaces/Unicode relocation and read-only install. Record actual
DLL load paths and timings. Do not modify the developer machine's system PATH,
rename installed SDKs or disable Defender to simulate a clean computer.
Only after those gates pass should a release zip/hash be generated.

## Historical Initial 100-point Report (Superseded by Final Acceptance)

Final draft measurement after build-only modules were excluded:
15,728 manifest payload files, 790.43 MiB payload, 793.64 MiB including manifest;
711.52 MiB Python/Paddle runtime. Static PE audit: 260 binaries, the same two
missing CRT imports. No pip, PyInstaller, hooks, win32ctypes, pefile or peutils
module remains in the runtime.

The table is the task's acceptance ledger, not a claim all gates passed.
Git outcome is reported after commit/push in the completion message; `git log -1`
and `git status -sb` are authoritative (a commit cannot contain its own hash).

| # | Item | Outcome |
| --- | --- | --- |
| 1 | Full completion | No; technical implementation/local tests complete, release acceptance pending |
| 2 | Main blockers | Missing two CRT DLLs, license completion, clean-machine/disconnected testing |
| 3 | Initial Git | Clean main, 0644b4f; Phase 6.1A/6 ancestors present |
| 4 | Modified files | .gitignore, CMakeLists, README, architecture, helper, main, Paddle engine, Tesseract engine, integration probe |
| 5 | Added files | RuntimeLocator, RuntimeSelfCheck, locator/model tests, six scripts/catalog files, two deployment/license docs |
| 6 | Alternatives | PyInstaller one-dir and isolated portable Python |
| 7 | PyInstaller | Build passed; READY failed PaddleX dependency checks |
| 8 | Portable Python | Imports and actual 20-request OCR passed locally |
| 9 | Selected | Portable Python + helper script |
| 10 | Reason | Explicit metadata/native dependency layout; working local inference |
| 11 | Other rejected | This PyInstaller PoC did not initialize; more collection investigation needed |
| 12 | Helper form | Persistent Python script process |
| 13 | Command | Bundled python -B -u helper --models bundled-root --cpu-threads 4 |
| 14 | System Python | Not intended/used by isolated interpreter; clean-machine gate pending |
| 15 | pip at runtime | No; build-only pip excluded |
| 16 | User env required | No in portable mode; optional developer overrides |
| 17 | Locator order | Overrides, app-relative, nonportable checkout, explicit error |
| 18 | Overrides | TRANSLATOR_OCR_PYTHON / HELPER / MODELS retained |
| 19 | Layout | Above; all application-relative assets |
| 20 | Qt | windeployqt Release deployment |
| 21 | MinGW | Three matching compiler runtime DLLs copied |
| 22 | qwindows | Bundled, real QApplication initialization passed locally |
| 23 | Images | gif/ico/jpeg/svg plugins, PNG in QtGui |
| 24 | PaddleOCR | 3.7.0 |
| 25 | PaddlePaddle | 3.3.1 |
| 26 | PaddleX | 3.7.2 |
| 27 | Python | 3.12.14 |
| 28 | Models | PP-OCRv6_small_det and PP-OCRv6_small_rec |
| 29 | Model total | 30.02 MiB |
| 30 | MKL-DNN | Explicitly disabled, logged |
| 31 | Download | Local det/rec paths required; no replacement download implementation |
| 32 | Offline | Offline flags/local inference passed; physically disconnected test pending |
| 33 | Manifest | Generated versions/commit/dirty-state/relative files/hash/bytes |
| 34 | Absolute paths | None in manifest; validator rejects them |
| 35 | Cold helper | 3296.59 ms ASCII PoC; 9303.96 ms Unicode model staging test |
| 36 | First OCR | 235.28 / 291.65 ms after READY; native cold call includes startup |
| 37 | Warm median | 152.03 / 166.08 ms PoCs; read-only native 159 ms |
| 38 | Warm mean | 153.07 / 166.53 ms PoCs; read-only native 160.68 ms |
| 39 | Warm >300 ms | Zero in each reported series |
| 40 | Same PID | Yes, 20 requests per smoke |
| 41 | Stop/Start | Real capture pipeline halted/resumed; PID retained |
| 42 | Restart OCR | 130 ms |
| 43 | Close | No helper after owner exit; GUI normal close tested |
| 44 | Total size | 793.64 MiB including manifest; payload 790.43 MiB |
| 45 | Qt/deployment size | Approximately 36.24 MiB Qt/plugins/MinGW/config combined |
| 46 | Python/Paddle | 711.52 MiB; helper script about 0.01 MiB |
| 47 | Models | 30.02 MiB |
| 48 | Tesseract | Zero MiB; explicit unavailability strategy B |
| 49 | Notices | Approximately 12.02 MiB; inventory is not clearance |
| 50 | Portable Tess fallback | Not available; developer fallback preserved |
| 51 | Minimal PATH | System32-only PATH local tests passed |
| 52 | Move | QA copy at a different directory passed |
| 53 | Spaces | Passed locally |
| 54 | Unicode | Install/model path passed with verified user-cache staging; profile limitation above |
| 55 | Read-only | Write denied, 20 OCR passed, model hashes unchanged |
| 56 | Python pollution | Invalid PYTHONHOME/PYTHONPATH ignored; user site isolated |
| 57 | Missing helper | Locator explicit error, tests pass, no checkout borrowing |
| 58 | Missing models | Explicit local-asset error, no download, tests pass |
| 59 | Audit | 260 PE files in earlier draft; two missing CRT candidates; final count below |
| 60 | Validator | Fail-closed hashes/files/versions/paths; eight tests include build-only exclusion regression coverage |
| 61 | Clean machine type | None available; not executed |
| 62 | Clean Python absence | Not verified |
| 63 | Clean Qt absence | Not verified |
| 64 | Clean Paddle absence | Not verified |
| 65 | Clean app launch | Pending |
| 66 | Clean Paddle OCR | Pending |
| 67 | Clean Tesseract | Intentionally unavailable in current draft |
| 68 | Clean offline | Pending |
| 69 | Clean Stop/Start | Pending |
| 70 | Clean Close | Pending |
| 71 | Clean timings | Not measured |
| 72 | AV/SmartScreen | No local block observed; unsigned, fresh-machine warning behavior untested |
| 73 | VC runtime | Required by actual audited pyclipper/Paddle dependencies; incomplete |
| 74 | License matrix | Generated inventory and third-party-runtime-licenses.md |
| 75 | Qt terms | Owner must confirm acquisition/terms, required sources/relinking/notices |
| 76 | Release | Build passed |
| 77 | Debug | Build passed |
| 78 | CTest count | 13 |
| 79 | CTest passed | 13, zero failures |
| 80 | README | Updated, explicitly draft/incomplete |
| 81 | Architecture | Updated locator/cache/deployment boundary |
| 82 | Deployment doc | This file |
| 83 | License doc | Added, actual shipped components and transitive gaps |
| 84 | Builder | scripts/package_windows.py |
| 85 | Validator | scripts/validate_windows_package.py |
| 86 | Zip | Not generated before acceptance |
| 87 | Zip size | Not applicable |
| 88 | Zip hash | Not applicable |
| 89 | Git binaries | Excluded; staging reviewed before commit |
| 90 | Git models | Only text hash catalog, no weights |
| 91 | Git API keys | No credential material intentionally included; tracked changes checked |
| 92 | Git status | Reported after commit/push; see git status -sb |
| 93 | Commit | Requested message: Add Windows portable OCR runtime packaging |
| 94 | Push | Reported after executing origin main push |
| 95 | LunaTranslator | Read-only reference; git status remained empty |
| 96 | False positives | Known Limitation, production filtering still deferred |
| 97 | Installer | Not implemented |
| 98 | Phase 7 | Not started |
| 99 | Next | Resolve licensed native inputs/notices, then true clean-machine/offline acceptance |
| 100 | User flow | Maintainer builds/validates/finishes gates; user copies folder, runs Translator.exe, chooses PP-OCRv6 Small, Region, Start; provider credentials configured per machine |
