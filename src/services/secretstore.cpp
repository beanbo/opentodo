#include "secretstore.h"

#include <QCoreApplication>
#include <QSettings>

#ifdef HAS_QTKEYCHAIN
// FetchContent build: qtkeychain is consumed without installation so the
// header lives directly at the source-tree root rather than under
// <prefix>/include/qt6keychain/. CMake adds qtkeychain_SOURCE_DIR to our
// include path; the include is therefore just <keychain.h>.
#  include <keychain.h>
#endif

// Sec-1: SecretStore implementation.
//
// Two backends:
//   1. QtKeychain (desktop) — async jobs that hit the OS keyring. On Windows
//      this becomes a Credential Manager entry visible in `cmdkey /list` under
//      the service name returned by serviceName(). On macOS it's a generic
//      Keychain item; on Linux it goes via libsecret (gnome-keyring) or
//      kwallet depending on what the desktop session exposes via DBus.
//
//   2. QSettings fallback (Android, WASM) — plaintext under the "secrets/"
//      group. Documented as suboptimal in the header; we keep it because
//      these platforms either have no OS keyring (WASM) or require Java
//      bridge work (Android KeyStore) we haven't built yet.
//
// The cache is written *before* the async job in both cases so that an
// immediately-following read (synchronous via cachedSecret(), or async via
// getSecret()) sees the value. This is required to keep AIAssistant's
// existing API (which calls loadApiKey() inside request handlers) functional
// during the transition.

#ifndef HAS_QTKEYCHAIN
const char *SecretStore::kFallbackGroup = "secrets";
#endif

SecretStore::SecretStore(QObject *parent)
    : QObject(parent)
{
}

SecretStore::~SecretStore() = default;

bool SecretStore::isKeyringAvailable()
{
#ifdef HAS_QTKEYCHAIN
    return true;
#else
    return false;
#endif
}

QString SecretStore::serviceName()
{
    // Stable namespace string. Changing this orphans existing keyring entries,
    // so it must match across all versions of the app.
    return QStringLiteral("OpenTodo");
}

QString SecretStore::cachedSecret(const QString &key) const
{
    return m_cache.value(key);
}

bool SecretStore::hasCachedSecret(const QString &key) const
{
    return m_present.value(key, false);
}

void SecretStore::setSecret(const QString &key, const QString &value)
{
    // Write-through cache: subsequent cachedSecret() returns this immediately,
    // even if the async backend write is still pending. If the backend
    // ultimately fails we emit secretError so callers can decide whether to
    // surface the failure — but the cache stays populated so the in-process
    // experience continues to work for the rest of the session.
    m_cache[key]   = value;
    m_present[key] = true;
    ++m_writeGen[key];

#ifdef HAS_QTKEYCHAIN
    auto *job = new QKeychain::WritePasswordJob(serviceName(), this);
    job->setKey(key);
    job->setTextData(value);
    QObject::connect(job, &QKeychain::Job::finished, this,
                     [this, job, key]() {
        job->deleteLater();
        if (job->error() != QKeychain::NoError) {
            emit secretError(key, job->errorString());
            return;
        }
        emit secretStored(key);
    });
    job->start();
#else
    // Fallback: plaintext under secrets/<key>.
    QSettings s;
    s.beginGroup(QLatin1String(kFallbackGroup));
    s.setValue(key, value);
    s.endGroup();
    s.sync();
    // Emit synchronously — the fallback is itself synchronous. Wrap in a
    // queued connection idiom by using QMetaObject::invokeMethod with
    // Qt::QueuedConnection so callers can connect-then-call without
    // race conditions on signal handlers that haven't been wired yet.
    QMetaObject::invokeMethod(this, [this, key]() {
        emit secretStored(key);
    }, Qt::QueuedConnection);
#endif
}

void SecretStore::getSecret(const QString &key)
{
#ifdef HAS_QTKEYCHAIN
    auto *job = new QKeychain::ReadPasswordJob(serviceName(), this);
    job->setKey(key);
    const quint64 issuedGen = m_writeGen.value(key);
    QObject::connect(job, &QKeychain::Job::finished, this,
                     [this, job, key, issuedGen]() {
        job->deleteLater();
        // A set/delete for this key was issued after this read: its value is
        // newer than whatever the backend returned to us, so report that one
        // and keep the cache as the write left it.
        if (m_writeGen.value(key) != issuedGen) {
            emit secretRetrieved(key, m_cache.value(key));
            return;
        }
        if (job->error() != QKeychain::NoError) {
            // EntryNotFound is the normal "no key yet" state — emit a
            // retrieved-with-empty-value signal in that specific case so the
            // caller doesn't have to special-case missing entries. All other
            // errors (access denied, backend unavailable) are real errors.
            if (job->error() == QKeychain::EntryNotFound) {
                m_cache[key]   = QString();
                m_present[key] = true;
                emit secretRetrieved(key, QString());
                return;
            }
            emit secretError(key, job->errorString());
            return;
        }
        const QString val = job->textData();
        m_cache[key]   = val;
        m_present[key] = true;
        emit secretRetrieved(key, val);
    });
    job->start();
#else
    QSettings s;
    s.beginGroup(QLatin1String(kFallbackGroup));
    const QString val = s.value(key).toString();
    s.endGroup();
    m_cache[key]   = val;
    m_present[key] = true;
    QMetaObject::invokeMethod(this, [this, key, val]() {
        emit secretRetrieved(key, val);
    }, Qt::QueuedConnection);
#endif
}

void SecretStore::deleteSecret(const QString &key)
{
    m_cache.remove(key);
    m_present.remove(key);
    ++m_writeGen[key];

#ifdef HAS_QTKEYCHAIN
    auto *job = new QKeychain::DeletePasswordJob(serviceName(), this);
    job->setKey(key);
    QObject::connect(job, &QKeychain::Job::finished, this,
                     [this, job, key]() {
        job->deleteLater();
        if (job->error() != QKeychain::NoError) {
            // Same convention as getSecret: missing-entry is benign.
            if (job->error() == QKeychain::EntryNotFound) {
                emit secretDeleted(key);
                return;
            }
            emit secretError(key, job->errorString());
            return;
        }
        emit secretDeleted(key);
    });
    job->start();
#else
    QSettings s;
    s.beginGroup(QLatin1String(kFallbackGroup));
    s.remove(key);
    s.endGroup();
    s.sync();
    QMetaObject::invokeMethod(this, [this, key]() {
        emit secretDeleted(key);
    }, Qt::QueuedConnection);
#endif
}
