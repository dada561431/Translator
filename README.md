# Translator

`Translator` 是一个基于 Qt 6、C++17 和 Qt Widgets 的实时屏幕文字识别与翻译程序。本项目参考 LunaTranslator 的架构和功能设计，但采用独立的 Qt 6/C++ 实现；原 LunaTranslator 源码保持独立且不受本工程影响。

## Phase 5 Translation Backend

One Region selection now triggers one local OCR operation and one optional
asynchronous translation. `OcrCoordinator::resultReady` feeds
`TranslationCoordinator`, then `ITranslator` and the official DeepL API through
QtNetwork. Increasing IDs reject stale/duplicate responses. New input invalidates
the old translation immediately. Pending shows `翻译中…`; Error shows `翻译失败`
without losing the original text. Settings changes also invalidate pending work.

Settings offer `None` (`none`, default) and `DeepL` (`deepl`). None never sends
translation requests. DeepL sends OCR **text** to the service, never images or
region pixels. OCR is local; online translation is not entirely local processing.

### DeepL configuration

Set `DEEPL_API_KEY` in the launching process environment. In Qt Creator use
the run environment editor. Do not store credentials in tracked project files,
QSettings, screenshots, shared logs, or commits. Environment changes require
restarting Translator. A missing key fails locally with
`DeepL API key is not configured.`. No key-entry UI is implemented; future
user-level credential management should use a secure credential store.

Optional `DEEPL_API_URL` defaults to `https://api-free.deepl.com/v2/translate`.
Pro users set `https://api.deepl.com/v2/translate`. Only these official HTTPS
endpoints are accepted; redirects are not followed. TLS verification is enabled.
Requests have a 15-second deadline and no automatic retries.

| App ID | Source code | Target code |
| --- | --- | --- |
| `auto` | omit `source_lang` | invalid |
| `en` | `EN` | `EN-US` |
| `zh` | `ZH` | `ZH-HANS` (simplified) |
| `ja` | `JA` | `JA` |
| `ko` | `KO` | `KO` |

Translation `auto` uses DeepL detection; OCR `auto` still falls back to English.
Equal source/target IDs return text locally. Blank/error OCR is never translated.
Offline CTest uses fake/coordinator and HTTP fixtures, not real API calls.
For manual EN → ZH and ZH → EN checks with a configured legitimate key:

```powershell
.\build\mingw\TranslatorDeepLProbe.exe
```

Without a key it reports SKIPPED. Real-provider and Region-to-translation manual
acceptance remain pending in this environment because no key was configured.
See `docs/translation-phase5.md`. Phase 6 realtime capture is not implemented.

## Phase 4.1 OCR accuracy status

Phase 4 adds a single-frame OCR path after Region capture. A successful capture
produces a `CaptureResult::image`; `OcrCoordinator` passes that image and the
configured source-language ID to an `IOcrEngine` on a worker thread and emits an
`OcrResult`. Valid recognized text, including an empty result, is passed to
`TranslationWindow::setOriginalText()`; an empty result clears prior original
text after the placeholder has been replaced. This is not continuous capture or
continuous translation: Start/Stop remains a UI state control. Phase 5 translates
valid OCR text only when DeepL is selected.

The initial backend is Tesseract (`tesseract` engine ID). Language IDs are
`auto`, `zh`, `en`, `ja`, and `ko`; `auto` is an English `eng` fallback, not
source-language detection. Phase 4.1 adds a Qt-only OCR preprocessor, conditional
2x/3x smooth scaling, grayscale conversion, percentile contrast stretching,
subtitle-oriented PSM 6/7 selection, explicit trained-data errors, and detailed
Debug diagnostics. It does not force binary thresholding and does not add
OpenCV or another OCR runtime.

Tesseract 5.4 was manually verified with `eng` and `chi_sim`. The Japanese and
Korean language mappings (`jpn`, `kor`) and missing-model diagnostics exist, but
Japanese/Korean recognition quality has not been verified. Runtime and matching
trained data are required on other machines. Packaging and trained-data
distribution remain future work.

Automated verification includes the Phase 2/3/4 regression suites and the
`phase41_ocr_tuning` preprocessor/PSM/error suite. Manual tools generate
temporary fixed-font samples and compare the same image with original RGB and
optimized preprocessing. Real Bilibili subtitle tests found improvement on
small ordinary text but poor reliability on artistic text, outlines, and
complex moving backgrounds. See `docs/ocr-accuracy-phase41.md`. PaddleOCR or
another scene-text backend should be evaluated later; it is not implemented in
this phase.

Run all checks with:

```powershell
cmake -S . -B .\build\mingw -G Ninja -DCMAKE_PREFIX_PATH="<Qt6 install prefix>"
cmake --build .\build\mingw
ctest --test-dir .\build\mingw --output-on-failure
```

Automated tests do not establish recognition quality on every font, video, or
DPI configuration. `TranslatorOcrBenchmark` and
`TranslatorOcrSampleGenerator` are manual test tools, not production pipeline
components.

## 当前阶段

当前为 Phase 5：保留 Phase 4.1 OCR，增加官方 DeepL 异步翻译与过期结果保护。

已实现：

- Qt 6 application 和轻量 `TranslationWindow`
- 无系统标题栏、始终置顶的透明字幕悬浮窗口
- Region/Start/Stop/Settings/Close 工具栏
- 独立 `SettingsDialog`
- Source/Target Language 和 OCR/Translator engine 选择
- 上方译文、下方原文的自动换行字幕区域
- 高对比字幕文字与轻量阴影
- 鼠标进入时显示、离开后隐藏的半透明工具栏
- 启动时可见的双语 UI placeholder 与 hover 工具栏
- Start/Stop 基础 UI 状态切换
- 基于 `QSettings` 的配置持久化
- 非法或过期配置的默认值回退
- 悬浮窗口位置与尺寸恢复，以及屏幕外位置回退
- 任意当前显示器内的单屏区域选择，支持反向拖动、Escape 取消
- 基于 `QScreen::grabWindow()` 的一次性截图，得到 `QImage`
- 上次有效 Region 与显示器名称持久化，失效区域启动时安全忽略
- Qt 逻辑坐标和 High-DPI 截图像素尺寸记录
- 可替换的 OCR 接口、Tesseract 后端和异步 `OcrCoordinator`
- OCR 层内的放大、灰度化和对比度拉伸预处理
- 面向单行/多行字幕的 PSM 7/6 动态选择
- 输入/处理尺寸、语言资源、预处理和耗时 Debug 诊断
- OCR 有效结果更新原文字幕；空结果可清除先前原文
- 独立翻译接口、协调器、语言映射与 DeepL 后端
- None/DeepL 配置、QtNetwork 异步请求、错误状态与 stale response 保护

## 尚未实现

- Continuous capture / Real-time pipeline
- Overlay click-through
- Hook
- TTS

Region 按钮会选择并截取一次屏幕区域，然后发起一次本地 OCR；选择 DeepL 时有效文字进入官方翻译 API，None 不发送翻译请求。取消选择不会覆盖上次有效 Region。Debug 构建仅在本机系统临时目录覆盖保存一张 `Translator/last_capture.png` 供验证；Release 构建不写这张调试图。截图不会上传或写入仓库。

## 当前 UI 架构

应用启动后首先显示 `TranslationWindow`。语言和引擎配置不长期占用悬浮窗口，而是由工具栏的 Settings 按钮打开唯一的 `SettingsDialog`。两个窗口共享同一个 `SettingsManager`，所有配置继续由 `QSettings` 集中持久化。

`TranslationWindow` 的主体背景完全透明，以 `QLabel` 显示居中的双层字幕：较大的译文在上，较小的原文在下。字幕使用高对比文字和阴影保持复杂背景下的基本可读性，不使用大型文本编辑框或背景面板。

启动时译文和原文位置分别显示 `实时翻译将在这里显示` 与 `Original text appears here`，用于标示悬浮窗位置，不进入配置或业务管线。每个字段在首次收到非空真实内容时独立替换自己的 placeholder。

半透明工具栏启动时可见；进入真实字幕状态后，鼠标进入整个悬浮窗时显示，真正离开窗口 400ms 后隐藏。placeholder 始终可见，因此窗口仍可被发现。窗口通过 Qt 原生 `QWindow::startSystemMove()` 支持从工具栏空白处或字幕区域拖动，并通过 `startSystemResize()` 支持边缘缩放。当前不启用鼠标穿透。

`TranslationWindow` 通过 `regionSelectionRequested()` 请求选区，`CaptureCoordinator` 隐藏悬浮窗并启动 `RegionSelector`。选区 overlay 关闭后延迟一次短暂合成周期，再由 `ScreenCaptureService` 截取 `CaptureResult::image`，最后恢复悬浮窗。截图仅在单个 `QScreen` 内进行，不支持跨不同 DPI 显示器拖出一个区域。

## 构建

需要 CMake、Ninja、支持 C++17 的编译器，以及包含 Core、Gui、Widgets、Network 组件的 Qt 6 开发环境。Qt 安装位置通过标准 CMake 机制发现；必要时由构建者在命令行设置 `CMAKE_PREFIX_PATH` 或 `Qt6_DIR`，也可使用环境变量 `CMAKE_PREFIX_PATH`。Windows 上构建和运行时还需让对应 MinGW 与 Qt 的 `bin` 目录可从 `PATH` 找到。工程本身不硬编码本机安装路径。

```powershell
cmake -S . -B .\build\mingw -G Ninja -DCMAKE_PREFIX_PATH="<Qt6 install prefix>"
cmake --build .\build\mingw
```

运行自动化检查：

```powershell
ctest --test-dir .\build\mingw --output-on-failure
```

应用程序生成于 `build/mingw/Translator.exe`。
