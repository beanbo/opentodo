#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QList>
#include <QPair>
#include <QQmlEngine>
#include <QTimer>

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

public:
    explicit AIAssistant(DataStore *store, QObject *parent = nullptr);

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

    // Sec-1: the key under which we stash the Claude API key in the
    // SecretStore. Exposed as a public constant so the migration code in
    // main.cpp can reference it without duplicating the literal.
    static const char *secretKey();

    bool isResponding() const { return m_isResponding; }
    bool isConfigured() const;  // true if API key is set
    bool isGeneratingSubtasks() const { return m_isGeneratingSubtasks; }
    bool isGeneratingDescription() const { return m_isGeneratingDescription; }
    bool isSuggestingPriority() const { return m_isSuggestingPriority; }

    Q_INVOKABLE void sendMessage(const QString &userMessage);
    Q_INVOKABLE void clearConversation();
    Q_INVOKABLE void setApiKey(const QString &key);  // saves to QSettings (XOR-obfuscated)

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
    void responseReceived(const QString &text);
    void errorReceived(const QString &error);
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

    QString loadApiKey() const;

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
    QPointer<SecretStore> m_secretStore;  // Sec-1: nullable; legacy QSettings path when unset
    QNetworkAccessManager m_nam;
    QPointer<QNetworkAccessManager> m_namOverride;  // test hook
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

    QList<QPair<QString, QString>> m_history;  // (role, text) pairs
    static constexpr int MAX_HISTORY = 20;
};
