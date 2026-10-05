#include "ocr/PaddleOcrEngine.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QThread>
#include <QtEndian>

namespace {
constexpr quint32 maxReply = 4 * 1024 * 1024;
constexpr qint64 maxPixels = 64 * 1024 * 1024;
QString checkoutRoot(QString start)
{
    QDir dir(start);
    do {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("helpers/ocr/paddle_helper.py"))))
            return dir.absolutePath();
    } while (dir.cdUp());
    return {};
}
bool interrupted() { return QThread::currentThread()->isInterruptionRequested(); }
}

PaddleHelperOptions PaddleHelperOptions::fromEnvironment()
{
    QString root = checkoutRoot(QCoreApplication::applicationDirPath());
    if (root.isEmpty()) root = checkoutRoot(QDir::currentPath());
    PaddleHelperOptions options;
    options.python = qEnvironmentVariable("TRANSLATOR_OCR_PYTHON");
    options.script = qEnvironmentVariable("TRANSLATOR_OCR_HELPER");
    options.models = qEnvironmentVariable("TRANSLATOR_OCR_MODELS");
    options.cache = QDir(root.isEmpty() ? QCoreApplication::applicationDirPath() : root)
                        .filePath(QStringLiteral(".cache/ocr-helper"));
    if (!root.isEmpty()) {
        if (options.python.isEmpty()) options.python = QDir(root).filePath(
            QStringLiteral("benchmarks/ocr_phase61a/.venv/Scripts/python.exe"));
        if (options.script.isEmpty()) options.script = QDir(root).filePath(QStringLiteral("helpers/ocr/paddle_helper.py"));
        if (options.models.isEmpty()) options.models = QDir(root).filePath(QStringLiteral("benchmarks/ocr_phase61a/models"));
    }
    return options;
}

PaddleOcrEngine::PaddleOcrEngine(PaddleHelperOptions options) : options_(std::move(options))
{ clock_.start(); }
PaddleOcrEngine::~PaddleOcrEngine() { stop(); }
QString PaddleOcrEngine::id() const { return QStringLiteral("paddle-small"); }

void PaddleOcrEngine::drainLogs()
{
    if (!process_) return;
    const auto bytes = process_->readAllStandardError();
    if (bytes.isEmpty()) return;
    diagnostic_ = (diagnostic_ + bytes).right(8192);
    // Helper diagnostics never contain image data or recognized text.
    qInfo().noquote() << "[PaddleHelper]" << QString::fromUtf8(bytes.left(8192)).trimmed();
}

void PaddleOcrEngine::stop()
{
    ready_ = false;
    input_.clear();
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(1000);
    }
    process_.reset();
}

void PaddleOcrEngine::fail(const QString &error)
{
    qWarning().noquote() << "[PaddleHelper]" << error.left(500);
    stop();
    failures_ = qMin(failures_ + 1, 6);
    retryAfterMs_ = clock_.elapsed() + qMin(30000, qMax(1, options_.restartBackoffMs) * (1 << (failures_ - 1)));
}

bool PaddleOcrEngine::readReply(QJsonObject &reply, QElapsedTimer &timer, int timeoutMs, QString &error)
{
    while (timer.elapsed() < timeoutMs && !interrupted()) {
        drainLogs();
        input_.append(process_->readAllStandardOutput());
        if (input_.size() > maxReply + 4) { error = QStringLiteral("Helper response exceeds limit."); return false; }
        if (input_.size() >= 4) {
            const quint32 length = qFromBigEndian<quint32>(input_.constData());
            if (!length || length > maxReply) { error = QStringLiteral("Invalid helper frame length."); return false; }
            if (input_.size() >= length + 4) {
                QJsonParseError parse;
                const auto document = QJsonDocument::fromJson(input_.mid(4, length), &parse);
                input_.remove(0, length + 4);
                if (parse.error != QJsonParseError::NoError || !document.isObject()) {
                    error = QStringLiteral("Invalid helper JSON response."); return false;
                }
                reply = document.object();
                return true;
            }
        }
        if (process_->state() == QProcess::NotRunning) {
            error = QStringLiteral("OCR helper exited (code %1). %2")
                .arg(process_->exitCode()).arg(QString::fromUtf8(diagnostic_).right(500));
            return false;
        }
        process_->waitForReadyRead(qMax(1, qMin(50, timeoutMs - int(timer.elapsed()))));
    }
    error = interrupted() ? QStringLiteral("OCR interrupted during shutdown.") : QStringLiteral("OCR helper timed out.");
    return false;
}

bool PaddleOcrEngine::send(const QJsonObject &header, const QByteArray &pixels,
                          QElapsedTimer &timer, int timeoutMs, QString &error)
{
    const QByteArray json = QJsonDocument(header).toJson(QJsonDocument::Compact);
    QByteArray frame(4, Qt::Uninitialized);
    qToBigEndian<quint32>(quint32(json.size()), frame.data());
    frame.append(json);
    if (process_->write(frame) != frame.size() || process_->write(pixels) != pixels.size()) {
        error = QStringLiteral("Cannot write to OCR helper."); return false;
    }
    while (process_->bytesToWrite() && timer.elapsed() < timeoutMs && !interrupted()) {
        drainLogs();
        if (process_->state() == QProcess::NotRunning) break;
        process_->waitForBytesWritten(qMax(1, qMin(50, timeoutMs - int(timer.elapsed()))));
    }
    if (process_->bytesToWrite() || process_->state() == QProcess::NotRunning || interrupted()) {
        error = QStringLiteral("OCR helper write timed out or interrupted."); return false;
    }
    return true;
}

bool PaddleOcrEngine::ensureReady(QString &error)
{
    if (ready_ && process_ && process_->state() == QProcess::Running) return true;
    if (ready_ && process_) {
        drainLogs();
        error = QStringLiteral("OCR helper exited unexpectedly (code %1).").arg(process_->exitCode());
        fail(error);
        return false;
    }
    if (clock_.elapsed() < retryAfterMs_) {
        error = QStringLiteral("OCR helper restarting after failure; select Tesseract to fall back.");
        return false;
    }
    stop();
    if (!QFileInfo(options_.python).isFile() || !QFileInfo(options_.script).isFile()) {
        error = QStringLiteral("Paddle helper runtime missing. Configure TRANSLATOR_OCR_PYTHON / TRANSLATOR_OCR_HELPER, or select Tesseract.");
        fail(error); return false;
    }
    if (!QDir(options_.models).exists()) {
        error = QStringLiteral("Paddle models missing. Configure TRANSLATOR_OCR_MODELS, or select Tesseract.");
        fail(error); return false;
    }
    process_ = std::make_unique<QProcess>();
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    // Do not pass provider credentials or unrelated environment secrets to Python.
    const auto system = QProcessEnvironment::systemEnvironment();
    QProcessEnvironment env;
    for (const auto &key : system.keys()) {
        const auto upper = key.toUpper();
        if (QStringList{QStringLiteral("PATH"), QStringLiteral("SYSTEMROOT"), QStringLiteral("WINDIR"),
            QStringLiteral("TEMP"), QStringLiteral("TMP"), QStringLiteral("USERPROFILE"),
            QStringLiteral("LOCALAPPDATA"), QStringLiteral("APPDATA"), QStringLiteral("NUMBER_OF_PROCESSORS")}.contains(upper))
            env.insert(key, system.value(key));
    }
    env.insert(QStringLiteral("PYTHONNOUSERSITE"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    env.insert(QStringLiteral("HF_HUB_OFFLINE"), QStringLiteral("1"));
    env.insert(QStringLiteral("HF_HUB_DISABLE_TELEMETRY"), QStringLiteral("1"));
    env.insert(QStringLiteral("PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK"), QStringLiteral("True"));
    env.insert(QStringLiteral("PADDLE_PDX_CACHE_HOME"), options_.cache);
    env.insert(QStringLiteral("HF_HOME"), QDir(options_.cache).filePath(QStringLiteral("hf")));
    env.insert(QStringLiteral("MODELSCOPE_CACHE"), QDir(options_.cache).filePath(QStringLiteral("modelscope")));
    env.insert(QStringLiteral("PADDLE_HOME"), QDir(options_.cache).filePath(QStringLiteral("paddle")));
    process_->setProcessEnvironment(env);
    diagnostic_.clear();
    process_->start(options_.python, {QStringLiteral("-u"), options_.script,
        QStringLiteral("--models"), options_.models, QStringLiteral("--cpu-threads"), QStringLiteral("4")});
    QElapsedTimer timer; timer.start();
    while (process_->state() == QProcess::Starting && timer.elapsed() < options_.startupTimeoutMs && !interrupted())
        process_->waitForStarted(50);
    QJsonObject reply;
    if (!readReply(reply, timer, options_.startupTimeoutMs, error)) { fail(error); return false; }
    if (reply.value("type") == QLatin1String("error")) {
        error = QStringLiteral("Paddle startup failed: %1").arg(reply.value("error").toString().left(500));
        fail(error); return false;
    }
    if (reply.value("type") != QLatin1String("ready") || reply.value("protocol").toInt() != 1
        || reply.value("engine") != id() || !reply.value("mkldnn").isBool() || reply.value("mkldnn").toBool()) {
        error = QStringLiteral("Invalid helper readiness or MKL-DNN configuration.");
        fail(error); return false;
    }
    ready_ = true;
    qInfo() << "[PaddleHelper] ready; MKL-DNN disabled; pid=" << process_->processId();
    return true;
}

OcrResult PaddleOcrEngine::recognize(const QImage &image, const QString &sourceLanguage)
{
    OcrResult result;
    result.engineId = id(); result.sourceLanguage = sourceLanguage; result.inputSize = image.size();
    QElapsedTimer total; total.start();
    if (image.isNull() || image.width() > 16384 || image.height() > 16384
        || qint64(image.width()) * image.height() * 3 > maxPixels) {
        result.error = QStringLiteral("Invalid or oversized OCR image."); return result;
    }
    if (sourceLanguage != QLatin1String("auto") && sourceLanguage != QLatin1String("zh")
        && sourceLanguage != QLatin1String("en") && sourceLanguage != QLatin1String("ja")) {
        result.error = QStringLiteral("PP-OCRv6 Small supports auto/zh/en/ja here; select Tesseract for this language.");
        return result;
    }
    if (interrupted()) { result.error = QStringLiteral("OCR interrupted during shutdown."); return result; }
    if (!ensureReady(result.error)) { result.elapsedMs = total.elapsed(); return result; }
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    if (rgb.isNull() || rgb.sizeInBytes() > maxPixels) {
        result.error = QStringLiteral("Cannot convert OCR pixels within limit."); return result;
    }
    const QString token = QString::number(++request_);
    const QJsonObject header{{"type", "recognize"}, {"protocol", 1}, {"request_id", token},
        {"language", sourceLanguage}, {"width", rgb.width()}, {"height", rgb.height()},
        {"stride", rgb.bytesPerLine()}, {"payload_bytes", qint64(rgb.sizeInBytes())}, {"format", "RGB888"}};
    const auto pixels = QByteArray::fromRawData(reinterpret_cast<const char *>(rgb.constBits()), rgb.sizeInBytes());
    QElapsedTimer call; call.start();
    QJsonObject reply;
    if (!send(header, pixels, call, options_.requestTimeoutMs, result.error)
        || !readReply(reply, call, options_.requestTimeoutMs, result.error)) {
        fail(result.error);
    } else if (reply.value("protocol").toInt() != 1 || reply.value("request_id").toString() != token) {
        result.error = QStringLiteral("OCR helper response identity mismatch."); fail(result.error);
    } else if (reply.value("type") == QLatin1String("error")) {
        result.error = QStringLiteral("Paddle inference failed: %1").arg(reply.value("error").toString().left(500));
        fail(result.error);
    } else if (reply.value("type") != QLatin1String("result") || !reply.value("text").isString()
        || !reply.value("scores").isArray() || !reply.value("boxes").isArray()
        || !reply.value("elapsed_ms").isDouble() || reply.value("elapsed_ms").toDouble() < 0) {
        result.error = QStringLiteral("Invalid helper result schema."); fail(result.error);
    } else {
        const auto scores = reply.value("scores").toArray(), boxes = reply.value("boxes").toArray();
        bool valid = scores.size() == boxes.size() && scores.size() <= 10000;
        // Optional protocol-v1 metadata: old helpers remain usable without box texts.
        if (reply.contains("box_texts")) {
            const auto texts = reply.value("box_texts").toArray();
            valid = valid && reply.value("box_texts").isArray() && texts.size() == scores.size();
            for (const auto text : texts) {
                valid = valid && text.isString();
                result.boxTexts.append(text.toString());
            }
        }
        for (const auto score : scores) {
            valid = valid && score.isDouble() && score.toDouble() >= 0 && score.toDouble() <= 1;
            result.confidences.append(score.toDouble());
        }
        for (const auto box : boxes) {
            QPolygonF polygon;
            valid = valid && box.isArray() && box.toArray().size() == 4;
            for (const auto point : box.toArray()) {
                const auto xy = point.toArray();
                valid = valid && point.isArray() && xy.size() == 2 && xy.at(0).isDouble() && xy.at(1).isDouble();
                if (xy.size() == 2) polygon.append(QPointF(xy.at(0).toDouble(), xy.at(1).toDouble()));
            }
            result.boxes.append(polygon);
        }
        if (!valid) { result.error = QStringLiteral("Invalid helper boxes/confidences."); fail(result.error); }
        else {
            result.text = reply.value("text").toString();
            result.recognitionMs = qint64(reply.value("elapsed_ms").toDouble());
            result.preprocessingMode = QStringLiteral("paddle-native");
            result.boxCount = scores.size(); result.helperPid = process_->processId();
            result.helperRequestId = token;
            result.detectionScoreStatus = reply.value("detection_score_status").toString(
                QStringLiteral("not exposed by current result path"));
#ifndef NDEBUG
            qDebug() << "[PaddleMetadata] request=" << token << "rawBoxes=" << result.boxCount
                     << "region=" << image.size() << "detectionScore=" << result.detectionScoreStatus;
#endif
            failures_ = 0; retryAfterMs_ = 0;
        }
    }
    result.elapsedMs = total.elapsed();
    return result;
}
