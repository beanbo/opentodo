#pragma once

#include <QObject>
#include <QQmlEngine>

class DataStore;
class GDriveClient;

class SnapshotManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit SnapshotManager(DataStore *store, QObject *parent = nullptr);

    bool shouldCompact() const;
    void createSnapshot();
    int commitCount() const;

    void setGDriveClient(GDriveClient *client);
    void setCompactionThreshold(int threshold);
    // B-2.9: must be kept current so createSnapshot only purges ancestral logs.
    void setHeadCommitId(const QString &headId);

signals:
    void snapshotCreated();
    void snapshotFailed(const QString &error);

private:
    DataStore *m_store;
    GDriveClient *m_gdrive = nullptr;
    int m_compactionThreshold = 100;
    int m_commitCount = 0;
    QString m_headCommitId; // B-2.9: tracks last confirmed uploaded commit

    void incrementCommitCount();
    void resetCommitCount();

    friend class SyncEngine;
};
