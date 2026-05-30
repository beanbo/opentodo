#include <QtTest>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonArray>

#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "model/label.h"
#include "model/filter.h"
#include "model/reminder.h"
#include "model/comment.h"
#include "utils/uuid.h"

class TestModels : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() {}
    void cleanupTestCase() {}

    // ── Task ──────────────────────────────────────────────────────────────────

    void task_default_construction()
    {
        Task t;
        QVERIFY(!t.id().isEmpty());
        QVERIFY(UuidHelper::isValid(t.id()));
        QVERIFY(t.createdAt().isValid());
        QVERIFY(t.updatedAt().isValid());
        QVERIFY(!t.isDeleted());
        QVERIFY(!t.isCompleted());
        QCOMPARE(t.priority(), 4); // canonical default: p4 normal (was 1 pre-fix)
        QVERIFY(t.labels().isEmpty());
        QVERIFY(t.parentId().isEmpty());
        QVERIFY(t.due().isNull());
    }

    void task_setters_emit_signals()
    {
        Task t;
        QSignalSpy spyContent(&t, &Task::contentChanged);
        QSignalSpy spyPriority(&t, &Task::priorityChanged);
        QSignalSpy spyLabels(&t, &Task::labelsChanged);
        QSignalSpy spyDeleted(&t, &Task::isDeletedChanged);
        QSignalSpy spyCompleted(&t, &Task::isCompletedChanged);
        QSignalSpy spyDue(&t, &Task::dueChanged);
        QSignalSpy spyParent(&t, &Task::parentIdChanged);
        QSignalSpy spyDesc(&t, &Task::descriptionChanged);
        QSignalSpy spyProject(&t, &Task::projectIdChanged);
        QSignalSpy spySection(&t, &Task::sectionIdChanged);

        t.setContent(QStringLiteral("Hello"));
        QCOMPARE(spyContent.count(), 1);

        t.setPriority(3);
        QCOMPARE(spyPriority.count(), 1);

        t.setLabels({QStringLiteral("work"), QStringLiteral("urgent")});
        QCOMPARE(spyLabels.count(), 1);

        t.setIsDeleted(true);
        QCOMPARE(spyDeleted.count(), 1);

        t.setIsCompleted(true);
        QCOMPARE(spyCompleted.count(), 1);

        DueObject due;
        due.date = QStringLiteral("2026-05-01");
        due.isRecurring = true;
        t.setDue(due);
        QCOMPARE(spyDue.count(), 1);

        t.setParentId(QStringLiteral("parent-uuid"));
        QCOMPARE(spyParent.count(), 1);

        t.setDescription(QStringLiteral("desc"));
        QCOMPARE(spyDesc.count(), 1);

        t.setProjectId(QStringLiteral("proj-1"));
        QCOMPARE(spyProject.count(), 1);

        t.setSectionId(QStringLiteral("sec-1"));
        QCOMPARE(spySection.count(), 1);
    }

    void task_setter_no_duplicate_signal()
    {
        // Setters should NOT re-emit if value is unchanged
        Task t;
        t.setContent(QStringLiteral("Same"));
        QSignalSpy spy(&t, &Task::contentChanged);
        t.setContent(QStringLiteral("Same")); // same value
        QCOMPARE(spy.count(), 0);
    }

    void task_round_trip_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("priority");
        QTest::addColumn<QStringList>("labels");
        QTest::addColumn<QString>("dueDate");
        QTest::addColumn<bool>("isRecurring");
        QTest::addColumn<QString>("parentId");
        QTest::addColumn<bool>("isCompleted");

        QTest::newRow("basic")
            << "Buy milk" << 1 << QStringList{} << "" << false << "" << false;
        QTest::newRow("with-due")
            << "Report" << 2 << QStringList{"work"} << "2026-06-15" << false << "" << false;
        QTest::newRow("recurring")
            << "Exercise" << 1 << QStringList{"health", "daily"} << "2026-05-02" << true << "" << false;
        QTest::newRow("subtask")
            << "Sub-item" << 4 << QStringList{} << "" << false << "parent-id-123" << false;
        QTest::newRow("completed")
            << "Done task" << 1 << QStringList{"done"} << "" << false << "" << true;
    }

    void task_round_trip()
    {
        QFETCH(QString, content);
        QFETCH(int, priority);
        QFETCH(QStringList, labels);
        QFETCH(QString, dueDate);
        QFETCH(bool, isRecurring);
        QFETCH(QString, parentId);
        QFETCH(bool, isCompleted);

        Task original;
        original.setContent(content);
        original.setPriority(priority);
        original.setLabels(labels);
        original.setParentId(parentId);
        original.setIsCompleted(isCompleted);

        if (!dueDate.isEmpty()) {
            DueObject due;
            due.date = dueDate;
            due.isRecurring = isRecurring;
            due.string = QStringLiteral("every day");
            due.lang = QStringLiteral("en");
            original.setDue(due);
        }

        QJsonObject json = original.toJson();

        // Verify is_deleted field is present in JSON
        QVERIFY(json.contains(QStringLiteral("is_deleted")));
        QCOMPARE(json[QStringLiteral("is_deleted")].toBool(), false);

        QScopedPointer<Task> restored(Task::fromJson(json));

        QCOMPARE(restored->id(), original.id());
        QCOMPARE(restored->content(), content);
        QCOMPARE(restored->priority(), priority);
        QCOMPARE(restored->labels(), labels);
        QCOMPARE(restored->parentId(), parentId);
        QCOMPARE(restored->isCompleted(), isCompleted);
        QCOMPARE(restored->isDeleted(), false);

        if (!dueDate.isEmpty()) {
            QCOMPARE(restored->dueDate(), dueDate);
            QCOMPARE(restored->dueIsRecurring(), isRecurring);
            QCOMPARE(restored->dueString(), QStringLiteral("every day"));
            QCOMPARE(restored->dueLang(), QStringLiteral("en"));
        }
    }

    void task_soft_delete_round_trip()
    {
        Task t;
        t.setContent(QStringLiteral("Delete me"));
        t.setIsDeleted(true);

        QJsonObject json = t.toJson();
        QVERIFY(json[QStringLiteral("is_deleted")].toBool());

        QScopedPointer<Task> restored(Task::fromJson(json));
        QVERIFY(restored->isDeleted());
        QCOMPARE(restored->content(), QStringLiteral("Delete me"));
    }

    void task_due_object_full_round_trip()
    {
        Task t;
        DueObject due;
        due.date = QStringLiteral("2026-12-25");
        due.datetime = QStringLiteral("2026-12-25T10:00:00");
        due.timezone = QStringLiteral("Europe/Moscow");
        due.string = QStringLiteral("Dec 25");
        due.isRecurring = true;
        due.lang = QStringLiteral("ru");
        t.setDue(due);

        QJsonObject json = t.toJson();
        QScopedPointer<Task> r(Task::fromJson(json));

        QCOMPARE(r->dueDate(), QStringLiteral("2026-12-25"));
        QCOMPARE(r->dueDatetime(), QStringLiteral("2026-12-25T10:00:00"));
        QCOMPARE(r->dueTimezone(), QStringLiteral("Europe/Moscow"));
        QCOMPARE(r->dueString(), QStringLiteral("Dec 25"));
        QVERIFY(r->dueIsRecurring());
        QCOMPARE(r->dueLang(), QStringLiteral("ru"));
    }

    void task_priority_range()
    {
        for (int p = 1; p <= 4; ++p) {
            Task t;
            t.setPriority(p);
            QJsonObject json = t.toJson();
            QScopedPointer<Task> r(Task::fromJson(json));
            QCOMPARE(r->priority(), p);
        }
    }

    void task_labels_round_trip()
    {
        Task t;
        QStringList lbls = {QStringLiteral("alpha"), QStringLiteral("beta"), QStringLiteral("gamma")};
        t.setLabels(lbls);

        QJsonObject json = t.toJson();
        QVERIFY(json[QStringLiteral("labels")].isArray());
        QCOMPARE(json[QStringLiteral("labels")].toArray().size(), 3);

        QScopedPointer<Task> r(Task::fromJson(json));
        QCOMPARE(r->labels(), lbls);
    }

    // ── Project ───────────────────────────────────────────────────────────────

    void project_default_construction()
    {
        Project p;
        QVERIFY(!p.id().isEmpty());
        QVERIFY(UuidHelper::isValid(p.id()));
        QVERIFY(p.createdAt().isValid());
        QVERIFY(p.updatedAt().isValid());
        QVERIFY(!p.isDeleted());
        QVERIFY(!p.isInbox());
        QVERIFY(!p.isArchived());
        QVERIFY(!p.isFavorite());
        QCOMPARE(p.color(), QStringLiteral("grey"));
        QCOMPARE(p.viewStyle(), QStringLiteral("list"));
    }

    void project_setters_emit_signals()
    {
        Project p;
        QSignalSpy spyName(&p, &Project::nameChanged);
        QSignalSpy spyColor(&p, &Project::colorChanged);
        QSignalSpy spyFav(&p, &Project::isFavoriteChanged);
        QSignalSpy spyDeleted(&p, &Project::isDeletedChanged);
        QSignalSpy spyInbox(&p, &Project::isInboxChanged);

        p.setName(QStringLiteral("Work"));
        QCOMPARE(spyName.count(), 1);

        p.setColor(QStringLiteral("red"));
        QCOMPARE(spyColor.count(), 1);

        p.setIsFavorite(true);
        QCOMPARE(spyFav.count(), 1);

        p.setIsDeleted(true);
        QCOMPARE(spyDeleted.count(), 1);

        p.setIsInbox(true);
        QCOMPARE(spyInbox.count(), 1);
    }

    void project_round_trip()
    {
        Project orig;
        orig.setName(QStringLiteral("My Project"));
        orig.setColor(QStringLiteral("blue"));
        orig.setIsFavorite(true);
        orig.setIsInbox(false);
        orig.setViewStyle(QStringLiteral("board"));
        orig.setOrder(5);

        QJsonObject json = orig.toJson();
        QScopedPointer<Project> r(Project::fromJson(json));

        QCOMPARE(r->id(), orig.id());
        QCOMPARE(r->name(), QStringLiteral("My Project"));
        QCOMPARE(r->color(), QStringLiteral("blue"));
        QVERIFY(r->isFavorite());
        QVERIFY(!r->isInbox());
        QCOMPARE(r->viewStyle(), QStringLiteral("board"));
        QCOMPARE(r->order(), 5);
        QVERIFY(!r->isDeleted());
    }

    void project_soft_delete_round_trip()
    {
        Project p;
        p.setName(QStringLiteral("Archived"));
        p.setIsDeleted(true);

        QJsonObject json = p.toJson();
        QVERIFY(json[QStringLiteral("is_deleted")].toBool());

        QScopedPointer<Project> r(Project::fromJson(json));
        QVERIFY(r->isDeleted());
    }

    // ── Section ───────────────────────────────────────────────────────────────

    void section_default_construction()
    {
        Section s;
        QVERIFY(!s.id().isEmpty());
        QVERIFY(UuidHelper::isValid(s.id()));
        QVERIFY(s.createdAt().isValid());
        QVERIFY(!s.isDeleted());
        QVERIFY(!s.isCollapsed());
        QVERIFY(!s.isArchived());
    }

    void section_setters_emit_signals()
    {
        Section s;
        QSignalSpy spyName(&s, &Section::nameChanged);
        QSignalSpy spyProject(&s, &Section::projectIdChanged);
        QSignalSpy spyDeleted(&s, &Section::isDeletedChanged);
        QSignalSpy spyCollapsed(&s, &Section::isCollapsedChanged);

        s.setName(QStringLiteral("Sprint 1"));
        QCOMPARE(spyName.count(), 1);

        s.setProjectId(QStringLiteral("proj-uuid"));
        QCOMPARE(spyProject.count(), 1);

        s.setIsDeleted(true);
        QCOMPARE(spyDeleted.count(), 1);

        s.setIsCollapsed(true);
        QCOMPARE(spyCollapsed.count(), 1);
    }

    void section_round_trip()
    {
        Section orig;
        orig.setName(QStringLiteral("Backlog"));
        orig.setProjectId(QStringLiteral("proj-abc"));
        orig.setOrder(2);
        orig.setIsCollapsed(true);
        orig.setIsArchived(false);

        QJsonObject json = orig.toJson();
        QScopedPointer<Section> r(Section::fromJson(json));

        QCOMPARE(r->id(), orig.id());
        QCOMPARE(r->name(), QStringLiteral("Backlog"));
        QCOMPARE(r->projectId(), QStringLiteral("proj-abc"));
        QCOMPARE(r->order(), 2);
        QVERIFY(r->isCollapsed());
        QVERIFY(!r->isArchived());
        QVERIFY(!r->isDeleted());
    }

    void section_soft_delete_round_trip()
    {
        Section s;
        s.setIsDeleted(true);
        QJsonObject json = s.toJson();
        QVERIFY(json[QStringLiteral("is_deleted")].toBool());
        QScopedPointer<Section> r(Section::fromJson(json));
        QVERIFY(r->isDeleted());
    }

    // ── Label ─────────────────────────────────────────────────────────────────

    void label_default_construction()
    {
        Label l;
        QVERIFY(!l.id().isEmpty());
        QVERIFY(UuidHelper::isValid(l.id()));
        QVERIFY(!l.isDeleted());
        QVERIFY(!l.isFavorite());
        QCOMPARE(l.color(), QStringLiteral("charcoal"));
    }

    void label_setters_emit_signals()
    {
        Label l;
        QSignalSpy spyName(&l, &Label::nameChanged);
        QSignalSpy spyColor(&l, &Label::colorChanged);
        QSignalSpy spyFav(&l, &Label::isFavoriteChanged);
        QSignalSpy spyDeleted(&l, &Label::isDeletedChanged);

        l.setName(QStringLiteral("urgent"));
        QCOMPARE(spyName.count(), 1);

        l.setColor(QStringLiteral("red"));
        QCOMPARE(spyColor.count(), 1);

        l.setIsFavorite(true);
        QCOMPARE(spyFav.count(), 1);

        l.setIsDeleted(true);
        QCOMPARE(spyDeleted.count(), 1);
    }

    void label_round_trip()
    {
        Label orig;
        orig.setName(QStringLiteral("home"));
        orig.setColor(QStringLiteral("green"));
        orig.setIsFavorite(true);
        orig.setOrder(3);

        QJsonObject json = orig.toJson();
        QScopedPointer<Label> r(Label::fromJson(json));

        QCOMPARE(r->id(), orig.id());
        QCOMPARE(r->name(), QStringLiteral("home"));
        QCOMPARE(r->color(), QStringLiteral("green"));
        QVERIFY(r->isFavorite());
        QCOMPARE(r->order(), 3);
        QVERIFY(!r->isDeleted());
    }

    void label_soft_delete_round_trip()
    {
        Label l;
        l.setIsDeleted(true);
        QJsonObject json = l.toJson();
        QVERIFY(json[QStringLiteral("is_deleted")].toBool());
        QScopedPointer<Label> r(Label::fromJson(json));
        QVERIFY(r->isDeleted());
    }

    // ── Filter ────────────────────────────────────────────────────────────────

    void filter_default_construction()
    {
        Filter f;
        QVERIFY(!f.id().isEmpty());
        QVERIFY(UuidHelper::isValid(f.id()));
        QVERIFY(!f.isDeleted());
        QVERIFY(!f.isFavorite());
        QCOMPARE(f.color(), QStringLiteral("charcoal"));
        QVERIFY(f.query().isEmpty());
    }

    void filter_setters_emit_signals()
    {
        Filter f;
        QSignalSpy spyName(&f, &Filter::nameChanged);
        QSignalSpy spyQuery(&f, &Filter::queryChanged);
        QSignalSpy spyColor(&f, &Filter::colorChanged);
        QSignalSpy spyFav(&f, &Filter::isFavoriteChanged);
        QSignalSpy spyDeleted(&f, &Filter::isDeletedChanged);

        f.setName(QStringLiteral("Today + Overdue"));
        QCOMPARE(spyName.count(), 1);

        f.setQuery(QStringLiteral("today | overdue"));
        QCOMPARE(spyQuery.count(), 1);

        f.setColor(QStringLiteral("blue"));
        QCOMPARE(spyColor.count(), 1);

        f.setIsFavorite(true);
        QCOMPARE(spyFav.count(), 1);

        f.setIsDeleted(true);
        QCOMPARE(spyDeleted.count(), 1);
    }

    void filter_round_trip()
    {
        Filter orig;
        orig.setName(QStringLiteral("High Priority"));
        orig.setQuery(QStringLiteral("p1 | p2"));
        orig.setColor(QStringLiteral("purple"));
        orig.setIsFavorite(false);
        orig.setOrder(1);

        QJsonObject json = orig.toJson();
        QScopedPointer<Filter> r(Filter::fromJson(json));

        QCOMPARE(r->id(), orig.id());
        QCOMPARE(r->name(), QStringLiteral("High Priority"));
        QCOMPARE(r->query(), QStringLiteral("p1 | p2"));
        QCOMPARE(r->color(), QStringLiteral("purple"));
        QVERIFY(!r->isFavorite());
        QCOMPARE(r->order(), 1);
        QVERIFY(!r->isDeleted());
    }

    void filter_soft_delete_round_trip()
    {
        Filter f;
        f.setIsDeleted(true);
        QJsonObject json = f.toJson();
        QVERIFY(json[QStringLiteral("is_deleted")].toBool());
        QScopedPointer<Filter> r(Filter::fromJson(json));
        QVERIFY(r->isDeleted());
    }

    // ── Reminder ──────────────────────────────────────────────────────────────

    void reminder_default_construction()
    {
        Reminder r;
        QVERIFY(!r.id().isEmpty());
        QVERIFY(UuidHelper::isValid(r.id()));
        QVERIFY(!r.isDeleted());
        QCOMPARE(r.type(), QStringLiteral("absolute"));
        QCOMPARE(r.minuteOffset(), 0);
    }

    void reminder_setters_emit_signals()
    {
        Reminder r;
        QSignalSpy spyTask(&r, &Reminder::taskIdChanged);
        QSignalSpy spyType(&r, &Reminder::typeChanged);
        QSignalSpy spyOffset(&r, &Reminder::minuteOffsetChanged);
        QSignalSpy spyDeleted(&r, &Reminder::isDeletedChanged);
        QSignalSpy spyDue(&r, &Reminder::dueChanged);

        r.setTaskId(QStringLiteral("task-1"));
        QCOMPARE(spyTask.count(), 1);

        r.setType(QStringLiteral("relative"));
        QCOMPARE(spyType.count(), 1);

        r.setMinuteOffset(30);
        QCOMPARE(spyOffset.count(), 1);

        r.setIsDeleted(true);
        QCOMPARE(spyDeleted.count(), 1);

        ReminderDue due;
        due.date = QStringLiteral("2026-05-10");
        r.setDue(due);
        QCOMPARE(spyDue.count(), 1);
    }

    void reminder_round_trip_absolute()
    {
        Reminder orig;
        orig.setTaskId(QStringLiteral("task-abc"));
        orig.setType(QStringLiteral("absolute"));
        ReminderDue due;
        due.date = QStringLiteral("2026-06-01");
        due.datetime = QStringLiteral("2026-06-01T09:00:00");
        due.lang = QStringLiteral("en");
        orig.setDue(due);

        QJsonObject json = orig.toJson();
        QScopedPointer<Reminder> r(Reminder::fromJson(json));

        QCOMPARE(r->id(), orig.id());
        QCOMPARE(r->taskId(), QStringLiteral("task-abc"));
        QCOMPARE(r->type(), QStringLiteral("absolute"));
        QCOMPARE(r->due().date, QStringLiteral("2026-06-01"));
        QCOMPARE(r->due().datetime, QStringLiteral("2026-06-01T09:00:00"));
        QVERIFY(!r->isDeleted());
    }

    void reminder_round_trip_relative()
    {
        Reminder orig;
        orig.setTaskId(QStringLiteral("task-xyz"));
        orig.setType(QStringLiteral("relative"));
        orig.setMinuteOffset(15);

        QJsonObject json = orig.toJson();
        QScopedPointer<Reminder> r(Reminder::fromJson(json));

        QCOMPARE(r->type(), QStringLiteral("relative"));
        QCOMPARE(r->minuteOffset(), 15);
    }

    void reminder_soft_delete_round_trip()
    {
        Reminder rem;
        rem.setIsDeleted(true);
        QJsonObject json = rem.toJson();
        QVERIFY(json[QStringLiteral("is_deleted")].toBool());
        QScopedPointer<Reminder> r(Reminder::fromJson(json));
        QVERIFY(r->isDeleted());
    }

    // ── Comment ───────────────────────────────────────────────────────────────

    void comment_default_construction()
    {
        Comment c;
        QVERIFY(!c.id().isEmpty());
        QVERIFY(UuidHelper::isValid(c.id()));
        QVERIFY(c.postedAt().isValid());
        QVERIFY(!c.isDeleted());
        QVERIFY(c.content().isEmpty());
    }

    void comment_setters_emit_signals()
    {
        Comment c;
        QSignalSpy spyContent(&c, &Comment::contentChanged);
        QSignalSpy spyTask(&c, &Comment::taskIdChanged);
        QSignalSpy spyProject(&c, &Comment::projectIdChanged);
        QSignalSpy spyDeleted(&c, &Comment::isDeletedChanged);
        QSignalSpy spyPosted(&c, &Comment::postedAtChanged);

        c.setContent(QStringLiteral("Nice work!"));
        QCOMPARE(spyContent.count(), 1);

        c.setTaskId(QStringLiteral("task-1"));
        QCOMPARE(spyTask.count(), 1);

        c.setProjectId(QStringLiteral("proj-1"));
        QCOMPARE(spyProject.count(), 1);

        c.setIsDeleted(true);
        QCOMPARE(spyDeleted.count(), 1);

        c.setPostedAt(QDateTime::currentDateTimeUtc().addDays(-1));
        QCOMPARE(spyPosted.count(), 1);
    }

    void comment_round_trip()
    {
        Comment orig;
        orig.setContent(QStringLiteral("Looks good to me"));
        orig.setTaskId(QStringLiteral("t-001"));
        orig.setProjectId(QStringLiteral("p-001"));

        QDateTime posted = QDateTime::fromString(
            QStringLiteral("2026-04-01T12:00:00.000Z"), Qt::ISODateWithMs);
        orig.setPostedAt(posted);

        QJsonObject json = orig.toJson();
        QScopedPointer<Comment> r(Comment::fromJson(json));

        QCOMPARE(r->id(), orig.id());
        QCOMPARE(r->content(), QStringLiteral("Looks good to me"));
        QCOMPARE(r->taskId(), QStringLiteral("t-001"));
        QCOMPARE(r->projectId(), QStringLiteral("p-001"));
        QCOMPARE(r->postedAt(), posted);
        QVERIFY(!r->isDeleted());
    }

    void comment_soft_delete_round_trip()
    {
        Comment c;
        c.setContent(QStringLiteral("To be deleted"));
        c.setIsDeleted(true);
        QJsonObject json = c.toJson();
        QVERIFY(json[QStringLiteral("is_deleted")].toBool());
        QScopedPointer<Comment> r(Comment::fromJson(json));
        QVERIFY(r->isDeleted());
        QCOMPARE(r->content(), QStringLiteral("To be deleted"));
    }
};

QTEST_GUILESS_MAIN(TestModels)
#include "test_models.moc"
