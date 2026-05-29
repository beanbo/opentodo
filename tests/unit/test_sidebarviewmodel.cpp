#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDate>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/label.h"
#include "model/filter.h"
#include "viewmodels/projectviewmodel.h"
#include "viewmodels/sidebarviewmodel.h"

// ============================================================================
// Comprehensive unit tests for SidebarViewModel.
//
// Coverage:
//   - bootstrap state with empty store
//   - inboxCount / todayCount / upcomingCount semantics
//   - navigation slot signals (navigationRequested)
//   - favorites projection (project / label / filter favorites)
//   - label ordering (labelOrder/setLabelOrder, persistence to QSettings,
//     append-new-at-end behavior)
//   - dataChanged-driven labelsChanged emission
//   - projectsTree-equivalent: SidebarViewModel does not expose a tree directly,
//     so the orchestrator's mention is treated as "verify via headers; flat" —
//     header has no projectsTree property, so that case is omitted as
//     instructed ("skip if flat").
// ============================================================================

namespace {

struct Fixture {
    DataStore *store = nullptr;
    ProjectViewModel *projectVM = nullptr;
    SidebarViewModel *sidebar = nullptr;
    QString inboxId;
    QString workId;
    QString homeId;

    ~Fixture() {
        delete sidebar;
        delete projectVM;
        delete store;
    }
};

// Build a minimal fixture: inbox + two ordinary projects, a SidebarViewModel
// wired to the real DataStore + ProjectViewModel.
static Fixture *makeFixture()
{
    auto *f = new Fixture;
    f->store = new DataStore;
    f->projectVM = new ProjectViewModel(f->store);

    Project *inbox = f->store->addProject(QStringLiteral("Inbox"));
    inbox->setIsInbox(true);
    f->inboxId = inbox->id();

    Project *work = f->store->addProject(QStringLiteral("Work"));
    f->workId = work->id();

    Project *home = f->store->addProject(QStringLiteral("Home"));
    f->homeId = home->id();

    f->sidebar = new SidebarViewModel(f->store, f->projectVM);
    return f;
}

static QString isoDate(const QDate &d)
{
    return d.toString(QStringLiteral("yyyy-MM-dd"));
}

} // anonymous namespace


class TestSidebarViewModel : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        // Use a dedicated QSettings scope so the test does not pollute the
        // user's real OpenTodo settings, and so each test reads a clean slate.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_SidebarVM"));
        QCoreApplication::setApplicationName(QStringLiteral("test_sidebarviewmodel"));
    }

    void init()
    {
        // Wipe any leftover sidebar/labelOrder from a previous test slot.
        QSettings s;
        s.remove(QStringLiteral("sidebar/labelOrder"));
        s.sync();
    }

    void cleanup()
    {
        QSettings s;
        s.remove(QStringLiteral("sidebar/labelOrder"));
        s.sync();
    }

    // =====================================================================
    // Bootstrap
    // =====================================================================

    void bootstrap_empty_store_has_zero_counts_and_no_favorites()
    {
        DataStore store;
        ProjectViewModel pvm(&store);
        SidebarViewModel sidebar(&store, &pvm);

        QCOMPARE(sidebar.inboxCount(), 0);
        QCOMPARE(sidebar.todayCount(), 0);
        QCOMPARE(sidebar.upcomingCount(), 0);
        QCOMPARE(sidebar.favorites().size(), 0);
        QCOMPARE(sidebar.labels().size(), 0);
        // Default current view is "inbox" per header default-init.
        QCOMPARE(sidebar.currentView(), QStringLiteral("inbox"));
        QVERIFY(sidebar.currentItemId().isEmpty());
        QVERIFY(sidebar.sidebarVisible());
    }

    // =====================================================================
    // inboxCount
    // =====================================================================

    void inboxCount_counts_top_level_inbox_tasks()
    {
        Fixture *f = makeFixture();

        // Two top-level inbox tasks; one task in another project; one inbox
        // sub-task (parentId set) which must NOT count.
        f->store->addTask(QStringLiteral("Inbox 1"), f->inboxId);
        f->store->addTask(QStringLiteral("Inbox 2"), f->inboxId);
        f->store->addTask(QStringLiteral("Work 1"),  f->workId);

        Task *parent = f->store->addTask(QStringLiteral("Inbox parent"), f->inboxId);
        Task *child  = f->store->addTask(QStringLiteral("Inbox child"), f->inboxId);
        child->setParentId(parent->id());

        // updateCounts() runs from constructor + on data signals; nudge it.
        emit f->store->tasksChanged();   // starts the 50 ms counts debounce

        // 3 top-level inbox tasks (Inbox 1, Inbox 2, Inbox parent); the child
        // and Work 1 are excluded.
        QTRY_COMPARE(f->sidebar->inboxCount(), 3);

        delete f;
    }

    void inboxCount_excludes_completed()
    {
        Fixture *f = makeFixture();

        Task *t1 = f->store->addTask(QStringLiteral("A"), f->inboxId);
        Task *t2 = f->store->addTask(QStringLiteral("B"), f->inboxId);
        t2->setIsCompleted(true);

        emit f->store->tasksChanged();   // starts the 50 ms counts debounce

        QTRY_COMPARE(f->sidebar->inboxCount(), 1);
        Q_UNUSED(t1);
        delete f;
    }

    // =====================================================================
    // todayCount
    // =====================================================================

    void todayCount_includes_due_today_excludes_tomorrow()
    {
        Fixture *f = makeFixture();
        const QDate today    = QDate::currentDate();
        const QDate tomorrow = today.addDays(1);

        Task *a = f->store->addTask(QStringLiteral("Due today"), f->inboxId);
        a->setDueDate(isoDate(today));

        Task *b = f->store->addTask(QStringLiteral("Due tomorrow"), f->inboxId);
        b->setDueDate(isoDate(tomorrow));

        emit f->store->tasksChanged();   // starts the 50 ms counts debounce

        QTRY_COMPARE(f->sidebar->todayCount(), 1);

        delete f;
    }

    void todayCount_includes_overdue()
    {
        Fixture *f = makeFixture();
        const QDate today = QDate::currentDate();

        Task *t = f->store->addTask(QStringLiteral("Overdue"), f->inboxId);
        t->setDueDate(isoDate(today.addDays(-3)));

        emit f->store->tasksChanged();   // starts the 50 ms counts debounce

        // updateCounts() treats any due date <= today as "today".
        QTRY_COMPARE(f->sidebar->todayCount(), 1);

        delete f;
    }

    void todayCount_excludes_completed_tasks()
    {
        Fixture *f = makeFixture();
        const QDate today = QDate::currentDate();

        Task *t = f->store->addTask(QStringLiteral("Done today"), f->inboxId);
        t->setDueDate(isoDate(today));
        t->setIsCompleted(true);

        emit f->store->tasksChanged();   // starts the 50 ms counts debounce

        QCOMPARE(f->sidebar->todayCount(), 0);

        delete f;
    }

    // =====================================================================
    // upcomingCount
    // =====================================================================

    void upcomingCount_counts_future_dated_open_tasks()
    {
        Fixture *f = makeFixture();
        const QDate today = QDate::currentDate();

        Task *t1 = f->store->addTask(QStringLiteral("In 1 day"), f->inboxId);
        t1->setDueDate(isoDate(today.addDays(1)));

        Task *t2 = f->store->addTask(QStringLiteral("In 5 days"), f->inboxId);
        t2->setDueDate(isoDate(today.addDays(5)));

        Task *t3 = f->store->addTask(QStringLiteral("In 30 days"), f->inboxId);
        t3->setDueDate(isoDate(today.addDays(30)));

        // Today task — NOT upcoming (it's "today").
        Task *t4 = f->store->addTask(QStringLiteral("Today"), f->inboxId);
        t4->setDueDate(isoDate(today));

        emit f->store->tasksChanged();   // starts the 50 ms counts debounce

        // SidebarViewModel::updateCounts() counts ANY future-dated open task
        // as upcoming (no 7-day cap in the implementation), so all three
        // future tasks count.
        QTRY_COMPARE(f->sidebar->upcomingCount(), 3);
        QCOMPARE(f->sidebar->todayCount(),    1);

        delete f;
    }

    // =====================================================================
    // Navigation
    // =====================================================================

    void navigateToInbox_emits_navigationRequested_with_inbox()
    {
        Fixture *f = makeFixture();
        QSignalSpy spy(f->sidebar, &SidebarViewModel::navigationRequested);

        f->sidebar->navigateToInbox();

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("inbox"));
        // navigateToInbox uses inboxProject id as itemId (not "" — the
        // implementation passes the inbox project id along).
        QCOMPARE(spy.at(0).at(1).toString(), f->inboxId);
        QCOMPARE(f->sidebar->currentView(), QStringLiteral("inbox"));

        delete f;
    }

    void navigateToToday_emits_today_with_empty_id()
    {
        Fixture *f = makeFixture();
        QSignalSpy spy(f->sidebar, &SidebarViewModel::navigationRequested);

        f->sidebar->navigateToToday();

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("today"));
        QVERIFY(spy.at(0).at(1).toString().isEmpty());
        QCOMPARE(f->sidebar->currentView(), QStringLiteral("today"));

        delete f;
    }

    void navigateToUpcoming_emits_upcoming_with_empty_id()
    {
        Fixture *f = makeFixture();
        QSignalSpy spy(f->sidebar, &SidebarViewModel::navigationRequested);

        f->sidebar->navigateToUpcoming();

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("upcoming"));
        QVERIFY(spy.at(0).at(1).toString().isEmpty());

        delete f;
    }

    void navigateToProject_emits_project_with_id()
    {
        Fixture *f = makeFixture();
        QSignalSpy spy(f->sidebar, &SidebarViewModel::navigationRequested);

        f->sidebar->navigateToProject(f->workId);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("project"));
        QCOMPARE(spy.at(0).at(1).toString(), f->workId);
        QCOMPARE(f->sidebar->currentView(),   QStringLiteral("project"));
        QCOMPARE(f->sidebar->currentItemId(), f->workId);

        delete f;
    }

    void navigateToLabel_emits_label_with_id()
    {
        Fixture *f = makeFixture();
        QSignalSpy spy(f->sidebar, &SidebarViewModel::navigationRequested);

        f->sidebar->navigateToLabel(QStringLiteral("urgent"));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("label"));
        QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("urgent"));

        delete f;
    }

    void navigateTo_sets_currentView_and_currentItemId()
    {
        Fixture *f = makeFixture();
        f->sidebar->navigateTo(QStringLiteral("filter"), QStringLiteral("filter-42"));
        QCOMPARE(f->sidebar->currentView(),   QStringLiteral("filter"));
        QCOMPARE(f->sidebar->currentItemId(), QStringLiteral("filter-42"));
        delete f;
    }

    // =====================================================================
    // Favorites
    // =====================================================================

    void favorites_includes_starred_project()
    {
        Fixture *f = makeFixture();

        Project *work = f->store->projectById(f->workId);
        QVERIFY(work);
        work->setIsFavorite(true);
        // Nudge the sidebar.
        emit f->store->projectsChanged();

        const QVariantList favs = f->sidebar->favorites();

        // Find the project entry in the favorites projection.
        bool found = false;
        for (const QVariant &v : favs) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("type")).toString() == QLatin1String("project") &&
                m.value(QStringLiteral("id")).toString() == f->workId) {
                found = true;
                QCOMPARE(m.value(QStringLiteral("name")).toString(), QStringLiteral("Work"));
                break;
            }
        }
        QVERIFY(found);

        delete f;
    }

    void favorites_unstarring_removes_from_list()
    {
        Fixture *f = makeFixture();

        Project *work = f->store->projectById(f->workId);
        QVERIFY(work);

        work->setIsFavorite(true);
        emit f->store->projectsChanged();
        QCOMPARE(f->sidebar->favorites().size(), 1);

        work->setIsFavorite(false);
        emit f->store->projectsChanged();
        QCOMPARE(f->sidebar->favorites().size(), 0);

        delete f;
    }

    void favorites_includes_starred_label_and_filter()
    {
        Fixture *f = makeFixture();

        Label *l = f->store->addLabel(QStringLiteral("urgent"));
        QVERIFY(l);
        l->setIsFavorite(true);
        emit f->store->labelsChanged();

        Filter *fl = f->store->addFilter(QStringLiteral("My filter"), QStringLiteral("today"));
        QVERIFY(fl);
        fl->setIsFavorite(true);
        emit f->store->filtersChanged();

        const QVariantList favs = f->sidebar->favorites();
        bool labelOK = false;
        bool filterOK = false;
        for (const QVariant &v : favs) {
            const QVariantMap m = v.toMap();
            const QString type = m.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("label") && m.value(QStringLiteral("name")).toString() == QLatin1String("urgent"))
                labelOK = true;
            if (type == QLatin1String("filter") && m.value(QStringLiteral("name")).toString() == QLatin1String("My filter"))
                filterOK = true;
        }
        QVERIFY(labelOK);
        QVERIFY(filterOK);

        delete f;
    }

    // =====================================================================
    // Labels & label ordering
    // =====================================================================

    void labelOrder_returns_empty_for_fresh_settings()
    {
        Fixture *f = makeFixture();
        const QStringList order = f->sidebar->labelOrder();
        QVERIFY(order.isEmpty());
        delete f;
    }

    void labels_returns_natural_order_when_no_saved_order()
    {
        Fixture *f = makeFixture();

        Label *l1 = f->store->addLabel(QStringLiteral("alpha"));
        Label *l2 = f->store->addLabel(QStringLiteral("beta"));
        Label *l3 = f->store->addLabel(QStringLiteral("gamma"));
        QVERIFY(l1 && l2 && l3);
        emit f->store->labelsChanged();

        const QVariantList ls = f->sidebar->labels();
        QCOMPARE(ls.size(), 3);
        // No saved order -> they're appended in DataStore order (the order
        // we added them).
        QCOMPARE(ls.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("alpha"));
        QCOMPARE(ls.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("beta"));
        QCOMPARE(ls.at(2).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("gamma"));

        delete f;
    }

    void setLabelOrder_persists_to_qsettings()
    {
        Fixture *f = makeFixture();

        QStringList order = {QStringLiteral("id-1"), QStringLiteral("id-2")};
        f->sidebar->setLabelOrder(order);

        QSettings s;
        const QStringList read = s.value(QStringLiteral("sidebar/labelOrder")).toStringList();
        QCOMPARE(read, order);
        // Round-trip via the public API too.
        QCOMPARE(f->sidebar->labelOrder(), order);

        delete f;
    }

    void setLabelOrder_emits_labelOrderChanged_and_labelsChanged()
    {
        Fixture *f = makeFixture();
        QSignalSpy orderSpy(f->sidebar, &SidebarViewModel::labelOrderChanged);
        QSignalSpy labelsSpy(f->sidebar, &SidebarViewModel::labelsChanged);

        f->sidebar->setLabelOrder({QStringLiteral("a"), QStringLiteral("b")});

        QCOMPARE(orderSpy.count(), 1);
        QCOMPARE(labelsSpy.count(), 1);

        delete f;
    }

    void labels_uses_saved_order_when_present()
    {
        Fixture *f = makeFixture();

        Label *l1 = f->store->addLabel(QStringLiteral("alpha"));
        Label *l2 = f->store->addLabel(QStringLiteral("beta"));
        Label *l3 = f->store->addLabel(QStringLiteral("gamma"));
        QVERIFY(l1 && l2 && l3);

        // Save an explicit order: gamma, alpha, beta.
        f->sidebar->setLabelOrder({l3->id(), l1->id(), l2->id()});
        emit f->store->labelsChanged();

        const QVariantList ls = f->sidebar->labels();
        QCOMPARE(ls.size(), 3);
        QCOMPARE(ls.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("gamma"));
        QCOMPARE(ls.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("alpha"));
        QCOMPARE(ls.at(2).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("beta"));

        delete f;
    }

    void labels_appends_unordered_labels_at_end()
    {
        Fixture *f = makeFixture();

        Label *l1 = f->store->addLabel(QStringLiteral("alpha"));
        Label *l2 = f->store->addLabel(QStringLiteral("beta"));
        QVERIFY(l1 && l2);

        // Save order containing only alpha.
        f->sidebar->setLabelOrder({l1->id()});

        // Now create gamma — not in saved order; should appear at the end.
        Label *l3 = f->store->addLabel(QStringLiteral("gamma"));
        QVERIFY(l3);
        emit f->store->labelsChanged();

        const QVariantList ls = f->sidebar->labels();
        QCOMPARE(ls.size(), 3);
        QCOMPARE(ls.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("alpha"));
        // beta and gamma fall through to pass-2 in DataStore order: beta then gamma.
        QCOMPARE(ls.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("beta"));
        QCOMPARE(ls.at(2).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("gamma"));

        delete f;
    }

    void labels_skips_stale_ids_in_saved_order()
    {
        Fixture *f = makeFixture();

        Label *l1 = f->store->addLabel(QStringLiteral("alpha"));
        QVERIFY(l1);

        // Saved order references alpha and a deleted/stale id.
        f->sidebar->setLabelOrder({QStringLiteral("ghost-id"), l1->id()});
        emit f->store->labelsChanged();

        const QVariantList ls = f->sidebar->labels();
        QCOMPARE(ls.size(), 1);
        QCOMPARE(ls.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("alpha"));

        delete f;
    }

    void datastore_labelsChanged_triggers_sidebar_labelsChanged()
    {
        Fixture *f = makeFixture();
        QSignalSpy spy(f->sidebar, &SidebarViewModel::labelsChanged);

        // Adding a label fires DataStore::labelsChanged, which the sidebar
        // listens to and rebroadcasts as labelsChanged.
        f->store->addLabel(QStringLiteral("ping"));

        QVERIFY(spy.count() >= 1);
        delete f;
    }

    void datastore_tasksChanged_recomputes_counts()
    {
        Fixture *f = makeFixture();
        QCOMPARE(f->sidebar->inboxCount(), 0);

        f->store->addTask(QStringLiteral("New"), f->inboxId);
        // addTask emits tasksChanged, but updateCounts() runs via the 50 ms
        // debounce timer (Q3.24), not synchronously — QTRY_COMPARE polls until
        // the debounce fires.
        QTRY_COMPARE(f->sidebar->inboxCount(), 1);
        delete f;
    }

    // =====================================================================
    // Sidebar visibility
    // =====================================================================

    void toggleSidebar_flips_sidebarVisible()
    {
        Fixture *f = makeFixture();
        QSignalSpy spy(f->sidebar, &SidebarViewModel::sidebarVisibleChanged);

        QVERIFY(f->sidebar->sidebarVisible());
        f->sidebar->toggleSidebar();
        QVERIFY(!f->sidebar->sidebarVisible());
        f->sidebar->toggleSidebar();
        QVERIFY(f->sidebar->sidebarVisible());
        QCOMPARE(spy.count(), 2);

        delete f;
    }
};

QTEST_GUILESS_MAIN(TestSidebarViewModel)
#include "test_sidebarviewmodel.moc"
