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
    // existing rolling-backup file (.bak / .bak.1 / .bak.2), most-recent first.
    Q_INVOKABLE QVariantList listBackups() const;

    // Restores state from the given backup path. Flushes current state through
    // the normal rotation chain first so the user can undo. Returns true on
    // success; false if the path is missing or contains invalid JSON.
    Q_INVOKABLE bool restoreBackup(const QString &backupPath);

signals:
    void loaded();
    void saved();
    void dirtyChanged();
    void loadError(const QString &error);
    void saveError(const QString &error);

private slots:
    void onDataChanged();
    void performSave();

private:
    void ensureDataDirectory();
    void createDefaultState();
    QString stateFilePath() const;
    QString localLogPath() const;

    DataStore *m_store;
    QTimer m_saveTimer;
    bool m_dirty = false;
    QString m_dataPath;
};
