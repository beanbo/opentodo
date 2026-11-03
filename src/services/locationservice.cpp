#include "locationservice.h"

#include <QGeoPositionInfoSource>
#include <QGeoPositionInfo>

LocationService::LocationService(QObject *parent)
    : QObject(parent)
{
}

void LocationService::start()
{
    if (m_active)
        return;

    if (!m_source) {
        m_source = QGeoPositionInfoSource::createDefaultSource(this);
        if (!m_source) {
            emit locationError(QStringLiteral("No position source available on this platform"));
            return;
        }

        connect(m_source, &QGeoPositionInfoSource::positionUpdated,
                this, &LocationService::onPositionUpdated);
        connect(m_source, &QGeoPositionInfoSource::errorOccurred,
                this, &LocationService::onError);

        // Request updates every 30 seconds
        m_source->setUpdateInterval(30000);
    }

    // F-5.18: reset the startup flag so the first valid position triggers
    // geofence evaluation for all currently-registered fences.
    m_startupEvaluationDone = false;

    m_source->startUpdates();
    m_active = true;
    emit activeChanged();
}

void LocationService::stop()
{
    if (!m_active)
        return;

    if (m_source)
        m_source->stopUpdates();

    m_active = false;
    emit activeChanged();
}

void LocationService::addGeofence(const QString &reminderId, double lat, double lon, double radius)
{
    Geofence fence;
    fence.reminderId = reminderId;
    fence.center = QGeoCoordinate(lat, lon);
    fence.radiusMeters = radius;
    fence.triggered = false;
    // Re-registering an UNCHANGED fence keeps its inside/outside state: the
    // fences are reloaded on every remindersChanged, and resetting
    // `triggered` made the next position update fire "entered" again for a
    // place the user never left (duplicate alerts). A moved / resized fence
    // starts fresh.
    const auto existing = m_geofences.constFind(reminderId);
    if (existing != m_geofences.constEnd()
        && existing->center == fence.center
        && qFuzzyCompare(existing->radiusMeters + 1.0, radius + 1.0)) {
        fence.triggered = existing->triggered;
    }
    m_geofences.insert(reminderId, fence);
}

QStringList LocationService::geofenceIds() const
{
    return m_geofences.keys();
}

void LocationService::removeGeofence(const QString &reminderId)
{
    m_geofences.remove(reminderId);
}

void LocationService::clearGeofences()
{
    m_geofences.clear();
}

bool LocationService::isActive() const
{
    return m_active;
}

bool LocationService::hasPermission() const
{
    if (!m_source)
        return false;
    // A null or None error state combined with a valid source generally
    // indicates we have access. Platforms that deny permission surface it
    // via QGeoPositionInfoSource::AccessError.
    return m_source->error() != QGeoPositionInfoSource::AccessError;
}

void LocationService::onPositionUpdated(const QGeoPositionInfo &info)
{
    if (!info.coordinate().isValid())
        return;

    const QGeoCoordinate pos = info.coordinate();

    // B-5.19: Emit permissionChanged() on the first successful fix if we
    // previously had no permission (e.g. the OS granted it after start()).
    if (!m_hasPermission) {
        m_hasPermission = true;
        emit permissionChanged();
    }

    m_lastPosition = pos;

    // Wave 3 follow-up: notify QML of every valid update so observers can
    // populate coordinate-bearing UI without polling lastKnownPosition().
    emit positionUpdated(pos);

    // F-5.18: On the very first valid position after start(), evaluate every
    // registered geofence and fire enteredGeofence for any fence we are
    // currently inside. This catches the "already inside at startup" case
    // that the transition-based checkGeofences() would otherwise miss.
    if (!m_startupEvaluationDone) {
        m_startupEvaluationDone = true;
        evaluateGeofencesOnStart(pos);
    } else {
        checkGeofences(pos);
    }
}

QGeoCoordinate LocationService::lastKnownPosition() const
{
    return m_lastPosition;
}

void LocationService::onError(QGeoPositionInfoSource::Error error)
{
    switch (error) {
    case QGeoPositionInfoSource::AccessError:
        // B-5.19: update the permission flag and notify.
        if (m_hasPermission) {
            m_hasPermission = false;
            emit permissionChanged();
        }
        emit locationError(QStringLiteral("Location permission denied"));
        break;
    case QGeoPositionInfoSource::ClosedError:
        emit locationError(QStringLiteral("Location source closed unexpectedly"));
        break;
    case QGeoPositionInfoSource::UnknownSourceError:
        emit locationError(QStringLiteral("Unknown location source error"));
        break;
    case QGeoPositionInfoSource::UpdateTimeoutError:
        emit locationError(QStringLiteral("Location update timed out"));
        break;
    case QGeoPositionInfoSource::NoError:
    default:
        break;
    }
}

// F-5.18: Startup evaluation — fires enteredGeofence for every fence the device
// is currently inside, without relying on a transition from outside→inside.
// The `triggered` flag is set so subsequent regular checkGeofences() calls work
// correctly (they only fire on transitions relative to the stored flag).
void LocationService::evaluateGeofencesOnStart(const QGeoCoordinate &position)
{
    for (auto it = m_geofences.begin(); it != m_geofences.end(); ++it) {
        Geofence &fence = it.value();
        const double dist = position.distanceTo(fence.center);
        const bool inside = dist <= fence.radiusMeters;

        fence.triggered = inside;   // prime the transition state
        if (inside)
            emit enteredGeofence(fence.reminderId);
    }
}

void LocationService::checkGeofences(const QGeoCoordinate &position)
{
    for (auto it = m_geofences.begin(); it != m_geofences.end(); ++it) {
        Geofence &fence = it.value();
        double distanceMeters = position.distanceTo(fence.center);
        bool inside = distanceMeters <= fence.radiusMeters;

        if (inside && !fence.triggered) {
            fence.triggered = true;
            emit enteredGeofence(fence.reminderId);
        } else if (!inside && fence.triggered) {
            fence.triggered = false;
            emit leftGeofence(fence.reminderId);
        }
    }
}
