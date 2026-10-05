#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QtEndian>
#include <cstdio>
#include <iostream>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#include <qt_windows.h>
#endif
#include "app/OcrCoordinator.h"
#include "config/SettingsManager.h"
#include "gui/SettingsDialog.h"
#include "ocr/OcrEngineFactory.h"

namespace {
int failures = 0;
void check(bool condition, const char *message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
bool processAlive(qint64 pid) {
#ifdef Q_OS_WIN
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!handle) return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(handle, &code) && code == STILL_ACTIVE;
    CloseHandle(handle);
    return alive;
#else
    Q_UNUSED(pid);
    return false;
#endif
}
QByteArray exact(QFile &stream, qint64 count) {
    QByteArray bytes;
    while (bytes.size() < count) {
        auto block = stream.read(count - bytes.size());
        if (block.isEmpty()) return {};
        bytes.append(block);
    }
    return bytes;
}
void frame(QFile &stream, const QJsonObject &object, bool fragment = false) {
    const auto json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QByteArray bytes(4, Qt::Uninitialized);
    qToBigEndian<quint32>(json.size(), bytes.data()); bytes.append(json);
    if (fragment) {
        for (const char byte : bytes) { stream.write(&byte, 1); stream.flush(); }
    } else { stream.write(bytes); stream.flush(); }
}
int fakeHelper(const QString &mode) {
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#endif
    QFile input, output;
    if (!input.open(stdin, QIODevice::ReadOnly | QIODevice::Unbuffered)
        || !output.open(stdout, QIODevice::WriteOnly | QIODevice::Unbuffered)) return 18;
    if (mode == QLatin1String("hang-ready")) { QThread::sleep(20); return 0; }
    if (mode == QLatin1String("oversize")) {
        QByteArray bytes(4, Qt::Uninitialized); qToBigEndian<quint32>(8 * 1024 * 1024, bytes.data());
        output.write(bytes); output.flush(); QThread::sleep(1); return 0;
    }
    if (mode == QLatin1String("malformed")) {
        output.write(QByteArray::fromHex("000000017b")); output.flush(); return 0;
    }
    if (mode == QLatin1String("startup-error")) {
        frame(output, {{"type", "error"}, {"error", "missing model"}}); return 1;
    }
    frame(output, {{"type", "ready"}, {"protocol", 1}, {"engine", "paddle-small"},
        {"mkldnn", mode == QLatin1String("mkldnn")}}, true);
    int calls = 0;
    while (true) {
        const auto prefix = exact(input, 4);
        if (prefix.isEmpty()) return 0;
        const auto header = QJsonDocument::fromJson(exact(input, qFromBigEndian<quint32>(prefix.constData()))).object();
        const auto pixels = exact(input, header.value("payload_bytes").toInteger());
        if (mode == QLatin1String("crash")) return 17;
        if (mode == QLatin1String("hang-infer")) { QThread::sleep(20); return 0; }
        if (mode == QLatin1String("infer-error")) {
            frame(output, {{"protocol", 1}, {"type", "error"}, {"request_id", header.value("request_id")},
                {"error", "inference failed"}}); return 1;
        }
        const bool empty = mode == QLatin1String("empty");
        QJsonArray scores, boxes;
        if (!empty) {
            scores.append(mode == QLatin1String("bad-score") ? 2.0 : .9);
            boxes.append(QJsonArray{QJsonArray{0, 0}, QJsonArray{5, 0}, QJsonArray{5, 2}, QJsonArray{0, 2}});
        }
        const QString text = empty ? QString() : QStringLiteral("%1:%2,%3,%4:%5")
            .arg(++calls).arg(quint8(pixels.at(0))).arg(quint8(pixels.at(1))).arg(quint8(pixels.at(2)))
            .arg(header.value("stride").toInt());
        QJsonObject response{{"protocol", 1}, {"type", "result"}, {"request_id", mode == QLatin1String("mismatch")
            ? QJsonValue("wrong") : header.value("request_id")}, {"text", text},
            {"scores", scores}, {"boxes", boxes}, {"elapsed_ms", 7}};
        if (mode == QLatin1String("metadata")) response.insert("box_texts", QJsonArray{text});
        if (mode == QLatin1String("bad-text-count")) response.insert("box_texts", QJsonArray{});
        if (mode == QLatin1String("bad-text-type")) response.insert("box_texts", QJsonArray{42});
        frame(output, response, true);
    }
}
PaddleHelperOptions options(const QString &root, const QString &mode) {
    PaddleHelperOptions value;
    value.python = QCoreApplication::applicationFilePath();
    value.script = root + "/" + mode + ".py";
    value.models = root; value.cache = root;
    value.startupTimeoutMs = 2000; value.requestTimeoutMs = 150; value.restartBackoffMs = 5000;
    QFile file(value.script);
    if (!file.open(QIODevice::WriteOnly)) return value;
    file.write("fixture");
    return value;
}
bool until(const std::function<bool()> &condition, int timeout = 3000) {
    QElapsedTimer clock; clock.start();
    while (!condition() && clock.elapsed() < timeout) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    return condition();
}
}

int main(int argc, char **argv) {
    if (argc > 2 && QByteArray(argv[1]) == "-u") return fakeHelper(QFileInfo(QString::fromLocal8Bit(argv[2])).baseName());
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("TranslatorPhase61Tests")); app.setApplicationName(QStringLiteral("Paddle"));
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    SettingsManager settings;
    settings.setOcrEngine(QStringLiteral("paddle-small"));
    check(SettingsManager().ocrEngine() == QStringLiteral("paddle-small"), "Paddle engine persists");
    SettingsDialog dialog(settings);
    auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("ocrEngineCombo"));
    check(combo && combo->findData("paddle-small") >= 0 && combo->findData("tesseract") >= 0,
          "Settings offers both engines");
    settings.setOcrEngine(QStringLiteral("unknown"));
    check(settings.ocrEngine() == QStringLiteral("tesseract"), "invalid engine falls back to Tesseract");
    check(OcrEngineFactory::create("tesseract")->id() == "tesseract", "factory retains Tesseract");
    check(!OcrEngineFactory::create("unknown"), "factory rejects unknown engine");
    QImage image(5, 2, QImage::Format_RGB32); image.fill(QColor(10, 20, 30));
    qint64 oldPid = 0;
    {
        PaddleOcrEngine engine(options(dir.path(), "valid"));
        const auto first = engine.recognize(image, "zh"), second = engine.recognize(image, "auto");
        if (!first.isValid()) std::cerr << "First helper error: " << first.error.toStdString() << '\n';
        check(first.isValid() && first.text == "1:10,20,30:16", "RGB bytes and padded stride transmitted exactly");
        check(second.isValid() && second.text == "2:10,20,30:16", "persistent process keeps state");
        check(first.helperPid > 0 && second.helperPid == first.helperPid, "helper PID remains identical");
        check(first.boxCount == 1 && first.confidences.size() == 1 && first.boxes.size() == 1,
              "box and confidence metadata survives IPC");
        check(first.boxTexts.isEmpty(), "older helper without optional box texts stays compatible");
        check(!engine.recognize({}, "zh").isValid(), "null input rejected");
        check(!engine.recognize(image, "ko").isValid(), "unsupported language explicit");
        oldPid = first.helperPid;
    }
    check(oldPid > 0 && !processAlive(oldPid), "engine destructor leaves no resident child behind");
    {
        PaddleOcrEngine engine(options(dir.path(), "metadata"));
        const auto value = engine.recognize(image, "zh");
        check(value.isValid() && value.boxTexts == QStringList{value.text}
              && !value.helperRequestId.isEmpty(), "per-box texts and request identity survive IPC");
        check(value.detectionScoreStatus == "not exposed by current result path",
              "missing detection confidence is explicit, not invented");
    }
    {
        auto config = options(dir.path(), "valid"); config.restartBackoffMs = 30;
        PaddleOcrEngine engine(config);
        auto before = engine.recognize(image, "zh");
        QProcess killer;
#ifdef Q_OS_WIN
        killer.start("taskkill", {"/PID", QString::number(before.helperPid), "/F"});
        check(killer.waitForFinished(2000) && killer.exitCode() == 0, "fixture child can be terminated externally");
#endif
        check(!engine.recognize(image, "zh").isValid(), "external crash returns error, never stale text");
        QThread::msleep(80);
        const auto after = engine.recognize(image, "zh");
        check(after.isValid() && after.helperPid != before.helperPid && after.text.startsWith("1:"),
              "after backoff a fresh child recovers without parallel helpers");
    }
    for (const auto &mode : {"mismatch", "bad-score", "bad-text-count", "bad-text-type", "crash", "infer-error", "mkldnn", "oversize", "malformed", "startup-error"}) {
        PaddleOcrEngine engine(options(dir.path(), mode));
        const auto result = engine.recognize(image, "zh");
        check(!result.isValid() && !result.error.isEmpty(), mode);
        check(!engine.recognize(image, "zh").isValid(), "failure retry is backed off, not fabricated success");
    }
    {
        PaddleOcrEngine engine(options(dir.path(), "empty"));
        auto result = engine.recognize(image, "en");
        check(result.isValid() && result.text.isEmpty(), "empty is distinct from helper failure");
    }
    for (const auto &mode : {"hang-ready", "hang-infer"}) {
        auto config = options(dir.path(), mode); config.startupTimeoutMs = 150;
        PaddleOcrEngine engine(config); QElapsedTimer time; time.start();
        check(!engine.recognize(image, "zh").isValid(), "timeout produces explicit error");
        check(time.elapsed() < 1500, "hung helper killed within bounded deadline");
    }
    {
        auto config = options(dir.path(), "valid"); config.python = dir.path() + "/missing.exe";
        PaddleOcrEngine engine(config);
        check(!engine.recognize(image, "zh").isValid(), "missing runtime explicit");
    }
    {
        const auto config = options(dir.path(), "valid");
        int received = 0; QList<OcrResult> values;
        OcrCoordinator coordinator({}, nullptr, [config](const QString &id) { return OcrEngineFactory::create(id, config); });
        QObject::connect(&coordinator, &OcrCoordinator::taskFinished, &app, [&](quint64, const OcrResult &result) {
            ++received; values.append(result);
        });
        CaptureResult capture; capture.image = image;
        coordinator.tryRecognize(capture, "zh", "paddle-small");
        check(until([&] { return received == 1; }), "background worker completes Paddle request");
        coordinator.tryRecognize(capture, "zh", "tesseract");
        check(until([&] { return received == 2; }), "switch to Tesseract completes");
        coordinator.tryRecognize(capture, "zh", "paddle-small");
        check(until([&] { return received == 3; }), "switch back to Paddle completes");
        check(values.size() == 3 && values[0].isValid() && values[0].engineId == "paddle-small"
            && values[1].engineId == "tesseract" && values[2].engineId == "paddle-small"
            && values[2].text.startsWith("1:"), "worker replaces engine on captured ID, not GUI-thread settings read");
        if (values.size() == 3) check(values[2].helperPid != values[0].helperPid && values[0].helperPid != oldPid,
                                      "switch discards old resident process");
    }
    {
        auto config = options(dir.path(), "hang-infer"); config.requestTimeoutMs = 20000;
        auto coordinator = std::make_unique<OcrCoordinator>(OcrCoordinator::EngineFactory{}, nullptr,
            [config](const QString &) { return std::make_unique<PaddleOcrEngine>(config); });
        CaptureResult capture; capture.image = image;
        coordinator->tryRecognize(capture, "zh", "paddle-small");
        QElapsedTimer wait; wait.start();
        while (wait.elapsed() < 100) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QElapsedTimer close; close.start(); coordinator.reset();
        check(close.elapsed() < 1500, "Close interrupts a blocked helper without waiting 20 seconds");
    }
    if (!failures) std::cout << "Phase 6.1B helper contract checks passed\n";
    return failures ? 1 : 0;
}
