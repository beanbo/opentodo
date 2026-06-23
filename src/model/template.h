#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QQmlEngine>

// Template — a reusable, named task tree.
//
// `items` is a JSON array of task specs, each: { content, priority, labels[],
// dueString?, children[] }. The tree is stored as a single whole-object blob
// rather than as separate Task entities, so a Template is one sync entity (no
// per-spec CRDT — templates are personal and rarely edited concurrently).
// Insertion (TemplateViewModel::insertTemplate) materialises the specs into
// real Tasks under a chosen project/section.
class Template : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString id READ id WRITE setId NOTIFY idChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY itemsChanged)
    Q_PROPERTY(QDateTime createdAt READ createdAt WRITE setCreatedAt NOTIFY createdAtChanged)
    Q_PROPERTY(QDateTime updatedAt READ updatedAt WRITE setUpdatedAt NOTIFY updatedAtChanged)
    Q_PROPERTY(bool isDeleted READ isDeleted WRITE setIsDeleted NOTIFY isDeletedChanged)

public:
    explicit Template(QObject *parent = nullptr);

    QString id() const;
    void setId(const QString &id);

    QString name() const;
    void setName(const QString &name);

    QJsonArray items() const;
    void setItems(const QJsonArray &items);
    int itemCount() const;

    QDateTime createdAt() const;
    void setCreatedAt(const QDateTime &dt);

    QDateTime updatedAt() const;
    void setUpdatedAt(const QDateTime &dt);

    bool isDeleted() const;
    void setIsDeleted(bool deleted);

    Q_INVOKABLE QJsonObject toJson() const;
    static Template *fromJson(const QJsonObject &json, QObject *parent = nullptr);
    // Apply present keys via setters (signals fire). Dedups template_update.
    void applyJson(const QJsonObject &json);

signals:
    void idChanged();
    void nameChanged();
    void itemsChanged();
    void createdAtChanged();
    void updatedAtChanged();
    void isDeletedChanged();

private:
    QString m_id;
    QString m_name;
    QJsonArray m_items;
    QDateTime m_createdAt;
    QDateTime m_updatedAt;
    bool m_isDeleted = false;
};
