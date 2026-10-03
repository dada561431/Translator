#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkReply>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <cstring>
#include <iostream>
#include <memory>

#include "app/TranslationCoordinator.h"
#include "config/SettingsManager.h"
#include "gui/SettingsDialog.h"
#include "gui/TranslationWindow.h"
#include "translator/DeepLTranslator.h"
#include "translator/TranslationLanguageMapper.h"

namespace {
int failures = 0;
void check(bool value, const char *message)
{
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

class FakeTranslator final : public ITranslator
{
public:
    QString id() const override { return QStringLiteral("test_only"); }
    void translate(const TranslationRequest &request) override { requests.append(request); }
    void complete(int index, const QString &text, const QString &error = {})
    {
        const TranslationRequest request = requests.at(index);
        TranslationResult result;
        result.requestId = request.requestId;
        result.sourceText = request.sourceText;
        result.sourceLanguage = request.sourceLanguage;
        result.targetLanguage = request.targetLanguage;
        result.translatedText = text;
        result.success = error.isEmpty();
        result.error = error;
        result.provider = id();
        emit resultReady(result);
    }
    QList<TranslationRequest> requests;
};

struct Response
{
    QByteArray body = R"({"translations":[{"text":"translated sample"}]})";
    int status = 200;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    int delayMs = 5;
    bool hang = false;
};

class MemoryReply final : public QNetworkReply
{
public:
    MemoryReply(const QNetworkRequest &request, const Response &response, QObject *parent)
        : QNetworkReply(parent), response_(response)
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        if (!response.hang) QTimer::singleShot(response.delayMs, this, [this] {
            if (isFinished()) return;
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response_.status);
            if (response_.error != NoError) setError(response_.error, QStringLiteral("test transport error"));
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }
    void abort() override
    {
        if (isFinished()) return;
        setError(OperationCanceledError, QStringLiteral("aborted"));
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override
    {
        return response_.body.size() - offset_ + QNetworkReply::bytesAvailable();
    }
protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 count = qMin(maxSize, qint64(response_.body.size()) - offset_);
        if (count <= 0) return -1;
        std::memcpy(data, response_.body.constData() + offset_, size_t(count));
        offset_ += count;
        return count;
    }
private:
    Response response_;
    qint64 offset_ = 0;
};

class MemoryNetwork final : public QNetworkAccessManager
{
public:
    Response response;
    int calls = 0;
    QNetworkRequest sentRequest;
    QByteArray sentBody;
    Operation sentOperation = UnknownOperation;
protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                QIODevice *outgoingData) override
    {
        ++calls;
        sentRequest = request;
        sentOperation = operation;
        sentBody = outgoingData ? outgoingData->readAll() : QByteArray();
        return new MemoryReply(request, response, this);
    }
};

TranslationResult awaitResult(DeepLTranslator &translator, const TranslationRequest &request)
{
    TranslationResult result;
    bool received = false;
    QEventLoop loop;
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    const auto connection = QObject::connect(&translator, &ITranslator::resultReady, &loop,
        [&](const TranslationResult &value) { result = value; received = true; loop.quit(); });
    translator.translate(request);
    check(!received, "backend returns immediately; result delivery is asynchronous");
    guard.start(1500);
    loop.exec();
    QObject::disconnect(connection);
    check(received, "backend emits a bounded result");
    return result;
}

void coordinatorChecks(QApplication &application)
{
    SettingsManager settings;
    settings.setSourceLanguage(QStringLiteral("en"));
    settings.setTargetLanguage(QStringLiteral("zh"));
    settings.setTranslator(QStringLiteral("deepl"));
    auto fake = std::make_unique<FakeTranslator>();
    FakeTranslator *backend = fake.get();
    TranslationCoordinator coordinator(settings, std::move(fake));
    TranslationWindow window(settings);
    QObject::connect(&coordinator, &TranslationCoordinator::stateChanged,
                     &window, &TranslationWindow::setTranslationState);
    QList<TranslationResult> forwarded;
    QObject::connect(&coordinator, &TranslationCoordinator::resultReady, &window,
        [&](const TranslationResult &result) {
            forwarded.append(result);
            if (result.success) window.setTranslatedText(result.translatedText);
        });
    auto *translated = window.findChild<QLabel *>(QStringLiteral("translatedLabel"));
    auto *original = window.findChild<QLabel *>(QStringLiteral("originalLabel"));
    check(translated && original, "subtitle UI exists");
    const QString placeholder = translated->text();
    auto submit = [&](const QString &text) {
        OcrResult ocr;
        ocr.text = text;
        ocr.sourceLanguage = QStringLiteral("en");
        window.setOriginalText(text);
        coordinator.acceptOcr(ocr);
    };
    submit(QStringLiteral(" \n\t"));
    OcrResult failed;
    failed.text = QStringLiteral("ignored");
    failed.error = QStringLiteral("OCR failed");
    coordinator.acceptOcr(failed);
    check(backend->requests.isEmpty(), "blank/error OCR never invokes translator");
    submit(QStringLiteral("Hello"));
    submit(QStringLiteral("Good morning"));
    check(backend->requests.size() == 2 && backend->requests[1].requestId > backend->requests[0].requestId,
          "requests carry increasing IDs");
    check(backend->requests[1].sourceText == QStringLiteral("Good morning")
          && backend->requests[1].sourceLanguage == QStringLiteral("en")
          && backend->requests[1].targetLanguage == QStringLiteral("zh"), "request context preserved");
    backend->complete(1, QStringLiteral("早上好"));
    backend->complete(0, QStringLiteral("你好"));
    backend->complete(1, QStringLiteral("duplicate result"));
    check(forwarded.size() == 1 && translated->text() == QStringLiteral("早上好"),
          "B then A: only B reaches UI; stale A ignored");
    submit(QStringLiteral("new original"));
    check(translated->text() != QStringLiteral("早上好") && original->text() == QStringLiteral("new original"),
          "new OCR immediately invalidates old translated subtitle");
    backend->complete(2, {}, QStringLiteral("test backend failure"));
    check(forwarded.size() == 2 && !forwarded.last().success
          && translated->text() == QStringLiteral("翻译失败")
          && original->text() == QStringLiteral("new original"), "error retains original and shows light status");
    submit(QStringLiteral("soon blank"));
    submit(QString());
    backend->complete(3, QStringLiteral("must not display"));
    check(forwarded.size() == 2 && translated->text() == placeholder,
          "empty OCR invalidates an in-flight translation");
    submit(QStringLiteral("switch provider"));
    settings.setTranslator(QStringLiteral("none"));
    backend->complete(4, QStringLiteral("must not display"));
    submit(QStringLiteral("none original"));
    check(backend->requests.size() == 5 && forwarded.size() == 2
          && translated->text() == placeholder, "none disables calls and rejects previous replies");
    settings.setTranslator(QStringLiteral("deepl"));
    submit(QStringLiteral("settings changed"));
    settings.setTargetLanguage(QStringLiteral("ja"));
    backend->complete(5, QStringLiteral("old target"));
    check(forwarded.size() == 2, "language change invalidates active translation");
    settings.setTargetLanguage(QStringLiteral("en"));
    submit(QStringLiteral("same language"));
    check(backend->requests.size() == 6 && forwarded.last().success
          && forwarded.last().translatedText == QStringLiteral("same language"),
          "same language returns locally with request ID and no backend call");
    settings.setSourceLanguage(QStringLiteral("ja"));
    submit(QStringLiteral("old English OCR"));
    check(backend->requests.size() == 6, "OCR from previous source language is rejected");

    SettingsDialog dialog(settings);
    auto *combo = dialog.findChild<QComboBox *>(QStringLiteral("translatorCombo"));
    check(combo && combo->findData(QStringLiteral("deepl")) >= 0
          && combo->findData(QStringLiteral("none")) >= 0, "settings expose only none/deepl real providers");
    combo->setCurrentIndex(combo->findData(QStringLiteral("none")));
    SettingsManager reloaded;
    check(reloaded.translator() == QStringLiteral("none"), "provider setting persists");
    combo->setCurrentIndex(combo->findData(QStringLiteral("deepl")));
    SettingsManager deeplReloaded;
    check(deeplReloaded.translator() == QStringLiteral("deepl"), "DeepL provider setting persists");
    window.show();
    application.processEvents();
    check(window.isVisible() && window.testAttribute(Qt::WA_TranslucentBackground),
          "UI remains transparent and responsive");
    check(translated->font().pointSizeF() > original->font().pointSizeF()
          && translated->y() < original->y(), "translation remains above original with larger font");
    check(translated->textFormat() == Qt::PlainText && original->textFormat() == Qt::PlainText,
          "provider/OCR text is displayed literally");
    if (application.arguments().size() > 1) window.grab().save(application.arguments()[1]);
    window.close();
}

void backendChecks()
{
    TranslationRequest request{42, QStringLiteral("Hello world"), QStringLiteral("en"), QStringLiteral("zh")};
    MemoryNetwork network;
    DeepLConfiguration config;
    config.apiKey.clear();
    DeepLTranslator missing(config, &network);
    const auto missingResult = awaitResult(missing, request);
    check(!missingResult.success && missingResult.error == QStringLiteral("DeepL API key is not configured.")
          && network.calls == 0, "missing API key fails clearly without network");
    SettingsManager settings;
    settings.setSourceLanguage(QStringLiteral("en"));
    settings.setTargetLanguage(QStringLiteral("zh"));
    settings.setTranslator(QStringLiteral("deepl"));
    TranslationWindow window(settings);
    TranslationCoordinator missingCoordinator(settings, std::make_unique<DeepLTranslator>(config, &network));
    QObject::connect(&missingCoordinator, &TranslationCoordinator::stateChanged,
                     &window, &TranslationWindow::setTranslationState);
    OcrResult ocr;
    ocr.text = request.sourceText;
    window.setOriginalText(ocr.text);
    missingCoordinator.acceptOcr(ocr);
    QCoreApplication::processEvents();
    check(window.findChild<QLabel *>(QStringLiteral("translatedLabel"))->text() == QStringLiteral("翻译失败")
          && window.findChild<QLabel *>(QStringLiteral("originalLabel"))->text() == ocr.text
          && network.calls == 0, "missing key flows to UI without losing original or sending network");
    config.apiKey = QStringLiteral("test-only-placeholder");
    config.timeoutMs = 100;
    DeepLTranslator translator(config, &network);
    auto result = awaitResult(translator, request);
    check(result.success && result.requestId == 42 && result.sourceText == request.sourceText,
          "successful response retains source and request identity");
    check(network.sentOperation == QNetworkAccessManager::PostOperation
          && network.sentRequest.url() == config.endpoint
          && network.sentRequest.rawHeader("Authorization") == QByteArrayLiteral("DeepL-Auth-Key test-only-placeholder"),
          "official HTTPS POST and auth scheme used");
    check(network.sentRequest.transferTimeout() == 100
          && network.sentRequest.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt()
                 == QNetworkRequest::ManualRedirectPolicy, "timeout and redirect policy are explicit");
    auto body = QJsonDocument::fromJson(network.sentBody).object();
    check(body.value(QStringLiteral("source_lang")) == QStringLiteral("EN")
          && body.value(QStringLiteral("target_lang")) == QStringLiteral("ZH-HANS")
          && body.value(QStringLiteral("text")).toArray().first() == request.sourceText
          && body.size() == 3, "JSON sends only text and mapped languages; no image/key");
    request.sourceLanguage = QStringLiteral("auto");
    awaitResult(translator, request);
    check(!QJsonDocument::fromJson(network.sentBody).object().contains(QStringLiteral("source_lang")),
          "translation auto omits source_lang for provider detection");
    const int calls = network.calls;
    request.sourceLanguage = request.targetLanguage;
    check(awaitResult(translator, request).translatedText == request.sourceText && network.calls == calls,
          "direct backend same-language shortcut never sends network");
    request.sourceLanguage = QStringLiteral("en");
    request.sourceText = QStringLiteral(" \n\t ");
    check(!awaitResult(translator, request).success && network.calls == calls, "empty backend input never sends network");
    request.sourceText = QStringLiteral("Hello world");
    request.targetLanguage.clear();
    check(!awaitResult(translator, request).success && network.calls == calls, "empty target rejected locally");
    request.targetLanguage = QStringLiteral("zh");

    for (const QByteArray &invalid : {QByteArrayLiteral("not JSON"), QByteArrayLiteral("[]"),
         QByteArrayLiteral("{}"), QByteArrayLiteral("{\"translations\":[]}"),
         QByteArrayLiteral("{\"translations\":[{}]}"),
         QByteArrayLiteral("{\"translations\":[{\"text\":\"  \"}]}"),
         QByteArrayLiteral("{\"message\":\"provider error\"}")}) {
        check(!DeepLTranslator::parseResponse(request, invalid, 200).success, "invalid/missing/empty JSON fails safely");
    }
    for (int status : {400, 401, 403, 429, 456, 500, 503, 302}) {
        network.response.status = status;
        network.response.error = QNetworkReply::ContentAccessDenied;
        result = awaitResult(translator, request);
        check(!result.success && !result.error.isEmpty() && result.httpStatus == status,
              "HTTP/provider errors are preserved as bounded failures");
    }
    network.response.status = 0;
    for (auto error : {QNetworkReply::HostNotFoundError, QNetworkReply::ConnectionRefusedError,
                      QNetworkReply::SslHandshakeFailedError}) {
        network.response.error = error;
        check(!awaitResult(translator, request).success, "DNS/connection/TLS errors do not crash");
    }
    network.response.error = QNetworkReply::NoError;
    network.response.hang = true;
    bool uiPulse = false;
    QTimer::singleShot(10, &window, [&] {
        window.setOriginalText(QStringLiteral("UI responds while network is pending"));
        uiPulse = true;
    });
    result = awaitResult(translator, request);
    check(uiPulse, "GUI event loop processes UI updates during pending network");
    check(!result.success && result.error.contains(QStringLiteral("timed out"))
          && result.elapsedMs >= 70 && result.elapsedMs < 1000, "hanging reply is aborted by deadline");
    config.endpoint = QUrl(QStringLiteral("http://api-free.deepl.com/v2/translate"));
    DeepLTranslator insecure(config, &network);
    const int lastCalls = network.calls;
    check(!awaitResult(insecure, request).success && network.calls == lastCalls,
          "non-HTTPS endpoint rejected without sending credentials");
    config.endpoint = QUrl(QStringLiteral("https://example.com/v2/translate"));
    DeepLTranslator unknown(config, &network);
    check(!awaitResult(unknown, request).success && network.calls == lastCalls,
          "unknown provider host rejected");

    for (const QString &language : {QStringLiteral("en"), QStringLiteral("zh"),
                                   QStringLiteral("ja"), QStringLiteral("ko")}) {
        check(!TranslationLanguageMapper::sourceCode(language).isEmpty()
              && !TranslationLanguageMapper::targetCode(language).isEmpty(), "language mappings cover all app languages");
    }
    check(TranslationLanguageMapper::sourceCode(QStringLiteral("zh")) == QStringLiteral("ZH")
          && TranslationLanguageMapper::targetCode(QStringLiteral("zh")) == QStringLiteral("ZH-HANS")
          && TranslationLanguageMapper::isSupportedSource(QStringLiteral("auto"))
          && TranslationLanguageMapper::sourceCode(QStringLiteral("auto")).isEmpty()
          && TranslationLanguageMapper::targetCode(QStringLiteral("auto")).isEmpty(), "auto/Chinese mapping is explicit");
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("TranslatorPhase5Tests"));
    application.setApplicationName(QStringLiteral("TranslationTest"));
    QTemporaryDir directory;
    check(directory.isValid(), "isolated settings directory exists");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    coordinatorChecks(application);
    backendChecks();
    if (!failures) std::cout << "All Phase 5 translation checks passed (offline transport).\n";
    return failures ? 1 : 0;
}
