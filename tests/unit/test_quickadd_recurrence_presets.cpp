#include <QtTest>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QDate>

#include "nlp/dateparser.h"
#include "storage/datastore.h"
#include "model/project.h"
#include "model/task.h"
#include "viewmodels/taskviewmodel.h"

// =============================================================================
// QuickAdd recurrence preset tests
//
// QuickAdd.qml exposes a "🔁" recurrence chip (recurChip) whose menu inserts
// canonical recurrence phrases into the task field — e.g. "каждый день",
// "каждую неделю", "каждый месяц", "каждый год", as well as annual "каждый
// год DD.MM" entries (last item: "День рождения…" → opens a date picker that
// builds the same DD.MM string). When the task is submitted, QML calls
// `taskViewModel.addTask(content, projectId, priority, dueString, labels)`
// (or addTaskFromQuickAdd which also routes through addTask). addTask then
// invokes DateParser::parse(dueString, lang) and writes the resulting RRULE
// back to the new Task via setDueRecurrenceRule + setDueIsRecurring.
//
// This suite covers two layers:
//  (a) Direct DateParser::parse for every preset phrase the QML menu inserts,
//      both Russian (default) and English (where applicable — every-X presets
//      exist in rules_en too even though the chip currently emits Russian).
//  (b) End-to-end TaskViewModel::addTask integration: the task created with
//      a preset dueString carries the correct recurrenceRule + isRecurring
//      flag on its Task object.
//
// Plus three edge cases: empty dueString, garbage dueString ("blah"), and a
// non-recurring date keyword ("сегодня") to confirm it does NOT get flagged
// as recurring.
// =============================================================================

class TestQuickAddRecurrencePresets : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Per-process unique QSettings org/app — keeps this test's QSettings
        // namespace isolated from the user's real OpenTodo install AND from
        // other tests that may run in parallel and share QStandardPaths.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_QuickAddRec"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_quickadd_recurrence_presets"));
    }

    // ---------------------------------------------------------------------
    // (a) DateParser direct: Russian presets — these are the exact phrases
    //     QuickAdd.qml's recurMenu emits when the user picks each menu item
    //     (lines ~298-326 of qml/components/QuickAdd.qml).
    // ---------------------------------------------------------------------

    void parser_ru_daily()
    {
        DateParser parser;
        const auto r = parser.parse(QString::fromUtf8("каждый день"),
                                    QStringLiteral("ru"));
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QCOMPARE(r.recurrenceRule, QStringLiteral("FREQ=DAILY"));
    }

    void parser_ru_weekly()
    {
        DateParser parser;
        const auto r = parser.parse(QString::fromUtf8("каждую неделю"),
                                    QStringLiteral("ru"));
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QCOMPARE(r.recurrenceRule, QStringLiteral("FREQ=WEEKLY"));
    }

    void parser_ru_monthly()
    {
        DateParser parser;
        const auto r = parser.parse(QString::fromUtf8("каждый месяц"),
                                    QStringLiteral("ru"));
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QCOMPARE(r.recurrenceRule, QStringLiteral("FREQ=MONTHLY"));
    }

    void parser_ru_yearly()
    {
        DateParser parser;
        const auto r = parser.parse(QString::fromUtf8("каждый год"),
                                    QStringLiteral("ru"));
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QCOMPARE(r.recurrenceRule, QStringLiteral("FREQ=YEARLY"));
    }

    // ---------------------------------------------------------------------
    // (a) DateParser direct: English equivalents — the Russian chip is the
    //     only one wired in QuickAdd.qml today, but rules_en.cpp supports
    //     "every X" recurrence so localisation work would lean on these.
    // ---------------------------------------------------------------------

    void parser_en_daily()
    {
        DateParser parser;
        const auto r = parser.parse(QStringLiteral("every day"),
                                    QStringLiteral("en"));
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QCOMPARE(r.recurrenceRule, QStringLiteral("FREQ=DAILY"));
    }

    void parser_en_weekly()
    {
        DateParser parser;
        const auto r = parser.parse(QStringLiteral("every week"),
                                    QStringLiteral("en"));
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QCOMPARE(r.recurrenceRule, QStringLiteral("FREQ=WEEKLY"));
    }

    void parser_en_monthly()
    {
        DateParser parser;
        const auto r = parser.parse(QStringLiteral("every month"),
                                    QStringLiteral("en"));
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QCOMPARE(r.recurrenceRule, QStringLiteral("FREQ=MONTHLY"));
    }

    void parser_en_yearly()
    {
        DateParser parser;
        const auto r = parser.parse(QStringLiteral("every year"),
                                    QStringLiteral("en"));
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QCOMPARE(r.recurrenceRule, QStringLiteral("FREQ=YEARLY"));
    }

    // ---------------------------------------------------------------------
    // (a) DateParser: the I22-C "Каждый год DD.MM" preset — the menu builds
    //     the phrase via Qt.formatDate(...,"dd.MM"), and the picker variant
    //     also synthesises the same shape from the picked date.
    // ---------------------------------------------------------------------

    void parser_ru_annual_with_date()
    {
        DateParser parser;
        const auto r = parser.parse(QString::fromUtf8("каждый год 9.05"),
                                    QStringLiteral("ru"));
        QVERIFY(r.valid);
        QVERIFY(r.isRecurring);
        QCOMPARE(r.recurrenceRule,
                 QStringLiteral("FREQ=YEARLY;BYMONTH=5;BYMONTHDAY=9"));
    }

    // ---------------------------------------------------------------------
    // (b) TaskViewModel end-to-end: each preset string surfaces on the new
    //     Task as dueIsRecurring()==true and the expected dueRecurrenceRule().
    //     Mirrors the call shape from QuickAdd.qml::submitTask:
    //       taskViewModel.addTask(content, selectedProjectId, priority,
    //                              selectedDueString, labels)
    // ---------------------------------------------------------------------

    void integration_addtask_daily()
    {
        // Bare recurrence "каждый день" — DateParser anchors to today and
        // returns valid=true, isRecurring=true, recurrenceRule=FREQ=DAILY.
        // TaskViewModel::addTask defaults the parser language to "en" when the
        // language setting is empty; we set it to "ru" so the Russian rules
        // table is consulted (mirrors production where the user picks a UI
        // language at first run).
        DataStore store;
        store.setSetting(QStringLiteral("language"), QStringLiteral("ru"));
        Project *inbox = store.addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        TaskViewModel vm(&store);

        const QString id = vm.addTask(QStringLiteral("Test"),
                                      inbox->id(), 4,
                                      QString::fromUtf8("каждый день"),
                                      QStringList{});
        QVERIFY(!id.isEmpty());
        Task *t = store.taskById(id);
        QVERIFY(t != nullptr);
        QVERIFY(t->dueIsRecurring());
        QCOMPARE(t->dueRecurrenceRule(), QStringLiteral("FREQ=DAILY"));
        // Bare recurrence anchors on today's date so the task surfaces in
        // Today/Upcoming views immediately rather than as undated.
        QCOMPARE(t->dueDate(), QDate::currentDate().toString(Qt::ISODate));
    }

    void integration_addtask_weekly()
    {
        DataStore store;
        store.setSetting(QStringLiteral("language"), QStringLiteral("ru"));
        Project *inbox = store.addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        TaskViewModel vm(&store);

        const QString id = vm.addTask(QStringLiteral("Test"),
                                      inbox->id(), 4,
                                      QString::fromUtf8("каждую неделю"),
                                      QStringList{});
        QVERIFY(!id.isEmpty());
        Task *t = store.taskById(id);
        QVERIFY(t != nullptr);
        QVERIFY(t->dueIsRecurring());
        QCOMPARE(t->dueRecurrenceRule(), QStringLiteral("FREQ=WEEKLY"));
        QCOMPARE(t->dueDate(), QDate::currentDate().toString(Qt::ISODate));
    }

    void integration_addtask_monthly()
    {
        DataStore store;
        store.setSetting(QStringLiteral("language"), QStringLiteral("ru"));
        Project *inbox = store.addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        TaskViewModel vm(&store);

        const QString id = vm.addTask(QStringLiteral("Test"),
                                      inbox->id(), 4,
                                      QString::fromUtf8("каждый месяц"),
                                      QStringList{});
        QVERIFY(!id.isEmpty());
        Task *t = store.taskById(id);
        QVERIFY(t != nullptr);
        QVERIFY(t->dueIsRecurring());
        QCOMPARE(t->dueRecurrenceRule(), QStringLiteral("FREQ=MONTHLY"));
        QCOMPARE(t->dueDate(), QDate::currentDate().toString(Qt::ISODate));
    }

    void integration_addtask_yearly()
    {
        DataStore store;
        store.setSetting(QStringLiteral("language"), QStringLiteral("ru"));
        Project *inbox = store.addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        TaskViewModel vm(&store);

        const QString id = vm.addTask(QStringLiteral("Test"),
                                      inbox->id(), 4,
                                      QString::fromUtf8("каждый год"),
                                      QStringList{});
        QVERIFY(!id.isEmpty());
        Task *t = store.taskById(id);
        QVERIFY(t != nullptr);
        QVERIFY(t->dueIsRecurring());
        QCOMPARE(t->dueRecurrenceRule(), QStringLiteral("FREQ=YEARLY"));
        QCOMPARE(t->dueDate(), QDate::currentDate().toString(Qt::ISODate));
    }

    // I22-C: annual preset built via the date picker — DD.MM gets stitched
    // onto "каждый год " and submitted as the dueString. Verify the task's
    // RRULE includes both BYMONTH and BYMONTHDAY (annual anchored on date).
    void integration_addtask_annual_picked_date()
    {
        DataStore store;
        store.setSetting(QStringLiteral("language"), QStringLiteral("ru"));
        Project *inbox = store.addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        TaskViewModel vm(&store);

        const QString id = vm.addTask(QStringLiteral("Birthday"),
                                      inbox->id(), 4,
                                      QString::fromUtf8("каждый год 15.03"),
                                      QStringList{});
        QVERIFY(!id.isEmpty());
        Task *t = store.taskById(id);
        QVERIFY(t != nullptr);
        QVERIFY(t->dueIsRecurring());
        QCOMPARE(t->dueRecurrenceRule(),
                 QStringLiteral("FREQ=YEARLY;BYMONTH=3;BYMONTHDAY=15"));
    }

    // ---------------------------------------------------------------------
    // Edge cases
    // ---------------------------------------------------------------------

    // Empty dueString — addTask short-circuits the parser branch entirely
    // (`if (!dueString.isEmpty())`), so the task is created but isRecurring
    // remains false and the rule string stays empty.
    void edge_empty_due_string()
    {
        DataStore store;
        Project *inbox = store.addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        TaskViewModel vm(&store);

        const QString id = vm.addTask(QStringLiteral("Plain task"),
                                      inbox->id(), 4,
                                      QString(),
                                      QStringList{});
        QVERIFY(!id.isEmpty());
        Task *t = store.taskById(id);
        QVERIFY(t != nullptr);
        QVERIFY(!t->dueIsRecurring());
        QVERIFY(t->dueRecurrenceRule().isEmpty());
    }

    // Garbage dueString — DateParser returns valid==false, addTask skips the
    // setDue* block, task is still created without recurrence. Confirms the
    // graceful-fallback contract that QuickAdd's freeform path relies on.
    void edge_invalid_due_string()
    {
        DataStore store;
        Project *inbox = store.addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        TaskViewModel vm(&store);

        const QString id = vm.addTask(QStringLiteral("Plain task"),
                                      inbox->id(), 4,
                                      QStringLiteral("blah"),
                                      QStringList{});
        QVERIFY(!id.isEmpty());
        Task *t = store.taskById(id);
        QVERIFY(t != nullptr);
        QVERIFY(!t->dueIsRecurring());
        QVERIFY(t->dueRecurrenceRule().isEmpty());
    }

    // "сегодня" is a one-shot date keyword, not a recurrence preset. The task
    // should receive a dueDate but isRecurring must remain false. Guards
    // against a future regression where any date keyword accidentally trips
    // the recurrence flag.
    void edge_today_not_recurring()
    {
        DataStore store;
        Project *inbox = store.addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);
        // Default language for the parser inside addTask is "en" when the
        // setting is empty, but rules_ru is invoked when lang is explicitly
        // "ru". We seed the language setting so "сегодня" is recognised.
        store.setSetting(QStringLiteral("language"), QStringLiteral("ru"));
        TaskViewModel vm(&store);

        const QString id = vm.addTask(QStringLiteral("One-off"),
                                      inbox->id(), 4,
                                      QString::fromUtf8("сегодня"),
                                      QStringList{});
        QVERIFY(!id.isEmpty());
        Task *t = store.taskById(id);
        QVERIFY(t != nullptr);
        QVERIFY(!t->dueIsRecurring());
        QVERIFY(t->dueRecurrenceRule().isEmpty());
        // Sanity: the date itself should have been populated to today.
        QCOMPARE(t->dueDate(),
                 QDate::currentDate().toString(Qt::ISODate));
    }
};

QTEST_GUILESS_MAIN(TestQuickAddRecurrencePresets)
#include "test_quickadd_recurrence_presets.moc"
