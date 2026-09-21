# Translator

`Translator` 是一个基于 Qt 6、C++17 和 Qt Widgets 的实时屏幕文字识别与翻译程序。它参考 LunaTranslator 的功能边界和运行流程，但不会机械地逐行移植 Python 代码。

原 LunaTranslator 源码保持独立且不受本工程影响。

## 当前阶段

当前为 Phase 1：架构分析与可构建工程骨架。

已完成：

- 分析原项目的启动、GUI、文本源、OCR、翻译、语言处理、配置、Windows native 和异步调用关系。
- 记录 Qt 6/C++ 的模块划分与迁移边界，详见 [docs/architecture.md](docs/architecture.md)。
- 创建独立 CMake 工程。
- 创建最小 `QApplication` 入口和 `QMainWindow` 窗口。

## 尚未实现

- Screen Capture
- OCR
- Translation
- Real-time Pipeline
- Overlay
- Hook
- TTS

这些能力属于后续阶段。本阶段没有引入 OpenCV、PaddleOCR、Tesseract、Python runtime、翻译 SDK 或 AI runtime。

## 构建

需要 CMake、支持 C++17 的编译器，以及包含 Core、Gui、Widgets 组件的 Qt 6 开发环境。Qt 安装位置通过标准 CMake 机制发现；必要时由构建者设置 `CMAKE_PREFIX_PATH` 或 `Qt6_DIR`，工程本身不硬编码安装路径。

从仓库根目录执行：

```powershell
cmake -S . -B .\build\mingw
cmake --build .\build\mingw --config Release
```

对于单配置生成器，可直接运行：

```powershell
cmake --build .\build\mingw
```
