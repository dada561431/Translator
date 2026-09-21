# Translator

`Translator` 是一个基于 Qt 6、C++17 和 Qt Widgets 的实时屏幕文字识别与翻译程序。本项目参考 LunaTranslator 的架构和功能设计，但采用独立的 Qt 6/C++ 实现；原 LunaTranslator 源码保持独立且不受本工程影响。

## 当前阶段

当前为 Phase 2.5：悬浮翻译窗口 UI 架构。

已实现：

- Qt 6 application 和轻量 `TranslationWindow`
- 无系统标题栏、始终置顶的半透明悬浮窗口
- Region/Start/Stop/Settings/Close 工具栏
- 独立 `SettingsDialog`
- Source/Target Language 和 OCR/Translator engine 选择
- Original Text 和 Translation 只读显示区域
- Start/Stop 基础 UI 状态切换
- 基于 `QSettings` 的配置持久化
- 非法或过期配置的默认值回退
- 悬浮窗口位置与尺寸恢复，以及屏幕外位置回退

## 尚未实现

- Region selection
- Screen capture
- OCR backend
- Translation backend
- Real-time pipeline
- Overlay
- Hook
- TTS

当前界面不会伪装这些后端已经可用。未实现操作只更新悬浮窗状态标签，不会发起截图、OCR、翻译或网络请求。

## 当前 UI 架构

应用启动后首先显示 `TranslationWindow`。语言和引擎配置不长期占用悬浮窗口，而是由工具栏的 Settings 按钮打开唯一的 `SettingsDialog`。两个窗口共享同一个 `SettingsManager`，所有配置继续由 `QSettings` 集中持久化。

`TranslationWindow` 使用 Qt 原生 `QWindow::startSystemMove()` 支持从工具栏空白区域拖动。当前不启用鼠标穿透或工具栏自动隐藏，以确保窗口仍可直接操作。

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
