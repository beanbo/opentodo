#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QQmlEngine>

class AttachmentObject {
    Q_GADGET
    Q_PROPERTY(QString fileName MEMBER fileName)
    Q_PROPERTY(QString fileType MEMBER fileType)
    Q_PROPERTY(QString fileUrl MEMBER fileUrl)
    Q_PROPERTY(int fileSize MEMBER fileSize)
public:
    QString fileName;
    QString fileType;
    QString fileUrl;
    int fileSize = 0;

    QJsonObject toJson() const;
    static AttachmentObject fromJson(const QJsonObject &json);
    bool isNull() const { return fileName.isEmpty(); }
};

class Comment : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString id READ id WRITE setId NOTIFY idChanged)
    Q_PROPERTY(QString taskId READ taskId WRITE setTaskId NOTIFY taskIdChanged)
    Q_PROPERTY(QString projectId READ projectId WRITE setProjectId NOTIFY projectIdChanged)
    Q_PROPERTY(QString content READ content WRITE setContent NOTIFY contentChanged)
    Q_PROPERTY(QDateTime postedAt READ postedAt WRITE setPostedAt NOTIFY postedAtChanged)
    Q_PROPERTY(QDateTime updatedAt READ updatedAt WRITE setUpdatedAt NOTIFY updatedAtChanged)
    Q_PROPERTY(AttachmentObject attachment READ attachment WRITE setAttachment NOTIFY attachmentChanged)
    Q_PROPERTY(bool isDeleted READ isDeleted WRITE setIsDeleted NOTIFY isDeletedChanged)
    // Who wrote this comment: "" (default) = the user; "claude" = the OpenTodo→Claude
    // bridge (CLI / CliBridge). Set once at creation; the UI shows a distinct
    // author label. Syncs as a per-field CRDT register like every other field.
    Q_PROPERTY(QString author READ author WRITE setAuthor NOTIFY authorChanged)

public:
    explicit Comment(QObject *parent = nullptr);

    QString id() const;
    void setId(const QString &id);

    QString taskId() const;
    void setTaskId(const QString &taskId);

    QString projectId() const;
    void setProjectId(const QString &projectId);

    QString content() const;
    void setContent(const QString &content);

    QDateTime postedAt() const;
    void setPostedAt(const QDateTime &dt);

    QDateTime updatedAt() const;
    void setUpdatedAt(const QDateTime &dt);

    AttachmentObject attachment() const;
    void setAttachment(const AttachmentObject &att);

    bool isDeleted() const;
    void setIsDeleted(bool deleted);

    QString author() const;
    void setAuthor(const QString &author);

    Q_INVOKABLE QJsonObject toJson() const;
    static Comment *fromJson(const QJsonObject &json, QObject *parent = nullptr);
    // Apply present keys via setters (signals fire). Dedups comment_update.
    void applyJson(const QJsonObject &json);

signals:
    void idChanged();
    void taskIdChanged();
    void projectIdChanged();
    void contentChanged();
    void postedAtChanged();
    void updatedAtChanged();
    void attachmentChanged();
    void isDeletedChanged();
    void authorChanged();

private:
    QString m_id;
    QString m_taskId;
    QString m_projectId;
    QString m_content;
    QDateTime m_postedAt;
    QDateTime m_updatedAt;
    AttachmentObject m_attachment;
    bool m_isDeleted = false;
    QString m_author;
};
