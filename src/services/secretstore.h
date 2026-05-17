#pragma once

// Sec-1: SecretStore — unified secret persistence backed by the native OS
// keyring (Windows Credential Manager / macOS Keychain / libsecret / KWallet
// via QtKeychain) on desktop, and by QSettings on Android / WASM where no
// real OS keyring is available.
//
// The Claude API key used to be stored XOR-obfuscated under "ai/k" in
// QSettings, which on Windows resolves to the user's registry hive — readable
// in plaintext by any process running as the user (the XOR layer is
// security-by-obscurity, not real protection). Migrating to the OS keyring
// gives us per-process ACLs on Windows and the equivalent on macOS / Linux.
//
// API shape:
//   - setSecret(key, value)            async write; secretStored or secretError
//   - getSecret(key)                   async read;  secretRetrieved or secretError
//   - deleteSecret(key)                async delete; secretDeleted or secretError
//   - cachedSecret(key)                synchronous best-effort read from cache
//   - hasCachedSecret(key)             true iff a value is currently cached
//
// The in-memory cache means callers that need a value synchronously (e.g.
// AIAssistant::loadApiKey() from inside a request handler) can rely on the
// cache after a successful prior write or warm-up read. The cache is populated
// by every successful setSecret / getSecret call.

#include <QObject>
#include <QString>
#include <QHash>

class QSettings;

class SecretStore : public QObject
{
    Q_OBJECT
public:
    explicit SecretStore(QObject *parent = nullptr);
    ~SecretStore() override;

    // Returns true if this build uses the real OS keyring (QtKeychain),
    // false if it falls back to QSettings. Callers should rarely need to
    // branch on this — it's exposed primarily for diagnostic logging.
    static bool isKeyringAvailable();

    // Synchronous best-effort accessors that read from the in-memory cache.
    // cachedSecret() returns an empty string if the value is not cached.
    // hasCachedSecret() distinguishes "cached as empty" from "not cached"
    // — useful for the AIAssistant migration path where an empty key means
    // "user explicitly cleared it" and should not trigger a re-read.
    QString cachedSecret(const QString &key) const;
    bool hasCachedSecret(const QString &key) const;

    // Async API. All three emit exactly one of their result signals
    // (signal naming below) on completion. Multiple in-flight jobs for
    // the same key are not serialised — callers that need a strict order
    // should chain via the completion signals.
public slots:
    void setSecret(const QString &key, const QString &value);
    void getSecret(const QString &key);
    void deleteSecret(const QString &key);

signals:
    void secretStored(const QString &key);
    void secretRetrieved(const QString &key, const QString &value);
    void secretDeleted(const QString &key);
    void secretError(const QString &key, const QString &reason);

private:
    // The "service name" passed to QtKeychain. QtKeychain uses this string as
    // the namespace under which entries appear in Credential Manager / Keychain;
    // a stable name across versions matters because changing it would orphan
    // existing entries.
    static QString serviceName();

    // In-memory cache. Populated by successful writes and reads. The empty
    // QString sentinel distinguishes "explicit empty value" from "not in
    // cache" — we use the parallel m_present set for the latter.
    mutable QHash<QString, QString> m_cache;
    mutable QHash<QString, bool>    m_present;

#ifndef HAS_QTKEYCHAIN
    // QSettings fallback group. Values are stored as plaintext — this path is
    // only used on Android / WASM where there is no OS keyring to target.
    // Storing under a distinct group ("secrets/") keeps the keys out of
    // the main settings namespace.
    static const char *kFallbackGroup;
#endif
};
