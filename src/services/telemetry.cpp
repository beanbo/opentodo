#include "telemetry.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUuid>
#include <QVariant>

// S-5.37: If the endpoint is empty or contains "TODO", no HTTP requests are made.
// Replace this constant with the real endpoint before shipping.
static const QString TELEMETRY_ENDPOINT =
    QStringLiteral("https://telemetry.opentodo.app/track");

static constexpr int FLUSH_INTERVAL_MS    = 5 * 60 * 1000; // 5 minutes
static constexpr int QUIT_FLUSH_DEADLINE  = 2000;           // 2 s synchronous deadline on quit

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

    // B-5.36: On app quit, flush any pending events synchronously with a deadline.
    if (QCoreApplication::instance()) {
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                         this, [this]() { flush(/*synchronous=*/true); });
    }
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

void Telemetry::trackEvent(const QString &name, const QVariantMap &props)
{
    // Strictly no-op when telemetry is disabled — no data leaves the device.
    if (!enabled())
        return;

    // S-5.37: Skip all sends if the endpoint is a placeholder or empty.
    if (TELEMETRY_ENDPOINT.isEmpty() || TELEMETRY_ENDPOINT.contains(QStringLiteral("TODO"))) {
        qWarning("Telemetry: endpoint is not configured — event '%s' dropped.",
                 qPrintable(name));
        return;
    }

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

    m_queue.enqueue(event);
    m_flushTimer.start(); // restart the debounce window
}

void Telemetry::flush(bool synchronous)
{
    if (m_queue.isEmpty())
        return;

    // Collect all queued events into a JSON array for a single POST.
    QJsonArray batch;
    while (!m_queue.isEmpty())
        batch.append(m_queue.dequeue());

    const QByteArray body = QJsonDocument(batch).toJson(QJsonDocument::Compact);

    QNetworkRequest request{QUrl(TELEMETRY_ENDPOINT)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("User-Agent", "OpenTodo-Telemetry");

    QNetworkReply *reply = m_nam.post(request, body);

    if (synchronous) {
        // B-5.36: On quit, block up to QUIT_FLUSH_DEADLINE ms waiting for the reply
        // so in-flight events are not lost when the process exits.
        QDeadlineTimer deadline(QUIT_FLUSH_DEADLINE);
        while (!reply->isFinished() && !deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    QObject::connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
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
