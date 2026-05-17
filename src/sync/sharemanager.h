#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QQmlEngine>

class DataStore;
class GDriveClient;

/**
 * @brief Publishes a snapshot of a project / selection of tasks as a public
 *        Google Drive link.
 *
 * Layout on Drive: each share becomes a single JSON file at the user's Drive
 * root with anonymous reader permission set. The recipient opens the link in
 * a browser → Drive's built-in JSON viewer renders the snapshot. The same
 * file id can be passed to revokeShare() to take the link down again.
 *
 * Wire format matches TaskExporter::exportProjectToJson — the snapshot has
 * a "format" sentinel and contains the project, sections, tasks, and
 * comments. A separate "format" sentinel is used for selection-style
 * (multi-task) shares.
 *
 * Threading: lives on the main Qt event-loop thread. All Q_INVOKABLE methods
 * are SYNCHRONOUS — they block on QEventLoop for the underlying Drive HTTP
 * calls (same pattern as DriveApi). Emit signals at the end so QML
 * Connections fire.
 */
class ShareManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use the shareManager context property.")

public:
    ShareManager(DataStore *store, GDriveClient *drive, QObject *parent = nullptr);

    /**
     * @brief Publish a snapshot of `projectId` (plus its sections, tasks, and
     *        comments) as a public Drive link.
     *
     * Fires either shareLinkReady(url, fileId) on success or
     * shareError(reason) on failure. The fileId can be passed back to
     * revokeShare() to take the link down.
     */
    Q_INVOKABLE void shareProject(const QString &projectId);

    /**
     * @brief Publish an arbitrary selection of tasks (and their direct
     *        comments) as a public Drive link.
     */
    Q_INVOKABLE void shareTasks(const QStringList &taskIds);

    /**
     * @brief Revoke a previously-published share by deleting the underlying
     *        Drive file. Fires shareRevoked(fileId) on success or
     *        shareError on failure.
     */
    Q_INVOKABLE void revokeShare(const QString &fileId);

    /**
     * @brief Returns true when the underlying Drive client is signed in. QML
     *        binds enabled-states to this so the "Share" menu item is greyed
     *        out before the user connects Drive.
     */
    Q_INVOKABLE bool isAvailable() const;

signals:
    void shareLinkReady(const QString &url, const QString &fileId);
    void shareError(const QString &reason);
    void shareRevoked(const QString &fileId);

private:
    // Build the project-snapshot JSON shape — same as TaskExporter::exportProjectToJson
    // but returned as a QJsonObject so we can hand it straight to uploadPublicFile.
    QJsonObject buildProjectSnapshot(const QString &projectId) const;

    // Build a tasks-only snapshot. Includes parent task chain if all parents
    // are also in the selection, otherwise the selected task is rooted.
    QJsonObject buildTasksSnapshot(const QStringList &taskIds) const;

    // Sanitize a name for use in a Drive filename. Strips path separators and
    // limits length so Drive does not reject the upload.
    static QString sanitizeForFilename(const QString &raw);

    DataStore    *m_store;
    GDriveClient *m_drive;
};
