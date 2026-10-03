#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPainter>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include "app/RealtimePipelineCoordinator.h"
#include "app/OcrCoordinator.h"
#include "app/TranslationCoordinator.h"
#include "config/SettingsManager.h"
#include "ocr/TesseractOcrEngine.h"
#include "translator/TranslatorFactory.h"
#include "credentials/ICredentialStore.h"

// This is an offline, synthetic-input probe, NOT screen/video/UI acceptance.
int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("TranslatorRealtimeProbe"));
    app.setApplicationName(QStringLiteral("SyntheticInput"));
    QTemporaryDir temporary;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    const auto args = app.arguments();
    const int duration = args.size() > 1 ? qMax(12000, args.at(1).toInt()) : 300000;
    const QString fontPath = qEnvironmentVariable("TRANSLATOR_TEST_FONT");
    const int fontId = fontPath.isEmpty() ? -1 : QFontDatabase::addApplicationFont(fontPath);
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    if (families.isEmpty()) {
        QTextStream(stderr) << "Set TRANSLATOR_TEST_FONT to a readable local font file.\n";
        return 2;
    }
    SettingsManager settings;
    settings.setSourceLanguage(QStringLiteral("en"));
    settings.setTranslator(args.contains(QStringLiteral("--deepl"))
                           ? QStringLiteral("deepl") : QStringLiteral("none"));
    settings.setCaptureRegion(QRect(QGuiApplication::primaryScreen()->geometry().topLeft(), QSize(160, 50)),
                              QGuiApplication::primaryScreen()->name());
    OcrCoordinator ocr([] { return std::make_unique<TesseractOcrEngine>(); });
    auto credentials = createPlatformCredentialStore();
    TranslationCoordinator translation(settings, [&] { return TranslatorFactory::create(settings, *credentials); });
    QElapsedTimer clock; clock.start();
    QImage image(640, 100, QImage::Format_RGB32);
    QString visible;
    auto render = [&](const QString &text) {
        visible = text;
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setPen(Qt::black);
        painter.setFont(QFont(families.first(), 24));
        painter.drawText(image.rect(), Qt::AlignCenter, text);
    };
    render(QStringLiteral("Hello Phase 6"));
    RealtimePipelineCoordinator pipeline(settings, ocr, translation, nullptr,
        [&](const QRect &region, const QString &) {
            CaptureResult capture;
            capture.image = image; capture.globalRect = region;
            capture.screenName = QStringLiteral("synthetic-not-screen");
            return capture;
        });
    quint64 uiHeartbeat = 0, accepted = 0;
    QObject::connect(&translation, &TranslationCoordinator::resultReady, &app,
        [&](const TranslationResult &result) {
            QTextStream(stdout) << "TRANSLATION t_ms=" << clock.elapsed() << " success=" << result.success
                << " provider=" << result.provider << " elapsed_ms=" << result.elapsedMs
                << " text=" << result.translatedText << Qt::endl;
        });
    QTimer heartbeat;
    heartbeat.setInterval(50);
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++uiHeartbeat; });
    heartbeat.start();
    QTimer contentTimer;
    contentTimer.setInterval(1500);
    QObject::connect(&contentTimer, &QTimer::timeout, &app, [&] {
        render(QStringLiteral("Realtime OCR Test %1").arg(clock.elapsed() / 1500));
    });
    QObject::connect(&pipeline, &RealtimePipelineCoordinator::sampleAccepted, &app,
        [&](quint64 session, quint64 frame, const OcrResult &result, qint64 latency) {
            ++accepted;
            QTextStream(stdout) << "SYNTHETIC sample t_ms=" << clock.elapsed() << " session=" << session
                << " frame=" << frame << " visible=" << visible << " ocr=" << result.text
                << " ocr_ms=" << result.elapsedMs << " capture_to_ocr_ms=" << latency << Qt::endl;
        });
    QTimer::singleShot(5200, &app, [&] {
        const auto &s = pipeline.statistics();
        QTextStream(stdout) << "STATIC captures=" << s.captures << " ocr=" << s.ocrStarted
            << " unchanged=" << s.unchangedFrames << Qt::endl;
        render(QStringLiteral("Realtime OCR Test 123"));
    });
    QTimer::singleShot(7000, &contentTimer, [&] { contentTimer.start(); });
    QTimer::singleShot(duration / 2, &app, [&] {
        pipeline.stop();
        const auto captures = pipeline.statistics().captures;
        const auto ocrCount = pipeline.statistics().ocrStarted;
        const auto updates = accepted;
        QTimer::singleShot(5200, &app, [&, captures, ocrCount, updates] {
            QTextStream(stdout) << "STOP_5S captures_unchanged=" << (captures == pipeline.statistics().captures)
                << " ocr_unchanged=" << (ocrCount == pipeline.statistics().ocrStarted)
                << " updates_unchanged=" << (updates == accepted) << Qt::endl;
            pipeline.start();
        });
    });
    QTimer::singleShot(duration, &app, [&] {
        pipeline.stop();
        const auto &s = pipeline.statistics();
        QTextStream(stdout) << "SUMMARY synthetic_only=true elapsed_ms=" << clock.elapsed()
            << " captures=" << s.captures << " ocr=" << s.ocrStarted << " unchanged=" << s.unchangedFrames
            << " duplicates=" << s.duplicateTexts << " translations=" << s.translationsStarted
            << " accepted=" << accepted << " heartbeat=" << uiHeartbeat
            << " pending=" << pipeline.pendingCount() << " max_capture_us=" << s.maxCaptureUs
            << " max_diff_us=" << s.maxDiffUs << Qt::endl;
        app.quit();
    });
    if (!pipeline.start()) return 1;
    return app.exec();
}
