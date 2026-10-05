#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPainter>
#include <QScreen>
#include <QSemaphore>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <atomic>
#include <iostream>
#include "app/RealtimePipelineCoordinator.h"
#include "app/OcrCoordinator.h"
#include "app/TranslationCoordinator.h"
#include "config/SettingsManager.h"

namespace {
int failures = 0;
void check(bool ok, const char *label) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
bool until(const std::function<bool()> &ready, int limit = 2000) {
    QElapsedTimer elapsed; elapsed.start();
    while (!ready() && elapsed.elapsed() < limit)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    return ready();
}
void waitMs(int ms) {
    QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec();
}
struct EngineState {
    QSemaphore permit;
    std::atomic<int> calls{0}, active{0}, maximum{0}, lastPixel{-1};
};
class Engine final : public IOcrEngine {
public:
    explicit Engine(std::shared_ptr<EngineState> s) : state(std::move(s)) {}
    QString id() const override { return QStringLiteral("controlled"); }
    OcrResult recognize(const QImage &image, const QString &) override {
        const int value = image.pixelColor(0, 0).red();
        state->lastPixel = value;
        const int active = ++state->active;
        state->maximum = qMax(state->maximum.load(), active);
        ++state->calls;
        state->permit.tryAcquire(1, 2000);
        --state->active;
        OcrResult result;
        if (value == 120) result.error = QStringLiteral("synthetic error");
        else if (value == 90) result.text.clear();
        else if (value == 30) result.text = QStringLiteral(" Hello  world \r\n");
        else if (value == 60 || value == 180) result.text = QStringLiteral("World");
        else result.text = QStringLiteral("Hello world");
        return result;
    }
    std::shared_ptr<EngineState> state;
};
class Translator final : public ITranslator {
public:
    QString id() const override { return QStringLiteral("fake"); }
    void translate(const TranslationRequest &request) override { requests.append(request); }
    void finish(int index) {
        TranslationResult result;
        result.requestId = requests.at(index).requestId;
        result.success = true; result.translatedText = QStringLiteral("translated");
        emit resultReady(result);
    }
    QList<TranslationRequest> requests;
};
struct Fixture {
    SettingsManager settings;
    std::shared_ptr<EngineState> state = std::make_shared<EngineState>();
    OcrCoordinator ocr{[this] { return std::make_unique<Engine>(state); }};
    Translator *backend = nullptr;
    TranslationCoordinator translation{settings, makeBackend()};
    int pixel = 0, captureCalls = 0, completions = 0, originalUpdates = 0, clears = 0;
    bool failCapture = false;
    QString original;
    RealtimePipelineCoordinator pipeline;
    std::unique_ptr<ITranslator> makeBackend() {
        auto value = std::make_unique<Translator>(); backend = value.get(); return value;
    }
    explicit Fixture(int interval = 100000)
        : pipeline(settings, ocr, translation, nullptr, [this](const QRect &rect, const QString &screen) {
            ++captureCalls;
            CaptureResult result;
            result.globalRect = rect;
            result.screenName = screen.isEmpty() ? QStringLiteral("offscreen-fixture") : screen;
            if (failCapture) result.error = QStringLiteral("capture error");
            else { result.image = QImage(160, 50, QImage::Format_RGB32);
                result.image.fill(QColor(pixel, pixel, pixel)); }
            return result;
        }, RealtimeOptions{interval, 2, 4}) {
        QSettings().clear();
        settings.setSourceLanguage(QStringLiteral("en"));
        settings.setTranslator(QStringLiteral("none"));
        QObject::connect(&ocr, &OcrCoordinator::taskFinished, &pipeline,
                         [this] { ++completions; });
        QObject::connect(&pipeline, &RealtimePipelineCoordinator::originalTextReady, &pipeline,
                         [this](const QString &text) { original = text; ++originalUpdates; });
        QObject::connect(&pipeline, &RealtimePipelineCoordinator::subtitlesCleared, &pipeline,
                         [this] { original.clear(); ++clears; });
    }
    ~Fixture() { pipeline.stop(); state->permit.release(10); }
    void region() { settings.setCaptureRegion(QRect(QGuiApplication::primaryScreen()->geometry().topLeft(),
                                  QSize(160, 50)), QGuiApplication::primaryScreen()->name()); }
    void finish() {
        const int expected = completions + 1;
        state->permit.release();
        check(until([&] { return completions >= expected; }), "worker completes without blocking GUI");
    }
    void frame(int value) { pixel = value; pipeline.tick(); }
};

void comparatorTests() {
    FrameComparator compare;
    QImage a(640, 200, QImage::Format_RGB32); a.fill(Qt::black);
    check(!compare.accept({}), "null frame safe");
    check(compare.accept(a), "first frame changed");
    check(!compare.accept(a), "identical unchanged");
    QImage noise = a; noise.setPixelColor(1, 1, QColor(2, 2, 2));
    noise.setPixelColor(2, 1, QColor(1, 1, 1));
    check(!compare.accept(noise), "minor noise unchanged");
    QImage character = a;
    QPainter characterPainter(&character);
    characterPainter.fillRect(400, 80, 8, 12, Qt::white);
    characterPainter.end();
    check(compare.accept(character), "single small character block changed");
    QImage letters = a;
    QPainter painter(&letters); painter.fillRect(100, 80, 40, 20, Qt::white); painter.end();
    check(compare.accept(letters), "small text-like block changed");
    QImage large = a; large.fill(Qt::white);
    check(compare.accept(large), "large block changed");
    check(compare.accept(large.scaled(320, 100)), "size change detected");
    const auto thumb = FrameComparator::thumbnail(a);
    check(thumb.width() <= 160 && thumb.height() <= 90, "bounded comparison dimensions");
    QElapsedTimer time; time.start();
    for (int i = 0; i < 100; ++i) compare.accept(a);
    std::cout << "640x200 frame comparison mean_us=" << time.nsecsElapsed() / 100000 << '\n';
    TextDeduplicator dedup;
    check(dedup.accept(TextDeduplicator::normalize(QStringLiteral("Hello world"))), "first text accepted");
    check(!dedup.accept(TextDeduplicator::normalize(QStringLiteral("Hello world"))), "same text duplicate");
    check(!dedup.accept(TextDeduplicator::normalize(QStringLiteral(" Hello  world \t"))), "spaces duplicate");
    check(dedup.accept(TextDeduplicator::normalize(QStringLiteral("Hello world!"))), "punctuation retained");
    check(TextDeduplicator::normalize(QStringLiteral(" A \r\n B\rC ")) == QStringLiteral("A\nB\nC"),
          "normalized line breaks preserved");
}
} // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("TranslatorPhase6Tests"));
    app.setApplicationName(QStringLiteral("Realtime"));
    QTemporaryDir temp;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
    comparatorTests();
    {
        Fixture f;
        check(!f.pipeline.start() && !f.pipeline.isRunning(), "no region cannot start");
        f.region(); check(f.pipeline.start(), "valid region starts");
        const auto session = f.pipeline.sessionId();
        check(f.captureCalls == 1, "Start captures immediately");
        check(f.pipeline.start() && f.pipeline.sessionId() == session && f.captureCalls == 1,
              "Start idempotent");
        f.finish(); check(f.original == QStringLiteral("Hello world"), "None updates original");
        for (int i = 0; i < 20; ++i) f.pipeline.tick();
        check(f.pipeline.statistics().ocrStarted == 1 && f.pipeline.statistics().unchangedFrames == 20,
              "static frames skip OCR");
        f.pipeline.stop(); f.pipeline.stop();
        const int count = f.captureCalls; f.pipeline.tick();
        check(!f.pipeline.isRunning() && f.captureCalls == count, "Stop idempotent and no capture");
        check(f.pipeline.start() && f.pipeline.sessionId() != session, "restart new session"); f.finish();
    }
    {
        Fixture f; f.region(); f.pipeline.start();
        check(until([&] { return f.state->calls == 1; }), "A starts");
        f.frame(30); f.frame(60); f.frame(150);
        check(f.state->calls == 1 && f.pipeline.pendingCount() == 1, "B/C/D bound to one latest frame");
        f.finish(); check(until([&] { return f.state->calls == 2; }), "pending D starts after A");
        check(f.state->lastPixel == 150 && f.state->maximum == 1, "only A then D, active max one");
        f.finish(); check(f.pipeline.statistics().pendingReplaced == 2, "B/C overwritten");
    }
    {
        Fixture f; f.region(); f.pipeline.start();
        QElapsedTimer stopTime; stopTime.start();
        f.pipeline.stop(); f.pixel = 60; f.pipeline.start();
        check(stopTime.elapsed() < 100, "Stop/restart never waits for blocked OCR worker");
        f.finish(); check(f.originalUpdates == 0 && f.pipeline.statistics().staleOcr == 1,
                          "old session OCR cannot update UI or translate");
        f.finish(); check(f.original == QStringLiteral("World"), "new session OCR accepted");
    }
    {
        Fixture f; f.region(); f.settings.setTranslator(QStringLiteral("deepl")); f.pipeline.start();
        f.finish(); f.frame(30); f.finish(); f.frame(150); f.finish();
        f.frame(60); f.finish(); f.frame(180); f.finish();
        check(f.backend->requests.size() == 2, "Hello/Hello/Hello/World/World translate twice");
        f.pipeline.stop();
        int displayed = 0;
        QObject::connect(&f.translation, &TranslationCoordinator::resultReady, &app,
                         [&](const TranslationResult &) { ++displayed; });
        if (f.backend->requests.size() >= 2) f.backend->finish(1);
        check(displayed == 0, "Stop rejects pending translation");
    }
    {
        Fixture f; f.region(); f.pipeline.start(); f.finish();
        f.frame(90); f.finish(); check(!f.original.isEmpty() && f.clears == 0, "single empty holds text");
        f.pipeline.tick(); f.finish(); check(f.original.isEmpty() && f.clears == 1, "stable empty confirmation clears");
        f.frame(0); f.finish(); f.frame(120); f.finish();
        check(!f.original.isEmpty() && f.clears == 1, "OCR error not empty");
        f.failCapture = true; f.pipeline.tick(); check(f.pipeline.isRunning(), "transient capture failure continues");
        f.pipeline.tick(); f.pipeline.tick(); f.pipeline.tick();
        check(!f.pipeline.isRunning(), "four consecutive capture failures stop");
    }
    {
        Fixture f; f.region(); f.pipeline.start();
        const auto session = f.pipeline.sessionId();
        f.settings.setTargetLanguage(QStringLiteral("ja"));
        check(f.pipeline.sessionId() != session && f.pipeline.isRunning(), "settings rotate session while running");
        f.pipeline.tick(); f.finish(); check(f.originalUpdates == 0, "settings reject old OCR");
        f.finish(); check(f.originalUpdates == 1, "new settings reprocess unchanged frame");
        CaptureResult one; one.globalRect = QRect(0, 0, 160, 50); one.screenName = QStringLiteral("fake");
        one.image = QImage(160, 50, QImage::Format_RGB32); one.image.fill(QColor(60, 60, 60));
        f.pipeline.acceptOneShot(one);
        check(!f.pipeline.isRunning(), "one-shot stops realtime"); f.finish();
        check(f.original == QStringLiteral("World"), "one-shot identity accepted while stopped");
        one.image.fill(QColor(90, 90, 90));
        f.pipeline.acceptOneShot(one); f.finish();
        check(f.original.isEmpty(), "one-shot blank retains prior clear behavior without a timer");
    }
    {
        Fixture f(15); f.region(); f.pipeline.start(); f.finish(); waitMs(70);
        check(f.captureCalls >= 3, "timer produces ticks");
        f.pipeline.stop(); const int count = f.captureCalls; waitMs(50);
        check(count == f.captureCalls, "stopped timer produces no ticks");
    }
    {
        Fixture f; f.region(); f.pipeline.start(); f.finish();
        f.settings.setCaptureRegion(QRect(-99999, -99999, 160, 50), QStringLiteral("missing-screen"));
        f.pipeline.tick();
        check(!f.pipeline.isRunning(), "lost saved screen stops instead of guessing another screen");
    }
    {
        Fixture f; f.region(); f.pipeline.start();
        f.pipeline.stop(); f.finish();
        check(f.originalUpdates == 0 && f.backend->requests.isEmpty(), "Stop rejects OCR without restart");
        CaptureResult shot; shot.globalRect = QRect(0, 0, 160, 50); shot.screenName = QStringLiteral("fixture");
        shot.image = QImage(160, 50, QImage::Format_RGB32); shot.image.fill(Qt::black);
        f.pipeline.acceptOneShot(shot);
        f.pixel = 60; f.pipeline.start(); f.finish();
        check(f.originalUpdates == 0, "Start retires earlier one-shot identity");
        f.finish(); check(f.original == QStringLiteral("World"), "realtime succeeds after one-shot retirement");
    }
    {
        Fixture f; f.region(); f.pipeline.start();
        check(until([&] { return f.state->calls == 1; }), "engine-switch old request is in flight");
        const auto session = f.pipeline.sessionId();
        f.settings.setOcrEngine(QStringLiteral("paddle-small"));
        check(f.pipeline.sessionId() != session, "OCR engine change retires the session");
        f.pipeline.tick(); f.finish();
        check(f.originalUpdates == 0 && f.backend->requests.isEmpty(), "old engine cannot publish or translate");
        f.finish();
        check(f.originalUpdates == 1, "new engine snapshot reprocesses the same frame");
    }
    {
        auto state = std::make_shared<EngineState>();
        OcrCoordinator ocr([state] { return std::make_unique<Engine>(state); });
        CaptureResult capture; capture.image = QImage(160, 50, QImage::Format_RGB32);
        capture.image.fill(Qt::black); ocr.recognize(capture, QStringLiteral("en"));
        for (int value : {30, 60, 150}) {
            capture.image.fill(QColor(value, value, value));
            ocr.recognize(capture, QStringLiteral("en"));
        }
        state->permit.release();
        check(until([&] { return state->calls == 2; }), "legacy recognize API has only one pending job");
        check(state->lastPixel == 150 && state->maximum == 1, "legacy API also runs A then latest D");
        state->permit.release();
        check(until([&] { return !ocr.isBusy(); }), "legacy bounded OCR drains");
    }
    std::cout << "Phase 6 failures=" << failures << '\n';
    return failures ? 1 : 0;
}
