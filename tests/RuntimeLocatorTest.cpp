#include "ocr/PaddleRuntimeLocator.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("TranslatorLocatorTest"));
    QTemporaryDir temp;
    const QString root = temp.path() + QStringLiteral("/portable space \u4e2d\u6587");
    auto touch = [](const QString &path) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path); return file.open(QIODevice::WriteOnly);
    };
    int failures = 0;
    auto check = [&](bool value) { if (!value) ++failures; };
    touch(root + "/runtime-manifest.json");
    touch(root + "/ocr/runtime/python.exe"); touch(root + "/ocr/helper/paddle_helper.py");
    for (const QString &name : {QStringLiteral("PP-OCRv6_small_det"), QStringLiteral("PP-OCRv6_small_rec")})
        for (const QString &file : {QStringLiteral("inference.yml"), QStringLiteral("inference.json"), QStringLiteral("inference.pdiparams")})
            touch(root + "/ocr/models/" + name + '/' + name + "_infer/" + file);
    QProcessEnvironment env;
    auto found = PaddleRuntimeLocator::locate(root, QDir::currentPath(), env);
    check(found.validationError.isEmpty()); check(found.python.startsWith(root));
    check(!found.cache.startsWith(root));
    check(PaddleRuntimeLocator::arguments(found).first() == "-B");
    touch(root + "/ocr/helper/PaddleOcrHelper.exe");
    found = PaddleRuntimeLocator::locate(root, QDir::currentPath(), env);
    check(!found.executable.isEmpty()); check(PaddleRuntimeLocator::arguments(found).first() == "--models");
    env.insert("TRANSLATOR_OCR_PYTHON", root + "/override/python.exe");
    touch(root + "/override/python.exe");
    found = PaddleRuntimeLocator::locate(root, QDir::currentPath(), env);
    check(found.executable.isEmpty()); check(found.python == env.value("TRANSLATOR_OCR_PYTHON"));
    env.insert("PYTHONHOME", "untrusted"); env.insert("PYTHONPATH", "untrusted"); env.insert("DEEP_L_API_KEY", "not-a-real-key");
    const auto isolated = PaddleRuntimeLocator::environment(found, env);
    check(!isolated.contains("PYTHONHOME") && !isolated.contains("PYTHONPATH"));
    check(!isolated.contains("DEEP_L_API_KEY"));
    check(isolated.value("HF_HUB_OFFLINE") == "1");
    QFile::remove(root + "/ocr/models/PP-OCRv6_small_rec/PP-OCRv6_small_rec_infer/inference.pdiparams");
    check(!PaddleRuntimeLocator::locate(root, QDir::currentPath(), env).validationError.isEmpty());
    QFile::remove(root + "/ocr/helper/PaddleOcrHelper.exe"); QFile::remove(root + "/ocr/runtime/python.exe");
    found = PaddleRuntimeLocator::locate(root, QDir::currentPath(), {});
    check(!found.validationError.isEmpty()); check(found.python.startsWith(root));
    std::cout << "runtime locator failures=" << failures << '\n';
    return failures ? 1 : 0;
}
