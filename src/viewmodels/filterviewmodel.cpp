#include "filterviewmodel.h"
#include "storage/datastore.h"
#include "model/filter.h"
#include "filter/filterparser.h"
#include "services/telemetry.h"

#include <algorithm>

FilterViewModel::FilterViewModel(DataStore *store, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
{
    connect(m_store, &DataStore::filtersChanged, this, &FilterViewModel::onFiltersChanged);
    connect(m_store, &DataStore::aboutToReset, this, [this]() {
        beginResetModel();
        m_filters.clear();
        endResetModel();
    });
    refresh();
}

void FilterViewModel::setTelemetry(Telemetry *telemetry)
{
    m_telemetry = telemetry;
}

int FilterViewModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_filters.size();
}

QVariant FilterViewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_filters.size())
        return {};

    const Filter *f = m_filters.at(index.row());

    switch (role) {
    case IdRole: return f->id();
    case NameRole: return f->name();
    case QueryRole: return f->query();
    case ColorRole: return f->color();
    case OrderRole: return f->order();
    case IsFavoriteRole: return f->isFavorite();
    case FilterObjectRole: return QVariant::fromValue(const_cast<Filter *>(f));
    }

    return {};
}

QHash<int, QByteArray> FilterViewModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {QueryRole, "query"},
        {ColorRole, "color"},
        {OrderRole, "order"},
        {IsFavoriteRole, "isFavorite"},
        {FilterObjectRole, "filterObject"},
    };
}

int FilterViewModel::count() const { return m_filters.size(); }

QString FilterViewModel::addFilter(const QString &name, const QString &query, const QString &color)
{
    Filter *f = m_store->addFilter(name, query);
    if (!f) return {};
    f->setColor(color);

    if (m_telemetry)
        m_telemetry->trackEvent(QStringLiteral("filter_add"), {});

    emit filterAdded(f->id());
    return f->id();
}

bool FilterViewModel::updateFilter(const QString &id, const QString &field, const QVariant &value)
{
    Filter *f = m_store->filterById(id);
    if (!f) return false;

    if (field == QStringLiteral("name")) f->setName(value.toString());
    else if (field == QStringLiteral("query")) f->setQuery(value.toString());
    else if (field == QStringLiteral("color")) f->setColor(value.toString());
    else if (field == QStringLiteral("order")) f->setOrder(value.toInt());
    else if (field == QStringLiteral("isFavorite")) f->setIsFavorite(value.toBool());
    else return false;

    emit filterChanged(id);

    int idx = m_filters.indexOf(f);
    if (idx >= 0)
        emit dataChanged(index(idx), index(idx));

    return true;
}

bool FilterViewModel::deleteFilter(const QString &id)
{
    bool ok = m_store->removeFilter(id);
    if (ok) {
        if (m_telemetry)
            m_telemetry->trackEvent(QStringLiteral("filter_delete"), {});
        emit filterRemoved(id);
    }
    return ok;
}

bool FilterViewModel::setFavorite(const QString &id, bool favorite)
{
    return updateFilter(id, QStringLiteral("isFavorite"), favorite);
}

bool FilterViewModel::reorderFilter(const QString &id, int newOrder)
{
    return updateFilter(id, QStringLiteral("order"), newOrder);
}

bool FilterViewModel::isQueryValid(const QString &query) const
{
    FilterParser parser;
    return parser.isValid(query);
}

QString FilterViewModel::queryError(const QString &query) const
{
    FilterParser parser;
    parser.isValid(query);
    return parser.lastError();
}

Filter *FilterViewModel::filterById(const QString &id) const
{
    return m_store->filterById(id);
}

void FilterViewModel::onFiltersChanged()
{
    refresh();
}

void FilterViewModel::refresh()
{
    beginResetModel();
    m_filters = m_store->allFilters();
    std::sort(m_filters.begin(), m_filters.end(), [](const Filter *a, const Filter *b) {
        return a->order() < b->order();
    });
    endResetModel();
    emit countChanged();
}
