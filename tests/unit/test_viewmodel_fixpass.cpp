// Regression tests for the ViewModel fix pass (findings V1–V14, P5, P10, P12,
// M3, M11, S2-VM). Each slot names the finding it pins; the comment states the
// pre-fix behaviour it guards against.

#include <QtTest>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>

#include <memory>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "model/filter.h"
#include "services/undostack.h"
#include "utils/settingskeys.h"
#include "viewmodels/taskviewmodel.h"
#include "viewmodels/karmaviewmodel.h"
#include "viewmodels/settingsviewmodel.h"
#include "viewmodels/sidebarviewmodel.h"
#include "viewmodels/projectviewmodel.h"
#include "viewmodels/notificationsviewmodel.h"

namespace {

const QString kFmt = QStringLiteral("yyyy-MM-dd");

QString isoDay(int offset) { return QDate::currentDate().addDays(offset).toString(kFmt); }

struct Store {
    DataStore store;
    QString inboxId;
    Store()
    {
        Project *inbox = store.addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        inboxId = inbox->id();
    }
};

QStringList visibleIds(const TaskViewModel &vm)
{
    QStringList out;
    for (int i = 0; i < vm.rowCount(); ++i)
        out << vm.data(vm.index(i, 0), TaskViewModel::IdRole).toString();
    return out;
}

// Mirror of the main.cpp TaskViewModel::taskChanged → Karma wiring.
void wireKarma(TaskViewModel &vm, KarmaViewModel &karma, DataStore *store)
{
    auto prev = std::make_shared<QHash<QString, bool>>();
    for (auto *t : store->allTasks())
        prev->insert(t->id(), t->isCompleted());
    QObject::connect(&vm, &TaskViewModel::taskChanged, &karma,
                     [&karma, store, prev](const QString &id) {
        Task *t = store->taskById(id);
        if (!t) return;
        const bool now = t->isCompleted();
        const bool was = prev->value(id, false);
        if (now && !was)      karma.onTaskCompleted();
        else if (!now && was) karma.onTaskUncompleted();
        prev->insert(id, now);
    });
}

QDate mondayOfThisWeek()
{
    const QDate today = QDate::currentDate();
    return today.addDays(-(today.dayOfWeek() - 1));
}

} // namespace

class TestViewModelFixPass : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_VmFixPass"));
        QCoreApplication::setApplicationName(QStringLiteral("test_viewmodel_fixpass"));
    }
    void init()    { QSettings s; s.clear(); s.sync(); }
    void cleanup() { QSettings s; s.clear(); s.sync(); }

    // ── V1 ───────────────────────────────────────────────────────────────
    // Pre-fix: addTask used m_currentProjectId unchecked → a label name /
    // stale id produced an orphan task with project_id = "work".
    void v1_addTask_falls_back_to_inbox_for_non_project_id()
    {
        Store s;
        TaskViewModel vm(&s.store);
        vm.setCurrentProjectId(QStringLiteral("work"));   // a label name
        const QString id = vm.addTask(QStringLiteral("x"));
        QVERIFY(!id.isEmpty());
        QCOMPARE(s.store.taskById(id)->projectId(), s.inboxId);

        Project *gone = s.store.addProject(QStringLiteral("Gone"));
        const QString goneId = gone->id();
        QVERIFY(s.store.removeProject(goneId));
        vm.setCurrentProjectId(goneId);
        const QString id2 = vm.addTask(QStringLiteral("y"));
        QCOMPARE(s.store.taskById(id2)->projectId(), s.inboxId);

        // A live project is still honoured.
        Project *work = s.store.addProject(QStringLiteral("Work"));
        vm.setCurrentProjectId(work->id());
        const QString id3 = vm.addTask(QStringLiteral("z"));
        QCOMPARE(s.store.taskById(id3)->projectId(), work->id());
    }

    // ── V2 ───────────────────────────────────────────────────────────────
    // Pre-fix: the saved filter's UUID was evaluated as a DSL query → empty.
    void v2_saved_filter_by_id_shows_matching_tasks_and_follows_edits()
    {
        Store s;
        Task *t = s.store.addTask(QStringLiteral("tagged"), s.inboxId);
        t->setLabels({QStringLiteral("urgent")});
        s.store.addTask(QStringLiteral("plain"), s.inboxId);
        Filter *f = s.store.addFilter(QStringLiteral("Urgent"), QStringLiteral("@urgent"));
        QVERIFY(f);

        TaskViewModel vm(&s.store);
        vm.setFilterMode(TaskViewModel::FilterMode::Filter);
        vm.setCurrentFilter(f->id());
        QCOMPARE(visibleIds(vm), QStringList{t->id()});

        // Raw DSL (filter chips) still works.
        vm.setCurrentFilter(QStringLiteral("@urgent"));
        QCOMPARE(visibleIds(vm), QStringList{t->id()});

        // Editing the active saved filter's query re-evaluates the list.
        vm.setCurrentFilter(f->id());
        f->setQuery(QStringLiteral("@nomatch"));
        emit s.store.filtersChanged();
        QTRY_COMPARE(vm.rowCount(), 0);
    }

    // ── V3 ───────────────────────────────────────────────────────────────
    // Pre-fix: completeTask → store tasksChanged recount ran BEFORE
    // onTaskCompleted, so the goal crossing was never detected in the app.
    void v3_daily_goal_bonus_and_streak_fire_through_completeTask()
    {
        Store s;
        s.store.setSetting(QLatin1String(SettingsKeys::DailyGoal), 2);
        s.store.setSetting(QLatin1String(SettingsKeys::WeeklyGoal), 0);
        Task *a = s.store.addTask(QStringLiteral("a"), s.inboxId);
        Task *b = s.store.addTask(QStringLiteral("b"), s.inboxId);

        TaskViewModel vm(&s.store);
        KarmaViewModel karma(&s.store);
        wireKarma(vm, karma, &s.store);

        QVERIFY(vm.completeTask(a->id()));
        QCOMPARE(karma.karmaScore(), 1);
        QVERIFY(vm.completeTask(b->id()));
        QCOMPARE(karma.karmaScore(), 7);       // +1 +5 bonus
        QCOMPARE(karma.dailyStreak(), 1);

        QVERIFY(vm.uncompleteTask(b->id()));
        QCOMPARE(karma.karmaScore(), 1);       // -1 -5
        QCOMPARE(karma.dailyStreak(), 0);
    }

    void v3_weekly_goal_bonus_fires_through_completeTask()
    {
        Store s;
        s.store.setSetting(QLatin1String(SettingsKeys::DailyGoal), 0);
        s.store.setSetting(QLatin1String(SettingsKeys::WeeklyGoal), 2);
        Task *a = s.store.addTask(QStringLiteral("a"), s.inboxId);
        Task *b = s.store.addTask(QStringLiteral("b"), s.inboxId);

        TaskViewModel vm(&s.store);
        KarmaViewModel karma(&s.store);
        wireKarma(vm, karma, &s.store);

        QVERIFY(vm.completeTask(a->id()));
        QVERIFY(vm.completeTask(b->id()));
        QCOMPARE(karma.karmaScore(), 12);      // 1 + 1 + 10
        QCOMPARE(karma.weeklyStreak(), 1);
    }

    // Uncompleting a task that was completed on an EARLIER day must not
    // un-cross today's goal.
    void v3_uncompleting_yesterdays_task_does_not_uncross_today()
    {
        Store s;
        s.store.setSetting(QLatin1String(SettingsKeys::DailyGoal), 1);
        s.store.setSetting(QLatin1String(SettingsKeys::WeeklyGoal), 0);
        Task *y = s.store.addTask(QStringLiteral("yesterday"), s.inboxId);
        y->setIsCompleted(true);
        y->setCompletedAt(QDateTime(QDate::currentDate().addDays(-1), QTime(12, 0)).toUTC());
        Task *t = s.store.addTask(QStringLiteral("today"), s.inboxId);

        TaskViewModel vm(&s.store);
        KarmaViewModel karma(&s.store);
        wireKarma(vm, karma, &s.store);

        QVERIFY(vm.completeTask(t->id()));
        QCOMPARE(karma.karmaScore(), 6);
        QVERIFY(vm.uncompleteTask(y->id()));
        QCOMPARE(karma.karmaScore(), 5);       // only the base -1
        QCOMPARE(karma.dailyStreak(), 1);
    }

    // Un-cross then re-cross on the same day restores (not resets) a streak.
    void v3_daily_uncross_recross_keeps_long_streak()
    {
        {
            QSettings qs;
            qs.setValue(QStringLiteral("karma/lastStreakIncrementDate"),
                        QDate::currentDate().addDays(-1).toString(Qt::ISODate));
        }
        Store s;
        s.store.setSetting(QLatin1String(SettingsKeys::DailyGoal), 1);
        s.store.setSetting(QLatin1String(SettingsKeys::WeeklyGoal), 0);
        s.store.setSetting(QLatin1String(SettingsKeys::DailyStreak), 10);
        Task *x = s.store.addTask(QStringLiteral("x"), s.inboxId);

        TaskViewModel vm(&s.store);
        KarmaViewModel karma(&s.store);
        wireKarma(vm, karma, &s.store);

        QVERIFY(vm.completeTask(x->id()));
        QCOMPARE(karma.dailyStreak(), 11);
        QVERIFY(vm.uncompleteTask(x->id()));
        QCOMPARE(karma.dailyStreak(), 10);
        QVERIFY(vm.completeTask(x->id()));
        QCOMPARE(karma.dailyStreak(), 11);     // pre-fix: reset to 1
    }

    void v3_weekly_streak_resets_after_missed_week_data()
    {
        QTest::addColumn<int>("lastBumpWeeksAgo");   // -1 = never recorded
        QTest::addColumn<bool>("vacation");
        QTest::addColumn<int>("expectedStreak");
        QTest::newRow("previous week extends")   << 1  << false << 6;
        QTest::newRow("missed week resets")      << 3  << false << 1;
        QTest::newRow("vacation keeps")          << 3  << true  << 6;
        QTest::newRow("same week no double bump") << 0 << false << 5;
        QTest::newRow("never recorded extends")  << -1 << false << 6;
    }
    void v3_weekly_streak_resets_after_missed_week()
    {
        QFETCH(int, lastBumpWeeksAgo);
        QFETCH(bool, vacation);
        QFETCH(int, expectedStreak);
        if (lastBumpWeeksAgo >= 0) {
            QSettings qs;
            qs.setValue(QStringLiteral("karma/lastWeeklyStreakWeekStart"),
                        mondayOfThisWeek().addDays(-7 * lastBumpWeeksAgo).toString(Qt::ISODate));
        }
        Store s;
        s.store.setSetting(QLatin1String(SettingsKeys::WeekStart), 1);
        s.store.setSetting(QLatin1String(SettingsKeys::DailyGoal), 0);
        s.store.setSetting(QLatin1String(SettingsKeys::WeeklyGoal), 1);
        s.store.setSetting(QLatin1String(SettingsKeys::WeeklyStreak), 5);
        s.store.setSetting(QLatin1String(SettingsKeys::VacationMode), vacation);
        Task *x = s.store.addTask(QStringLiteral("x"), s.inboxId);

        TaskViewModel vm(&s.store);
        KarmaViewModel karma(&s.store);
        wireKarma(vm, karma, &s.store);

        QVERIFY(vm.completeTask(x->id()));
        QCOMPARE(karma.weeklyStreak(), expectedStreak);
    }

    // ── V4 ───────────────────────────────────────────────────────────────
    // Pre-fix: default sort "order" + global pinned partition → Upcoming day
    // headers split / out of order.
    void v4_upcoming_is_date_sorted_with_pinned_inside_its_day()
    {
        Store s;
        Task *a = s.store.addTask(QStringLiteral("+3"), s.inboxId);
        a->setDueDate(isoDay(3)); a->setOrder(1);
        Task *b = s.store.addTask(QStringLiteral("+1"), s.inboxId);
        b->setDueDate(isoDay(1)); b->setOrder(2);
        Task *c = s.store.addTask(QStringLiteral("+1 pinned"), s.inboxId);
        c->setDueDate(isoDay(1)); c->setOrder(3); c->setIsPinned(true);
        Task *d = s.store.addTask(QStringLiteral("+5 pinned"), s.inboxId);
        d->setDueDate(isoDay(5)); d->setOrder(0); d->setIsPinned(true);

        TaskViewModel vm(&s.store);
        vm.setFilterMode(TaskViewModel::FilterMode::Upcoming);
        QCOMPARE(visibleIds(vm), (QStringList{c->id(), b->id(), a->id(), d->id()}));
    }

    // ── V5 ───────────────────────────────────────────────────────────────
    void v5_priority_sort_puts_p1_first()
    {
        Store s;
        Task *p4 = s.store.addTask(QStringLiteral("p4"), s.inboxId);
        p4->setPriority(4); p4->setOrder(1);
        Task *p1 = s.store.addTask(QStringLiteral("p1"), s.inboxId);
        p1->setPriority(1); p1->setOrder(2);
        Task *p2 = s.store.addTask(QStringLiteral("p2"), s.inboxId);
        p2->setPriority(2); p2->setOrder(3);

        TaskViewModel vm(&s.store);
        vm.setSortBy(QStringLiteral("priority"));
        QCOMPARE(visibleIds(vm), (QStringList{p1->id(), p2->id(), p4->id()}));
    }

    // ── V6 ───────────────────────────────────────────────────────────────
    // Pre-fix: the QSignalBlocker swallowed the explicit dataChanged → no
    // autosave / sync after a drag reorder.
    void v6_moveTaskOrder_emits_store_dataChanged()
    {
        Store s;
        Task *a = s.store.addTask(QStringLiteral("a"), s.inboxId);
        a->setOrder(1);
        Task *b = s.store.addTask(QStringLiteral("b"), s.inboxId);
        b->setOrder(2);

        TaskViewModel vm(&s.store);
        QCOMPARE(visibleIds(vm), (QStringList{a->id(), b->id()}));
        QSignalSpy spy(&s.store, &DataStore::dataChanged);
        QVERIFY(vm.moveTaskOrder(b->id(), 0));
        QVERIFY(spy.count() >= 1);
        QVERIFY(b->order() < a->order());
        QCOMPARE(visibleIds(vm), (QStringList{b->id(), a->id()}));
    }

    // ── V7 ───────────────────────────────────────────────────────────────
    // Pre-fix: subtasks were stripped from Today even when their parent was
    // not listed there, while the sidebar badge counted them.
    void v7_subtasks_visible_in_today_unless_parent_listed()
    {
        Store s;
        Task *p = s.store.addTask(QStringLiteral("parent no due"), s.inboxId);
        Task *c1 = s.store.addTask(QStringLiteral("child due today"), s.inboxId);
        c1->setParentId(p->id()); c1->setDueDate(isoDay(0));
        Task *q = s.store.addTask(QStringLiteral("parent due today"), s.inboxId);
        q->setDueDate(isoDay(0));
        Task *c2 = s.store.addTask(QStringLiteral("nested child due today"), s.inboxId);
        c2->setParentId(q->id()); c2->setDueDate(isoDay(0));

        TaskViewModel vm(&s.store);
        vm.setFilterMode(TaskViewModel::FilterMode::Today);
        const QStringList ids = visibleIds(vm);
        QVERIFY(ids.contains(c1->id()));
        QVERIFY(ids.contains(q->id()));
        QVERIFY(!ids.contains(c2->id()));   // renders nested under q
        QVERIFY(!ids.contains(p->id()));
        QCOMPARE(ids.size(), 2);

        // Hierarchy views stay top-level only.
        vm.setFilterMode(TaskViewModel::FilterMode::Inbox);
        QVERIFY(!visibleIds(vm).contains(c1->id()));

        SidebarViewModel sidebar(&s.store, nullptr);
        QCOMPARE(sidebar.todayCount(), 2);
    }

    // ── V8 ───────────────────────────────────────────────────────────────
    void v8_removeCompleted_handles_subtasks()
    {
        Store s;
        Project *w = s.store.addProject(QStringLiteral("W"));
        const QString wid = w->id();
        auto add = [&](const QString &name, const QString &parent, bool done) {
            Task *t = s.store.addTask(name, wid);
            if (!parent.isEmpty()) t->setParentId(parent);
            if (done) t->setIsCompleted(true);
            return t;
        };
        Task *p1 = add(QStringLiteral("open parent"), {}, false);
        Task *c1 = add(QStringLiteral("done child of open"), p1->id(), true);
        Task *p2 = add(QStringLiteral("done parent, active child"), {}, true);
        Task *a2 = add(QStringLiteral("active child"), p2->id(), false);
        Task *p3 = add(QStringLiteral("done parent, done child"), {}, true);
        Task *c3 = add(QStringLiteral("done child"), p3->id(), true);
        Task *t4 = add(QStringLiteral("done top"), {}, true);

        ProjectViewModel pvm(&s.store);
        QCOMPARE(pvm.removeCompletedTasksInProject(wid), 4);
        QVERIFY(c1->isDeleted());
        QVERIFY(p3->isDeleted());
        QVERIFY(c3->isDeleted());
        QVERIFY(t4->isDeleted());
        QVERIFY(!p1->isDeleted());
        QVERIFY(!p2->isDeleted());   // kept: removing it would delete a2
        QVERIFY(!a2->isDeleted());
    }

    // ── V9 ───────────────────────────────────────────────────────────────
    // Pre-fix: any settingsChanged without a pending key (karma_score on each
    // completion) fired EVERY NOTIFY (sync timer restarted, theme re-applied).
    void v9_unrelated_setting_write_fires_no_notify()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        QSignalSpy syncMode(&vm, &SettingsViewModel::syncModeChanged);
        QSignalSpy syncInterval(&vm, &SettingsViewModel::syncIntervalChanged);
        QSignalSpy theme(&vm, &SettingsViewModel::themeChanged);
        QSignalSpy language(&vm, &SettingsViewModel::languageChanged);

        store.setSetting(QLatin1String(SettingsKeys::KarmaScore), 5);
        store.setSetting(QLatin1String(SettingsKeys::RecentSearches), QStringList{QStringLiteral("q")});
        QCOMPARE(syncMode.count(), 0);
        QCOMPARE(syncInterval.count(), 0);
        QCOMPARE(theme.count(), 0);
        QCOMPARE(language.count(), 0);

        // An external change of one key notifies exactly that property.
        store.setSetting(QLatin1String(SettingsKeys::SyncMode), QStringLiteral("manual"));
        QCOMPARE(syncMode.count(), 1);
        QCOMPARE(syncInterval.count(), 0);
        QCOMPARE(theme.count(), 0);

        // The typed setter fires its own NOTIFY exactly once (no double).
        vm.setTheme(QStringLiteral("dark"));
        QCOMPARE(theme.count(), 1);
        QCOMPARE(syncMode.count(), 1);
    }

    // ── V10 ──────────────────────────────────────────────────────────────
    void v10_weekStart_legacy_zero_reads_as_monday()
    {
        DataStore store;
        SettingsViewModel vm(&store);
        store.setSetting(QLatin1String(SettingsKeys::WeekStart), 0);
        QCOMPARE(vm.weekStart(), 1);
        vm.setWeekStart(0);
        QCOMPARE(store.setting(QLatin1String(SettingsKeys::WeekStart)).toInt(), 1);
        vm.setWeekStart(9);
        QCOMPARE(vm.weekStart(), 1);
        vm.setWeekStart(7);
        QCOMPARE(vm.weekStart(), 7);
    }

    // ── V11 ──────────────────────────────────────────────────────────────
    void v11_emptyTrash_emits_tasksPurged_not_tasksRemoved()
    {
        Store s;
        Task *t = s.store.addTask(QStringLiteral("trash me"), s.inboxId);
        QVERIFY(s.store.removeTask(t->id()));
        TaskViewModel vm(&s.store);
        QSignalSpy purged(&vm, &TaskViewModel::tasksPurged);
        QSignalSpy removed(&vm, &TaskViewModel::tasksRemoved);
        vm.emptyTrash();
        QCOMPARE(purged.count(), 1);
        QCOMPARE(removed.count(), 0);
    }

    // ── V12 ──────────────────────────────────────────────────────────────
    void v12_date_rollover_refreshes_today()
    {
        Store s;
        Task *t = s.store.addTask(QStringLiteral("tomorrow"), s.inboxId);
        t->setDueDate(isoDay(1));
        TaskViewModel vm(&s.store);
        vm.setFilterMode(TaskViewModel::FilterMode::Today);
        QVERIFY(!visibleIds(vm).contains(t->id()));

        // Simulate "the day the task is due has arrived" without any store
        // signal, then let the rollover check run.
        t->setDueDate(isoDay(0));
        QVERIFY(!visibleIds(vm).contains(t->id()));
        vm.simulateDateRolloverForTesting();
        QVERIFY(visibleIds(vm).contains(t->id()));
    }

    // ── V13 ──────────────────────────────────────────────────────────────
    // Pre-fix: a store reload made every already-completed task look newly
    // completed → a flood of "Задача выполнена" notifications.
    void v13_store_reload_does_not_flood_completion_notifications()
    {
        Store s;
        Task *a = s.store.addTask(QStringLiteral("a"), s.inboxId);
        Task *b = s.store.addTask(QStringLiteral("b"), s.inboxId);
        const QString bId = b->id();
        NotificationsViewModel nvm(&s.store, nullptr);
        QCOMPARE(nvm.count(), 0);

        // Completed "elsewhere" (e.g. arrives via a sync merge reload).
        a->setIsCompleted(true);
        s.store.loadFromJson(s.store.toJson());
        QCOMPARE(nvm.count(), 0);

        // Normal completions after the reload still notify.
        Task *b2 = s.store.taskById(bId);
        QVERIFY(b2);
        b2->setIsCompleted(true);
        emit s.store.tasksChanged();
        QCOMPARE(nvm.count(), 1);
    }

    // ── V14 ──────────────────────────────────────────────────────────────
    void v14_failed_reorder_does_not_reparent()
    {
        Store s;
        Project *parent = s.store.addProject(QStringLiteral("Parent"));
        Project *target = s.store.addProject(QStringLiteral("Archived child"));
        target->setParentId(parent->id());
        target->setIsArchived(true);
        Project *dragged = s.store.addProject(QStringLiteral("Dragged"));
        ProjectViewModel pvm(&s.store);

        QVERIFY(!pvm.reorderProjectById(dragged->id(), target->id(), true));
        QCOMPARE(dragged->parentId(), QString());
    }

    // ── P5 ───────────────────────────────────────────────────────────────
    void p5_every_bang_advances_from_completion_date()
    {
        Store s;
        Task *t = s.store.addTask(QStringLiteral("water plants"), s.inboxId);
        t->setDueDate(isoDay(-10));
        t->setDueString(QStringLiteral("every! 3 days"));
        t->setDueIsRecurring(true);
        t->setDueRecurrenceRule(QStringLiteral("FREQ=DAILY;INTERVAL=3;FROM=COMPLETION"));
        Task *u = s.store.addTask(QStringLiteral("scheduled"), s.inboxId);
        u->setDueDate(isoDay(-10));
        u->setDueString(QStringLiteral("every 3 days"));
        u->setDueIsRecurring(true);
        u->setDueRecurrenceRule(QStringLiteral("FREQ=DAILY;INTERVAL=3"));

        TaskViewModel vm(&s.store);
        QVERIFY(vm.completeTask(t->id()));
        QCOMPARE(t->dueDate(), isoDay(3));       // from today
        QVERIFY(vm.completeTask(u->id()));
        QCOMPARE(u->dueDate(), isoDay(-7));      // from the old due date
    }

    // ── P10 ──────────────────────────────────────────────────────────────
    // Pre-fix: undoing B pushed "complete B" again → Ctrl+Z toggled B forever.
    void p10_undo_walks_back_instead_of_ping_pong()
    {
        Store s;
        Task *a = s.store.addTask(QStringLiteral("a"), s.inboxId);
        Task *b = s.store.addTask(QStringLiteral("b"), s.inboxId);
        UndoStack undo;
        TaskViewModel vm(&s.store);
        vm.setUndoStack(&undo);

        QVERIFY(vm.completeTask(a->id()));
        QVERIFY(vm.completeTask(b->id()));
        QCOMPARE(undo.count(), 2);

        QVERIFY(undo.undo());
        QVERIFY(!b->isCompleted());
        QVERIFY(a->isCompleted());
        QCOMPARE(undo.count(), 1);

        QVERIFY(undo.undo());
        QVERIFY(!a->isCompleted());
        QCOMPARE(undo.count(), 0);
    }

    // ── P12 ──────────────────────────────────────────────────────────────
    void p12_quickadd_extra_hash_tokens_become_labels()
    {
        Store s;
        Project *work = s.store.addProject(QStringLiteral("Work"));
        TaskViewModel vm(&s.store);
        const QString id = vm.addTaskFromQuickAdd(QStringLiteral("Buy milk #Work #urgent @home"));
        QVERIFY(!id.isEmpty());
        Task *t = s.store.taskById(id);
        QCOMPARE(t->projectId(), work->id());
        QVERIFY(t->labels().contains(QStringLiteral("urgent")));
        QVERIFY(t->labels().contains(QStringLiteral("home")));
        QVERIFY(!t->content().contains(QStringLiteral("urgent")));

        const QVariantMap preview = vm.previewQuickAdd(QStringLiteral("Buy milk #Work #urgent"));
        QVERIFY(preview.value(QStringLiteral("labels")).toStringList()
                    .contains(QStringLiteral("urgent")));
    }

    // ── M3 ───────────────────────────────────────────────────────────────
    void m3_snooze_keeps_recurrence_and_time_and_is_undoable()
    {
        Store s;
        Task *t = s.store.addTask(QStringLiteral("standup"), s.inboxId);
        t->setDueDate(isoDay(2));
        t->setDueDatetime(isoDay(2) + QStringLiteral("T09:30:00"));
        t->setDueString(QStringLiteral("every monday"));
        t->setDueIsRecurring(true);
        t->setDueRecurrenceRule(QStringLiteral("FREQ=WEEKLY;BYDAY=MO"));
        UndoStack undo;
        TaskViewModel vm(&s.store);
        vm.setUndoStack(&undo);

        QVERIFY(vm.snoozeTask(t->id(), 1));
        QCOMPARE(t->dueDate(), isoDay(3));
        QVERIFY(t->dueDatetime().startsWith(isoDay(3) + QStringLiteral("T09:30")));
        QCOMPARE(t->dueString(), QStringLiteral("every monday"));
        QVERIFY(t->dueIsRecurring());
        QCOMPARE(t->dueRecurrenceRule(), QStringLiteral("FREQ=WEEKLY;BYDAY=MO"));
        QCOMPARE(undo.count(), 1);

        QVERIFY(undo.undo());
        QCOMPARE(t->dueDate(), isoDay(2));
        QVERIFY(t->dueDatetime().startsWith(isoDay(2) + QStringLiteral("T09:30")));
        QCOMPARE(undo.count(), 0);
    }

    void m3_snooze_overdue_and_undated_land_relative_to_today()
    {
        Store s;
        Task *overdue = s.store.addTask(QStringLiteral("overdue"), s.inboxId);
        overdue->setDueDate(isoDay(-5));
        Task *undated = s.store.addTask(QStringLiteral("undated"), s.inboxId);
        TaskViewModel vm(&s.store);

        QVERIFY(vm.snoozeTask(overdue->id(), 1));
        QCOMPARE(overdue->dueDate(), isoDay(1));
        QVERIFY(vm.snoozeTask(undated->id(), 2));
        QCOMPARE(undated->dueDate(), isoDay(2));
        QVERIFY(!vm.snoozeTask(QStringLiteral("nope"), 1));
    }

    // ── M11 ──────────────────────────────────────────────────────────────
    void m11_bulkMoveToProject_single_undo_restores_all()
    {
        Store s;
        Project *work = s.store.addProject(QStringLiteral("Work"));
        Section *sec = s.store.addSection(QStringLiteral("S"), s.inboxId);
        Task *a = s.store.addTask(QStringLiteral("a"), s.inboxId);
        a->setSectionId(sec->id());
        Task *b = s.store.addTask(QStringLiteral("b"), s.inboxId);
        Task *c = s.store.addTask(QStringLiteral("c"), s.inboxId);
        Task *child = s.store.addTask(QStringLiteral("child"), s.inboxId);
        child->setParentId(a->id());
        child->setSectionId(sec->id());

        UndoStack undo;
        TaskViewModel vm(&s.store);
        vm.setUndoStack(&undo);
        vm.selectTask(a->id());
        vm.selectTask(b->id());
        vm.selectTask(c->id());
        vm.bulkMoveToProject(work->id());

        QCOMPARE(a->projectId(), work->id());
        QCOMPARE(child->projectId(), work->id());
        QCOMPARE(undo.count(), 1);

        QVERIFY(undo.undo());
        QCOMPARE(undo.count(), 0);
        for (Task *t : {a, b, c, child})
            QCOMPARE(t->projectId(), s.inboxId);
        QCOMPARE(a->sectionId(), sec->id());
        QCOMPARE(child->sectionId(), sec->id());
    }

    // ── S2-VM ────────────────────────────────────────────────────────────
    // Pre-fix: a whole-store reload closed the editor for good.
    void s2_selected_task_reselected_after_store_reload()
    {
        Store s;
        Task *t = s.store.addTask(QStringLiteral("open in editor"), s.inboxId);
        const QString id = t->id();
        TaskViewModel vm(&s.store);
        vm.setSelectedTask(t);

        s.store.loadFromJson(s.store.toJson());

        auto *sel = qobject_cast<Task *>(vm.selectedTask());
        QVERIFY(sel);
        QCOMPARE(sel->id(), id);
        QCOMPARE(sel, s.store.taskById(id));

        // A reload in which the task no longer exists leaves it unselected.
        QJsonObject root = s.store.toJson();
        root[QStringLiteral("tasks")] = QJsonArray{};
        s.store.loadFromJson(root);
        QVERIFY(!vm.selectedTask());
    }
};

QTEST_GUILESS_MAIN(TestViewModelFixPass)
#include "test_viewmodel_fixpass.moc"
