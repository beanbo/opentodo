#pragma once

// LlmProvider — pluggable backend for AIAssistant.
//
// Each concrete subclass knows how to convert a (model, systemPrompt,
// messages, maxTokens) tuple plus an API key into a fully-formed
// QNetworkRequest + body, and how to extract the assistant's reply text
// from the raw response bytes. The shared retry/cancel/history logic in
// AIAssistant calls into the active provider through this interface.
//
// Adding a new provider: subclass LlmProvider, implement the six pure
// virtuals, and register it inside LlmProviderRegistry::createAll() in
// llmprovider.cpp. No other code change is required.

#include <QByteArray>
#include <QList>
#include <QNetworkRequest>
#include <QPair>
#include <QString>
#include <QStringList>

class LlmProvider
{
public:
    virtual ~LlmProvider() = default;

    // A (HTTP request, body) pair ready to be POSTed by QNetworkAccessManager.
    struct RequestData {
        QNetworkRequest request;
        QByteArray body;
    };

    // Build a chat-completion POST request.
    //   apiKey        – the provider-specific secret. Empty key ⇒ caller
    //                   should not invoke this (the precondition is checked
    //                   in AIAssistant before calling).
    //   model         – the model identifier the user chose for this provider.
    //   systemPrompt  – instructions that go into the provider's "system"
    //                   slot (system / system_instruction / role=system msg).
    //   messages      – ordered list of (role, content) pairs, where role is
    //                   "user" or "assistant". The provider is responsible for
    //                   translating "assistant" to "model" when its API
    //                   requires that (e.g. Gemini).
    //   maxTokens     – output cap. Mapped to the provider-appropriate field
    //                   (max_tokens / maxOutputTokens).
    virtual RequestData buildRequest(
        const QString &apiKey,
        const QString &model,
        const QString &systemPrompt,
        const QList<QPair<QString, QString>> &messages,
        int maxTokens) const = 0;

    // Extract the assistant's text reply from a raw response body.
    // On success: returns the text and leaves *errorOut empty.
    // On failure: returns an empty string and sets *errorOut to a
    // user-facing message (already localized to Russian — these surface
    // straight into the chat UI). If the body contains a structured API
    // error envelope, parse it and surface that message.
    //   httpStatus    – HTTP status code from the reply. Providers can use
    //                   it for status-conditional error parsing.
    //   rawBody       – the response body bytes as returned by readAll().
    virtual QString parseResponse(
        const QByteArray &rawBody,
        int httpStatus,
        QString *errorOut) const = 0;

    // Stable identifier, e.g. "anthropic" / "gemini" / "openai_compat".
    // Used as the SecretStore key suffix and the QSettings sub-key prefix,
    // so changing it post-release would orphan stored configuration.
    virtual QString id() const = 0;

    // SecretStore key under which this provider's API key lives. The
    // Anthropic provider keeps the legacy "claude_api_key" name for
    // backward-compat; new providers should follow the "<id>_api_key"
    // convention.
    virtual QString secretKey() const = 0;

    // Localized display name shown in the provider dropdown.
    virtual QString displayName() const = 0;

    // Default model used when the user hasn't picked one yet.
    virtual QString defaultModel() const = 0;

    // Recommended model identifiers for the dropdown. Users can also type
    // a custom ID in the "Custom" field, so this list does not need to be
    // exhaustive — pick three or four sensible defaults.
    virtual QStringList suggestedModels() const = 0;

    // Placeholder text shown in the API-key field (e.g. "sk-ant-api03-…").
    virtual QString apiKeyPlaceholder() const = 0;

    // Where the user gets the key. Shown as a clickable link under the
    // key field. Empty ⇒ no link is shown.
    virtual QString consoleUrl() const = 0;
};


// ─── Concrete providers ──────────────────────────────────────────────────────

// Anthropic Claude. Endpoint /v1/messages, header x-api-key + anthropic-version.
// This is the original provider — the request shape is preserved exactly so
// that pre-existing tests (which inspect the x-api-key header and the
// content[].type=="text" response field) continue to pass. Models that think
// by default (the 5.x generation, Fable) additionally get
// output_config.effort="low" plus max_tokens headroom for the thinking, and
// the ones that support it opt into the server-side refusal fallback.
class AnthropicProvider : public LlmProvider
{
public:
    // Workspace for keys that are not scoped to one (personal / service
    // account keys created for several workspaces): the API rejects their
    // requests unless an anthropic-workspace-id header names the workspace.
    // Empty ⇒ no header (single-workspace and legacy workspace keys). Set by
    // AIAssistant from QSettings.
    void setWorkspaceId(const QString &id) { m_workspaceId = id.trimmed(); }
    QString workspaceId() const            { return m_workspaceId; }

    RequestData buildRequest(const QString &apiKey,
                             const QString &model,
                             const QString &systemPrompt,
                             const QList<QPair<QString, QString>> &messages,
                             int maxTokens) const override;
    QString parseResponse(const QByteArray &rawBody, int httpStatus,
                          QString *errorOut) const override;

    QString id() const override          { return QStringLiteral("anthropic"); }
    QString secretKey() const override   { return QStringLiteral("claude_api_key"); }
    QString displayName() const override { return QStringLiteral("Anthropic Claude"); }
    QString defaultModel() const override;
    QStringList suggestedModels() const override;
    QString apiKeyPlaceholder() const override { return QStringLiteral("sk-ant-api03-…"); }
    QString consoleUrl() const override        { return QStringLiteral("https://console.anthropic.com/settings/keys"); }

private:
    QString m_workspaceId;
};

// Google Gemini. Key goes in the URL query string (?key=…); the body uses
// system_instruction + contents with role="model" for assistant turns.
// generationConfig.thinkingConfig.thinkingBudget=0 disables the
// reasoning-token surcharge that Gemini 2.5 Flash applies by default.
class GeminiProvider : public LlmProvider
{
public:
    RequestData buildRequest(const QString &apiKey,
                             const QString &model,
                             const QString &systemPrompt,
                             const QList<QPair<QString, QString>> &messages,
                             int maxTokens) const override;
    QString parseResponse(const QByteArray &rawBody, int httpStatus,
                          QString *errorOut) const override;

    QString id() const override          { return QStringLiteral("gemini"); }
    QString secretKey() const override   { return QStringLiteral("gemini_api_key"); }
    QString displayName() const override { return QStringLiteral("Google Gemini"); }
    QString defaultModel() const override;
    QStringList suggestedModels() const override;
    QString apiKeyPlaceholder() const override { return QStringLiteral("AIzaSy…"); }
    QString consoleUrl() const override        { return QStringLiteral("https://aistudio.google.com/apikey"); }
};

// OpenAI-compatible. Covers OpenAI itself, OpenRouter, Groq, Together,
// local Ollama, anything else that speaks /v1/chat/completions with a
// Bearer auth header. The base URL is configurable (without the /v1
// suffix); the provider appends "/v1/chat/completions".
class OpenAICompatibleProvider : public LlmProvider
{
public:
    // The base URL (e.g. "https://api.openai.com") is set externally by
    // AIAssistant from QSettings so it persists across runs. Setting an
    // empty URL falls back to OpenAI's host; a malformed / scheme-less /
    // public-cleartext URL is rejected (effectiveBaseUrl() returns "")
    // rather than redirected, so the key never reaches an unintended host.
    void setBaseUrl(const QString &url) { m_baseUrl = url; }
    QString baseUrl() const             { return m_baseUrl; }
    QString effectiveBaseUrl() const;  // m_baseUrl, OpenAI default if unset, "" if rejected

    RequestData buildRequest(const QString &apiKey,
                             const QString &model,
                             const QString &systemPrompt,
                             const QList<QPair<QString, QString>> &messages,
                             int maxTokens) const override;
    QString parseResponse(const QByteArray &rawBody, int httpStatus,
                          QString *errorOut) const override;

    QString id() const override          { return QStringLiteral("openai_compat"); }
    QString secretKey() const override   { return QStringLiteral("openai_compat_api_key"); }
    QString displayName() const override { return QStringLiteral("OpenAI-совместимый"); }
    QString defaultModel() const override;
    QStringList suggestedModels() const override;
    QString apiKeyPlaceholder() const override { return QStringLiteral("sk-… или OpenRouter/Groq токен"); }
    QString consoleUrl() const override        { return QStringLiteral("https://platform.openai.com/api-keys"); }

private:
    QString m_baseUrl;  // empty ⇒ OpenAI default in effectiveBaseUrl()
};
