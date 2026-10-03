#include "translator/DeepLTranslator.h"
#include "translator/TranslationLanguageMapper.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTimer>

namespace {

TranslationResult makeResult(const TranslationRequest &request)
{
    TranslationResult result;
    result.requestId = request.requestId;
    result.sourceText = request.sourceText;
    result.sourceLanguage = request.sourceLanguage;
    result.targetLanguage = request.targetLanguage;
    result.provider = QStringLiteral("deepl");
    return result;
}

bool isOfficialEndpoint(const QUrl &url)
{
    return url.isValid() && url.scheme() == QLatin1String("https")
        && (url.host() == QLatin1String("api-free.deepl.com")
            || url.host() == QLatin1String("api.deepl.com"))
        && url.path() == QLatin1String("/v2/translate") && url.port(443) == 443
        && url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment();
}

} // namespace

DeepLConfiguration DeepLConfiguration::fromEnvironment()
{
    DeepLConfiguration configuration;
    configuration.apiKey = qEnvironmentVariable("DEEPL_API_KEY").trimmed();
    const QString url = qEnvironmentVariable("DEEPL_API_URL").trimmed();
    if (!url.isEmpty()) configuration.endpoint = QUrl(url);
    return configuration;
}

DeepLTranslator::DeepLTranslator(QObject *parent)
    : DeepLTranslator(DeepLConfiguration::fromEnvironment(), nullptr, parent)
{
}

DeepLTranslator::DeepLTranslator(const DeepLConfiguration &configuration,
                               QNetworkAccessManager *manager, QObject *parent)
    : ITranslator(parent), configuration_(configuration),
      manager_(manager ? manager : new QNetworkAccessManager(this))
{
}

QString DeepLTranslator::id() const { return QStringLiteral("deepl"); }

void DeepLTranslator::postResult(const TranslationResult &result)
{
    QTimer::singleShot(0, this, [this, result] { emit resultReady(result); });
}

void DeepLTranslator::translate(const TranslationRequest &request)
{
    TranslationResult result = makeResult(request);
    const QString sourceCode = TranslationLanguageMapper::sourceCode(request.sourceLanguage);
    const QString targetCode = TranslationLanguageMapper::targetCode(request.targetLanguage);
    if (request.sourceText.trimmed().isEmpty()) {
        result.error = QStringLiteral("Translation text is empty.");
    } else if (!TranslationLanguageMapper::isSupportedSource(request.sourceLanguage)
               || targetCode.isEmpty()) {
        result.error = QStringLiteral("Unsupported translation language.");
    } else if (request.sourceLanguage == request.targetLanguage) {
        result.success = true;
        result.translatedText = request.sourceText;
    } else if (configuration_.apiKey.trimmed().isEmpty()) {
        result.error = QStringLiteral("DeepL API key is not configured.");
    } else if (!isOfficialEndpoint(configuration_.endpoint)) {
        result.error = QStringLiteral("DeepL endpoint must be an official HTTPS /v2/translate URL.");
    } else if (configuration_.apiKey.contains(QLatin1Char('\r'))
               || configuration_.apiKey.contains(QLatin1Char('\n'))) {
        result.error = QStringLiteral("DeepL API key configuration is invalid.");
    }
    if (result.success || !result.error.isEmpty()) {
        postResult(result);
        return;
    }

    QJsonObject body{{QStringLiteral("text"), QJsonArray{request.sourceText}},
                     {QStringLiteral("target_lang"), targetCode}};
    if (!sourceCode.isEmpty()) body.insert(QStringLiteral("source_lang"), sourceCode);
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    if (payload.size() > 128 * 1024) {
        result.error = QStringLiteral("DeepL request exceeds the 128 KiB limit.");
        postResult(result);
        return;
    }

    const int timeoutMs = qMax(1, configuration_.timeoutMs);
    QNetworkRequest networkRequest(configuration_.endpoint);
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    networkRequest.setRawHeader("Authorization", "DeepL-Auth-Key " + configuration_.apiKey.toUtf8());
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                QNetworkRequest::ManualRedirectPolicy);
    networkRequest.setTransferTimeout(timeoutMs);
    QElapsedTimer elapsed;
    elapsed.start();
    QNetworkReply *reply = manager_->post(networkRequest, payload);
    auto *deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, [reply] {
        reply->setProperty("translationTimedOut", true);
        reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, deadline, request, elapsed] {
        deadline->stop();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        TranslationResult result = makeResult(request);
        result.httpStatus = status;
        if (reply->property("translationTimedOut").toBool()
            || reply->error() == QNetworkReply::TimeoutError) {
            result.error = QStringLiteral("DeepL translation timed out.");
        } else if (status >= 300) {
            result = parseResponse(request, reply->readAll(), status);
        } else if (reply->error() != QNetworkReply::NoError) {
            // Do not log raw transport strings or response bodies containing echoed credentials/text.
            result.error = reply->error() == QNetworkReply::SslHandshakeFailedError
                ? QStringLiteral("DeepL TLS verification or handshake failed.")
                : QStringLiteral("DeepL network request failed (code %1).")
                      .arg(int(reply->error()));
        } else {
            result = parseResponse(request, reply->readAll(), status);
        }
        result.elapsedMs = elapsed.elapsed();
        reply->deleteLater();
        emit resultReady(result);
    });
    deadline->start(timeoutMs);
}

TranslationResult DeepLTranslator::parseResponse(const TranslationRequest &request,
                                                const QByteArray &body, int httpStatus)
{
    TranslationResult result = makeResult(request);
    result.httpStatus = httpStatus;
    if (httpStatus != 200) {
        if (httpStatus == 401 || httpStatus == 403)
            result.error = QStringLiteral("DeepL authentication failed (HTTP %1).").arg(httpStatus);
        else if (httpStatus == 429)
            result.error = QStringLiteral("DeepL rate limit exceeded (HTTP 429).");
        else if (httpStatus == 456)
            result.error = QStringLiteral("DeepL quota exceeded (HTTP 456).");
        else
            result.error = QStringLiteral("DeepL returned HTTP %1.").arg(httpStatus);
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("DeepL returned invalid JSON.");
        return result;
    }
    const QJsonObject object = document.object();
    if (object.contains(QStringLiteral("message"))) {
        result.error = QStringLiteral("DeepL returned a provider error response.");
        return result;
    }
    const QJsonArray translations = object.value(QStringLiteral("translations")).toArray();
    if (translations.size() != 1 || !translations.first().isObject()) {
        result.error = QStringLiteral("DeepL response is missing a single translation.");
        return result;
    }
    const QJsonValue text = translations.first().toObject().value(QStringLiteral("text"));
    if (!text.isString() || text.toString().trimmed().isEmpty()) {
        result.error = QStringLiteral("DeepL response contains missing or empty translation text.");
        return result;
    }
    result.translatedText = text.toString().trimmed();
    result.success = true;
    return result;
}
