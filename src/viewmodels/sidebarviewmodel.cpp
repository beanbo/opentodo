#include "sidebarviewmodel.h"
#include "projectviewmodel.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/label.h"
#include "model/filter.h"

#include <QDate>
#include <QSet>
#include <QSettings>

SidebarViewModel::SidebarViewModel(DataStore *store, ProjectViewModel *projectVM, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_projectVM(projectVM)
{
    connect(m_store, &DataStore::tasksChanged, this, &SidebarViewModel::onDataChanged);
    connect(m_store, &DataStore::projectsChanged, this, &SidebarViewModel::onDataChanged);
    connect(m_store, &DataStore::labelsChanged, this, [this]() {
        emit favoritesChanged();
        emit labelsChanged();
    });
    connect(m_store, &DataStore::filtersChanged, this, [this]() { emit favoritesChanged(); });

    // Midnight rollover guard.  If the app is left open from 23:59 to 00:30
    // with no data mutations, onDataChanged() never fires, so the "Сегодня"
    // counter would otherwise stay frozen at yesterday's task set.  Tick once
    // a minute, compare today vs the cached last-seen date, recompute on
    // change.  60 s granularity is fine — the worst-case user-visible drift
    // is one minute after midnight, well below human-perceptible "this is wrong".
    m_lastSeenDate = QDate::currentDate();
    m_midnightCheckTimer.setInterval(60'000);
    m_midnightCheckTimer.setSingleShot(false);
    connect(&m_midnightCheckTimer, &QTimer::timeout, this,
            &SidebarViewModel::onMidnightTimerTick);
    m_midnightCheckTimer.start();

    updateCounts();
}

int SidebarViewModel::inboxCount() const { return m_inboxCount; }
int SidebarViewModel::todayCount() const { return m_todayCount; }
int SidebarViewModel::upcomingCount() const { return m_upcomingCount; }

QString SidebarViewModel::currentView() const { return m_currentView; }
void SidebarViewModel::setCurrentView(const QString &view) {
    if (m_currentView != view) {
        m_currentView = view;
        emit currentViewChanged();
    }
}

QString SidebarViewModel::currentItemId() const { return m_currentItemId; }
void SidebarViewModel::setCurrentItemId(const QString &id) {
    if (m_currentItemId != id) {
        m_currentItemId = id;
        emit currentItemIdChanged();
    }
}

QVariantList SidebarViewModel::favorites() const
{
    QVariantList result;

    // Favorite projects
    // `icon` is emitted explicitly even when empty so the QML delegate at
    // SidebarContent.qml:566 can branch on `modelData.icon` without falling
    // through to the `undefined` path that returned `#` instead of the
    // configured emoji.
    for (auto *p : m_store->allProjects()) {
        if (p->isFavorite()) {
            QVariantMap item;
            item[QStringLiteral("type")] = QStringLiteral("project");
            item[QStringLiteral("id")] = p->id();
            item[QStringLiteral("name")] = p->name();
            item[QStringLiteral("color")] = p->color();
            item[QStringLiteral("icon")] = p->icon();
            result.append(item);
        }
    }

    // Favorite labels
    for (auto *l : m_store->allLabels()) {
        if (l->isFavorite()) {
            QVariantMap item;
            item[QStringLiteral("type")] = QStringLiteral("label");
            item[QStringLiteral("id")] = l->id();
            item[QStringLiteral("name")] = l->name();
            item[QStringLiteral("color")] = l->color();
            result.append(item);
        }
    }

    // Favorite filters
    for (auto *f : m_store->allFilters()) {
        if (f->isFavorite()) {
            QVariantMap item;
            item[QStringLiteral("type")] = QStringLiteral("filter");
            item[QStringLiteral("id")] = f->id();
            item[QStringLiteral("name")] = f->name();
            item[QStringLiteral("color")] = f->color();
            result.append(item);
        }
    }

    return result;
}

QVariantList SidebarViewModel::labels() const
{
    // Read persisted order (label IDs) and build the result.
    // Strategy: emit labels in saved order first; any newly created labels
    // (not yet present in saved order) are appended at the end in their
    // natural DataStore order. Tombstoned/deleted labels filtered out.
    const QStringList order = labelOrder();
    const auto allLabels = m_store->allLabels();

    QVariantList result;
    QSet<QString> emitted;

    // Pass 1: emit labels referenced by saved order (skip stale IDs)
    for (const QString &id : order) {
        for (auto *l : allLabels) {
            if (l->id() == id && !emitted.contains(id)) {
                QVariantMap item;
                item[QStringLiteral("id")] = l->id();
                item[QStringLiteral("name")] = l->name();
                item[QStringLiteral("color")] = l->color();
                item[QStringLiteral("isFavorite")] = l->isFavorite();
                result.append(item);
                emitted.insert(id);
                break;
            }
        }
    }

    // Pass 2: append remaining labels not in saved order (new labels)
    for (auto *l : allLabels) {
        if (!emitted.contains(l->id())) {
            QVariantMap item;
            item[QStringLiteral("id")] = l->id();
            item[QStringLiteral("name")] = l->name();
            item[QStringLiteral("color")] = l->color();
            item[QStringLiteral("isFavorite")] = l->isFavorite();
            result.append(item);
        }
    }

    return result;
}

QStringList SidebarViewModel::labelOrder() const
{
    QSettings s;
    return s.value(QStringLiteral("sidebar/labelOrder")).toStringList();
}

void SidebarViewModel::setLabelOrder(const QStringList &order)
{
    QSettings s;
    s.setValue(QStringLiteral("sidebar/labelOrder"), order);
    s.sync();
    emit labelOrderChanged();
    emit labelsChanged();
}

bool SidebarViewModel::sidebarVisible() const { return m_sidebarVisible; }
void SidebarViewModel::setSidebarVisible(bool visible) {
    if (m_sidebarVisible != visible) {
        m_sidebarVisible = visible;
        emit sidebarVisibleChanged();
    }
}

void SidebarViewModel::navigateTo(const QString &view, const QString &itemId)
{
    setCurrentView(view);
    setCurrentItemId(itemId);
    emit navigationRequested(view, itemId);
}

void SidebarViewModel::toggleSidebar()
{
    setSidebarVisible(!m_sidebarVisible);
}

void SidebarViewModel::navigateToInbox()
{
    auto *inbox = m_store->inboxProject();
    navigateTo(QStringLiteral("inbox"), inbox ? inbox->id() : QString());
}

void SidebarViewModel::navigateToToday()
{
    navigateTo(QStringLiteral("today"));
}

void SidebarViewModel::navigateToUpcoming()
{
    navigateTo(QStringLiteral("upcoming"));
}

void SidebarViewModel::navigateToProject(const QString &projectId)
{
    navigateTo(QStringLiteral("project"), projectId);
}

void SidebarViewModel::navigateToLabel(const QString &labelName)
{
    navigateTo(QStringLiteral("label"), labelName);
}

void SidebarViewModel::navigateToFilter(const QString &filterId)
{
    auto *f = m_store->filterById(filterId);
    if (f)
        navigateTo(QStringLiteral("filter"), filterId);
}

void SidebarViewModel::onDataChanged()
{
    updateCounts();
    emit favoritesChanged();
    emit labelsChanged();
}

void SidebarViewModel::updateCounts()
{
    int newInbox = 0;
    int newToday = 0;
    int newUpcoming = 0;

    auto *inbox = m_store->inboxProject();
    const QString inboxId = inbox ? inbox->id() : QString();
    const QDate today = QDate::currentDate();

    const auto tasks = m_store->allTasks();
    for (const auto *t : tasks) {
        if (t->isCompleted()) continue;

        // Inbox count
        if (t->projectId() == inboxId && t->parentId().isEmpty())
            ++newInbox;

        // Today count (due today or overdue)
        if (!t->dueDate().isEmpty()) {
            const QDate d = QDate::fromString(t->dueDate(), QStringLiteral("yyyy-MM-dd"));
            if (d.isValid()) {
                if (d <= today) ++newToday;
                if (d > today) ++newUpcoming;
            }
        }
    }

    bool changed = (newInbox != m_inboxCount || newToday != m_todayCount || newUpcoming != m_upcomingCount);
    m_inboxCount = newInbox;
    m_todayCount = newToday;
    m_upcomingCount = newUpcoming;
    if (changed) emit countsChanged();
}

void SidebarViewModel::onMidnightTimerTick()
{
    // Cheap calendar-date diff check; the timer fires once a minute, so this
    // path is effectively no-op every ~1440 ticks per day except the one tick
    // immediately after local midnight.
    const QDate today = QDate::currentDate();
    if (today != m_lastSeenDate) {
        m_lastSeenDate = today;
        updateCounts();
    }
}

void SidebarViewModel::simulateMidnightRolloverForTesting()
{
    // Test hook only: forcibly mark "yesterday" as the last-seen date so
    // the timer lambda treats the next tick as a real rollover.  Re-run the
    // lambda inline so the test doesn't have to wait 60 s for the timer.
    m_lastSeenDate = QDate::currentDate().addDays(-1);
    onMidnightTimerTick();
}

int SidebarViewModel::midnightCheckIntervalMsForTesting() const
{
    return m_midnightCheckTimer.interval();
}

bool SidebarViewModel::midnightCheckTimerActiveForTesting() const
{
    return m_midnightCheckTimer.isActive();
}
