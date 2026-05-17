#include "searchviewmodel.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/label.h"
#include "model/comment.h"
#include "utils/settingskeys.h"
#include "utils/textsearch.h"
#include <QSettings>

SearchViewModel::SearchViewModel(DataStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    // Load recent searches from settings
    const QVariant saved = m_store->setting(QLatin1String(SettingsKeys::RecentSearches));
    if (saved.isValid())
        m_recentSearches = saved.toStringList();

    // Restore last search query without triggering a search.
    // The user's first interaction (typing or pressing Enter) re-runs the search.
    QSettings qs;
    m_searchQuery = qs.value(QStringLiteral("search/last_query")).toString();

    // B-3.57: Re-run search when underlying data changes so results stay
    // current while the search panel is open.
    auto refreshIfActive = [this]() {
        if (m_searchQuery.length() >= 2)
            performSearch();
    };
    connect(m_store, &DataStore::tasksChanged,    this, refreshIfActive);
    connect(m_store, &DataStore::projectsChanged, this, refreshIfActive);
    connect(m_store, &DataStore::labelsChanged,   this, refreshIfActive);

    // B-3.56: Clear results when the store is about to be reset so the model
    // does not hold dangling pointers to destroyed entities.
    connect(m_store, &DataStore::aboutToReset, this, [this]() {
        m_results.clear();
        emit resultsChanged();
    });
}

QString SearchViewModel::searchQuery() const { return m_searchQuery; }
void SearchViewModel::setSearchQuery(const QString &query) {
    // Q2.26-B: re-run the search even when the new query equals the cached
    // one, provided we don't already have results staged. This covers the
    // post-startup case where the constructor restored m_searchQuery from
    // QSettings WITHOUT performing the search — the user (or the test
    // harness) re-submitting the same string must still trigger a fresh
    // search rather than silently returning the empty initial state.
    const bool changed = (m_searchQuery != query);
    const bool needsBackfill = !changed && query.length() >= 2
                               && m_results.isEmpty();
    if (!changed && !needsBackfill)
        return;

    if (changed) {
        m_searchQuery = query;

        // Persist: skip transient mid-typing states (1–2 chars) to avoid
        // saving "пр" when the user meant "проект". Save empty (explicit
        // clear) or anything long enough to be intentional.
        if (query.isEmpty() || query.length() >= 3) {
            QSettings qs;
            qs.setValue(QStringLiteral("search/last_query"), query);
        }
        emit searchQueryChanged();
    }

    if (query.length() >= 2)
        performSearch();
    else if (query.isEmpty()) {
        m_results.clear();
        emit resultsChanged();
    }
}

QVariantList SearchViewModel::results() const { return m_results; }
QStringList SearchViewModel::recentSearches() const { return m_recentSearches; }
int SearchViewModel::resultCount() const { return m_results.size(); }
bool SearchViewModel::isSearching() const { return m_isSearching; }

void SearchViewModel::search(const QString &query)
{
    setSearchQuery(query);
    if (!query.trimmed().isEmpty())
        addToRecentSearches(query.trimmed());
}

void SearchViewModel::clearSearch()
{
    m_searchQuery.clear();
    m_results.clear();

    // Explicit clear: next launch starts with a blank search bar.
    QSettings qs;
    qs.setValue(QStringLiteral("search/last_query"), QString());

    emit searchQueryChanged();
    emit resultsChanged();
}

void SearchViewModel::clearRecentSearches()
{
    m_recentSearches.clear();
    m_store->setSetting(QLatin1String(SettingsKeys::RecentSearches), QVariant());
    emit recentSearchesChanged();
}

void SearchViewModel::removeRecentSearch(const QString &query)
{
    m_recentSearches.removeAll(query);
    m_store->setSetting(QLatin1String(SettingsKeys::RecentSearches), m_recentSearches);
    emit recentSearchesChanged();
}

void SearchViewModel::addToRecentSearches(const QString &query)
{
    m_recentSearches.removeAll(query);
    m_recentSearches.prepend(query);
    while (m_recentSearches.size() > MAX_RECENT_SEARCHES)
        m_recentSearches.removeLast();

    m_store->setSetting(QLatin1String(SettingsKeys::RecentSearches), m_recentSearches);
    emit recentSearchesChanged();
}

void SearchViewModel::performSearch()
{
    m_isSearching = true;
    emit isSearchingChanged();

    m_results.clear();

    // Q2.26-B: Unicode-aware substring matching. Read the accent-sensitivity
    // toggle once at search start so every entity scan in this pass shares
    // the same normalization mode. Default false = "café"/"cafe"/"café" all
    // match the same query.
    const bool accentSensitive =
        m_store->setting(QLatin1String(SettingsKeys::SearchAccentSensitive))
                .toBool();
    const bool stripDiacritics = !accentSensitive;

    // Pre-normalize the needle exactly once. Project/label/comment loops below
    // call containsNormalized(haystack, normalizedNeedle) so the per-row work
    // is just normalize(haystack) + a single QString::contains.
    const QString normalizedQuery =
        TextSearch::normalize(m_searchQuery, stripDiacritics);

    // Search tasks (DataStore does its own pre-normalization for symmetry
    // with QML callers that don't go through SearchViewModel).
    const auto tasks = m_store->searchTasks(m_searchQuery, accentSensitive);
    for (auto *t : tasks) {
        QVariantMap item;
        item[QStringLiteral("type")] = QStringLiteral("task");
        item[QStringLiteral("id")] = t->id();
        item[QStringLiteral("title")] = t->content();
        item[QStringLiteral("subtitle")] = t->description();
        item[QStringLiteral("projectId")] = t->projectId();
        item[QStringLiteral("isCompleted")] = t->isCompleted();
        item[QStringLiteral("object")] = QVariant::fromValue(t);
        m_results.append(item);
    }

    // Pure-diacritic queries normalize to "" — skip the remaining scans so we
    // don't return every entity (empty needle matches everything).
    if (normalizedQuery.isEmpty()) {
        m_isSearching = false;
        emit isSearchingChanged();
        emit resultsChanged();
        return;
    }

    // Search projects
    for (auto *p : m_store->allProjects()) {
        if (TextSearch::containsNormalized(p->name(), normalizedQuery,
                                           stripDiacritics)) {
            QVariantMap item;
            item[QStringLiteral("type")] = QStringLiteral("project");
            item[QStringLiteral("id")] = p->id();
            item[QStringLiteral("title")] = p->name();
            item[QStringLiteral("subtitle")] = QStringLiteral("Проект");
            item[QStringLiteral("color")] = p->color();
            item[QStringLiteral("object")] = QVariant::fromValue(p);
            m_results.append(item);
        }
    }

    // Search labels
    for (auto *l : m_store->allLabels()) {
        if (TextSearch::containsNormalized(l->name(), normalizedQuery,
                                           stripDiacritics)) {
            QVariantMap item;
            item[QStringLiteral("type")] = QStringLiteral("label");
            item[QStringLiteral("id")] = l->id();
            item[QStringLiteral("title")] = l->name();
            item[QStringLiteral("subtitle")] = QStringLiteral("Метка");
            item[QStringLiteral("color")] = l->color();
            item[QStringLiteral("object")] = QVariant::fromValue(l);
            m_results.append(item);
        }
    }

    // Search comments
    const auto allTasks = m_store->allTasks();
    for (auto *t : allTasks) {
        const auto comments = m_store->commentsForTask(t->id());
        for (auto *c : comments) {
            if (TextSearch::containsNormalized(c->content(), normalizedQuery,
                                               stripDiacritics)) {
                QVariantMap item;
                item[QStringLiteral("type")] = QStringLiteral("comment");
                item[QStringLiteral("id")] = c->id();
                item[QStringLiteral("title")] = c->content().left(100);
                item[QStringLiteral("subtitle")] = QStringLiteral("Комментарий к: %1").arg(t->content());
                item[QStringLiteral("taskId")] = t->id();
                item[QStringLiteral("object")] = QVariant::fromValue(c);
                m_results.append(item);
            }
        }
    }

    m_isSearching = false;
    emit isSearchingChanged();
    emit resultsChanged();
}
