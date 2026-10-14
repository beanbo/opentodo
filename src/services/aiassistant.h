#pragma once

#include <QObject>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QList>
#include <QPair>
#include <QQmlEngine>
#include <QTimer>

#include <functional>
#include <memory>
#include <vector>

#include "services/llmprovider.h"

class DataStore;
class QNetworkReply;
class SecretStore;

class AIAssistant : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use the aiAssistant context property.")

    Q_PROPERTY(bool isResponding READ isResponding NOTIFY isRespondingChanged)
    Q_PROPERTY(bool isConfigured READ isConfigured NOTIFY isConfiguredChanged)
    Q_PROPERTY(bool isGeneratingSubtasks READ isGeneratingSubtasks NOTIFY isGeneratingSubtasksChanged)
    Q_PROPERTY(bool isGeneratingDescription READ isGeneratingDescription NOTIFY isGeneratingDescriptionChanged)
    Q_PROPERTY(bool isSuggestingPriority READ isSuggestingPriority NOTIFY isSuggestingPriorityChanged)

    // Multi-provider settings, persisted via QSettings (the API key itself
    // goes into the SecretStore — see secretKey() on each LlmProvider).
    Q_PROPERTY(QString currentProviderId READ currentProviderId
                                          WRITE setCurrentProviderId
                                          NOTIFY currentProviderChanged)
    Q_PROPERTY(QString currentModel       READ currentModel
                                          WRITE setCurrentModel
                                          NOTIFY currentModelChanged)
    Q_PROPERTY(QString openAiCompatBaseUrl READ openAiCompatBaseUrl
                                           WRITE setOpenAiCompatBaseUrl
                                           NOTIFY openAiCompatBaseUrlChanged)
    Q_PROPERTY(QStringList providerIds READ providerIds CONSTANT)

    // Outbound proxy for every AI request (services/aiproxy.h), persisted in
    // QSettings ai/proxy/*. proxyMode is "auto" | "manual" | "none"; proxyUrl
    // is used in "manual". proxyStatus describes the route the current
    // provider's requests take right now (never shows a proxy password).
    Q_PROPERTY(QString proxyMode   READ proxyMode WRITE setProxyMode NOTIFY proxyChanged)
    Q_PROPERTY(QString proxyUrl    READ proxyUrl  WRITE setProxyUrl  NOTIFY proxyChanged)
    Q_PROPERTY(QString proxyStatus READ proxyStatus NOTIFY proxyChanged)
    Q_PROPERTY(bool isTestingConnection READ isTestingConnection NOTIFY isTestingConnectionChanged)

    // wrkspc_… sent as anthropic-workspace-id; needed only for Anthropic keys
    // that are not scoped to a single workspace. Persisted in QSettings
    // ai/anthropic/workspace_id.
    Q_PROPERTY(QString anthropicWorkspaceId READ anthropicWorkspaceId
                                             WRITE setAnthropicWorkspaceId
                                             NOTIFY anthropicWorkspaceIdChanged)

public:
    explicit AIAssistant(DataStore *store, QObject *parent = nullptr);
    ~AIAssistant() override;

    // Sec-1: inject a SecretStore so the API key lives in the OS keyring
    // instead of QSettings. When unset (legacy / tests that don't wire it),
    // the class falls back to the original XOR-obfuscated QSettings path so
    // existing test fixtures keep passing without modification.
    // The pointer is not owned — the caller (typically main.cpp) keeps it
    // alive for the lifetime of the AIAssistant. Calling setSecretStore()
    // triggers an async warm-up read so cachedApiKey() becomes populated
    // shortly after wiring. Pre-existing plaintext keys in QSettings are
    // migrated into the SecretStore on this first wiring and removed from
    // QSettings.
    void setSecretStore(SecretStore *store);

    // Sec-1: the key under which we stash the *Anthropic* API key in the
    // SecretStore. Kept as a public constant so the migration code in
    // main.cpp (and the legacy single-provider tests) can reference it
    // without duplicating the literal. Multi-provider code prefers
    // LlmProvider::secretKey() on each provider instance.
    static const char *secretKey();

    bool isResponding() const { return m_isResponding; }
    bool isConfigured() const;
    bool isGeneratingSubtasks() const { return m_isGeneratingSubtasks; }
    bool isGeneratingDescription() const { return m_isGeneratingDescription; }
    bool isSuggestingPriority() const { return m_isSuggestingPriority; }

    // ── Multi-provider management ─────────────────────────────────────────
    QString currentProviderId() const { return m_currentProviderId; }
    void setCurrentProviderId(const QString &id);

    QString currentModel() const;
    void setCurrentModel(const QString &model);

    QString openAiCompatBaseUrl() const;
    void setOpenAiCompatBaseUrl(const QString &url);

    QStringList providerIds() const;

    Q_INVOKABLE QString  providerDisplayName(const QString &id) const;
    Q_INVOKABLE QString  providerDefaultModel(const QString &id) const;
    Q_INVOKABLE QStringList providerSuggestedModels(const QString &id) const;
    Q_INVOKABLE QString  providerConsoleUrl(const QString &id) const;
    Q_INVOKABLE QString  providerApiKeyPlaceholder(const QString &id) const;
    Q_INVOKABLE bool     providerHasKey(const QString &id) const;

    // ── Proxy ─────────────────────────────────────────────────────────────
    QString proxyMode() const;
    void setProxyMode(const QString &mode);
    QString proxyUrl() const;
    void setProxyUrl(const QString &url);
    QString proxyStatus() const;
    // Empty when `url` is a usable proxy address, else a user-facing error.
    Q_INVOKABLE QString validateProxyUrl(const QString &url) const;

    // Sends a one-line prompt with the current provider, model, key and
    // proxy, outside the chat history. Emits connectionTestFinished(ok,
    // message) where message carries the reply or error, the latency, the
    // model and the route.
    Q_INVOKABLE void testConnection();
    bool isTestingConnection() const { return m_isTestingConnection; }

    QString anthropicWorkspaceId() const;
    void setAnthropicWorkspaceId(const QString &id);
    // Lists the workspaces the stored Anthropic key can act in (Admin API
    // List Workspaces, which accepts keys not scoped to a workspace) and
    // emits anthropicWorkspacesFetched([{id, name}], error).
    Q_INVOKABLE void fetchAnthropicWorkspaces();

    Q_INVOKABLE void sendMessage(const QString &userMessage);
    Q_INVOKABLE void clearConversation();

    // ── Tool-use (create/modify tasks via confirmed structured actions) ───────
    // The assistant is instructed (buildSystemPrompt) to append a fenced
    //   ```action { "op": "...", ... }```
    // block when the user asks to mutate tasks. onChatReplyFinished extracts it,
    // emits actionProposed(json, humanSummary) for a confirmation UI, and strips
    // it from the displayed text. confirmAction() applies it (the ONLY path that
    // mutates) and emits actionExecuted. Supported ops: add_task, complete_task,
    // set_priority, delete_task. Portable across all providers (no per-provider
    // native tool-calling protocol).
    Q_INVOKABLE bool confirmAction(const QString &actionJson);

    // Route mutating ops (complete/delete) through the app's TaskViewModel so a
    // confirmed AI action gets the SAME treatment as a manual one: recurring-task
    // advance, karma, undo, and per-field CRDT _meta stamping. main.cpp injects
    // these (capturing TaskViewModel) — kept as std::function so this service
    // layer takes no link/header dependency on the viewmodel layer. When unset
    // (unit tests), executeAction falls back to direct DataStore mutation (still
    // stamping per-field _meta) so standalone fixtures keep working. Each returns
    // true on success.
    using TaskActionFn = std::function<bool(const QString &taskId)>;
    void setTaskActionHandlers(TaskActionFn completeTask, TaskActionFn deleteTask);

    // Test hooks (no network): extract the action JSON object from a raw
    // response (returns "" if none) and the response text with the block removed.
    static QString extractActionJson(const QString &response);
    static QString stripActionBlock(const QString &response);

    // Sets the API key for the *current* provider. Kept for QML/test
    // backward compat. Prefer setApiKeyFor(id, key) when targeting a
    // specific provider from the Settings UI.
    Q_INVOKABLE void setApiKey(const QString &key);
    Q_INVOKABLE void setApiKeyFor(const QString &providerId, const QString &key);

    // Subtask generation — independent of the chat flow.
    // Emits subtasksGenerated or subtaskGenerationFailed when done.
    Q_INVOKABLE void generateSubtasks(const QString &parentTaskId, int count = 5);
    Q_INVOKABLE void cancelSubtaskGeneration();

    // Description generation — independent of the chat flow.
    // Emits descriptionGenerated(text) or descriptionGenerationFailed(reason) when done.
    Q_INVOKABLE void generateDescription(const QString &taskTitle);

    // Priority suggestion — independent of the chat flow.
    // Emits prioritySuggested(p) on success or errorReceived(reason) on failure.
    Q_INVOKABLE void suggestPriority(const QString &title);

    // S-Q2.20: Cancel API. Each method aborts the corresponding in-flight reply
    // (if any) and clears the busy flag. The reply's finished() slot still
    // runs but silently discards the response (no error toast, no result).
    // Calling when nothing is in-flight is a safe no-op. The four cancels
    // operate independently — cancelChat() does not affect description, etc.
    Q_INVOKABLE void cancelChat();
    Q_INVOKABLE void cancelDescription();
    Q_INVOKABLE void cancelPriority();
    Q_INVOKABLE void cancelAll();

    // Exposed for tests: extract a priority digit (1–4) from a raw response. Returns 4 on failure.
    static int parsePriorityResponse(const QString &raw);

    // Exposed for tests: build the subtask prompt string without making a network call.
    QString buildSubtaskPrompt(const QString &content,
                                const QString &description,
                                int count) const;

    // Exposed for tests: parse a raw Claude response into a QStringList.
    static QStringList parseSubtasksResponse(const QString &raw);

    // S-Q2.20: Strip embedded <user_data>/</user_data> substrings from raw user
    // input so a malicious payload cannot close the boundary tag and re-open
    // the instruction context. Static + public so tests can exercise it
    // independent of the network plumbing. The returned string is safe to
    // splice between <user_data>…</user_data> delimiters.
    static QString sanitizeForUserDataWrap(const QString &raw);

    // S-AISafety: Maximum length of user-supplied content (task name / description /
    // chat message) that we will send to the API in a single request. Above this we
    // clamp/truncate or reject. Bound is a coarse guard against accidental cost
    // explosion (e.g. someone pasting a 10 MB log into a task) — the model itself
    // has a separate token cap. 8 KiB ~= 2-3 K tokens of Cyrillic which is far above
    // any reasonable task description.
    static constexpr int kMaxUserContentBytes = 8 * 1024;

    // Test hook: inject a custom QNetworkAccessManager so the test suite can
    // observe outbound requests without making real HTTP calls. Ownership is NOT
    // transferred — the caller must outlive the AIAssistant. Pass nullptr to
    // revert to the internal manager.
    void setNetworkManagerForTesting(QNetworkAccessManager *nam);

signals:
    void isRespondingChanged();
    void isConfiguredChanged();
    void isGeneratingSubtasksChanged();
    void isGeneratingDescriptionChanged();
    void isSuggestingPriorityChanged();
    void currentProviderChanged();
    void currentModelChanged();
    void openAiCompatBaseUrlChanged();
    void proxyChanged();
    void isTestingConnectionChanged();
    void connectionTestFinished(bool ok, const QString &message);
    void anthropicWorkspaceIdChanged();
    void anthropicWorkspacesFetched(const QVariantList &workspaces, const QString &error);
    void responseReceived(const QString &text);
    void errorReceived(const QString &error);
    // Tool-use: a task mutation the assistant proposes; QML shows `summary` with
    // confirm/cancel and calls confirmAction(actionJson) on confirm.
    void actionProposed(const QString &actionJson, const QString &summary);
    void actionExecuted(const QString &summary);
    // Subtask-specific signals
    void subtasksGenerated(const QStringList &suggestions, const QString &parentId);
    void subtaskGenerationFailed(const QString &error);
    // Description-specific signals
    void descriptionGenerated(const QString &description);
    void descriptionGenerationFailed(const QString &reason);
    // Priority-specific signal
    void prioritySuggested(int priority);
    // S-Q2.20: Soft "user aborted" notifications. Distinct from error signals
    // so QML can react without showing an error toast.
    void chatCanceled();
    void subtaskCanceled();
    void descriptionCanceled();
    void priorityCanceled();

private slots:
    // B-5.23: internal reply handlers (shared by first attempt and single retry)
    void onChatReplyFinished(QNetworkReply *reply, bool isRetry);
    void onSubtaskReplyFinished(QNetworkReply *reply, const QString &parentTaskId, bool isRetry);
    void onDescriptionReplyFinished(QNetworkReply *reply, bool isRetry);
    void onPriorityReplyFinished(QNetworkReply *reply, bool isRetry);

private:
    QString buildSystemPrompt() const;        // includes current task context (S-5.22)
    QString buildUserContextSnapshot() const; // current projects/tasks summary (S-5.22)

    // Tool-use: execute a parsed action against the store. Returns true on
    // success and fills `summary` with a human one-liner. Confirmation happens
    // in QML before this is reached (via confirmAction).
    bool executeAction(const QJsonObject &action, QString *summary);
    // Build the human summary for a proposed action (for the confirmation UI).
    static QString actionSummary(const QJsonObject &action);

    // S-5.1 / Sec-1: XOR-obfuscation helpers for the *legacy* QSettings storage
    // path. Still public-static because:
    //   (a) the migration step in main.cpp uses deobfuscate() to read the old
    //       plaintext value before moving it into the SecretStore;
    //   (b) tests that didn't wire a SecretStore continue to exercise these
    //       helpers (the storage backend stays QSettings in that path).
    // NOTE: This is obfuscation, NOT encryption. New code paths should
    // funnel through SecretStore, which uses QtKeychain on desktop.
    static QByteArray machineSecret();
    static QString obfuscate(const QString &plain);
    static QString deobfuscate(const QString &cipher);

    // QSettings key for the legacy XOR-obfuscated fallback path (used only
    // when no SecretStore is wired, e.g. in older test fixtures). The
    // Anthropic provider keeps the bare "ai/k" so legacy tests stay green;
    // newer providers use the namespaced "ai/k_<id>".
    static QString legacyQSettingsKey(const LlmProvider &p);

    LlmProvider *currentProvider() const;
    LlmProvider *providerById(const QString &id) const;

    // Installs a fresh AiProxyFactory on m_nam from the persisted settings.
    void applyProxySettings();
    // Endpoint URL of the current provider (for the proxyStatus route).
    QUrl currentEndpointUrl() const;

    // Per-provider key load/save. SecretStore when wired; QSettings XOR
    // fallback otherwise. The Anthropic legacy path is preserved exactly.
    QString loadKeyFor(const LlmProvider &p) const;
    void writeKeyFor(const LlmProvider &p, const QString &key);

    // Per-provider model load/save. Reads QSettings ai/<id>/model and
    // falls back to provider->defaultModel().
    QString modelFor(const LlmProvider &p) const;
    void writeModelFor(const LlmProvider &p, const QString &model);

    // Snapshot of the args we passed to the latest provider->buildRequest()
    // for each flow. Used to reconstruct the request on a single 429/5xx
    // retry without re-running the prompt-building logic.
    struct PendingArgs {
        QString model;
        QString systemPrompt;
        QList<QPair<QString, QString>> messages;
        int maxTokens = 0;
    };
    PendingArgs m_pendingChat;
    PendingArgs m_pendingSubtask;
    PendingArgs m_pendingDescription;
    PendingArgs m_pendingPriority;

    // Helper to fire a chat POST and wire its reply to onChatReplyFinished.
    void postChatRequest(const QNetworkRequest &req, const QByteArray &body, bool isRetry);

    // Helper to fire a subtask POST and wire its reply to onSubtaskReplyFinished.
    void postSubtaskRequest(const QNetworkRequest &req, const QByteArray &body,
                            const QString &parentTaskId, bool isRetry);

    // Helper to fire a description POST and wire its reply to onDescriptionReplyFinished.
    void postDescriptionRequest(const QNetworkRequest &req, const QByteArray &body, bool isRetry);

    // Helper to fire a priority POST and wire its reply to onPriorityReplyFinished.
    void postPriorityRequest(const QNetworkRequest &req, const QByteArray &body, bool isRetry);

    // Returns the active network manager — either the test-injected one or
    // the internal m_nam. All HTTP posts MUST go through this accessor.
    QNetworkAccessManager *nam();

    DataStore *m_store;
    TaskActionFn m_completeTaskFn;  // optional VM routing for complete_task (see setTaskActionHandlers)
    TaskActionFn m_deleteTaskFn;    // optional VM routing for delete_task
    QPointer<SecretStore> m_secretStore;  // Sec-1: nullable; legacy QSettings path when unset
    QNetworkAccessManager m_nam;
    QPointer<QNetworkAccessManager> m_namOverride;  // test hook

    // Providers are owned in registration order; m_currentProviderId picks
    // which one is active. Order also determines the dropdown order in UI.
    std::vector<std::unique_ptr<LlmProvider>> m_providers;
    QString m_currentProviderId;

    bool m_isResponding = false;
    bool m_isGeneratingSubtasks = false;
    bool m_subtaskCancelled = false;  // set by cancelSubtaskGeneration()
    bool m_isGeneratingDescription = false;
    bool m_isSuggestingPriority = false;

    // S-Q2.20: cancellation flags for the three flows that lacked them. The
    // reply handlers consult these before emitting result/error signals.
    bool m_chatCancelled        = false;
    bool m_descriptionCancelled = false;
    bool m_priorityCancelled    = false;

    // B-5.26 / S-Q2.20: QPointer so cancel*() can safely abort the HTTP request
    // even after the QNetworkReply has been schedule-deleted by `finished`.
    QPointer<QNetworkReply> m_subtaskReply;
    QPointer<QNetworkReply> m_chatReply;
    QPointer<QNetworkReply> m_descriptionReply;
    QPointer<QNetworkReply> m_priorityReply;
    QPointer<QNetworkReply> m_testReply;
    bool m_isTestingConnection = false;

    QList<QPair<QString, QString>> m_history;  // (role, text) pairs
    static constexpr int MAX_HISTORY = 20;
};
