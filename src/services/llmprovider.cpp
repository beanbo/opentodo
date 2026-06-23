#include "llmprovider.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

// ─── Common helpers ──────────────────────────────────────────────────────────

namespace {

// True if host is loopback or a private-LAN (RFC1918) address. Cleartext http://
// to these is acceptable (a local/LAN LLM server); http:// to a PUBLIC host is
// refused because the Authorization: Bearer key would cross the internet in the
// clear.
bool isLocalOrPrivateHost(const QString &host)
{
    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        return true;
    const QHostAddress a(host);
    if (a.isNull())
        return false;
    return a.isLoopback()
        || a.isInSubnet(QHostAddress(QStringLiteral("10.0.0.0")), 8)
        || a.isInSubnet(QHostAddress(QStringLiteral("172.16.0.0")), 12)
        || a.isInSubnet(QHostAddress(QStringLiteral("192.168.0.0")), 16);
}

// Parse an API-shaped JSON error envelope. Returns the human message when
// found, else returns an empty string. Each provider has a slightly different
// envelope shape, so the caller picks the right key path.
QString jsonStringAt(const QJsonObject &obj, std::initializer_list<const char *> path)
{
    QJsonValue v = obj;
    for (const char *key : path) {
        if (!v.isObject()) return {};
        v = v.toObject().value(QLatin1String(key));
    }
    return v.isString() ? v.toString() : QString();
}

// Localized "couldn't parse response" message reused by every provider.
QString invalidShapeError()
{
    return QStringLiteral("Неверный формат ответа от API.");
}

QString emptyResponseError()
{
    return QStringLiteral("Пустой ответ от API.");
}

}  // namespace


// ─── AnthropicProvider ───────────────────────────────────────────────────────

LlmProvider::RequestData
AnthropicProvider::buildRequest(const QString &apiKey,
                                 const QString &model,
                                 const QString &systemPrompt,
                                 const QList<QPair<QString, QString>> &messages,
                                 int maxTokens) const
{
    QJsonArray jsonMessages;
    for (const auto &[role, text] : messages) {
        QJsonObject msg;
        msg[QStringLiteral("role")]    = role;  // "user" | "assistant" — Anthropic uses both
        msg[QStringLiteral("content")] = text;
        jsonMessages.append(msg);
    }

    QJsonObject body;
    body[QStringLiteral("model")]      = model;
    body[QStringLiteral("max_tokens")] = maxTokens;
    if (!systemPrompt.isEmpty())
        body[QStringLiteral("system")] = systemPrompt;
    body[QStringLiteral("messages")]   = jsonMessages;

    QNetworkRequest req{QUrl(QStringLiteral("https://api.anthropic.com/v1/messages"))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("x-api-key",         apiKey.toUtf8());
    req.setRawHeader("anthropic-version", "2023-06-01");

    return { req, QJsonDocument(body).toJson(QJsonDocument::Compact) };
}

QString AnthropicProvider::parseResponse(const QByteArray &rawBody,
                                          int /*httpStatus*/,
                                          QString *errorOut) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(rawBody);
    if (!doc.isObject()) {
        if (errorOut) *errorOut = invalidShapeError();
        return {};
    }
    const QJsonObject root = doc.object();

    // Error envelope: { "error": { "message": "..." } }
    const QString apiErr = jsonStringAt(root, {"error", "message"});
    if (!apiErr.isEmpty()) {
        if (errorOut) *errorOut = apiErr;
        return {};
    }

    const QJsonArray content = root.value(QStringLiteral("content")).toArray();
    if (content.isEmpty()) {
        if (errorOut) *errorOut = emptyResponseError();
        return {};
    }
    for (const QJsonValue &block : content) {
        const QJsonObject b = block.toObject();
        if (b.value(QStringLiteral("type")).toString() == QLatin1String("text"))
            return b.value(QStringLiteral("text")).toString();
    }

    if (errorOut) *errorOut = QStringLiteral("Не удалось извлечь текст из ответа.");
    return {};
}

QString AnthropicProvider::defaultModel() const
{
    // Keeping Haiku 4.5 as the default since it's the cheap workhorse the
    // current build is already wired to. Users who want stronger reasoning
    // can pick Sonnet/Opus from the dropdown.
    return QStringLiteral("claude-haiku-4-5-20251001");
}

QStringList AnthropicProvider::suggestedModels() const
{
    return {
        QStringLiteral("claude-haiku-4-5-20251001"),
        QStringLiteral("claude-sonnet-4-6"),
        QStringLiteral("claude-opus-4-7"),
    };
}


// ─── GeminiProvider ──────────────────────────────────────────────────────────

LlmProvider::RequestData
GeminiProvider::buildRequest(const QString &apiKey,
                              const QString &model,
                              const QString &systemPrompt,
                              const QList<QPair<QString, QString>> &messages,
                              int maxTokens) const
{
    // Gemini distinguishes "user" and "model" — translate "assistant" → "model".
    QJsonArray contents;
    for (const auto &[role, text] : messages) {
        const QString geminiRole = (role == QLatin1String("assistant"))
            ? QStringLiteral("model")
            : QStringLiteral("user");

        QJsonObject parts;
        parts[QStringLiteral("text")] = text;

        QJsonObject turn;
        turn[QStringLiteral("role")]  = geminiRole;
        turn[QStringLiteral("parts")] = QJsonArray{ parts };
        contents.append(turn);
    }

    QJsonObject body;
    if (!systemPrompt.isEmpty()) {
        QJsonObject sysPart;
        sysPart[QStringLiteral("text")] = systemPrompt;
        QJsonObject sysInst;
        sysInst[QStringLiteral("parts")] = QJsonArray{ sysPart };
        body[QStringLiteral("system_instruction")] = sysInst;
    }
    body[QStringLiteral("contents")] = contents;

    // thinkingBudget=0 disables 2.5 Flash's default reasoning surcharge,
    // which is overkill for our short-prompt assistant flow and gobbles
    // hundreds of tokens per turn on the free tier.
    QJsonObject genCfg;
    genCfg[QStringLiteral("maxOutputTokens")] = maxTokens;
    QJsonObject thinkCfg;
    thinkCfg[QStringLiteral("thinkingBudget")] = 0;
    genCfg[QStringLiteral("thinkingConfig")] = thinkCfg;
    body[QStringLiteral("generationConfig")] = genCfg;

    QUrl url(QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/")
             + model + QStringLiteral(":generateContent"));

    QNetworkRequest req{url};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // Pass the key in a header rather than the URL query string, so it doesn't
    // leak into client logs / proxy access logs / crash breadcrumbs.
    req.setRawHeader("x-goog-api-key", apiKey.toUtf8());

    return { req, QJsonDocument(body).toJson(QJsonDocument::Compact) };
}

QString GeminiProvider::parseResponse(const QByteArray &rawBody,
                                       int /*httpStatus*/,
                                       QString *errorOut) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(rawBody);
    if (!doc.isObject()) {
        if (errorOut) *errorOut = invalidShapeError();
        return {};
    }
    const QJsonObject root = doc.object();

    // Error envelope: { "error": { "code": .., "message": "..", "status": ".." } }
    const QString apiErr = jsonStringAt(root, {"error", "message"});
    if (!apiErr.isEmpty()) {
        if (errorOut) *errorOut = apiErr;
        return {};
    }

    const QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
    if (candidates.isEmpty()) {
        // Sometimes Gemini returns promptFeedback.blockReason instead of an
        // error envelope when content was filtered. Surface that.
        const QString blocked = jsonStringAt(root, {"promptFeedback", "blockReason"});
        if (!blocked.isEmpty()) {
            if (errorOut) *errorOut = QStringLiteral("Запрос заблокирован Gemini: %1").arg(blocked);
            return {};
        }
        if (errorOut) *errorOut = emptyResponseError();
        return {};
    }

    const QJsonObject cand   = candidates.first().toObject();
    const QJsonArray  parts  = cand.value(QStringLiteral("content"))
                                  .toObject()
                                  .value(QStringLiteral("parts"))
                                  .toArray();
    QString out;
    for (const QJsonValue &p : parts) {
        const QString t = p.toObject().value(QStringLiteral("text")).toString();
        if (!t.isEmpty()) out += t;
    }
    if (out.isEmpty()) {
        // Check finishReason — MAX_TOKENS is a common cause of empty content
        const QString fr = cand.value(QStringLiteral("finishReason")).toString();
        if (fr == QLatin1String("MAX_TOKENS")) {
            if (errorOut) *errorOut = QStringLiteral("Ответ обрезан по лимиту токенов. Увеличь max_tokens или сократи запрос.");
            return {};
        }
        if (errorOut) *errorOut = QStringLiteral("Не удалось извлечь текст из ответа.");
        return {};
    }
    return out;
}

QString GeminiProvider::defaultModel() const
{
    // Flash-Lite has the highest free-tier RPD (1000/day) and is plenty
    // capable for short todo-assistant prompts.
    return QStringLiteral("gemini-2.5-flash-lite");
}

QStringList GeminiProvider::suggestedModels() const
{
    return {
        QStringLiteral("gemini-2.5-flash-lite"),
        QStringLiteral("gemini-2.5-flash"),
        QStringLiteral("gemini-2.0-flash"),
        QStringLiteral("gemini-2.5-pro"),
    };
}


// ─── OpenAICompatibleProvider ────────────────────────────────────────────────

QString OpenAICompatibleProvider::effectiveBaseUrl() const
{
    if (m_baseUrl.trimmed().isEmpty())
        return QStringLiteral("https://api.openai.com");
    const QString u = m_baseUrl.trimmed();

    // Require an EXPLICIT http(s):// scheme. Earlier iter-5 fix used
    // QUrl::fromUserInput, which silently synthesises a scheme for bare
    // hosts ("api.example.com" → "https://api.example.com") — validation
    // then passed but the function returned the un-prefixed string `u`,
    // and downstream `QUrl(u + "/v1/...")` parsed it as a relative URL
    // with no host. Demanding the user write the scheme keeps the bypass
    // shut and the request URL well-formed.
    //
    // We additionally use a STRICT parser (QUrl::StrictMode) so things
    // like "localhost:8080/path" are not misread (StrictMode treats the
    // scheme as the part before the first colon only when there is no
    // unambiguous host). An attacker-supplied base_url like file:///...
    // or ftp:// is then rejected by the scheme check below and we fall
    // back to api.openai.com — safer than letting the API key fly to a
    // hostile target via the Authorization header.
    if (!u.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        && !u.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        qWarning("OpenAICompatibleProvider: rejecting base_url '%s' "
                 "without explicit http(s):// scheme — falling back to api.openai.com",
                 qPrintable(u));
        return QStringLiteral("https://api.openai.com");
    }

    const QUrl parsed(u, QUrl::StrictMode);
    if (!parsed.isValid() || parsed.host().isEmpty()
        || (parsed.scheme() != QLatin1String("http")
            && parsed.scheme() != QLatin1String("https"))) {
        qWarning("OpenAICompatibleProvider: rejecting invalid base_url '%s' "
                 "— falling back to api.openai.com",
                 qPrintable(u));
        return QStringLiteral("https://api.openai.com");
    }

    // Refuse cleartext http:// to a PUBLIC host — the Authorization: Bearer API
    // key would cross the internet in the clear. Loopback / private-LAN (a local
    // Ollama / LM Studio) over http is fine.
    if (parsed.scheme() == QLatin1String("http") && !isLocalOrPrivateHost(parsed.host())) {
        qWarning("OpenAICompatibleProvider: rejecting cleartext http:// base_url "
                 "'%s' to a public host (API key would leak) — use https or a "
                 "loopback/LAN host; falling back to api.openai.com",
                 qPrintable(u));
        return QStringLiteral("https://api.openai.com");
    }

    // Strip a trailing slash so we can append "/v1/chat/completions" cleanly.
    QString out = u;
    while (out.endsWith(QLatin1Char('/'))) out.chop(1);
    return out;
}

LlmProvider::RequestData
OpenAICompatibleProvider::buildRequest(const QString &apiKey,
                                        const QString &model,
                                        const QString &systemPrompt,
                                        const QList<QPair<QString, QString>> &messages,
                                        int maxTokens) const
{
    QJsonArray jsonMessages;
    if (!systemPrompt.isEmpty()) {
        QJsonObject sys;
        sys[QStringLiteral("role")]    = QStringLiteral("system");
        sys[QStringLiteral("content")] = systemPrompt;
        jsonMessages.append(sys);
    }
    for (const auto &[role, text] : messages) {
        QJsonObject m;
        m[QStringLiteral("role")]    = role;  // "user" | "assistant"
        m[QStringLiteral("content")] = text;
        jsonMessages.append(m);
    }

    QJsonObject body;
    body[QStringLiteral("model")]      = model;
    body[QStringLiteral("max_tokens")] = maxTokens;
    body[QStringLiteral("messages")]   = jsonMessages;

    QNetworkRequest req{QUrl(effectiveBaseUrl() + QStringLiteral("/v1/chat/completions"))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());

    return { req, QJsonDocument(body).toJson(QJsonDocument::Compact) };
}

QString OpenAICompatibleProvider::parseResponse(const QByteArray &rawBody,
                                                  int /*httpStatus*/,
                                                  QString *errorOut) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(rawBody);
    if (!doc.isObject()) {
        if (errorOut) *errorOut = invalidShapeError();
        return {};
    }
    const QJsonObject root = doc.object();

    // OpenAI error: { "error": { "message": ".." } }
    const QString apiErr = jsonStringAt(root, {"error", "message"});
    if (!apiErr.isEmpty()) {
        if (errorOut) *errorOut = apiErr;
        return {};
    }

    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        if (errorOut) *errorOut = emptyResponseError();
        return {};
    }
    const QString text = choices.first()
        .toObject()
        .value(QStringLiteral("message"))
        .toObject()
        .value(QStringLiteral("content"))
        .toString();

    if (text.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("Не удалось извлечь текст из ответа.");
        return {};
    }
    return text;
}

QString OpenAICompatibleProvider::defaultModel() const
{
    return QStringLiteral("gpt-4o-mini");
}

QStringList OpenAICompatibleProvider::suggestedModels() const
{
    // Spanning a few common OpenAI-compatible endpoints. Users routing
    // through OpenRouter/Groq/Ollama will paste their own model IDs into
    // the "Custom" field.
    return {
        QStringLiteral("gpt-4o-mini"),
        QStringLiteral("gpt-4o"),
        QStringLiteral("anthropic/claude-3.5-haiku"),  // OpenRouter
        QStringLiteral("llama-3.3-70b-versatile"),     // Groq
    };
}
