#include "ocr/PaddleRuntimeLocator.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
QString checkoutRoot(QString start)
{
    QDir dir(start);
    do {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("helpers/ocr/paddle_helper.py"))))
            return dir.absolutePath();
    } while (dir.cdUp());
    return {};
}
}

PaddleHelperOptions PaddleRuntimeLocator::locate(const QString &app, const QString &cwd,
                                                const QProcessEnvironment &env)
{
    PaddleHelperOptions result;
    const QDir directory(app);
    const bool portable = QFileInfo::exists(directory.filePath(QStringLiteral("runtime-manifest.json")))
        || directory.exists(QStringLiteral("ocr"));
    QString root;
    if (portable) {
        result.portablePython = true;
        result.python = directory.filePath(QStringLiteral("ocr/runtime/python.exe"));
        result.script = directory.filePath(QStringLiteral("ocr/helper/paddle_helper.py"));
        result.models = directory.filePath(QStringLiteral("ocr/models"));
        const QString executable = directory.filePath(QStringLiteral("ocr/helper/PaddleOcrHelper.exe"));
        if (QFileInfo::exists(executable)) result.executable = executable;
    } else {
        root = checkoutRoot(app);
        if (root.isEmpty()) root = checkoutRoot(cwd);
        if (!root.isEmpty()) {
            result.python = QDir(root).filePath(QStringLiteral("benchmarks/ocr_phase61a/.venv/Scripts/python.exe"));
            result.script = QDir(root).filePath(QStringLiteral("helpers/ocr/paddle_helper.py"));
            result.models = QDir(root).filePath(QStringLiteral("benchmarks/ocr_phase61a/models"));
        }
    }
    for (const auto &entry : {qMakePair("TRANSLATOR_OCR_PYTHON", &result.python),
                              qMakePair("TRANSLATOR_OCR_HELPER", &result.script),
                              qMakePair("TRANSLATOR_OCR_MODELS", &result.models)}) {
        const QString value = env.value(QLatin1String(entry.first));
        if (!value.isEmpty()) {
            *entry.second = value;
            if (entry.second != &result.models) result.executable.clear();
        }
    }
    result.workingDirectory = QFileInfo(result.executable.isEmpty() ? result.python : result.executable).absolutePath();
    result.cache = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("ocr-helper"));
    if ((result.executable.isEmpty() && (!QFileInfo(result.python).isFile() || !QFileInfo(result.script).isFile()))
        || (!result.executable.isEmpty() && !QFileInfo(result.executable).isFile()))
        result.validationError = QStringLiteral("PP-OCRv6 runtime is incomplete. Restore the portable package or configure development overrides; select Tesseract to fall back.");
    for (const auto &name : {QStringLiteral("PP-OCRv6_small_det"), QStringLiteral("PP-OCRv6_small_rec")}) {
        QDir model(QDir(result.models).filePath(name + QLatin1Char('/') + name + QStringLiteral("_infer")));
        for (const auto &file : {QStringLiteral("inference.yml"), QStringLiteral("inference.json"), QStringLiteral("inference.pdiparams")})
            if (!QFileInfo(model.filePath(file)).isFile())
                result.validationError = QStringLiteral("PP-OCRv6 local model is missing or incomplete. Restore det/rec assets; no automatic download is performed.");
    }
    return result;
}

QStringList PaddleRuntimeLocator::arguments(const PaddleHelperOptions &options)
{
    QStringList args;
    if (options.executable.isEmpty()) {
        if (options.portablePython) args << QStringLiteral("-B");
        args << QStringLiteral("-u") << options.script;
    }
    args << QStringLiteral("--models") << options.models << QStringLiteral("--cpu-threads") << QStringLiteral("4");
    return args;
}

QProcessEnvironment PaddleRuntimeLocator::environment(const PaddleHelperOptions &options,
                                                       const QProcessEnvironment &system)
{
    QProcessEnvironment env;
    for (const auto &key : system.keys()) {
        if (QStringList{QStringLiteral("PATH"), QStringLiteral("SYSTEMROOT"), QStringLiteral("WINDIR"),
            QStringLiteral("TEMP"), QStringLiteral("TMP"), QStringLiteral("USERPROFILE"),
            QStringLiteral("LOCALAPPDATA"), QStringLiteral("APPDATA"), QStringLiteral("NUMBER_OF_PROCESSORS")}.contains(key.toUpper()))
            env.insert(key, system.value(key));
    }
    env.insert(QStringLiteral("PYTHONNOUSERSITE"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    env.insert(QStringLiteral("HF_HUB_OFFLINE"), QStringLiteral("1"));
    env.insert(QStringLiteral("HF_HUB_DISABLE_TELEMETRY"), QStringLiteral("1"));
    env.insert(QStringLiteral("PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK"), QStringLiteral("True"));
    env.insert(QStringLiteral("PADDLE_PDX_CACHE_HOME"), options.cache);
    env.insert(QStringLiteral("HF_HOME"), QDir(options.cache).filePath(QStringLiteral("hf")));
    env.insert(QStringLiteral("MODELSCOPE_CACHE"), QDir(options.cache).filePath(QStringLiteral("modelscope")));
    env.insert(QStringLiteral("PADDLE_HOME"), QDir(options.cache).filePath(QStringLiteral("paddle")));
    return env;
}
