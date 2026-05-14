#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QQmlEngine>

struct SyncOperation {
    QString type;       // e.g. "task_add", "task_update", "task_delete", "project_add", etc.
    QString entityId;
    QJsonObject data;   // full entity data for add, changed fields for update, empty for delete

    QJsonObject toJson() const;
    static SyncOperation fromJson(const QJsonObject &json);
};

class Commit : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString uuid READ uuid WRITE setUuid NOTIFY uuidChanged)
    Q_PROPERTY(QDateTime timestamp READ timestamp WRITE setTimestamp NOTIFY timestampChanged)
    Q_PROPERTY(QString deviceId READ deviceId WRITE setDeviceId NOTIFY deviceIdChanged)
    Q_PROPERTY(QString parentCommit READ parentCommit WRITE setParentCommit NOTIFY parentCommitChanged)
    Q_PROPERTY(QString hlc READ hlc WRITE setHlc NOTIFY hlcChanged)

public:
    explicit Commit(QObject *parent = nullptr);

    QString uuid() const;
    void setUuid(const QString &uuid);

    QDateTime timestamp() const;
    void setTimestamp(const QDateTime &ts);

    QString deviceId() const;
    void setDeviceId(const QString &deviceId);

    QString parentCommit() const;
    void setParentCommit(const QString &parent);

    QString hlc() const;
    void setHlc(const QString &hlc);

    QList<SyncOperation> operations() const;
    void setOperations(const QList<SyncOperation> &ops);
    void addOperation(const SyncOperation &op);

    Q_INVOKABLE QJsonObject toJson() const;
    static Commit *fromJson(const QJsonObject &json, QObject *parent = nullptr);

signals:
    void uuidChanged();
    void timestampChanged();
    void deviceIdChanged();
    void parentCommitChanged();
    void hlcChanged();

private:
    QString m_uuid;
    QDateTime m_timestamp;
    QString m_deviceId;
    QString m_parentCommit;
    QString m_hlc;
    QList<SyncOperation> m_operations;
};
