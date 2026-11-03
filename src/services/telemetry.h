#pragma once

#include <QObject>
#include <QString>
#include <QQmlEngine>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QQueue>
#include <QTimer>
#include <QJsonObject>
#include <QVariantMap>

class QNetworkReply;

class Telemetry : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use the telemetry context property.")

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool prompted READ prompted CONSTANT)
    Q_PROPERTY(int pendingEventCount READ pendingEventCount NOTIFY pendingEventCountChanged)
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit Telemetry(QObject *parent = nullptr);
    ~Telemetry() override;

    bool enabled() const;
    void setEnabled(bool e);
    bool prompted() const;
    // True only when this build has a collector endpoint configured; the
    // Settings toggle is hidden otherwise.
    bool available() const;

    // Diagnostics: number of events currently waiting in the in-memory queue.
    // Includes events that were just persisted to disk but not yet flushed.
    Q_INVOKABLE int pendingEventCount() const;

    Q_INVOKABLE void trackEvent(const QString &name, const QVariantMap &props = {});
    Q_INVOKABLE void markPrompted();

    // S-5.34: Reset the anonymous ID (allows user opt-out of continuity tracking).
    Q_INVOKABLE void resetAnonymousId();

    // Q3.09: Public hooks for the durability layer.
    // Returns the absolute path of the on-disk queue file (empty if AppData lookup fails).
    QString queueFilePath() const;
    // Test hook: force the queue to be persisted right now (skip the 1 s debounce).
    void persistQueueImmediate();

signals:
    void enabledChanged();
    void pendingEventCountChanged();

private:
    // B-5.36: flush with a deadline — called from both the timer and aboutToQuit.
    void flush(bool synchronous = false);
    QString anonymousId() const;

    // Q3.09: durability + safety helpers.
    //   loadQueueFromDisk()       — startup: pull telemetry_queue.json into m_queue
    //   schedulePersistQueue()    — mark dirty and arm the 1 s debounce timer
    //   writeQueueToDisk()        — atomic QSaveFile write of m_queue snapshot
    //   deleteQueueFile()         — remove telemetry_queue.json (on disable/empty)
    //   ensureQueueDirectory()    — mkpath the parent dir
    //   isRateLimited()           — sliding 60s / 60-event window check + record
    void loadQueueFromDisk();
    void schedulePersistQueue();
    void writeQueueToDisk();
    void deleteQueueFile();
    void ensureQueueDirectory() const;
    bool isRateLimited();
    void clearInFlightAndQueue();

    QNetworkAccessManager m_nam;
    QQueue<QJsonObject> m_queue;
    QTimer m_flushTimer;

    // Q3.09: disk-persistence machinery (mirrors FileManager 1 s debounce + QSaveFile).
    QTimer m_persistTimer;
    bool m_queueDirty = false;

    // Q3.09: rate-limit sliding window. Each entry is a ms-since-epoch stamp of
    // an accepted trackEvent call. Older entries are evicted in isRateLimited().
    QQueue<qint64> m_rateWindow;

    // Q3.09: track every in-flight POST so setEnabled(false) can abort them.
    // QPointer guards against early deletion (deleteLater fires before we
    // touch the pointer again on disable).
    QList<QPointer<QNetworkReply>> m_inFlight;
};
