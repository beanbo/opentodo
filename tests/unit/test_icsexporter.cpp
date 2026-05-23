// Unit tests for IcsExporter — RFC 5545 VCALENDAR / VTODO serialisation.
//
// Covers the contract the iCal-spec consumers (Apple Calendar, Google
// Calendar, Outlook) will actually parse:
//   - VCALENDAR envelope shape (BEGIN/END, VERSION, PRODID, CALSCALE).
//   - One VTODO per non-deleted task; UID stable; DTSTAMP present.
//   - PRIORITY maps p1=1, p2=3, p3=5, p4=9.
//   - STATUS: COMPLETED / NEEDS-ACTION as appropriate.
//   - DUE date-only uses VALUE=DATE; datetime uses naive YYYYMMDDTHHMMSS.
//   - RRULE present for "every day" / "every monday" / "every 2 weeks";
//     human string falls back to DESCRIPTION prefix when un-mappable.
//   - Soft-deleted tasks skipped entirely.
//   - Text escaping: comma / semicolon / backslash / newline.
//   - Line folding at 75 octets with CRLF + leading space.
//   - CRLF line endings everywhere.

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QStringList>
#include <QSignalSpy>

#include "services/icsexporter.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"

class TestIcsExporter : public QObject
{
    Q_OBJECT

private:
    // Helpers reused across slots. Static so we don't drag a fixture struct
    // around — every slot constructs its own DataStore.

    static QString render(const QList<Task *> &tasks)
    {
        return IcsExporter::exportTasks(tasks);
    }

    // Read the .ics file produced by exportToFile() back as raw bytes (no
    // text-mode CRLF mangling — we want to see the actual on-disk bytes).
    static QByteArray readBytes(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return f.readAll();
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_IcsExporter"));
        QCoreApplication::setApplicationName(QStringLiteral("test_icsexporter"));
    }

    // ── Envelope shape ────────────────────────────────────────────────────────

    void envelope_begin_end_calendar()
    {
        DataStore ds;
        const QString out = render(ds.allTasks());

        // CRLF line endings + sentinel openers/closers in the right order.
        QVERIFY2(out.startsWith(QStringLiteral("BEGIN:VCALENDAR\r\n")),
                 qPrintable(out.left(30)));
        QVERIFY2(out.endsWith(QStringLiteral("END:VCALENDAR\r\n")),
                 qPrintable(out.right(30)));
        QVERIFY(out.contains(QStringLiteral("VERSION:2.0\r\n")));
        QVERIFY(out.contains(QStringLiteral("PRODID:-//OpenTodo//OpenTodo 1.0//EN\r\n")));
        QVERIFY(out.contains(QStringLiteral("CALSCALE:GREGORIAN\r\n")));
        QVERIFY(out.contains(QStringLiteral("METHOD:PUBLISH\r\n")));
    }

    // ── Empty store produces a valid envelope with no VTODO blocks ────────────

    void empty_store_emits_no_vtodos()
    {
        DataStore ds;
        const QString out = render(ds.allTasks());
        QVERIFY(!out.contains(QStringLiteral("BEGIN:VTODO")));
        QVERIFY(!out.contains(QStringLiteral("END:VTODO")));
    }

    // ── One VTODO per non-deleted task ────────────────────────────────────────

    void one_vtodo_per_task_deleted_excluded()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Personal"));
        ds.addTask(QStringLiteral("Buy milk"),  p->id());
        ds.addTask(QStringLiteral("Walk dog"),  p->id());
        Task *dead = ds.addTask(QStringLiteral("Removed task"), p->id());
        dead->setIsDeleted(true);

        const QString out = render(ds.allTasks());

        // Three openers were created in DataStore but only two non-deleted.
        QCOMPARE(out.count(QStringLiteral("BEGIN:VTODO\r\n")), 2);
        QCOMPARE(out.count(QStringLiteral("END:VTODO\r\n")),   2);
        QVERIFY(out.contains(QStringLiteral("SUMMARY:Buy milk\r\n")));
        QVERIFY(out.contains(QStringLiteral("SUMMARY:Walk dog\r\n")));
        // Tombstoned task title must not leak.
        QVERIFY(!out.contains(QStringLiteral("Removed task")));
    }

    // ── UID stability across re-exports ───────────────────────────────────────

    void uid_is_stable_across_exports()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Stable"));
        Task *t = ds.addTask(QStringLiteral("Same task"), p->id());

        const QString out1 = render(ds.allTasks());
        const QString out2 = render(ds.allTasks());

        const QString uid = QStringLiteral("UID:") + t->id()
                            + QStringLiteral("@opentodo\r\n");
        QVERIFY(out1.contains(uid));
        QVERIFY(out2.contains(uid));
    }

    // ── PRIORITY mapping ──────────────────────────────────────────────────────

    void priority_maps_p1_p2_p3_p4_to_1_3_5_9()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Pri"));
        Task *a = ds.addTask(QStringLiteral("urgent"),  p->id()); a->setPriority(1);
        Task *b = ds.addTask(QStringLiteral("high"),    p->id()); b->setPriority(2);
        Task *c = ds.addTask(QStringLiteral("medium"),  p->id()); c->setPriority(3);
        Task *d = ds.addTask(QStringLiteral("low"),     p->id()); d->setPriority(4);

        const QString out = render(ds.allTasks());

        // Look for "<SUMMARY:...>" followed (in the same VTODO block) by
        // "PRIORITY:N". We use the predictable ordering of property emission:
        // SUMMARY appears before PRIORITY.
        auto priFor = [&](const QString &summary) -> QString {
            const int s = out.indexOf(QStringLiteral("SUMMARY:") + summary);
            if (s < 0) return {};
            const int p = out.indexOf(QStringLiteral("PRIORITY:"), s);
            const int end = out.indexOf(QStringLiteral("\r\n"), p);
            return out.mid(p + 9, end - (p + 9));
        };
        QCOMPARE(priFor(QStringLiteral("urgent")), QStringLiteral("1"));
        QCOMPARE(priFor(QStringLiteral("high")),   QStringLiteral("3"));
        QCOMPARE(priFor(QStringLiteral("medium")), QStringLiteral("5"));
        QCOMPARE(priFor(QStringLiteral("low")),    QStringLiteral("9"));
    }

    // ── STATUS mapping ────────────────────────────────────────────────────────

    void status_completed_vs_needs_action()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Status"));
        ds.addTask(QStringLiteral("open"), p->id());
        Task *done = ds.addTask(QStringLiteral("done"), p->id());
        done->setIsCompleted(true);

        const QString out = render(ds.allTasks());
        QVERIFY(out.contains(QStringLiteral("STATUS:NEEDS-ACTION\r\n")));
        QVERIFY(out.contains(QStringLiteral("STATUS:COMPLETED\r\n")));
    }

    // ── DUE formatting (date-only vs datetime) ────────────────────────────────

    void due_date_only_uses_value_date_param()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Dates"));
        Task *t = ds.addTask(QStringLiteral("Date only"), p->id());
        t->setDueDate(QStringLiteral("2026-05-17"));

        const QString out = render(ds.allTasks());
        QVERIFY2(out.contains(QStringLiteral("DUE;VALUE=DATE:20260517\r\n")),
                 qPrintable(out));
    }

    void due_datetime_uses_naive_local_format()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Times"));
        Task *t = ds.addTask(QStringLiteral("Lunch"), p->id());
        t->setDueDatetime(QStringLiteral("2026-05-17T12:30:00"));

        const QString out = render(ds.allTasks());
        QVERIFY2(out.contains(QStringLiteral("DUE:20260517T123000\r\n")),
                 qPrintable(out));
    }

    // ── Recurrence: simple human strings → RRULE ──────────────────────────────

    void recurrence_every_day_maps_to_freq_daily()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Recur"));
        Task *t = ds.addTask(QStringLiteral("Brush teeth"), p->id());
        DueObject d; d.string = QStringLiteral("every day"); d.isRecurring = true;
        t->setDue(d);

        const QString out = render(ds.allTasks());
        QVERIFY2(out.contains(QStringLiteral("RRULE:FREQ=DAILY\r\n")),
                 qPrintable(out));
    }

    void recurrence_every_monday_maps_to_weekly_byday()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Recur"));
        Task *t = ds.addTask(QStringLiteral("Stand-up"), p->id());
        DueObject d; d.string = QStringLiteral("every monday"); d.isRecurring = true;
        t->setDue(d);

        const QString out = render(ds.allTasks());
        QVERIFY2(out.contains(QStringLiteral("RRULE:FREQ=WEEKLY;BYDAY=MO\r\n")),
                 qPrintable(out));
    }

    void recurrence_every_n_weeks_uses_interval()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Recur"));
        Task *t = ds.addTask(QStringLiteral("Bi-weekly review"), p->id());
        DueObject d; d.string = QStringLiteral("every 2 weeks"); d.isRecurring = true;
        t->setDue(d);

        const QString out = render(ds.allTasks());
        QVERIFY2(out.contains(QStringLiteral("RRULE:FREQ=WEEKLY;INTERVAL=2\r\n")),
                 qPrintable(out));
    }

    void recurrence_every_weekday_uses_byday_mtwthf()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Recur"));
        Task *t = ds.addTask(QStringLiteral("Daily standup"), p->id());
        DueObject d; d.string = QStringLiteral("every weekday"); d.isRecurring = true;
        t->setDue(d);

        const QString out = render(ds.allTasks());
        QVERIFY2(out.contains(QStringLiteral("RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR\r\n")),
                 qPrintable(out));
    }

    void recurrence_unrecognised_falls_back_to_description_prefix()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Recur"));
        Task *t = ds.addTask(QStringLiteral("Quarterly review"), p->id());
        // No RRULE; human string the parser can't translate (e.g. random text).
        DueObject d;
        d.string = QStringLiteral("every quarter on the 14th of feb");
        d.isRecurring = true;
        t->setDue(d);
        t->setDescription(QStringLiteral("look at year-to-date numbers"));

        const QString out = render(ds.allTasks());
        // No RRULE emitted.
        QVERIFY(!out.contains(QStringLiteral("RRULE:")));
        // Description got the "Recurrence: …" prefix prepended. The full
        // logical line is longer than 75 octets so RFC 5545 §3.1 folding
        // splits it into a primary line + CRLF-SP continuation segments.
        // Un-fold by collapsing every CRLF+SP boundary, then assert the
        // expected logical content appears verbatim. Same approach as
        // long_summary_is_folded_at_75_octets below.
        QString unfolded = out;
        unfolded.replace(QStringLiteral("\r\n "), QString());
        QVERIFY2(unfolded.contains(
                     QStringLiteral("DESCRIPTION:Recurrence: every quarter on the 14th of feb\\n"
                                    "look at year-to-date numbers\r\n")),
                 qPrintable(out));
    }

    // ── Explicit RFC 5545 RRULE in due field passes through ───────────────────

    void recurrence_explicit_rrule_passes_through()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Recur"));
        Task *t = ds.addTask(QStringLiteral("Custom"), p->id());
        DueObject d;
        d.string = QStringLiteral("every other tuesday");
        d.isRecurring = true;
        d.recurrenceRule = QStringLiteral("FREQ=WEEKLY;BYDAY=TU;INTERVAL=2");
        t->setDue(d);

        const QString out = render(ds.allTasks());
        QVERIFY(out.contains(QStringLiteral("RRULE:FREQ=WEEKLY;BYDAY=TU;INTERVAL=2\r\n")));
    }

    // ── Text escaping ─────────────────────────────────────────────────────────

    void text_escape_comma_semicolon_backslash_newline()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Esc"));
        Task *t = ds.addTask(
            QStringLiteral("a, b; c\\ d\nline2"), p->id());

        const QString out = render(ds.allTasks());
        // Comma -> \, ; Semicolon -> \; ; Backslash -> \\ ; Newline -> \n
        QVERIFY2(out.contains(QStringLiteral("SUMMARY:a\\, b\\; c\\\\ d\\nline2\r\n")),
                 qPrintable(out));
    }

    // ── Line folding at 75 octets ─────────────────────────────────────────────

    void long_summary_is_folded_at_75_octets()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Long"));
        const QString longTitle(200, QLatin1Char('A'));
        ds.addTask(longTitle, p->id());

        const QByteArray utf8 = render(ds.allTasks()).toUtf8();
        const QList<QByteArray> lines = utf8.split('\n');
        // Every physical line (after splitting on LF, ignoring the trailing
        // CR) must be ≤ 75 octets. iCal folding uses CRLF + space — the
        // continuation lines begin with a leading SP after we re-join.
        for (const QByteArray &lnRaw : lines) {
            QByteArray ln = lnRaw;
            if (ln.endsWith('\r')) ln.chop(1);
            QVERIFY2(ln.size() <= 75,
                qPrintable(QStringLiteral("line %1 octets > 75: %2")
                               .arg(ln.size()).arg(QString::fromUtf8(ln.left(80)))));
        }
        // Sanity: the long title is preserved across folds — un-fold by
        // collapsing every CRLF+SP, then check the SUMMARY value reappears.
        QString unfolded = QString::fromUtf8(utf8);
        unfolded.replace(QStringLiteral("\r\n "), QString());
        QVERIFY(unfolded.contains(QStringLiteral("SUMMARY:") + longTitle));
    }

    // ── exportToFile round-trip ───────────────────────────────────────────────

    void file_write_round_trip()
    {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Disk"));
        ds.addTask(QStringLiteral("From disk"), p->id());

        const QString path = tmp.path() + QStringLiteral("/out.ics");
        QVERIFY(IcsExporter::exportToFile(ds.allTasks(), path));
        QVERIFY(QFile::exists(path));

        const QByteArray bytes = readBytes(path);
        QVERIFY(bytes.startsWith("BEGIN:VCALENDAR\r\n"));
        QVERIFY(bytes.contains("SUMMARY:From disk\r\n"));
        QVERIFY(bytes.endsWith("END:VCALENDAR\r\n"));
    }

    // ── QObject Q_INVOKABLE wires through file:// URLs ────────────────────────

    void invokable_exporter_strips_file_url_and_emits_succeeded()
    {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());

        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("URL"));
        ds.addTask(QStringLiteral("Via URL"), p->id());

        IcsExporter exp(&ds);
        QSignalSpy ok(&exp, &IcsExporter::exportSucceeded);
        QSignalSpy bad(&exp, &IcsExporter::exportFailed);

        const QString path = tmp.path() + QStringLiteral("/url-export.ics");
        const QString fileUrl = QStringLiteral("file:///") + path;

        const QString returned = exp.exportToIcs(fileUrl);
        QVERIFY2(!returned.isEmpty(), qPrintable(returned));
        QCOMPARE(ok.size(), 1);
        QCOMPARE(bad.size(), 0);
        QVERIFY(QFile::exists(path));
    }

    // ── Realistic scenario test combining priorities, recurrence, completion ──
    //
    // Mirrors the acceptance criteria: one recurring task, one with
    // description, one completed.
    void scenario_recurring_described_completed_mix()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Q3"));

        Task *a = ds.addTask(QStringLiteral("Recurring stand-up"), p->id());
        a->setPriority(2);
        DueObject da; da.string = QStringLiteral("every monday"); da.isRecurring = true;
        a->setDue(da);

        Task *b = ds.addTask(QStringLiteral("With description"), p->id());
        b->setDescription(QStringLiteral("Details, with comma; and semicolon."));

        Task *c = ds.addTask(QStringLiteral("Already done"), p->id());
        c->setIsCompleted(true);
        c->setCompletedAt(QDateTime(QDate(2026, 5, 1), QTime(9, 0), QTimeZone::UTC));

        const QString out = render(ds.allTasks());

        // VCALENDAR envelope sanity.
        QVERIFY(out.startsWith(QStringLiteral("BEGIN:VCALENDAR\r\n")));
        QVERIFY(out.endsWith(QStringLiteral("END:VCALENDAR\r\n")));
        QCOMPARE(out.count(QStringLiteral("BEGIN:VTODO\r\n")), 3);

        // Recurring task: RRULE present + priority mapped.
        QVERIFY(out.contains(QStringLiteral("SUMMARY:Recurring stand-up\r\n")));
        QVERIFY(out.contains(QStringLiteral("RRULE:FREQ=WEEKLY;BYDAY=MO\r\n")));

        // Described task: DESCRIPTION escaped properly.
        QVERIFY2(out.contains(
            QStringLiteral("DESCRIPTION:Details\\, with comma\\; and semicolon.\r\n")),
            qPrintable(out));

        // Completed task: STATUS:COMPLETED + COMPLETED stamp.
        QVERIFY(out.contains(QStringLiteral("SUMMARY:Already done\r\n")));
        QVERIFY(out.contains(QStringLiteral("STATUS:COMPLETED\r\n")));
        QVERIFY(out.contains(QStringLiteral("COMPLETED:20260501T090000Z\r\n")));
    }

    // ─────────────────────────────────────────────────────────────────────
    // Iter-night-36: subtask hierarchy must be preserved via
    // RELATED-TO;RELTYPE=PARENT (RFC 5545 §3.8.4.5).
    //
    // Before this fix, the exporter emitted every task as a top-level
    // VTODO with no parent reference. A multi-level outline
    // ("Прибраться в квартире" → "Кухня", "Гостиная") exported as a flat
    // sibling list in Apple Calendar / Outlook / Thunderbird — silently
    // losing the entire hierarchy on the first round-trip.
    //
    // Pin slots:
    //   1. subtaskEmits_relatedTo_parent
    //   2. grandchildEmits_relatedTo_parent (deep hierarchy)
    //   3. relatedTo_skippedWhenParentNotInExportSet (defensive — don't
    //      emit dangling references)
    //   4. topLevelTasks_haveNo_relatedTo (negative pin)
    // ─────────────────────────────────────────────────────────────────────

    void subtaskEmits_relatedTo_parent()
    {
        DataStore ds;
        Project *inbox = ds.addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);

        Task *parent = ds.addTask(QStringLiteral("Прибраться в квартире"),
                                  inbox->id());
        Task *child  = ds.addTask(QStringLiteral("Кухня"), inbox->id());
        child->setParentId(parent->id());

        const QString out = render(ds.allTasks());

        // Both tasks must be emitted.
        QVERIFY(out.contains(QStringLiteral("BEGIN:VTODO\r\n")));
        QCOMPARE(out.count(QStringLiteral("BEGIN:VTODO\r\n")), 2);

        // The child's VTODO must carry RELATED-TO;RELTYPE=PARENT pointing
        // at the parent's UID. The exact line we expect:
        const QString expected =
            QStringLiteral("RELATED-TO;RELTYPE=PARENT:") + parent->id()
            + QStringLiteral("@opentodo\r\n");
        QVERIFY2(out.contains(expected),
                 qPrintable(QStringLiteral(
                     "iter-night-36 regression: subtask VTODO missing "
                     "RELATED-TO;RELTYPE=PARENT reference. "
                     "Expected line:\n%1\n"
                     "Full output:\n%2").arg(expected, out)));

        // And conversely the PARENT itself must NOT carry a RELATED-TO.
        // The output should contain exactly one RELATED-TO line.
        QCOMPARE(out.count(QStringLiteral("RELATED-TO")), 1);
    }

    void grandchildEmits_relatedTo_parent_deep()
    {
        DataStore ds;
        Project *inbox = ds.addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);

        Task *root  = ds.addTask(QStringLiteral("Root"),       inbox->id());
        Task *child = ds.addTask(QStringLiteral("Child"),      inbox->id());
        Task *grand = ds.addTask(QStringLiteral("Grandchild"), inbox->id());
        child->setParentId(root->id());
        grand->setParentId(child->id());

        const QString out = render(ds.allTasks());
        QCOMPARE(out.count(QStringLiteral("BEGIN:VTODO\r\n")), 3);

        // Two RELATED-TO lines: child→root, grand→child.
        QCOMPARE(out.count(QStringLiteral("RELATED-TO;RELTYPE=PARENT:")), 2);

        const QString childToRoot  =
            QStringLiteral("RELATED-TO;RELTYPE=PARENT:") + root->id()
            + QStringLiteral("@opentodo\r\n");
        const QString grandToChild =
            QStringLiteral("RELATED-TO;RELTYPE=PARENT:") + child->id()
            + QStringLiteral("@opentodo\r\n");
        QVERIFY(out.contains(childToRoot));
        QVERIFY(out.contains(grandToChild));
    }

    void relatedTo_skippedWhenParentNotInExportSet()
    {
        DataStore ds;
        Project *inbox = ds.addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);

        Task *parent = ds.addTask(QStringLiteral("Parent"), inbox->id());
        Task *child  = ds.addTask(QStringLiteral("Child"),  inbox->id());
        child->setParentId(parent->id());

        // Build an export set containing ONLY the child — simulates the
        // "selected tasks" export flow where the parent was filtered out.
        const QString out = render({ child });

        // One VTODO emitted (the child).
        QCOMPARE(out.count(QStringLiteral("BEGIN:VTODO\r\n")), 1);
        // No RELATED-TO line — parent isn't in the export set, so the
        // reference would be dangling.
        QVERIFY2(!out.contains(QStringLiteral("RELATED-TO")),
                 "iter-night-36: child exported without its parent "
                 "must not emit a dangling RELATED-TO reference");
    }

    void topLevelTasks_haveNo_relatedTo_negative_pin()
    {
        DataStore ds;
        Project *inbox = ds.addProject(QStringLiteral("Inbox"));
        inbox->setIsInbox(true);

        // Three top-level tasks, no parent links anywhere.
        ds.addTask(QStringLiteral("A"), inbox->id());
        ds.addTask(QStringLiteral("B"), inbox->id());
        ds.addTask(QStringLiteral("C"), inbox->id());

        const QString out = render(ds.allTasks());
        QCOMPARE(out.count(QStringLiteral("BEGIN:VTODO\r\n")), 3);
        // No RELATED-TO anywhere — pure sibling list.
        QVERIFY(!out.contains(QStringLiteral("RELATED-TO")));
    }
};

QTEST_GUILESS_MAIN(TestIcsExporter)
#include "test_icsexporter.moc"
