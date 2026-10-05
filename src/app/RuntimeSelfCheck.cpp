#include "app/RuntimeSelfCheck.h"
#include "ocr/PaddleOcrEngine.h"
#include "ocr/TesseractOcrEngine.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QTextStream>
#include <algorithm>
#include <numeric>

int runRuntimeSelfCheck(const QString &reportPath)
{
    QImage image(720, 110, QImage::Format_RGB888);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setFont(QFont(QStringLiteral("Arial"), 32));
    painter.setPen(Qt::black);
    painter.drawText(image.rect(), Qt::AlignCenter, QStringLiteral("Hello Portable OCR"));
    painter.end();
    QJsonObject report;
    report.insert("qt", QString::fromLatin1(qVersion()));
    report.insert("qwindows", QFileInfo::exists(QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("platforms/qwindows.dll"))));
    bool success = true;
    QList<qint64> warm;
    QJsonArray requests;
    qint64 pid = 0;
    {
        PaddleOcrEngine engine;
        for (int i = 0; i < 20; ++i) {
            const auto result = engine.recognize(image, QStringLiteral("en"));
            if (!pid) pid = result.helperPid;
            success = success && result.isValid() && pid > 0 && pid == result.helperPid
                && result.text.simplified().compare(QStringLiteral("Hello Portable OCR"), Qt::CaseInsensitive) == 0;
            requests.append(QJsonObject{{"elapsed_ms", result.elapsedMs}, {"pid", result.helperPid},
                                        {"success", result.isValid()}, {"error", result.error}});
            if (i) warm.append(result.elapsedMs);
            if (!result.isValid()) break;
        }
    }
    TesseractOcrEngine fallback;
    const auto tess = fallback.recognize(image, QStringLiteral("en"));
    report.insert("tesseract_available", tess.isValid());
    report.insert("tesseract_error", tess.isValid() ? QString() : QStringLiteral("Tesseract runtime is not bundled in this package. Development installations remain supported outside portable mode."));
    report.insert("requests", requests);
    report.insert("success", success && requests.size() == 20);
    report.insert("helper_pid", pid);
    report.insert("translation_requests", 0);
    if (!warm.isEmpty()) {
        std::sort(warm.begin(), warm.end());
        report.insert("warm_median_ms", warm[warm.size() / 2]);
        report.insert("warm_mean_ms", double(std::accumulate(warm.begin(), warm.end(), qint64(0))) / warm.size());
        report.insert("warm_over_300", int(std::count_if(warm.begin(), warm.end(), [](qint64 ms) { return ms > 300; })));
    }
    const QByteArray bytes = QJsonDocument(report).toJson();
    if (!reportPath.isEmpty()) {
        QFile file(reportPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return 2;
    }
    QTextStream(stdout) << bytes << Qt::endl;
    return success && requests.size() == 20 ? 0 : 1;
}
