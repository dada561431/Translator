#pragma once
#include "ocr/PaddleOcrEngine.h"
#include <QProcessEnvironment>

class PaddleRuntimeLocator final
{
public:
    static PaddleHelperOptions locate(const QString &applicationDirectory,
                                     const QString &currentDirectory,
                                     const QProcessEnvironment &environment);
    static QStringList arguments(const PaddleHelperOptions &options);
    static QProcessEnvironment environment(const PaddleHelperOptions &options,
                                          const QProcessEnvironment &inherited);
};
