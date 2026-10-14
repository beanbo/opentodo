#include "llmprovider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include "services/aiproxy.h"  // isLocalOrPrivateHost

// ─── Common helpers ──────────────────────────────────────────────────────────

namespace {

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

// Claude models that think by default: the 5.x generation (Opus 5 / 5.5,
// Sonnet 5 / 5.5, Haiku 5.5) and the Fable / Mythos tier; on Opus 5.5 and the
// Fable tier thinking cannot be switched off at all. Older models (4.x, the
// claude-3-* names) answer without thinking when the field is omitted.
bool anthropicThinksByDefault(const QString &model)
{
    if (model.startsWith(QLatin1String("claude-fable-"))
        || model.startsWith(QLatin1String("claude-mythos-")))
        return true;
    static const QRegularExpression re(QStringLiteral("^claude-(?:opus|sonnet|haiku)-(\\d+)"));
    const QRegularExpressionMatch m = re.match(model);
    return m.hasMatch() && m.captured(1).toInt() >= 5;
}

// Models that take the server-side refusal fallback `fallbacks: "default"`.
// Claude Haiku 5.5 has no fallback target, so it is left out.
bool anthropicSupportsDefaultFallback(const QString &model)
{
    return model == QLatin1String("claude-opus-5-5")
        || model == QLatin1String("claude-opus-5")
        || model == QLatin1String("claude-sonnet-5-5")
        || model == QLatin1String("claude-fable-5-1");
}

// Thinking counts toward max_tokens. The callers' caps are sized for the reply
// alone (8 tokens for a priority digit), so thinking models get this on top.
constexpr int kAnthropicThinkingHeadroom = 4096;

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
    if (!m_workspaceId.isEmpty())
        req.setRawHeader("anthropic-workspace-id", m_workspaceId.toUtf8());

    if (anthropicThinksByDefault(model)) {
        // Effort is the thinking control on these models; "low" keeps it short
        // for these one-shot assistant prompts (and inside the 30 s timeout).
        QJsonObject outputConfig;
        outputConfig[QStringLiteral("effort")] = QStringLiteral("low");
        body[QStringLiteral("output_config")] = outputConfig;
        body[QStringLiteral("max_tokens")]    = maxTokens + kAnthropicThinkingHeadroom;
    }
    if (anthropicSupportsDefaultFallback(model)) {
        // A safety-classifier decline is re-run server-side on the model
        // Anthropic recommends for that refusal category.
        body[QStringLiteral("fallbacks")] = QStringLiteral("default");
        req.setRawHeader("anthropic-beta", "server-side-fallback-2026-07-01");
    }

    return { req, QJsonDocument(body).toJson(QJsonDocument::Compact) };
}

QString AnthropicProvider::parseResponse(const QByteArray &rawBody,
                                          int httpStatus,
                                          QString *errorOut) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(rawBody);
    if (!doc.isObject()) {
        if (errorOut) *errorOut = invalidShapeError();
        return {};
    }
    const QJsonObject root = doc.object();

    // Error envelope: { "error": { "type": "...", "message": "..." } }
    const QString apiErr = jsonStringAt(root, {"error", "message"});
    if (!apiErr.isEmpty()) {
        QString msg = apiErr;
        // A direct connection from an unsupported region gets a bare
        // 403 forbidden "Request not allowed" — say what actually fixes it.
        if (httpStatus == 403
            && jsonStringAt(root, {"error", "type"}) == QLatin1String("forbidden")) {
            msg += QStringLiteral(" — API Anthropic недоступен из этого региона напрямую. "
                                  "Укажи прокси в настройках AI помощника.");
        }
        // A key created for several workspaces must name one per request.
        if (httpStatus == 400 && apiErr.contains(QLatin1String("anthropic-workspace-id"))) {
            msg += QStringLiteral(" — Ключ не привязан к workspace: укажи Workspace ID "
                                  "в настройках AI помощника (кнопка «Найти»).");
        }
        if (errorOut) *errorOut = msg;
        return {};
    }

    // A safety-classifier decline is an HTTP 200 with stop_reason "refusal"
    // (after any server-side fallback also declined).
    const QString stopReason = root.value(QStringLiteral("stop_reason")).toString();
    if (stopReason == QLatin1String("refusal")) {
        if (errorOut) *errorOut = QStringLiteral("Claude отказался отвечать на этот запрос.");
        return {};
    }

    const QJsonArray content = root.value(QStringLiteral("content")).toArray();
    if (content.isEmpty()) {
        if (errorOut) *errorOut = emptyResponseError();
        return {};
    }
    // Read blocks by type: thinking models put (empty) thinking blocks first.
    // A "fallback" block marks a server-side switch to another model; text
    // before it is the declined model's partial output and is dropped.
    QString text;
    for (const QJsonValue &block : content) {
        const QJsonObject b = block.toObject();
        const QString type = b.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("fallback"))
            text.clear();
        else if (type == QLatin1String("text"))
            text += b.value(QStringLiteral("text")).toString();
    }
    if (!text.isEmpty())
        return text;

    if (stopReason == QLatin1String("max_tokens")) {
        if (errorOut) *errorOut = QStringLiteral("Ответ обрезан по лимиту токенов.");
        return {};
    }
    if (errorOut) *errorOut = QStringLiteral("Не удалось извлечь текст из ответа.");
    return {};
}

QString AnthropicProvider::defaultModel() const
{
    return QStringLiteral("claude-opus-5-5");
}

QStringList AnthropicProvider::suggestedModels() const
{
    return {
        QStringLiteral("claude-opus-5-5"),
        QStringLiteral("claude-sonnet-5-5"),
        QStringLiteral("claude-haiku-5-5"),
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
