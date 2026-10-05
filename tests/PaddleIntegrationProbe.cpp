#include <QApplication>
#include <QFile>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include "app/OcrCoordinator.h"
#include "app/CaptureCoordinator.h"
#include "app/RealtimePipelineCoordinator.h"
#include "app/TranslationCoordinator.h"
#include "config/SettingsManager.h"
#include "credentials/ICredentialStore.h"
#include "gui/TranslationWindow.h"
#include "ocr/OcrEngineFactory.h"
#include "translator/TranslatorFactory.h"

// Explicit opt-in probe: real local models, optionally real desktop capture/DeepL.
// No samples are embedded in this target, and it is not registered with CTest.
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        QTextStream(stderr) << message << Qt::endl;
    });
    app.setOrganizationName(QStringLiteral("TranslatorProject"));
    app.setApplicationName(QStringLiteral("Translator"));
    const auto args = app.arguments();
    if (args.size() < 4) {
        QTextStream(stderr) << "Usage: probe --batch|--live|--manual output.json image... [--deepl]\n";
        return 2;
    }
    SettingsManager productionSettings;
    const auto plan = productionSettings.deepLPlan();
    QTemporaryDir temporary;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    SettingsManager settings;
    settings.setOcrEngine(args[1] == "--manual" ? QStringLiteral("tesseract") : QStringLiteral("paddle-small"));
    settings.setSourceLanguage(QStringLiteral("zh"));
    settings.setTargetLanguage(QStringLiteral("en"));
    settings.setDeepLPlan(plan);
    settings.setTranslator(args.contains(QStringLiteral("--deepl")) ? QStringLiteral("deepl") : QStringLiteral("none"));
    auto credentials = createPlatformCredentialStore();
    const auto options = PaddleHelperOptions::fromEnvironment();
    OcrCoordinator ocr({}, nullptr, [options](const QString &id) { return OcrEngineFactory::create(id, options); });
    TranslationCoordinator translation(settings, [&] { return TranslatorFactory::create(settings, *credentials); });
    QStringList images;
    for (int i = 3; i < args.size(); ++i) if (!args[i].startsWith("--")) images.append(args[i]);
    if (images.isEmpty()) return 2;
    QJsonArray samples, translations;
    QElapsedTimer clock; clock.start();
    quint64 heartbeats = 0;
    QTimer heartbeat;
    heartbeat.setInterval(20);
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++heartbeats; });
    heartbeat.start();
    int failures = 0, index = 0, repeats = 0;
    bool finished = false;
    bool restarted = false;
    qint64 beforeRestartPid = 0, restartLatencyMs = 0;
    auto finish = [&](QJsonObject extra = {}) {
        if (finished) return;
        finished = true;
        extra.insert("samples", samples); extra.insert("translations", translations);
        extra.insert("heartbeat", qint64(heartbeats)); extra.insert("elapsed_ms", clock.elapsed());
        extra.insert("mode", args[1]);
        QFile output(args[2]);
        if (!output.open(QIODevice::WriteOnly) || output.write(QJsonDocument(extra).toJson()) < 0) ++failures;
        QTextStream(stdout) << "samples=" << samples.size() << " translations=" << translations.size()
                           << " failures=" << failures << " heartbeat=" << heartbeats << Qt::endl;
        app.exit(failures ? 1 : 0);
    };
    auto record = [&](const OcrResult &result, qint64 latency) {
        if (!result.isValid()) ++failures;
        QJsonArray boxMetadata;
        for (int i = 0; i < result.boxes.size(); ++i) {
            const auto &polygon = result.boxes[i];
            const auto bounds = polygon.boundingRect();
            const double width = result.inputSize.width(), height = result.inputSize.height();
            double twiceArea = 0;
            QJsonArray points;
            for (int j = 0; j < polygon.size(); ++j) {
                const auto &p = polygon[j], &next = polygon[(j + 1) % polygon.size()];
                twiceArea += p.x() * next.y() - next.x() * p.y();
                points.append(QJsonArray{p.x(), p.y()});
            }
            boxMetadata.append(QJsonObject{{"text", result.boxTexts.value(i)},
                {"score", result.confidences.value(i)}, {"box", points},
                {"width", bounds.width()}, {"height", bounds.height()}, {"area", qAbs(twiceArea) / 2},
                {"center_x", bounds.center().x()}, {"center_y", bounds.center().y()},
                {"relative_width", width > 0 ? bounds.width() / width : 0},
                {"relative_height", height > 0 ? bounds.height() / height : 0},
                {"relative_area", width * height > 0 ? qAbs(twiceArea) / (2 * width * height) : 0},
                {"relative_center_x", width > 0 ? bounds.center().x() / width : 0},
                {"relative_center_y", height > 0 ? bounds.center().y() / height : 0}});
        }
        samples.append(QJsonObject{{"filename", images[index]}, {"text", result.text}, {"error", result.error},
            {"request_id", result.helperRequestId}, {"results", boxMetadata},
            {"box_texts_available", result.boxTexts.size() == result.boxCount},
            {"detection_score_status", result.detectionScoreStatus},
            {"engine", result.engineId}, {"helper_pid", result.helperPid}, {"boxes", result.boxCount},
            {"input_width", result.inputSize.width()}, {"input_height", result.inputSize.height()},
            {"elapsed_ms", result.elapsedMs}, {"recognition_ms", result.recognitionMs}, {"latency_ms", latency}});
        QTextStream(stdout) << "sample=" << index << " pid=" << result.helperPid << " valid=" << result.isValid()
                           << " ms=" << result.elapsedMs << " error=" << result.error << Qt::endl;
    };
    QObject::connect(&translation, &TranslationCoordinator::resultReady, &app, [&](const TranslationResult &result) {
        translations.append(QJsonObject{{"success", result.success}, {"provider", result.provider},
            {"text", result.translatedText}, {"error", result.error}, {"http", result.httpStatus}, {"elapsed_ms", result.elapsedMs}});
        if (!result.success) ++failures;
    });
    QTimer::singleShot(args[1] == "--manual" ? 600000 : 90000, &app, [&] { ++failures; finish({{"timeout", true}}); });
    if (args[1] == QLatin1String("--batch")) {
        std::function<void()> next = [&] {
            CaptureResult capture; capture.image = QImage(images[index]);
            if (!ocr.tryRecognize(capture, "zh", "paddle-small")) { ++failures; finish(); }
        };
        QObject::connect(&ocr, &OcrCoordinator::taskFinished, &app, [&](quint64, const OcrResult &result) {
            record(result, result.elapsedMs);
            if (++repeats == 2) { repeats = 0; ++index; }
            if (index == images.size()) finish(); else next();
        });
        QTimer::singleShot(0, &app, next);
        return app.exec();
    }
    if (args[1] != QLatin1String("--live") && args[1] != QLatin1String("--manual")) return 2;
    auto *screen = QGuiApplication::primaryScreen();
    if (!screen || QGuiApplication::platformName() == QLatin1String("offscreen")) return 2;
    QLabel source;
    source.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    source.setPixmap(QPixmap(images.first()));
    source.setFixedSize(source.pixmap().size());
    source.move(screen->availableGeometry().topLeft() + QPoint(40, 60));
    source.show();
    settings.setCaptureRegion(QRect(source.pos(), source.size()), screen->name());
    TranslationWindow window(settings, nullptr, credentials.get());
    window.move(screen->availableGeometry().topLeft() + QPoint(100, 300));
    window.show();
    RealtimePipelineCoordinator pipeline(settings, ocr, translation);
    QObject::connect(&window, &TranslationWindow::startRequested, &pipeline, [&] { pipeline.start(); });
    QObject::connect(&window, &TranslationWindow::stopRequested, &pipeline, &RealtimePipelineCoordinator::stop);
    QObject::connect(&pipeline, &RealtimePipelineCoordinator::runningChanged, &window, &TranslationWindow::setTranslationRunning);
    QObject::connect(&pipeline, &RealtimePipelineCoordinator::feedback, &window, &TranslationWindow::setRegionFeedback);
    QObject::connect(&pipeline, &RealtimePipelineCoordinator::originalTextReady, &window, &TranslationWindow::setOriginalText);
    QObject::connect(&translation, &TranslationCoordinator::stateChanged, &window, &TranslationWindow::setTranslationState);
    QObject::connect(&translation, &TranslationCoordinator::resultReady, &window, [&](const TranslationResult &result) {
        if (result.success) window.setTranslatedText(result.translatedText);
    });
    if (args[1] == QLatin1String("--manual")) {
        CaptureCoordinator capture(window, settings);
        QObject::connect(&capture, &CaptureCoordinator::selectionStarted, &pipeline, &RealtimePipelineCoordinator::stop);
        QObject::connect(&capture, &CaptureCoordinator::captureCompleted, &pipeline, &RealtimePipelineCoordinator::acceptOneShot);
        QObject::connect(&pipeline, &RealtimePipelineCoordinator::sampleAccepted, &app,
            [&](quint64, quint64, const OcrResult &result, qint64 latency) { record(result, latency); });
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] {
            pipeline.stop();
            finish({{"captures", qint64(pipeline.statistics().captures)}, {"engine_setting", settings.ocrEngine()}});
        });
        return app.exec();
    }
    QObject::connect(&pipeline, &RealtimePipelineCoordinator::sampleAccepted, &app,
        [&](quint64, quint64, const OcrResult &result, qint64 latency) {
            record(result, latency);
            if (restarted) restartLatencyMs = result.elapsedMs;
            // Keep this frame stable long enough for its translation to return.
            QTimer::singleShot(5000, &app, [&, text = result.text] {
                if (++index < images.size()) source.setPixmap(QPixmap(images[index]));
                else {
                    --index;
                    window.findChild<QPushButton *>("stopButton")->click();
                    const auto captures = pipeline.statistics().captures;
                    QTimer::singleShot(1000, &app, [&, captures, text] {
                        if (captures != pipeline.statistics().captures) ++failures;
                        if (args.contains(QStringLiteral("--restart")) && !restarted) {
                            restarted = true;
                            beforeRestartPid = samples.last().toObject().value("helper_pid").toVariant().toLongLong();
                            window.findChild<QPushButton *>("startButton")->click();
                            return;
                        }
                        const bool samePid = !restarted || (beforeRestartPid > 0 && beforeRestartPid
                            == samples.last().toObject().value("helper_pid").toVariant().toLongLong());
                        if (!samePid) ++failures;
                        auto *original = window.findChild<QLabel *>("originalLabel");
                        auto *translated = window.findChild<QLabel *>("translatedLabel");
                        const bool originalVisible = original && original->text() == text;
                        // UI text is checked without logging credentials or altering user settings.
                        const bool translationVisible = translated && !translations.isEmpty()
                            && translated->text() == translations.last().toObject().value("text").toString();
                        if (!originalVisible || (settings.translator() == "deepl" && !translationVisible)) ++failures;
                        // Only this application's rendered subtitles, never the surrounding desktop.
                        if (!window.grab().save(args[2] + ".png")) ++failures;
                        finish({{"captures", qint64(pipeline.statistics().captures)},
                            {"unchanged", qint64(pipeline.statistics().unchangedFrames)},
                            {"original_visible", originalVisible}, {"translation_visible", translationVisible},
                            {"restart_tested", restarted}, {"restart_same_pid", samePid},
                            {"restart_first_ocr_ms", restartLatencyMs},
                            {"stop_halts_capture", captures == pipeline.statistics().captures}});
                    });
                }
            });
        });
    QTimer::singleShot(500, &window, [&] { window.findChild<QPushButton *>("startButton")->click(); });
    return app.exec();
}
