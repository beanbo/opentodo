#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QQmlEngine>

class Section : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString id READ id WRITE setId NOTIFY idChanged)
    Q_PROPERTY(QString projectId READ projectId WRITE setProjectId NOTIFY projectIdChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(int order READ order WRITE setOrder NOTIFY orderChanged)
    Q_PROPERTY(bool isCollapsed READ isCollapsed WRITE setIsCollapsed NOTIFY isCollapsedChanged)
    Q_PROPERTY(bool isArchived READ isArchived WRITE setIsArchived NOTIFY isArchivedChanged)
    Q_PROPERTY(QDateTime createdAt READ createdAt WRITE setCreatedAt NOTIFY createdAtChanged)
    Q_PROPERTY(QDateTime updatedAt READ updatedAt WRITE setUpdatedAt NOTIFY updatedAtChanged)
    Q_PROPERTY(bool isDeleted READ isDeleted WRITE setIsDeleted NOTIFY isDeletedChanged)

public:
    explicit Section(QObject *parent = nullptr);

    QString id() const;
    void setId(const QString &id);

    QString projectId() const;
    void setProjectId(const QString &projectId);

    QString name() const;
    void setName(const QString &name);

    int order() const;
    void setOrder(int order);

    bool isCollapsed() const;
    void setIsCollapsed(bool collapsed);

    bool isArchived() const;
    void setIsArchived(bool archived);

    QDateTime createdAt() const;
    void setCreatedAt(const QDateTime &dt);

    QDateTime updatedAt() const;
    void setUpdatedAt(const QDateTime &dt);

    bool isDeleted() const;
    void setIsDeleted(bool deleted);

    Q_INVOKABLE QJsonObject toJson() const;
    static Section *fromJson(const QJsonObject &json, QObject *parent = nullptr);
    // Apply present keys via setters (signals fire). Dedups section_update.
    void applyJson(const QJsonObject &json);

signals:
    void idChanged();
    void projectIdChanged();
    void nameChanged();
    void orderChanged();
    void isCollapsedChanged();
    void isArchivedChanged();
    void createdAtChanged();
    void updatedAtChanged();
    void isDeletedChanged();

private:
    QString m_id;
    QString m_projectId;
    QString m_name;
    int m_order = 0;
    bool m_isCollapsed = false;
    bool m_isArchived = false;
    QDateTime m_createdAt;
    QDateTime m_updatedAt;
    bool m_isDeleted = false;
};
