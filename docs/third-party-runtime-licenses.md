# Third-party Runtime License Inventory

## Status

This is the inventory of the local Phase 6.1C **draft**, not a redistribution clearance.
The collector preserves wheel license/NOTICE files, the CPython license, MinGW
runtime notices and the actual Qt source SBOM. Supplementary reviewed notices can
be provided with `--license-root`. Wheel metadata alone does not resolve bundled
native-library obligations. Source URLs below are upstream wheel metadata, not
independent legal conclusions. No project license is selected by this task.

Qt acquisition and applicable commercial/open-source terms must be confirmed by
the owner. Dynamic linking does not remove applicable source, replacement/relinking,
license and third-party notice duties. Consult [Qt licensing](https://doc.qt.io/qt-6/licensing.html)
and [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations).
This technical inventory is not legal advice.

Microsoft app-local VC redistribution inputs must have approved provenance, hashes
and license material. The current draft is missing `msvcp140.dll` and `vcomp140.dll`.
Do not copy them from System32 and declare redistribution approved. See
[Microsoft redistribution guidance](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170).

## Distributed Components

All rows below are actually present in the draft. Notice counts refer to the initial
collector result; the revised collector also includes complete license directories
(e.g. PDFium build-license text). Preserve copyrights and all component notices;
check reciprocal/source requirements individually before public distribution.

| Component | Version | Reported License | Distributed | Source URL | Notice / Review |
|---|---|---|---|---|---|
| aiohappyeyeballs | 2.7.1 | Python Software Foundation License | Yes, local draft | https://github.com/aio-libs/aiohappyeyeballs | 1 collected records; review bundled dependencies |
| aiohttp | 3.14.3 | Apache-2.0 AND MIT | Yes, local draft | https://github.com/aio-libs/aiohttp | 2 collected records; review bundled dependencies |
| aiosignal | 1.4.0 | Apache Software License | Yes, local draft | https://github.com/aio-libs/aiosignal | 1 collected records; review bundled dependencies |
| aistudio_sdk | 0.3.9 | Apache Software License | Yes, local draft | Not declared in wheel | 1 collected records; review bundled dependencies |
| annotated-types | 0.8.0 | MIT | Yes, local draft | https://github.com/annotated-types/annotated-types | 1 collected records; review bundled dependencies |
| anyio | 4.15.1 | MIT | Yes, local draft | https://github.com/agronholm/anyio | 1 collected records; review bundled dependencies |
| attrs | 26.1.0 | MIT | Yes, local draft | https://tidelift.com/subscription/pkg/pypi-attrs?utm_source=pypi-attrs&utm_medium=pypi | 1 collected records; review bundled dependencies |
| bce-python-sdk | 0.9.79 | Apache License 2.0 | Yes, local draft | http://bce.baidu.com | 0 collected records; review bundled dependencies |
| certifi | 2026.7.22 | Mozilla Public License 2.0 (MPL 2.0) | Yes, local draft | https://github.com/certifi/python-certifi | 1 collected records; review bundled dependencies |
| cffi | 2.1.1 | MIT-0 | Yes, local draft | https://github.com/python-cffi/cffi | 1 collected records; review bundled dependencies |
| chardet | 7.6.0 | 0BSD | Yes, local draft | https://github.com/chardet/chardet | 1 collected records; review bundled dependencies |
| charset-normalizer | 3.5.2 | MIT | Yes, local draft | Not declared in wheel | 1 collected records; review bundled dependencies |
| click | 8.5.0 | BSD-3-Clause | Yes, local draft | https://github.com/pallets/click/ | 1 collected records; review bundled dependencies |
| colorama | 0.4.6 | BSD License | Yes, local draft | https://github.com/tartley/colorama | 1 collected records; review bundled dependencies |
| colorlog | 6.12.0 | MIT License | Yes, local draft | https://github.com/borntyping/python-colorlog | 1 collected records; review bundled dependencies |
| crc32c | 2.9.post0 | GNU Lesser General Public License v2 or later (LGPLv2+) | Yes, local draft | Not declared in wheel | 4 collected records; review bundled dependencies |
| cryptography | 50.0.2 | Apache-2.0 OR BSD-3-Clause | Yes, local draft | https://github.com/pyca/cryptography | 3 collected records; review bundled dependencies |
| filelock | 4.0.11 | MIT | Yes, local draft | https://github.com/tox-dev/py-filelock | 1 collected records; review bundled dependencies |
| frozenlist | 1.8.0 | Apache-2.0 | Yes, local draft | https://github.com/aio-libs/frozenlist | 1 collected records; review bundled dependencies |
| fsspec | 2026.9.0 | BSD-3-Clause | Yes, local draft | https://github.com/fsspec/filesystem_spec | 1 collected records; review bundled dependencies |
| future | 1.0.0 | OSI Approved | Yes, local draft | https://github.com/PythonCharmers/python-future | 1 collected records; review bundled dependencies |
| h11 | 0.16.0 | MIT License | Yes, local draft | https://github.com/python-hyper/h11 | 1 collected records; review bundled dependencies |
| hf-xet | 1.6.0 | Apache-2.0 | Yes, local draft | https://github.com/huggingface/xet-core | 1 collected records; review bundled dependencies |
| httpcore | 1.0.9 | BSD-3-Clause | Yes, local draft | https://www.encode.io/httpcore/ | 1 collected records; review bundled dependencies |
| httpcore2 | 2.13.1 | BSD-3-Clause | Yes, local draft | https://github.com/pydantic/httpx2 | 1 collected records; review bundled dependencies |
| httpx | 0.28.1 | BSD License | Yes, local draft | https://github.com/encode/httpx | 1 collected records; review bundled dependencies |
| httpx2 | 2.13.1 | BSD-3-Clause | Yes, local draft | https://github.com/pydantic/httpx2 | 1 collected records; review bundled dependencies |
| huggingface_hub | 2.1.1 | Apache Software License | Yes, local draft | https://github.com/huggingface/huggingface_hub | 1 collected records; review bundled dependencies |
| idna | 3.20 | BSD-3-Clause | Yes, local draft | https://github.com/kjd/idna | 1 collected records; review bundled dependencies |
| imagesize | 2.0.1 | MIT | Yes, local draft | https://github.com/shibukawa/imagesize_py | 1 collected records; review bundled dependencies |
| modelscope | 1.40.1 | Apache-2.0 | Yes, local draft | https://github.com/modelscope/modelscope | 2 collected records; review bundled dependencies |
| modelscope-hub | 0.4.5 | Apache Software License | Yes, local draft | Not declared in wheel | 1 collected records; review bundled dependencies |
| multidict | 6.9.1 | Apache License 2.0 | Yes, local draft | https://github.com/aio-libs/multidict | 1 collected records; review bundled dependencies |
| networkx | 3.7 | BSD-3-Clause | Yes, local draft | https://networkx.org/ | 1 collected records; review bundled dependencies |
| numpy | 2.3.5 | BSD License | Yes, local draft | https://numpy.org | 4 collected records; review bundled dependencies |
| opencv-contrib-python | 4.10.0.84 | Apache Software License | Yes, local draft | https://github.com/opencv/opencv-python | 5 collected records; review bundled dependencies |
| opt-einsum | 3.3.0 | OSI Approved | Yes, local draft | https://github.com/dgasmith/opt_einsum | 1 collected records; review bundled dependencies |
| packaging | 26.3 | Apache-2.0 OR BSD-2-Clause | Yes, local draft | https://github.com/pypa/packaging | 5 collected records; review bundled dependencies |
| paddleocr | 3.7.0 | Apache License 2.0 | Yes, local draft | https://github.com/PaddlePaddle/PaddleOCR | 1 collected records; review bundled dependencies |
| paddlepaddle | 3.3.1 | Apache Software License | Yes, local draft | https://www.paddlepaddle.org.cn/ | 1 collected records; review bundled dependencies |
| paddlex | 3.7.2 | Apache-2.0 | Yes, local draft | Not declared in wheel | 1 collected records; review bundled dependencies |
| pandas | 3.0.6 | BSD License | Yes, local draft | https://pandas.pydata.org | 1 collected records; review bundled dependencies |
| pillow | 12.3.0 | MIT-CMU | Yes, local draft | https://tidelift.com/subscription/pkg/pypi-pillow?utm_source=pypi-pillow&utm_medium=pypi | 1 collected records; review bundled dependencies |
| prettytable | 3.18.0 | BSD-3-Clause | Yes, local draft | https://tidelift.com/subscription/pkg/pypi-prettytable?utm_source=pypi-prettytable&utm_medium=pypi | 1 collected records; review bundled dependencies |
| propcache | 0.5.4 | Apache-2.0 | Yes, local draft | https://github.com/aio-libs/propcache | 2 collected records; review bundled dependencies |
| protobuf | 7.36.2 | 3-Clause BSD License | Yes, local draft | https://developers.google.com/protocol-buffers/ | 1 collected records; review bundled dependencies |
| psutil | 7.2.2 | BSD-3-Clause | Yes, local draft | https://github.com/giampaolo/psutil | 1 collected records; review bundled dependencies |
| py-cpuinfo | 9.0.0 | MIT License | Yes, local draft | https://github.com/workhorsy/py-cpuinfo | 1 collected records; review bundled dependencies |
| pyclipper | 1.4.0 | OSI Approved | Yes, local draft | https://github.com/fonttools/pyclipper | 1 collected records; review bundled dependencies |
| pycparser | 3.0 | BSD-3-Clause | Yes, local draft | https://github.com/eliben/pycparser | 1 collected records; review bundled dependencies |
| pycryptodome | 3.24.0 | BSD License | Yes, local draft | https://github.com/Legrandin/pycryptodome/ | 1 collected records; review bundled dependencies |
| pydantic | 2.13.5 | MIT | Yes, local draft | https://github.com/pydantic/pydantic | 1 collected records; review bundled dependencies |
| pydantic_core | 2.46.5 | MIT | Yes, local draft | https://github.com/pydantic | 1 collected records; review bundled dependencies |
| pypdfium2 | 5.14.0 | BSD-3-Clause, Apache-2.0, dependency licenses | Yes, local draft | https://github.com/pypdfium2-team/pypdfium2 | 18 collected records; review bundled dependencies |
| python-bidi | 0.6.11 | GNU Library or Lesser General Public License (LGPL) | Yes, local draft | https://github.com/MeirKriheli/python-bidi | 4 collected records; review bundled dependencies |
| python-dateutil | 2.9.0.post0 | BSD License | Yes, local draft | https://github.com/dateutil/dateutil | 1 collected records; review bundled dependencies |
| PyYAML | 6.0.2 | MIT License | Yes, local draft | http://lists.sourceforge.net/lists/listinfo/yaml-core | 1 collected records; review bundled dependencies |
| requests | 2.34.2 | Apache Software License | Yes, local draft | https://github.com/psf/requests | 2 collected records; review bundled dependencies |
| ruamel.yaml | 0.19.1 | MIT License | Yes, local draft | https://sourceforge.net/p/ruamel-yaml/ | 1 collected records; review bundled dependencies |
| safetensors | 0.8.0 | Apache Software License | Yes, local draft | https://github.com/huggingface/safetensors | 1 collected records; review bundled dependencies |
| setuptools | 84.0.0 | MIT | Yes, local draft | https://github.com/pypa/setuptools | 19 collected records; review bundled dependencies |
| shapely | 2.1.2 | BSD License | Yes, local draft | https://github.com/shapely/shapely | 3 collected records; review bundled dependencies |
| six | 1.17.0 | MIT License | Yes, local draft | https://github.com/benjaminp/six | 1 collected records; review bundled dependencies |
| tqdm | 4.70.1 | MPL-2.0 AND MIT | Yes, local draft | https://tqdm.github.io | 1 collected records; review bundled dependencies |
| truststore | 0.10.4 | MIT | Yes, local draft | https://github.com/sethmlarson/truststore | 1 collected records; review bundled dependencies |
| typing-inspection | 0.4.4 | MIT | Yes, local draft | https://github.com/pydantic/typing-inspection | 1 collected records; review bundled dependencies |
| typing_extensions | 4.16.0 | PSF-2.0 | Yes, local draft | https://github.com/python/typing_extensions | 1 collected records; review bundled dependencies |
| tzdata | 2026.5 | Apache-2.0 | Yes, local draft | https://github.com/python/tzdata | 2 collected records; review bundled dependencies |
| ujson | 6.0.0 | BSD-3-Clause AND TCL | Yes, local draft | https://github.com/ultrajson/ultrajson | 1 collected records; review bundled dependencies |
| urllib3 | 2.8.0 | MIT | Yes, local draft | Not declared in wheel | 1 collected records; review bundled dependencies |
| wcwidth | 0.9.2 | MIT License | Yes, local draft | https://github.com/jquast/wcwidth | 1 collected records; review bundled dependencies |
| yarl | 1.25.1 | Apache-2.0 | Yes, local draft | https://github.com/aio-libs/yarl | 2 collected records; review bundled dependencies |
| CPython | 3.12.14 | PSF-2.0 and included notices | Yes, local draft | https://www.python.org/ | 1 collected records; review bundled dependencies |
| Qt Core/Gui/Widgets/Network/Svg and deployed plugins | 6.11.2 | LGPL-3.0/GPL/commercial alternatives; consult source SBOM | Yes, local draft | https://doc.qt.io/qt-6/licensing.html | 1 collected records; review bundled dependencies |
| MinGW runtime | 13.1 | GPL-3.0 with GCC runtime exception; mingw-w64/winpthreads notices | Yes, local draft | https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html | 1 collected records; review bundled dependencies |
| PP-OCRv6 small det/rec | Phase 6.1A verified assets | Apache-2.0 (official model cards) | Yes, local draft | https://huggingface.co/PaddlePaddle/PP-OCRv6_small_det | 0 collected records; review bundled dependencies |

## Binary and Transitive Review Still Required

| Component | Evidence / Version | Distributed | License / Source | Outstanding Requirement |
|---|---|---|---|---|
| Intel MKLML / libiomp5 / oneDNN | Paddle wheel: mklml.dll, libiomp5md.dll, mkldnn.dll; independent binary versions not verified | Yes | Upstream binary terms/third-party notices require review; https://github.com/PaddlePaddle/Paddle | Wheel Apache license is not proof for every embedded binary |
| BLAS / LAPACK / GCC Fortran / quadmath | Paddle wheel DLLs; independent versions not verified | Yes | https://github.com/PaddlePaddle/Paddle ; component terms require review | Preserve component-specific notices and runtime exceptions |
| OpenSSL / SQLite / libffi / Tcl/Tk | CPython DLL directory; exact native versions not independently audited | Yes | https://docs.python.org/3/license.html ; component-specific terms | CPython aggregate text and upstream notices require review |
| NumPy / Pandas / Shapely vendored native libraries | Wheel directories and collected aggregate license files | Yes | Corresponding wheel notices | Review each vendored library, not only Python-package license |
| OpenCV bundled third parties | OpenCV wheel license/third-party files retained | Yes | https://github.com/opencv/opencv-python | Review included codecs/native dependency notices |
| Qt bundled third parties | SDK source SPDX and CycloneDX files retained | Yes, only deployed modules/plugins | https://doc.qt.io/qt-6/licenses-used-in-qt.html | Collect required full text/notices and verify exact deployed-component coverage |
| Microsoft VC runtime | Python base already supplies vcruntime140/140_1; msvcp140/vcomp140 absent | Partial | Microsoft Software License Terms | Approved app-local runtime input and owner review outstanding |

## Not Distributed

Tesseract 5.4 and Leptonica are installed on the development machine but not copied
into the portable draft. The installation has Apache-2.0 top-level Tesseract text;
its numerous transitive DLLs have not received a complete redistribution audit.
Developer Tesseract remains supported. Portable selection reports unavailable
instead of searching Program Files/PATH and borrowing the installed engine.
The installer, PyInstaller, build tools and private benchmark media are not part of
the chosen portable package. No public release or zip is authorized as verified.
