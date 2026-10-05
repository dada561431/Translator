#pragma once
#include "ocr/IOcrEngine.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QProcess>
#include <memory>

struct PaddleHelperOptions
{
    QString python, script, models, cache;
    int startupTimeoutMs = 30000;
    int requestTimeoutMs = 10000;
    int restartBackoffMs = 1000;
    static PaddleHelperOptions fromEnvironment();
};

class PaddleOcrEngine final : public IOcrEngine
{
public:
    explicit PaddleOcrEngine(PaddleHelperOptions options = PaddleHelperOptions::fromEnvironment());
    ~PaddleOcrEngine() override;
    QString id() const override;
    OcrResult recognize(const QImage &image, const QString &sourceLanguage) override;

private:
    bool ensureReady(QString &error);
    bool readReply(QJsonObject &reply, QElapsedTimer &timer, int timeoutMs, QString &error);
    bool send(const QJsonObject &header, const QByteArray &pixels,
              QElapsedTimer &timer, int timeoutMs, QString &error);
    void stop();
    void fail(const QString &error);
    void drainLogs();
    PaddleHelperOptions options_;
    std::unique_ptr<QProcess> process_;
    QByteArray input_, diagnostic_;
    QElapsedTimer clock_;
    qint64 retryAfterMs_ = 0;
    quint64 request_ = 0;
    int failures_ = 0;
    bool ready_ = false;
};
