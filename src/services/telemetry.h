#pragma once

#include <QObject>
#include <QString>
#include <QQmlEngine>
#include <QNetworkAccessManager>
#include <QQueue>
#include <QTimer>
#include <QJsonObject>
#include <QVariantMap>

class Telemetry : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use the telemetry context property.")

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool prompted READ prompted CONSTANT)

public:
    explicit Telemetry(QObject *parent = nullptr);

    bool enabled() const;
    void setEnabled(bool e);
    bool prompted() const;

    Q_INVOKABLE void trackEvent(const QString &name, const QVariantMap &props = {});
    Q_INVOKABLE void markPrompted();

    // S-5.34: Reset the anonymous ID (allows user opt-out of continuity tracking).
    Q_INVOKABLE void resetAnonymousId();

signals:
    void enabledChanged();

private:
    // B-5.36: flush with a deadline — called from both the timer and aboutToQuit.
    void flush(bool synchronous = false);
    QString anonymousId() const;

    QNetworkAccessManager m_nam;
    QQueue<QJsonObject> m_queue;
    QTimer m_flushTimer;
};
