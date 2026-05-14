#include "syncviewmodel.h"
#include "sync/syncengine.h"
#include "sync/gdrive.h"
#include "services/telemetry.h"
#include <QTimer>

SyncViewModel::SyncViewModel(SyncEngine *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
    auto *uiRefreshTimer = new QTimer(this);
    uiRefreshTimer->setInterval(60000);   // every minute
    connect(uiRefreshTimer, &QTimer::timeout, this, [this]() {
        if (m_engine && m_engine->lastSyncTime().isValid())
            emit lastSyncTimeChanged();
    });
    uiRefreshTimer->start();

    connect(m_engine, &SyncEngine::statusChanged, this, &SyncViewModel::onStatusChanged);
    connect(m_engine, &SyncEngine::lastSyncTimeChanged, this, &SyncViewModel::lastSyncTimeChanged);
    connect(m_engine, &SyncEngine::syncCompleted, this, &SyncViewModel::onSyncCompleted);
    connect(m_engine, &SyncEngine::syncFailed, this, &SyncViewModel::onSyncFailed);
    connect(m_engine, &SyncEngine::lastErrorChanged, this, &SyncViewModel::lastErrorChanged);
    connect(m_engine, &SyncEngine::commitCreated, this, [this]() { emit pendingCommitsChanged(); });

    // Bridge GDriveClient::authenticationFailed → syncError so the global toast
    // surfaces OAuth errors. All other GDrive state (authenticated, accountEmail)
    // is now read directly from the gdriveClient context property in QML, so we
    // no longer need wrapper Q_PROPERTYs that bridged authenticatedChanged etc.
    if (auto *gdrive = m_engine->gdriveClient()) {
        connect(gdrive, &GDriveClient::authenticationFailed,
                this, [this](const QString &err) {
            emit syncError(err);
            emit lastErrorChanged();
        });
    }
}

void SyncViewModel::setTelemetry(Telemetry *telemetry)
{
    m_telemetry = telemetry;
}

QString SyncViewModel::syncStatus() const
{
    switch (m_engine->status()) {
    case SyncEngine::Synced: return QStringLiteral("synced");
    case SyncEngine::Syncing: return QStringLiteral("syncing");
    case SyncEngine::Error: return QStringLiteral("error");
    case SyncEngine::Offline: return QStringLiteral("offline");
    }
    return QStringLiteral("offline");
}

QDateTime SyncViewModel::lastSyncTime() const
{
    return m_engine->lastSyncTime();
}

int SyncViewModel::pendingCommits() const
{
    return m_engine->pendingCommits().size();
}

QString SyncViewModel::lastError() const
{
    return m_engine->lastError();
}

bool SyncViewModel::isSyncing() const
{
    return m_engine->status() == SyncEngine::Syncing;
}

void SyncViewModel::sync()
{
    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("sync_full"), {});
    emit syncStarted();
    m_engine->sync();
}

void SyncViewModel::push()
{
    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("sync_push"), {});
    m_engine->push();
}

void SyncViewModel::pull()
{
    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("sync_pull"), {});
    m_engine->pull();
}

void SyncViewModel::setSyncInterval(int seconds)
{
    m_engine->setSyncInterval(seconds);
}

static QString _pluralMinutes(int n) {
    const int m100 = n % 100;
    const int m10  = n % 10;
    if (m100 >= 11 && m100 <= 14) return QStringLiteral("минут");
    if (m10 == 1)                  return QStringLiteral("минуту");
    if (m10 >= 2 && m10 <= 4)     return QStringLiteral("минуты");
    return QStringLiteral("минут");
}

static QString _pluralHours(int n) {
    const int m100 = n % 100;
    const int m10  = n % 10;
    if (m100 >= 11 && m100 <= 14) return QStringLiteral("часов");
    if (m10 == 1)                  return QStringLiteral("час");
    if (m10 >= 2 && m10 <= 4)     return QStringLiteral("часа");
    return QStringLiteral("часов");
}

QString SyncViewModel::formatLastSyncTime() const
{
    const QDateTime dt = lastSyncTime();
    if (!dt.isValid())
        return QStringLiteral("Никогда");

    const QDateTime now = QDateTime::currentDateTime();
    const qint64 secs = dt.secsTo(now);
    if (secs < 0)
        return dt.toString(QStringLiteral("dd.MM HH:mm"));   // future timestamp — fallback
    if (secs < 60)
        return QStringLiteral("Только что");
    if (secs < 3600) {
        const int mins = static_cast<int>(secs / 60);
        return QStringLiteral("%1 %2 назад").arg(mins).arg(_pluralMinutes(mins));
    }
    if (secs < 24 * 3600) {
        const int hrs = static_cast<int>(secs / 3600);
        return QStringLiteral("%1 %2 назад").arg(hrs).arg(_pluralHours(hrs));
    }

    // Yesterday vs older
    const QDate today = QDate::currentDate();
    const QDate dtDate = dt.date();
    if (dtDate == today.addDays(-1))
        return QStringLiteral("Вчера в %1").arg(dt.toString(QStringLiteral("HH:mm")));

    return dt.toString(QStringLiteral("dd.MM в HH:mm"));
}

void SyncViewModel::onStatusChanged()
{
    emit syncStatusChanged();
}

void SyncViewModel::onSyncCompleted()
{
    emit pendingCommitsChanged();
    emit syncCompleted();
}

void SyncViewModel::onSyncFailed(const QString &error)
{
    emit syncError(error);
    emit syncStatusChanged();
    emit lastErrorChanged();
}
