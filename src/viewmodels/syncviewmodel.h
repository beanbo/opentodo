#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QQmlEngine>

class SyncEngine;
class Telemetry;

class SyncViewModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("SyncViewModel is constructed in C++ and exposed via context property.")

    Q_PROPERTY(QString syncStatus READ syncStatus NOTIFY syncStatusChanged)
    Q_PROPERTY(QDateTime lastSyncTime READ lastSyncTime NOTIFY lastSyncTimeChanged)
    Q_PROPERTY(int pendingCommits READ pendingCommits NOTIFY pendingCommitsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool isSyncing READ isSyncing NOTIFY syncStatusChanged)

public:
    explicit SyncViewModel(SyncEngine *engine, QObject *parent = nullptr);

    QString syncStatus() const;
    QDateTime lastSyncTime() const;
    int pendingCommits() const;
    QString lastError() const;
    bool isSyncing() const;

    // Inject optional telemetry service. Wired in main.cpp.
    void setTelemetry(Telemetry *telemetry);

    Q_INVOKABLE void sync();
    Q_INVOKABLE void push();
    Q_INVOKABLE void pull();
    Q_INVOKABLE void setSyncInterval(int seconds);
    Q_INVOKABLE QString formatLastSyncTime() const;

signals:
    void syncStatusChanged();
    void lastSyncTimeChanged();
    void pendingCommitsChanged();
    void lastErrorChanged();
    void syncStarted();
    void syncCompleted();
    void syncError(const QString &message);

private slots:
    void onStatusChanged();
    void onSyncCompleted();
    void onSyncFailed(const QString &error);

private:
    SyncEngine *m_engine;
    Telemetry *m_telemetry = nullptr;
};
