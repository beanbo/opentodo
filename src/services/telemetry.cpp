#include "telemetry.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>

// S-5.37: If the endpoint is empty or contains "TODO", no HTTP requests are made.
// Open-source builds ship with NO collector: whoever distributes a build that
// should report usage sets the endpoint at configure time
// (-DOPENTODO_TELEMETRY_ENDPOINT=https://...) or at run time via the
// OPENTODO_TELEMETRY_ENDPOINT environment variable (the latter wins).
static QString telemetryEndpoint()
{
    const QString fromEnv = qEnvironmentVariable("OPENTODO_TELEMETRY_ENDPOINT").trimmed();
    if (!fromEnv.isEmpty())
        return fromEnv;
#ifdef OPENTODO_TELEMETRY_ENDPOINT
    return QStringLiteral(OPENTODO_TELEMETRY_ENDPOINT);
#else
    return QString();
#endif
}

static constexpr int FLUSH_INTERVAL_MS    = 5 * 60 * 1000; // 5 minutes
static constexpr int QUIT_FLUSH_DEADLINE  = 2000;           // 2 s synchronous deadline on quit
static constexpr int PERSIST_DEBOUNCE_MS  = 1000;           // 1 s — mirrors FileManager

// Q3.09: hard cap on the in-memory + on-disk queue. Reaching this limit drops
// the OLDEST event (FIFO) with a qWarning so the user can see something is
// wrong. Sized so a typical worst-case offline session (a couple of weeks of
// daily usage at a few events per session) fits comfortably under the cap.
static constexpr int MAX_QUEUE_SIZE       = 1000;

// Q3.09: rate-limit sliding window — at most RATE_LIMIT_EVENTS accepted
// trackEvent calls per RATE_LIMIT_WINDOW_MS. Excess calls are dropped
// SILENTLY (no qWarning — bursty event sources would otherwise spam the log
// just as hard as they would have spammed the endpoint).
static constexpr int RATE_LIMIT_EVENTS    = 60;
static constexpr int RATE_LIMIT_WINDOW_MS = 60 * 1000;

// S-5.35: Only these property keys are accepted in trackEvent props to prevent
// accidental PII leakage. Keys not in this list are silently rejected with a warning.
static const QStringList kAllowedPropKeys = {
    QStringLiteral("event_type"),
    QStringLiteral("duration_ms"),
    QStringLiteral("task_count"),
    QStringLiteral("project_count"),
    QStringLiteral("platform"),
    QStringLiteral("view"),
    QStringLiteral("priority"),
    QStringLiteral("label_count"),
    QStringLiteral("filter_used"),
    QStringLiteral("sync_success"),
    QStringLiteral("error_code"),
};

Telemetry::Telemetry(QObject *parent)
    : QObject(parent)
{
    m_flushTimer.setInterval(FLUSH_INTERVAL_MS);
    m_flushTimer.setSingleShot(true);
    QObject::connect(&m_flushTimer, &QTimer::timeout, this, [this]() { flush(); });

    // Q3.09: 1 s debounce for telemetry_queue.json writes — same shape as
    // FileManager's autosave (single-shot, restarted on every dirty mark).
    m_persistTimer.setInterval(PERSIST_DEBOUNCE_MS);
    m_persistTimer.setSingleShot(true);
    QObject::connect(&m_persistTimer, &QTimer::timeout,
                     this, &Telemetry::writeQueueToDisk);

    // Q3.09: rehydrate any events left over from a previous run (crash, kill,
    // ungraceful exit). If telemetry is still enabled and the endpoint is
    // valid the existing flush timer will pick them up on the next idle pass.
    loadQueueFromDisk();

    // B-5.36: On app quit, flush any pending events synchronously with a deadline.
    if (QCoreApplication::instance()) {
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                         this, [this]() {
            // Write whatever is queued to disk first — even if the network
            // flush below times out, the durable queue survives the exit.
            if (m_queueDirty)
                writeQueueToDisk();
            flush(/*synchronous=*/true);
        });
    }
}

Telemetry::~Telemetry()
{
    // iter-night-2: explicitly disconnect every in-flight reply's
    // `finished` lambda BEFORE the member subobjects start destructing.
    // QNetworkAccessManager is declared first in the header, so under
    // reverse-order destruction it's the LAST member to die — but its
    // destructor cancels pending replies which emit finished(), and our
    // lambdas reach back into m_queue / m_persistTimer / m_inFlight which
    // by then have already been destroyed. That use-after-destroy was
    // documented (and QSKIP'd) in test_telemetry_retry.cpp:287-294 but
    // never fixed in production code. Disconnecting here breaks the
    // signal chain while every member is still valid.
    for (auto &reply : m_inFlight) {
        if (reply)
            QObject::disconnect(reply, &QNetworkReply::finished, this, nullptr);
    }
    m_inFlight.clear();

    // Best-effort: if there are still dirty changes pending we persist them
    // here. We do NOT attempt a network flush from the destructor — the NAM
    // may already be tearing down.
    if (m_queueDirty)
        writeQueueToDisk();
}

bool Telemetry::enabled() const
{
    QSettings settings;
    // Default is false — telemetry is strictly opt-in; nothing is sent without explicit consent.
    return settings.value(QStringLiteral("telemetry/enabled"), false).toBool();
}

void Telemetry::setEnabled(bool e)
{
    if (e == enabled())
        return;
    QSettings settings;
    settings.setValue(QStringLiteral("telemetry/enabled"), e);

    // Q3.09: switching telemetry OFF must immediately stop the data flow:
    //   * abort any in-flight QNetworkReply (the last batch must not land)
    //   * clear the in-memory queue
    //   * delete telemetry_queue.json so a future re-enable does not silently
    //     replay events the user thought they had cancelled
    if (!e)
        clearInFlightAndQueue();

    emit enabledChanged();
}

bool Telemetry::prompted() const
{
    QSettings settings;
    return settings.value(QStringLiteral("telemetry/prompted"), false).toBool();
}

void Telemetry::markPrompted()
{
    QSettings settings;
    settings.setValue(QStringLiteral("telemetry/prompted"), true);
}

bool Telemetry::available() const
{
    const QString endpoint = telemetryEndpoint();
    return !endpoint.isEmpty() && !endpoint.contains(QStringLiteral("TODO"));
}

int Telemetry::pendingEventCount() const
{
    return m_queue.size();
}

void Telemetry::trackEvent(const QString &name, const QVariantMap &props)
{
    // Strictly no-op when telemetry is disabled — no data leaves the device.
    if (!enabled())
        return;

    // S-5.37: no endpoint configured → nothing is queued or sent. This is the
    // normal state of open-source builds, so it is silent.
    if (!available())
        return;

    // Q3.09: sliding-window rate limit — drop SILENTLY on overflow so a
    // bursty source can't fill the log just as hard as it would have filled
    // the network queue.
    if (isRateLimited())
        return;

    QJsonObject event;
    event[QStringLiteral("event")] = name;
    event[QStringLiteral("anonymous_id")] = anonymousId();
    event[QStringLiteral("version")] = QCoreApplication::applicationVersion();
    event[QStringLiteral("ts")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    // S-5.35: PII filter — only copy props whose keys are on the allowlist.
    if (!props.isEmpty()) {
        QJsonObject filtered;
        for (auto it = props.cbegin(); it != props.cend(); ++it) {
            if (kAllowedPropKeys.contains(it.key())) {
                filtered[it.key()] = QJsonValue::fromVariant(it.value());
            } else {
                qWarning("Telemetry: property key '%s' is not in the allowlist and was dropped.",
                         qPrintable(it.key()));
            }
        }
        if (!filtered.isEmpty())
            event[QStringLiteral("props")] = filtered;
    }

    // Q3.09: enforce queue cap by evicting the oldest entry. A loud qWarning
    // is intentional here — losing telemetry is a diagnostic-only problem,
    // but losing 1000+ events points at either an outage or a bug in the
    // emitter, and the user should see it in the log.
    while (m_queue.size() >= MAX_QUEUE_SIZE) {
        const QJsonObject dropped = m_queue.dequeue();
        qWarning("Telemetry: queue cap reached (%d); dropped oldest event '%s'.",
                 MAX_QUEUE_SIZE,
                 qPrintable(dropped.value(QStringLiteral("event")).toString()));
    }

    m_queue.enqueue(event);
    schedulePersistQueue();
    emit pendingEventCountChanged();
    m_flushTimer.start(); // restart the debounce window
}

void Telemetry::flush(bool synchronous)
{
    // A queue rehydrated from disk may predate the endpoint being removed;
    // without an endpoint it must stay put rather than POST to nowhere.
    if (m_queue.isEmpty() || !available())
        return;

    // Snapshot the queue contents we're about to upload so we can either
    // delete them from disk on success or restore them on failure (the
    // in-memory queue is drained below; if the POST fails we re-queue).
    QList<QJsonObject> snapshot;
    snapshot.reserve(m_queue.size());

    QJsonArray batch;
    while (!m_queue.isEmpty()) {
        const QJsonObject ev = m_queue.dequeue();
        snapshot.append(ev);
        batch.append(ev);
    }
    emit pendingEventCountChanged();

    const QByteArray body = QJsonDocument(batch).toJson(QJsonDocument::Compact);

    QNetworkRequest request{QUrl(telemetryEndpoint())};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("User-Agent", "OpenTodo-Telemetry");

    QNetworkReply *reply = m_nam.post(request, body);
    m_inFlight.append(QPointer<QNetworkReply>(reply));

    // Q3.09: on completion clear the on-disk queue if the upload succeeded,
    // otherwise re-queue the snapshot so the events aren't lost. We hand the
    // snapshot to the lambda by-value so it survives the local scope.
    QObject::connect(reply, &QNetworkReply::finished, this,
                     [this, reply, snapshot]() {
        const bool ok = (reply->error() == QNetworkReply::NoError);
        if (ok) {
            // Successful POST — drop telemetry_queue.json. If trackEvent
            // enqueued NEW events while the request was in flight, those
            // re-trigger schedulePersistQueue() in the next idle pass.
            if (m_queue.isEmpty()) {
                deleteQueueFile();
                m_queueDirty = false;
                m_persistTimer.stop();
            } else {
                schedulePersistQueue();
            }
        } else {
            // Network failed — restore the snapshot back to the head of the
            // queue so the next flush retries them. Respect the cap during
            // re-queue: if NEW events arrived in the meantime and pushed us
            // over MAX_QUEUE_SIZE, the front-of-queue (oldest) gets dropped.
            for (int i = snapshot.size() - 1; i >= 0; --i) {
                while (m_queue.size() >= MAX_QUEUE_SIZE)
                    m_queue.dequeue();
                m_queue.prepend(snapshot.at(i));
            }
            schedulePersistQueue();
            emit pendingEventCountChanged();
        }

        // Drop the QPointer to this reply from the in-flight list.
        m_inFlight.removeAll(QPointer<QNetworkReply>(reply));
        reply->deleteLater();
    });

    if (synchronous) {
        // B-5.36: On quit, block up to QUIT_FLUSH_DEADLINE ms waiting for the reply
        // so in-flight events are not lost when the process exits.
        QDeadlineTimer deadline(QUIT_FLUSH_DEADLINE);
        while (!reply->isFinished() && !deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
}

// S-5.34: Generate a random UUID once and store it — no machine fingerprinting.
// The ID is NOT derived from hardware; it is a random per-installation value.
// The user can reset tracking continuity by calling resetAnonymousId().
QString Telemetry::anonymousId() const
{
    QSettings settings;
    const QString cached = settings.value(QStringLiteral("telemetry/anonymous_id")).toString();
    if (!cached.isEmpty())
        return cached;

    // S-5.34: Use a fresh random UUID — no machineUniqueId() fingerprinting.
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    settings.setValue(QStringLiteral("telemetry/anonymous_id"), id);
    return id;
}

// S-5.34: Allow the user to reset their anonymous tracking ID.
// After a reset, the next event will get a new random UUID.
void Telemetry::resetAnonymousId()
{
    QSettings settings;
    settings.remove(QStringLiteral("telemetry/anonymous_id"));
}

// ─── Q3.09: durability + safety implementation ─────────────────────────────

QString Telemetry::queueFilePath() const
{
    // Storage root mirrors FileManager: $APPDATA/OpenTodo on desktop, $APPDATA
    // directly on Android.
    QString dir;
#ifdef Q_OS_ANDROID
    dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
    dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
          + QStringLiteral("/OpenTodo");
#endif
    if (dir.isEmpty())
        return QString();
    return dir + QStringLiteral("/telemetry_queue.json");
}

void Telemetry::ensureQueueDirectory() const
{
    const QString path = queueFilePath();
    if (path.isEmpty())
        return;
    const QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());
}

void Telemetry::schedulePersistQueue()
{
    m_queueDirty = true;
    m_persistTimer.start(); // single-shot, restarted on every dirty mark
}

void Telemetry::persistQueueImmediate()
{
    m_persistTimer.stop();
    if (m_queueDirty)
        writeQueueToDisk();
}

void Telemetry::writeQueueToDisk()
{
    m_queueDirty = false;

    const QString path = queueFilePath();
    if (path.isEmpty()) {
        qWarning("Telemetry: cannot resolve AppDataLocation; on-disk queue disabled.");
        return;
    }

    // Empty queue → remove the file rather than leaving an "[]" sitting around.
    if (m_queue.isEmpty()) {
        deleteQueueFile();
        return;
    }

    ensureQueueDirectory();

    QJsonArray arr;
    for (const QJsonObject &ev : m_queue)
        arr.append(ev);

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("Telemetry: failed to open queue file for write: %s",
                 qPrintable(out.errorString()));
        return;
    }
    out.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    if (!out.commit()) {
        qWarning("Telemetry: failed to commit queue file: %s",
                 qPrintable(out.errorString()));
    }
}

void Telemetry::deleteQueueFile()
{
    const QString path = queueFilePath();
    if (path.isEmpty())
        return;
    QFile::remove(path);
}

void Telemetry::loadQueueFromDisk()
{
    const QString path = queueFilePath();
    if (path.isEmpty())
        return;

    QFile in(path);
    if (!in.exists())
        return;
    if (!in.open(QIODevice::ReadOnly)) {
        qWarning("Telemetry: failed to open queue file for read: %s",
                 qPrintable(in.errorString()));
        return;
    }

    const QByteArray data = in.readAll();
    in.close();

    if (data.isEmpty())
        return;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        qWarning("Telemetry: queue file is corrupt (%s); discarding.",
                 qPrintable(err.errorString()));
        deleteQueueFile();
        return;
    }

    const QJsonArray arr = doc.array();
    int loaded = 0;
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;
        // Honour the cap on load too — a previous run with an older binary
        // might have grown the file beyond the limit.
        if (m_queue.size() >= MAX_QUEUE_SIZE) {
            qWarning("Telemetry: dropped queued event during load (cap %d reached).",
                     MAX_QUEUE_SIZE);
            break;
        }
        m_queue.enqueue(v.toObject());
        ++loaded;
    }
    if (loaded > 0) {
        emit pendingEventCountChanged();
        // Arm the flush timer so the rehydrated events get a chance to upload
        // without waiting for a fresh trackEvent call.
        m_flushTimer.start();
    }
}

bool Telemetry::isRateLimited()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 cutoff = now - RATE_LIMIT_WINDOW_MS;

    // Evict stamps that fell out of the sliding window.
    while (!m_rateWindow.isEmpty() && m_rateWindow.head() <= cutoff)
        m_rateWindow.dequeue();

    if (m_rateWindow.size() >= RATE_LIMIT_EVENTS)
        return true;

    m_rateWindow.enqueue(now);
    return false;
}

void Telemetry::clearInFlightAndQueue()
{
    // Abort any in-flight replies so the last batch never lands.
    for (auto &p : m_inFlight) {
        if (p)
            p->abort(); // finished() will fire from abort(); our handler re-queues then re-deletes
    }
    m_inFlight.clear();

    // After aborting the replies above their finished() lambda will run and
    // try to re-queue the snapshot. We're about to wipe the queue, but the
    // ordering inside Qt is: abort() emits finished() synchronously OR via
    // queued connection depending on the connection type. We used a direct
    // connect (default for same-thread), so finished() fires synchronously
    // inside abort() and the re-queue runs FIRST. We then wipe.
    m_queue.clear();
    m_rateWindow.clear();
    m_queueDirty = false;
    m_persistTimer.stop();
    m_flushTimer.stop();

    // Best effort: nuke the on-disk queue so a future re-enable does not pick
    // up events the user just opted out of.
    deleteQueueFile();

    emit pendingEventCountChanged();
}
