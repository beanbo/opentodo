#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QQmlEngine>

class Filter : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString id READ id WRITE setId NOTIFY idChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QString color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(int order READ order WRITE setOrder NOTIFY orderChanged)
    Q_PROPERTY(bool isFavorite READ isFavorite WRITE setIsFavorite NOTIFY isFavoriteChanged)
    Q_PROPERTY(QDateTime createdAt READ createdAt WRITE setCreatedAt NOTIFY createdAtChanged)
    Q_PROPERTY(QDateTime updatedAt READ updatedAt WRITE setUpdatedAt NOTIFY updatedAtChanged)
    Q_PROPERTY(bool isDeleted READ isDeleted WRITE setIsDeleted NOTIFY isDeletedChanged)

public:
    explicit Filter(QObject *parent = nullptr);

    QString id() const;
    void setId(const QString &id);

    QString name() const;
    void setName(const QString &name);

    QString query() const;
    void setQuery(const QString &query);

    QString color() const;
    void setColor(const QString &color);

    int order() const;
    void setOrder(int order);

    bool isFavorite() const;
    void setIsFavorite(bool fav);

    QDateTime createdAt() const;
    void setCreatedAt(const QDateTime &dt);

    QDateTime updatedAt() const;
    void setUpdatedAt(const QDateTime &dt);

    bool isDeleted() const;
    void setIsDeleted(bool deleted);

    Q_INVOKABLE QJsonObject toJson() const;
    static Filter *fromJson(const QJsonObject &json, QObject *parent = nullptr);

signals:
    void idChanged();
    void nameChanged();
    void queryChanged();
    void colorChanged();
    void orderChanged();
    void isFavoriteChanged();
    void createdAtChanged();
    void updatedAtChanged();
    void isDeletedChanged();

private:
    QString m_id;
    QString m_name;
    QString m_query;
    QString m_color = QStringLiteral("charcoal");
    int m_order = 0;
    bool m_isFavorite = false;
    QDateTime m_createdAt;
    QDateTime m_updatedAt;
    bool m_isDeleted = false;
};
