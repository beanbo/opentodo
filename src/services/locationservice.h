#pragma once
#include <QObject>
#include <QString>
#include <QGeoCoordinate>
#include <QMap>
#include <QStringList>

class QGeoPositionInfoSource;

struct Geofence {
    QString reminderId;
    QGeoCoordinate center;
    double radiusMeters = 200.0;
    bool triggered = false;
};

class LocationService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
    Q_PROPERTY(bool hasPermission READ hasPermission NOTIFY permissionChanged)
public:
    explicit LocationService(QObject *parent = nullptr);

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    // Adds or replaces the fence for `reminderId`. Re-adding an unchanged
    // fence (same center + radius) preserves its inside/outside state.
    Q_INVOKABLE void addGeofence(const QString &reminderId, double lat, double lon, double radius = 200.0);
    Q_INVOKABLE void removeGeofence(const QString &reminderId);
    Q_INVOKABLE void clearGeofences();
    QStringList geofenceIds() const;

    // QML helper: returns the most recent valid position from the OS, or an
    // invalid coordinate if the source has not produced one yet. Used by the
    // reminder dialog's "Use my current location" button.
    Q_INVOKABLE QGeoCoordinate lastKnownPosition() const;
    Q_INVOKABLE bool hasLastKnownPosition() const { return m_lastPosition.isValid(); }

    bool isActive() const;
    bool hasPermission() const;

signals:
    void enteredGeofence(const QString &reminderId);
    void leftGeofence(const QString &reminderId);
    void activeChanged();
    void permissionChanged();
    void locationError(const QString &error);
    // Wave 3 follow-up: emitted on every valid position update so QML can
    // observe live coordinates (e.g. EditorRemindersSection's "use current
    // location" button populates lat/lon without polling).
    void positionUpdated(const QGeoCoordinate &coordinate);

private slots:
    void onPositionUpdated(const QGeoPositionInfo &info);
    void onError(QGeoPositionInfoSource::Error error);

private:
    void checkGeofences(const QGeoCoordinate &position);

    // F-5.18: evaluate all geofences against an initial position on startup.
    void evaluateGeofencesOnStart(const QGeoCoordinate &position);

    QGeoPositionInfoSource *m_source = nullptr;
    QMap<QString, Geofence> m_geofences;
    QGeoCoordinate m_lastPosition;
    bool m_active = false;

    // F-5.18: track whether the first valid position has been processed.
    bool m_startupEvaluationDone = false;

    // B-5.19: track whether we had permission before the last update.
    bool m_hasPermission = false;
};
