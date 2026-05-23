#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

#include "sync/merger.h"

// Helper: build a minimal entity JSON object.
static QJsonObject makeEntity(const QString &id,
                              const QString &content,
                              const QString &updatedAt,
                              bool isDeleted = false)
{
    QJsonObject obj;
    obj[QStringLiteral("id")]         = id;
    obj[QStringLiteral("content")]    = content;
    obj[QStringLiteral("updated_at")] = updatedAt;
    obj[QStringLiteral("is_deleted")] = isDeleted;
    return obj;
}

// Helper: build a state JSON with one "tasks" array.
static QJsonObject makeState(const QJsonArray &tasks,
                             const QJsonObject &settings = QJsonObject())
{
    QJsonObject state;
    state[QStringLiteral("tasks")]    = tasks;
    state[QStringLiteral("projects")] = QJsonArray();
    state[QStringLiteral("sections")] = QJsonArray();
    state[QStringLiteral("labels")]   = QJsonArray();
    state[QStringLiteral("filters")]  = QJsonArray();
    state[QStringLiteral("reminders")]= QJsonArray();
    state[QStringLiteral("comments")] = QJsonArray();
    state[QStringLiteral("settings")] = settings;
    return state;
}

// Find an entity by id in a QJsonArray.
static QJsonObject findById(const QJsonArray &arr, const QString &id)
{
    for (const auto &v : arr) {
        const QJsonObject obj = v.toObject();
        if (obj[QStringLiteral("id")].toString() == id)
            return obj;
    }
    return QJsonObject();
}

class TestMerger : public QObject
{
    Q_OBJECT

private slots:
    // ── LWW: local is newer ──────────────────────────────────────────────────
    void lww_localNewer()
    {
        Merger m;
        const QString id = QStringLiteral("task-1");

        QJsonArray localArr;
        localArr.append(makeEntity(id, QStringLiteral("Local content"),
                                   QStringLiteral("2026-04-02T12:00:00.000Z")));
        QJsonArray remoteArr;
        remoteArr.append(makeEntity(id, QStringLiteral("Remote content"),
                                    QStringLiteral("2026-04-01T12:00:00.000Z")));

        const QJsonObject merged = m.mergeStates(makeState(localArr), makeState(remoteArr));
        const QJsonArray tasks   = merged[QStringLiteral("tasks")].toArray();
        const QJsonObject task   = findById(tasks, id);

        QVERIFY(!task.isEmpty());
        QCOMPARE(task[QStringLiteral("content")].toString(), QStringLiteral("Local content"));
    }

    // ── LWW: remote is newer ─────────────────────────────────────────────────
    void lww_remoteNewer()
    {
        Merger m;
        const QString id = QStringLiteral("task-2");

        QJsonArray localArr;
        localArr.append(makeEntity(id, QStringLiteral("Local content"),
                                   QStringLiteral("2026-03-01T10:00:00.000Z")));
        QJsonArray remoteArr;
        remoteArr.append(makeEntity(id, QStringLiteral("Remote content"),
                                    QStringLiteral("2026-03-15T10:00:00.000Z")));

        const QJsonObject merged = m.mergeStates(makeState(localArr), makeState(remoteArr));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        QCOMPARE(task[QStringLiteral("content")].toString(), QStringLiteral("Remote content"));
    }

    // ── LWW: equal timestamps — remote is used as base (remote wins) ────────
    void lww_equalTimestamps_remoteWins()
    {
        Merger m;
        const QString id  = QStringLiteral("task-eq");
        const QString ts  = QStringLiteral("2026-01-15T08:00:00.000Z");

        QJsonArray localArr;
        localArr.append(makeEntity(id, QStringLiteral("Local"), ts));
        QJsonArray remoteArr;
        remoteArr.append(makeEntity(id, QStringLiteral("Remote"), ts));

        const QJsonObject task = findById(
            m.mergeStates(makeState(localArr), makeState(remoteArr))[QStringLiteral("tasks")].toArray(),
            id);

        QVERIFY(!task.isEmpty());
        // When equal, resolveConflict starts with remote as base and never
        // replaces it (localTime > remoteTime is false), so remote wins.
        QCOMPARE(task[QStringLiteral("content")].toString(), QStringLiteral("Remote"));
    }

    // ── Tombstone: local deleted (old), remote valid (newer) → deleted wins ─
    void tombstone_localDeletedOldRemoteValidNewer()
    {
        Merger m;
        const QString id = QStringLiteral("task-tomb-1");

        QJsonArray localArr;
        localArr.append(makeEntity(id, QStringLiteral("Old local"),
                                   QStringLiteral("2025-01-01T00:00:00.000Z"),
                                   /*isDeleted=*/true));
        QJsonArray remoteArr;
        remoteArr.append(makeEntity(id, QStringLiteral("Newer remote"),
                                    QStringLiteral("2026-01-01T00:00:00.000Z"),
                                    /*isDeleted=*/false));

        const QJsonObject task = findById(
            m.mergeStates(makeState(localArr), makeState(remoteArr))[QStringLiteral("tasks")].toArray(),
            id);

        QVERIFY(!task.isEmpty());
        QVERIFY(task[QStringLiteral("is_deleted")].toBool());
    }

    // ── Tombstone: remote deleted (newer), local valid (older) → deleted wins
    void tombstone_remoteDeletedNewer()
    {
        Merger m;
        const QString id = QStringLiteral("task-tomb-2");

        QJsonArray localArr;
        localArr.append(makeEntity(id, QStringLiteral("Valid local"),
                                   QStringLiteral("2025-06-01T00:00:00.000Z"),
                                   /*isDeleted=*/false));
        QJsonArray remoteArr;
        remoteArr.append(makeEntity(id, QStringLiteral("Remote deleted"),
                                    QStringLiteral("2026-01-01T00:00:00.000Z"),
                                    /*isDeleted=*/true));

        const QJsonObject task = findById(
            m.mergeStates(makeState(localArr), makeState(remoteArr))[QStringLiteral("tasks")].toArray(),
            id);

        QVERIFY(!task.isEmpty());
        QVERIFY(task[QStringLiteral("is_deleted")].toBool());
    }

    // ── Tombstone: both deleted — stays deleted ──────────────────────────────
    void tombstone_bothDeleted()
    {
        Merger m;
        const QString id = QStringLiteral("task-tomb-3");

        QJsonArray localArr;
        localArr.append(makeEntity(id, QStringLiteral("x"),
                                   QStringLiteral("2026-01-01T00:00:00.000Z"),
                                   true));
        QJsonArray remoteArr;
        remoteArr.append(makeEntity(id, QStringLiteral("x"),
                                    QStringLiteral("2026-01-01T00:00:00.000Z"),
                                    true));

        const QJsonObject task = findById(
            m.mergeStates(makeState(localArr), makeState(remoteArr))[QStringLiteral("tasks")].toArray(),
            id);

        QVERIFY(!task.isEmpty());
        QVERIFY(task[QStringLiteral("is_deleted")].toBool());
    }

    // ── Set merge: union of A,B + B,C → A,B,C with B resolved by LWW ───────
    void merge_setUnion()
    {
        Merger m;

        QJsonArray localArr;
        localArr.append(makeEntity(QStringLiteral("A"), QStringLiteral("Task A"),
                                   QStringLiteral("2026-01-01T00:00:00.000Z")));
        localArr.append(makeEntity(QStringLiteral("B"), QStringLiteral("B local"),
                                   QStringLiteral("2026-01-01T00:00:00.000Z")));

        QJsonArray remoteArr;
        remoteArr.append(makeEntity(QStringLiteral("B"), QStringLiteral("B remote"),
                                    QStringLiteral("2026-02-01T00:00:00.000Z"))); // newer
        remoteArr.append(makeEntity(QStringLiteral("C"), QStringLiteral("Task C"),
                                    QStringLiteral("2026-01-15T00:00:00.000Z")));

        const QJsonArray merged = m.mergeStates(makeState(localArr), makeState(remoteArr))
                                    [QStringLiteral("tasks")].toArray();

        QCOMPARE(merged.size(), 3);

        const QJsonObject a = findById(merged, QStringLiteral("A"));
        const QJsonObject b = findById(merged, QStringLiteral("B"));
        const QJsonObject c = findById(merged, QStringLiteral("C"));

        QVERIFY(!a.isEmpty());
        QVERIFY(!b.isEmpty());
        QVERIFY(!c.isEmpty());

        // B conflict resolved by LWW — remote is newer
        QCOMPARE(b[QStringLiteral("content")].toString(), QStringLiteral("B remote"));
    }

    // ── Settings: remote-wins whole-object replacement ───────────────────────
    void settings_remoteWins()
    {
        Merger m;

        QJsonObject localSettings;
        localSettings[QStringLiteral("theme")]    = QStringLiteral("light");
        localSettings[QStringLiteral("language")] = QStringLiteral("ru");

        QJsonObject remoteSettings;
        remoteSettings[QStringLiteral("theme")]    = QStringLiteral("dark");
        remoteSettings[QStringLiteral("language")] = QStringLiteral("en");

        const QJsonObject result = m.mergeStates(makeState(QJsonArray(), localSettings),
                                                  makeState(QJsonArray(), remoteSettings));
        const QJsonObject s = result[QStringLiteral("settings")].toObject();

        QCOMPARE(s[QStringLiteral("theme")].toString(),    QStringLiteral("dark"));
        QCOMPARE(s[QStringLiteral("language")].toString(), QStringLiteral("en"));
    }

    // ── iter-night-30: empty remote settings must NOT wipe local ─────────────
    //
    // The whole-object-LWW design means remote wins WHEN remote has a payload.
    // When remote has none (peer hasn't pushed any user settings yet — fresh
    // device bootstrap, or an old snapshot with settings: {} key, or no
    // `settings` key at all), the merge must fall back to local. Otherwise
    // the very next pull-with-pending-commits writes back an empty settings
    // object and the user loses theme/language/karma/AI-provider/hotkeys/
    // default-project all at once. After loadFromJson(merged) the loss is
    // committed and propagated to peers on the next push.
    //
    // Three pin slots covering the three ways "no remote settings" arises:
    //   1. remote has `settings: {}` (empty but present)
    //   2. remote has NO `settings` key at all
    //   3. control: remote has actual settings — LWW still applies (this is
    //      the existing settings_remoteWins test; we don't duplicate it here
    //      but keep both for clarity).
    void settings_emptyRemoteKeepsLocal()
    {
        Merger m;

        QJsonObject localSettings;
        localSettings[QStringLiteral("theme")]    = QStringLiteral("kale");
        localSettings[QStringLiteral("language")] = QStringLiteral("ru");
        localSettings[QStringLiteral("karma_score")] = 1247;

        // Remote carries an explicit empty settings object.
        const QJsonObject result = m.mergeStates(makeState(QJsonArray(), localSettings),
                                                  makeState(QJsonArray(), QJsonObject()));
        const QJsonObject s = result[QStringLiteral("settings")].toObject();

        QCOMPARE(s[QStringLiteral("theme")].toString(),    QStringLiteral("kale"));
        QCOMPARE(s[QStringLiteral("language")].toString(), QStringLiteral("ru"));
        QCOMPARE(s[QStringLiteral("karma_score")].toInt(), 1247);
    }

    void settings_missingRemoteKeyKeepsLocal()
    {
        Merger m;

        QJsonObject localSettings;
        localSettings[QStringLiteral("theme")]      = QStringLiteral("dark");
        localSettings[QStringLiteral("hotkeys")]    = QStringLiteral("Ctrl+Alt+A");
        localSettings[QStringLiteral("default_project_id")] =
            QStringLiteral("11111111-2222-3333-4444-555555555555");

        // Build a remote state WITHOUT the "settings" key at all (simulates a
        // legacy snapshot or a peer that never serialised settings).
        QJsonObject localState = makeState(QJsonArray(), localSettings);
        QJsonObject remoteState;
        remoteState[QStringLiteral("tasks")]    = QJsonArray();
        remoteState[QStringLiteral("projects")] = QJsonArray();
        remoteState[QStringLiteral("sections")] = QJsonArray();
        remoteState[QStringLiteral("labels")]   = QJsonArray();
        remoteState[QStringLiteral("filters")]  = QJsonArray();
        remoteState[QStringLiteral("reminders")]= QJsonArray();
        remoteState[QStringLiteral("comments")] = QJsonArray();
        // (deliberately no "settings" key)

        const QJsonObject result = m.mergeStates(localState, remoteState);
        const QJsonObject s = result[QStringLiteral("settings")].toObject();

        QCOMPARE(s[QStringLiteral("theme")].toString(),   QStringLiteral("dark"));
        QCOMPARE(s[QStringLiteral("hotkeys")].toString(), QStringLiteral("Ctrl+Alt+A"));
        QCOMPARE(s[QStringLiteral("default_project_id")].toString(),
                 QStringLiteral("11111111-2222-3333-4444-555555555555"));
    }

    // ── Idempotence: merge(state, state) == state ────────────────────────────
    void idempotence()
    {
        Merger m;

        QJsonArray tasks;
        tasks.append(makeEntity(QStringLiteral("i1"), QStringLiteral("Task 1"),
                                QStringLiteral("2026-03-01T00:00:00.000Z")));
        tasks.append(makeEntity(QStringLiteral("i2"), QStringLiteral("Task 2"),
                                QStringLiteral("2026-03-02T00:00:00.000Z"), true));

        const QJsonObject state  = makeState(tasks);
        const QJsonObject merged = m.mergeStates(state, state);

        const QJsonArray orig   = state[QStringLiteral("tasks")].toArray();
        const QJsonArray result = merged[QStringLiteral("tasks")].toArray();

        QCOMPARE(result.size(), orig.size());

        for (const auto &v : orig) {
            const QJsonObject o = v.toObject();
            const QJsonObject r = findById(result, o[QStringLiteral("id")].toString());
            QCOMPARE(r[QStringLiteral("content")].toString(),
                     o[QStringLiteral("content")].toString());
            QCOMPARE(r[QStringLiteral("is_deleted")].toBool(),
                     o[QStringLiteral("is_deleted")].toBool());
        }
    }

    // ── Local-only entity kept ───────────────────────────────────────────────
    void localOnly_kept()
    {
        Merger m;
        const QString id = QStringLiteral("local-only");

        QJsonArray localArr;
        localArr.append(makeEntity(id, QStringLiteral("Only local"),
                                   QStringLiteral("2026-05-01T00:00:00.000Z")));

        const QJsonObject merged = m.mergeStates(makeState(localArr), makeState(QJsonArray()));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        QCOMPARE(task[QStringLiteral("content")].toString(), QStringLiteral("Only local"));
    }

    // ── Remote-only entity added ─────────────────────────────────────────────
    void remoteOnly_added()
    {
        Merger m;
        const QString id = QStringLiteral("remote-only");

        QJsonArray remoteArr;
        remoteArr.append(makeEntity(id, QStringLiteral("Only remote"),
                                    QStringLiteral("2026-05-01T00:00:00.000Z")));

        const QJsonObject merged = m.mergeStates(makeState(QJsonArray()), makeState(remoteArr));
        const QJsonObject task   = findById(merged[QStringLiteral("tasks")].toArray(), id);

        QVERIFY(!task.isEmpty());
        QCOMPARE(task[QStringLiteral("content")].toString(), QStringLiteral("Only remote"));
    }

    // ── All entity collections are merged (not just tasks) ───────────────────
    void allCollectionsMerged()
    {
        Merger m;

        auto makeProject = [](const QString &id, const QString &name, const QString &ts) {
            QJsonObject obj;
            obj[QStringLiteral("id")]         = id;
            obj[QStringLiteral("name")]        = name;
            obj[QStringLiteral("updated_at")] = ts;
            obj[QStringLiteral("is_deleted")] = false;
            return obj;
        };

        QJsonObject localState  = makeState(QJsonArray());
        QJsonObject remoteState = makeState(QJsonArray());

        QJsonArray localProjects;
        localProjects.append(makeProject(QStringLiteral("p1"), QStringLiteral("Local Project"),
                                         QStringLiteral("2026-01-01T00:00:00.000Z")));
        localState[QStringLiteral("projects")] = localProjects;

        QJsonArray remoteProjects;
        remoteProjects.append(makeProject(QStringLiteral("p2"), QStringLiteral("Remote Project"),
                                          QStringLiteral("2026-01-01T00:00:00.000Z")));
        remoteState[QStringLiteral("projects")] = remoteProjects;

        const QJsonObject result = m.mergeStates(localState, remoteState);
        const QJsonArray projects = result[QStringLiteral("projects")].toArray();

        QCOMPARE(projects.size(), 2);
        QVERIFY(!findById(projects, QStringLiteral("p1")).isEmpty());
        QVERIFY(!findById(projects, QStringLiteral("p2")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestMerger)
#include "test_merger.moc"
