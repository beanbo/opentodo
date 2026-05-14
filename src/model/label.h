#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QQmlEngine>

class Label : public QObject
{
    Q_OBJECT
    // Renamed QML type to avoid clashing with QtQuick.Controls.Label.
    QML_NAMED_ELEMENT(TaskLabel)
    QML_UNCREATABLE("Use DataStore for label CRUD; this type is only exposed via Q_PROPERTY.")

    Q_PROPERTY(QString id READ id WRITE setId NOTIFY idChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(int order READ order WRITE setOrder NOTIFY orderChanged)
    Q_PROPERTY(bool isFavorite READ isFavorite WRITE setIsFavorite NOTIFY isFavoriteChanged)
    Q_PROPERTY(bool isDeleted READ isDeleted WRITE setIsDeleted NOTIFY isDeletedChanged)

public:
    explicit Label(QObject *parent = nullptr);

    QString id() const;
    void setId(const QString &id);

    QString name() const;
    void setName(const QString &name);

    QString color() const;
    void setColor(const QString &color);

    int order() const;
    void setOrder(int order);

    bool isFavorite() const;
    void setIsFavorite(bool fav);

    bool isDeleted() const;
    void setIsDeleted(bool deleted);

    Q_INVOKABLE QJsonObject toJson() const;
    static Label *fromJson(const QJsonObject &json, QObject *parent = nullptr);

signals:
    void idChanged();
    void nameChanged();
    void colorChanged();
    void orderChanged();
    void isFavoriteChanged();
    void isDeletedChanged();

private:
    QString m_id;
    QString m_name;
    QString m_color = QStringLiteral("charcoal");
    int m_order = 0;
    bool m_isFavorite = false;
    bool m_isDeleted = false;
};
