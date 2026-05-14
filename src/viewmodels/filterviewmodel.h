#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QVariantList>
#include <QString>
#include <QQmlEngine>

#include "model/filter.h"
#include "storage/datastore.h"

class Telemetry;

class FilterViewModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("FilterViewModel is constructed in C++ and exposed via context property.")

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        QueryRole,
        ColorRole,
        OrderRole,
        IsFavoriteRole,
        FilterObjectRole
    };
    Q_ENUM(Roles)

    explicit FilterViewModel(DataStore *store, QObject *parent = nullptr);

    // Inject optional telemetry service. Wired in main.cpp.
    void setTelemetry(Telemetry *telemetry);

    // QAbstractListModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;

    // CRUD
    Q_INVOKABLE QString addFilter(const QString &name, const QString &query,
                                   const QString &color = QStringLiteral("charcoal"));
    Q_INVOKABLE bool updateFilter(const QString &id, const QString &field, const QVariant &value);
    Q_INVOKABLE bool deleteFilter(const QString &id);
    Q_INVOKABLE bool setFavorite(const QString &id, bool favorite);
    Q_INVOKABLE bool reorderFilter(const QString &id, int newOrder);

    // Evaluation
    Q_INVOKABLE bool isQueryValid(const QString &query) const;
    Q_INVOKABLE QString queryError(const QString &query) const;

    Q_INVOKABLE Filter *filterById(const QString &id) const;

signals:
    void countChanged();
    void filterAdded(const QString &id);
    void filterRemoved(const QString &id);
    void filterChanged(const QString &id);

private slots:
    void onFiltersChanged();

private:
    void refresh();

    DataStore *m_store;
    QList<Filter *> m_filters;
    Telemetry *m_telemetry = nullptr;
};
