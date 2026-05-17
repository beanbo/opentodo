#pragma once

#include <QObject>
#include <QTimer>
#include <QString>
#include <QQmlEngine>

class DataStore;

class FileManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString dataPath READ dataPath CONSTANT)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)

public:
    explicit FileManager(DataStore *store, QObject *parent = nullptr);

    QString dataPath() const;
    bool isDirty() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void saveImmediate();
    Q_INVOKABLE QString exportData() const;
    Q_INVOKABLE bool importData(const QString &jsonString);

    // Wave 3 follow-up: write the full exportData() string to a file (accepts
    // either a plain path or a "file:///..." URL). Used by SettingsSectionBackup
    // to replace the synchronous-XHR PUT path that breaks under WASM.
    Q_INVOKABLE bool exportTo(const QString &filePathOrUrl) const;

    // Backup history viewer (I2-B / I6-C follow-up).
    // Returns a QVariantList of maps {name, path, size, mtime} for each
    // existing backup file (.bak / .bak.1 / .bak.2 plus dated-rotation
    // backups like .bak.YYYYMMDDhhmmss), sorted by mtime (most-recent first).
    Q_INVOKABLE QVariantList listBackups() const;

    // Restores state from the given backup path. Flushes current state through
    // the normal rotation chain first so the user can undo. Returns true on
    // success; false if the path is missing or contains invalid JSON.
    Q_INVOKABLE bool restoreBackup(const QString &backupPath);

    // Backup retention policy (Q3.10 follow-up):
    //   * count cap: never more than `retentionCount` dated backups
    //     (the 3-generation rolling .bak / .bak.1 / .bak.2 chain is always
    //     preserved for the historical recovery path).
    //   * age cap : dated backups older than `retentionDays` are removed,
    //     EXCEPT the single most-recent dated backup is always preserved
    //     so the user is never left with zero recovery options.
    // Defaults: 50 backups, 30 days. Sweep runs automatically after every
    // successful save; can also be invoked manually from QML.
    Q_INVOKABLE int sweepOldBackups();

    Q_PROPERTY(int backupRetentionCount READ backupRetentionCount
               WRITE setBackupRetentionCount NOTIFY backupRetentionCountChanged)
    Q_PROPERTY(int backupRetentionDays  READ backupRetentionDays
               WRITE setBackupRetentionDays  NOTIFY backupRetentionDaysChanged)

    int backupRetentionCount() const { return m_backupRetentionCount; }
    void setBackupRetentionCount(int count);

    int backupRetentionDays() const { return m_backupRetentionDays; }
    void setBackupRetentionDays(int days);

    // Vacuum-related (Q3.10 follow-up): when state.json exceeds this size on
    // disk after a save, FileManager emits vacuumRecommended() so the UI can
    // prompt the user. Threshold defaults to 10 MB.
    Q_PROPERTY(qint64 vacuumThresholdBytes READ vacuumThresholdBytes
               WRITE setVacuumThresholdBytes NOTIFY vacuumThresholdBytesChanged)

    qint64 vacuumThresholdBytes() const { return m_vacuumThresholdBytes; }
    void setVacuumThresholdBytes(qint64 bytes);

    // One-shot safety backup written immediately before a vacuum runs.
    // Distinct from the rolling .bak chain — lives at state.json.pre-vacuum.bak
    // and is preserved across subsequent saves (rotation never touches it).
    // Returns the absolute path on success, empty string on failure.
    Q_INVOKABLE QString createPreVacuumBackup();

signals:
    void loaded();
    void saved();
    void dirtyChanged();
    void loadError(const QString &error);
    void saveError(const QString &error);
    // Q3.10: emitted by performSave() when the freshly-written state.json
    // exceeds vacuumThresholdBytes. The UI may prompt the user to vacuum.
    // Carries the current size so the message can say something useful.
    void vacuumRecommended(qint64 stateSizeBytes);
    void backupRetentionCountChanged();
    void backupRetentionDaysChanged();
    void vacuumThresholdBytesChanged();

private slots:
    void onDataChanged();
    void performSave();

private:
    void ensureDataDirectory();
    void createDefaultState();
    QString stateFilePath() const;
    QString localLogPath() const;

    // Internal helpers for backup pattern matching used by sweepOldBackups()
    // and listBackups(). Backups come in two flavors:
    //   * rolling: state.json.bak, .bak.1, .bak.2 (3-gen LRU chain)
    //   * dated  : state.json.bak.YYYYMMDDhhmmss (one per save, retained
    //              under retention policy until count/age cap hits)
    QStringList allBackupPaths() const;
    QStringList datedBackupPaths() const;
    void writeDatedBackup();   // copy state.json → dated backup, no rotation

    DataStore *m_store;
    QTimer m_saveTimer;
    bool m_dirty = false;
    QString m_dataPath;

    // Q3.10: backup retention policy (count + age cap).
    int    m_backupRetentionCount  = 50;
    int    m_backupRetentionDays   = 30;
    qint64 m_vacuumThresholdBytes  = 10LL * 1024 * 1024;   // 10 MB
};
