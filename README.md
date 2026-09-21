# Translator

`Translator` 是一个基于 Qt 6、C++17 和 Qt Widgets 的实时屏幕文字识别与翻译程序。本项目参考 LunaTranslator 的架构和功能设计，但采用独立的 Qt 6/C++ 实现；原 LunaTranslator 源码保持独立且不受本工程影响。

## 当前阶段

当前为 Phase 3：区域选择与单帧屏幕捕获。

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

## 尚未实现

- OCR backend
- Translation backend
- Continuous capture / Real-time pipeline
- Overlay click-through
- Hook
- TTS

Region 按钮现在会实际选择并截取一次屏幕区域，不会发起 OCR、翻译或网络请求。取消选择不会覆盖上次有效 Region。Debug 构建仅在本机系统临时目录覆盖保存一张 `Translator/last_capture.png` 供验证；Release 构建不写这张调试图。截图不会上传或写入仓库。

## 当前 UI 架构

应用启动后首先显示 `TranslationWindow`。语言和引擎配置不长期占用悬浮窗口，而是由工具栏的 Settings 按钮打开唯一的 `SettingsDialog`。两个窗口共享同一个 `SettingsManager`，所有配置继续由 `QSettings` 集中持久化。

`TranslationWindow` 的主体背景完全透明，以 `QLabel` 显示居中的双层字幕：较大的译文在上，较小的原文在下。字幕使用高对比文字和阴影保持复杂背景下的基本可读性，不使用大型文本编辑框或背景面板。

启动时译文和原文位置分别显示 `实时翻译将在这里显示` 与 `Original text appears here`，用于标示悬浮窗位置，不进入配置或业务管线。每个字段在首次收到非空真实内容时独立替换自己的 placeholder。

半透明工具栏启动时隐藏，鼠标进入整个悬浮窗时显示，真正离开窗口 400ms 后隐藏；placeholder 始终可见，因此窗口仍可被发现。窗口通过 Qt 原生 `QWindow::startSystemMove()` 支持从工具栏空白处或字幕区域拖动，并通过 `startSystemResize()` 支持边缘缩放。当前不启用鼠标穿透。

`TranslationWindow` 通过 `regionSelectionRequested()` 请求选区，`CaptureCoordinator` 隐藏悬浮窗并启动 `RegionSelector`。选区 overlay 关闭后延迟一次短暂合成周期，再由 `ScreenCaptureService` 截取 `CaptureResult::image`，最后恢复悬浮窗。截图仅在单个 `QScreen` 内进行，不支持跨不同 DPI 显示器拖出一个区域。

## 构建

需要 CMake、Ninja、支持 C++17 的编译器，以及包含 Core、Gui、Widgets 组件的 Qt 6 开发环境。Qt 安装位置通过标准 CMake 机制发现；必要时由构建者在命令行设置 `CMAKE_PREFIX_PATH` 或 `Qt6_DIR`，工程本身不硬编码本机安装路径。

```powershell
cmake -S . -B .\build\mingw -G Ninja
cmake --build .\build\mingw
```

运行自动化检查：

```powershell
ctest --test-dir .\build\mingw --output-on-failure
```

应用程序生成于 `build/mingw/Translator.exe`。
