# Translator

`Translator` 是一个基于 Qt 6、C++17 和 Qt Widgets 的实时屏幕文字识别与翻译程序。本项目参考 LunaTranslator 的架构和功能设计，但采用独立的 Qt 6/C++ 实现；原 LunaTranslator 源码保持独立且不受本工程影响。

## 当前阶段

当前为 Phase 2：基础翻译界面与设置持久化。

已实现：

- Qt 6 application 和 `QMainWindow`
- 基础翻译 UI
- Source/Target Language 选择
- OCR/Translator engine 选择
- Original Text 和 Translation 只读显示区域
- Start/Stop 基础 UI 状态切换
- 基于 `QSettings` 的配置持久化
- 非法或过期配置的默认值回退

## 尚未实现

- Region selection
- Screen capture
- OCR backend
- Translation backend
- Real-time pipeline
- Overlay
- Hook
- TTS

当前界面不会伪装这些后端已经可用。未实现操作只更新状态栏，不会发起截图、OCR、翻译或网络请求。

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
