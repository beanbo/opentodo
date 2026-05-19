#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QQmlEngine>

class Project : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString id READ id WRITE setId NOTIFY idChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY descriptionChanged)
    Q_PROPERTY(QString parentId READ parentId WRITE setParentId NOTIFY parentIdChanged)
    Q_PROPERTY(int order READ order WRITE setOrder NOTIFY orderChanged)
    Q_PROPERTY(bool isFavorite READ isFavorite WRITE setIsFavorite NOTIFY isFavoriteChanged)
    Q_PROPERTY(bool isInbox READ isInbox WRITE setIsInbox NOTIFY isInboxChanged)
    Q_PROPERTY(bool isArchived READ isArchived WRITE setIsArchived NOTIFY isArchivedChanged)
    Q_PROPERTY(QString viewStyle READ viewStyle WRITE setViewStyle NOTIFY viewStyleChanged)
    Q_PROPERTY(QDateTime createdAt READ createdAt WRITE setCreatedAt NOTIFY createdAtChanged)
    Q_PROPERTY(QDateTime updatedAt READ updatedAt WRITE setUpdatedAt NOTIFY updatedAtChanged)
    Q_PROPERTY(bool isDeleted READ isDeleted WRITE setIsDeleted NOTIFY isDeletedChanged)
    Q_PROPERTY(QString icon READ icon WRITE setIcon NOTIFY iconChanged)
    Q_PROPERTY(int weeklyGoal READ weeklyGoal WRITE setWeeklyGoal NOTIFY weeklyGoalChanged)
    Q_PROPERTY(QString notes READ notes WRITE setNotes NOTIFY notesChanged)
    Q_PROPERTY(QString linkedRepoPath READ linkedRepoPath WRITE setLinkedRepoPath NOTIFY linkedRepoPathChanged)

public:
    explicit Project(QObject *parent = nullptr);

    QString id() const;
    void setId(const QString &id);

    QString name() const;
    void setName(const QString &name);

    QString color() const;
    void setColor(const QString &color);

    QString description() const;
    void setDescription(const QString &description);

    QString parentId() const;
    void setParentId(const QString &parentId);

    int order() const;
    void setOrder(int order);

    bool isFavorite() const;
    void setIsFavorite(bool fav);

    bool isInbox() const;
    void setIsInbox(bool inbox);

    bool isArchived() const;
    void setIsArchived(bool archived);

    QString viewStyle() const;
    void setViewStyle(const QString &style);

    QDateTime createdAt() const;
    void setCreatedAt(const QDateTime &dt);

    QDateTime updatedAt() const;
    void setUpdatedAt(const QDateTime &dt);

    bool isDeleted() const;
    void setIsDeleted(bool deleted);

    QString icon() const;
    void setIcon(const QString &icon);

    int weeklyGoal() const;
    void setWeeklyGoal(int goal);

    QString notes() const;
    void setNotes(const QString &notes);

    QString linkedRepoPath() const;
    void setLinkedRepoPath(const QString &path);

    Q_INVOKABLE QJsonObject toJson() const;
    static Project *fromJson(const QJsonObject &json, QObject *parent = nullptr);

signals:
    void idChanged();
    void nameChanged();
    void colorChanged();
    void descriptionChanged();
    void parentIdChanged();
    void orderChanged();
    void isFavoriteChanged();
    void isInboxChanged();
    void isArchivedChanged();
    void viewStyleChanged();
    void createdAtChanged();
    void updatedAtChanged();
    void isDeletedChanged();
    void iconChanged();
    void weeklyGoalChanged();
    void notesChanged();
    void linkedRepoPathChanged();

private:
    QString m_id;
    QString m_name;
    QString m_color = QStringLiteral("grey");
    QString m_description;
    QString m_parentId;
    int m_order = 0;
    bool m_isFavorite = false;
    bool m_isInbox = false;
    bool m_isArchived = false;
    QString m_viewStyle = QStringLiteral("list");
    QDateTime m_createdAt;
    QDateTime m_updatedAt;
    bool m_isDeleted = false;
    QString m_icon;
    int m_weeklyGoal = 0;
    QString m_notes;
    QString m_linkedRepoPath;
};
