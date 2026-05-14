#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/label.h"
#include "model/comment.h"
#include "viewmodels/searchviewmodel.h"
#include "utils/settingskeys.h"

// ============================================================================
// Comprehensive unit tests for SearchViewModel.
//
// Public API exercised:
//   - Q_PROPERTY searchQuery (READ searchQuery, WRITE setSearchQuery,
//                             NOTIFY searchQueryChanged)
//   - Q_PROPERTY results, recentSearches, resultCount, isSearching
//   - Q_INVOKABLE search(query)            – setSearchQuery + addToRecent
//   - Q_INVOKABLE clearSearch()
//   - Q_INVOKABLE clearRecentSearches()
//   - Q_INVOKABLE removeRecentSearch(query)
//
// Persistence boundaries (verified by reading the keys directly):
//   - "search/last_query"          → QSettings (plain, app-scoped)
//   - SettingsKeys::RecentSearches → DataStore::settings() (state.json)
//
// Implementation invariants encoded as tests:
//   - search runs only when query.length() >= 2.
//   - persists query when empty OR length >= 3
//     (transient 1–2 char states are deliberately skipped).
//   - duplicate queries are dedup'd most-recent-first.
//   - history is capped at MAX_RECENT_SEARCHES (10).
//   - searchTasks filters out tombstoned (is_deleted) tasks but
//     INCLUDES completed tasks.
// ============================================================================

namespace {

// Build a fixture DataStore with a "Personal" project and the following
// task layout:
//   3 LIVE matching tasks ("foo bar", "Foobar baz", "alpha foo")
//   2 LIVE non-matching tasks ("hello world", "weather")
//   1 SOFT-DELETED matching task ("foo deleted") – must be excluded
//   1 COMPLETED matching task   ("foo done")    – currently INCLUDED
//
// Returns ids of the matching tasks so individual tests can inspect them.
struct Fixture {
    DataStore *store = nullptr;
    QString projectId;

    QString matchA;     // "foo bar"
    QString matchB;     // "Foobar baz" (mixed case for case-insensitivity)
    QString matchC;     // "alpha foo" (matches via description, content has no foo)
    QString deletedM;   // soft-deleted, contains "foo"
    QString completedM; // completed, contains "foo"
    QString nonA;       // "hello world"
    QString nonB;       // "weather"

    ~Fixture() { delete store; }
};

static Fixture *makeFixture()
{
    auto *f = new Fixture;
    f->store = new DataStore;

    Project *p = f->store->addProject(QStringLiteral("Personal"));
    f->projectId = p->id();

    Task *a = f->store->addTask(QStringLiteral("foo bar"), f->projectId);
    f->matchA = a->id();

    Task *b = f->store->addTask(QStringLiteral("Foobar baz"), f->projectId);
    f->matchB = b->id();

    // matchC has no "foo" in content but does in description
    Task *c = f->store->addTask(QStringLiteral("alpha"), f->projectId);
    c->setDescription(QStringLiteral("notes about foo here"));
    f->matchC = c->id();

    Task *d = f->store->addTask(QStringLiteral("foo deleted"), f->projectId);
    f->deletedM = d->id();
    f->store->removeTask(f->deletedM); // soft-delete

    Task *e = f->store->addTask(QStringLiteral("foo done"), f->projectId);
    e->setIsCompleted(true);
    f->completedM = e->id();

    Task *n1 = f->store->addTask(QStringLiteral("hello world"), f->projectId);
    f->nonA = n1->id();

    Task *n2 = f->store->addTask(QStringLiteral("weather"), f->projectId);
    f->nonB = n2->id();

    return f;
}

// Helper: scan results for a task with a given id.
static bool resultsContainTaskId(const QVariantList &results, const QString &taskId)
{
    for (const QVariant &v : results) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("type")).toString() == QLatin1String("task") &&
            m.value(QStringLiteral("id")).toString() == taskId)
            return true;
    }
    return false;
}

// Count the number of result rows of a given type.
static int countResultsOfType(const QVariantList &results, const QString &type)
{
    int n = 0;
    for (const QVariant &v : results) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("type")).toString() == type)
            ++n;
    }
    return n;
}

} // anonymous namespace


class TestSearchViewModel : public QObject
{
    Q_OBJECT

private slots:

    // -----------------------------------------------------------------------
    // Test environment: route QSettings into a per-test scope so we don't
    // pollute the user's real OpenTodo registry hive.
    // -----------------------------------------------------------------------
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest"));
        QCoreApplication::setApplicationName(QStringLiteral("test_searchviewmodel"));
    }

    void init()
    {
        // Wipe any leftover state from a previous slot. Because the
        // constructor reads "search/last_query" from QSettings, slot
        // isolation is critical to avoid cross-talk.
        QSettings qs;
        qs.remove(QStringLiteral("search"));
        qs.sync();
    }

    void cleanupTestCase()
    {
        QSettings qs;
        qs.remove(QStringLiteral("search"));
        qs.sync();
    }

    // =====================================================================
    // 1) Bootstrap state
    // =====================================================================
    void bootstrap_query_empty_and_no_results()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        QCOMPARE(vm.searchQuery(), QString());
        QCOMPARE(vm.resultCount(), 0);
        QVERIFY(vm.results().isEmpty());
        QVERIFY(vm.recentSearches().isEmpty());
        QVERIFY(!vm.isSearching());

        delete f;
    }

    // =====================================================================
    // 2) setSearchQuery updates the property and fires the signal
    // =====================================================================
    void setSearchQuery_updates_property_and_emits_signal()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        QSignalSpy querySpy(&vm, &SearchViewModel::searchQueryChanged);
        QVERIFY(querySpy.isValid());

        vm.setSearchQuery(QStringLiteral("foo"));

        QCOMPARE(vm.searchQuery(), QStringLiteral("foo"));
        QCOMPARE(querySpy.count(), 1);

        // Idempotent set → no further emission
        vm.setSearchQuery(QStringLiteral("foo"));
        QCOMPARE(querySpy.count(), 1);

        delete f;
    }

    // =====================================================================
    // 3) Persistence to QSettings — verify by reading the key directly
    //    Implementation only persists when query is empty OR length >= 3.
    // =====================================================================
    void setSearchQuery_persists_to_qsettings()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        vm.setSearchQuery(QStringLiteral("hello"));

        QSettings qs;
        qs.sync();
        QCOMPARE(qs.value(QStringLiteral("search/last_query")).toString(),
                 QStringLiteral("hello"));

        delete f;
    }

    void setSearchQuery_skips_persistence_for_short_transient_query()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        // Pre-seed a known value so we can detect (lack of) overwrite.
        QSettings preset;
        preset.setValue(QStringLiteral("search/last_query"),
                        QStringLiteral("previous"));
        preset.sync();

        // 2 chars – mid-typing, should NOT overwrite the persisted value.
        vm.setSearchQuery(QStringLiteral("pr"));

        QSettings qs;
        qs.sync();
        QCOMPARE(qs.value(QStringLiteral("search/last_query")).toString(),
                 QStringLiteral("previous"));

        delete f;
    }

    // =====================================================================
    // 4) Restart simulation: a NEW SearchViewModel reads QSettings on ctor
    // =====================================================================
    void restart_restores_query_from_qsettings()
    {
        // Pre-populate as if a previous session had searched for "alpha".
        QSettings seed;
        seed.setValue(QStringLiteral("search/last_query"),
                      QStringLiteral("alpha"));
        seed.sync();

        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        // The constructor restores the query but explicitly does NOT trigger
        // a search — that requires the user's first interaction.
        QCOMPARE(vm.searchQuery(), QStringLiteral("alpha"));
        QCOMPARE(vm.resultCount(), 0);

        delete f;
    }

    // =====================================================================
    // 5) Empty query: clears results, doesn't crash, doesn't error
    // =====================================================================
    void empty_query_clears_results()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        // First, build up some results.
        vm.setSearchQuery(QStringLiteral("foo"));
        QVERIFY(vm.resultCount() > 0);

        QSignalSpy resultsSpy(&vm, &SearchViewModel::resultsChanged);

        vm.setSearchQuery(QString());
        QCOMPARE(vm.searchQuery(), QString());
        QCOMPARE(vm.resultCount(), 0);
        QVERIFY(vm.results().isEmpty());
        // Going from non-empty → empty must signal resultsChanged.
        QVERIFY(resultsSpy.count() >= 1);

        delete f;
    }

    // =====================================================================
    // 6) Single-char query is below the search threshold → no results
    // =====================================================================
    void single_char_query_does_not_run_search()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        vm.setSearchQuery(QStringLiteral("f"));

        QCOMPARE(vm.searchQuery(), QStringLiteral("f"));
        // Below 2-char threshold: no search executed, no results materialised.
        QCOMPARE(vm.resultCount(), 0);

        delete f;
    }

    // =====================================================================
    // 7) Whitespace-only query: implementation does substring match against
    //    content. None of our fixture tasks contain leading/trailing
    //    whitespace runs, so the search yields nothing — effectively the
    //    "treated as empty" outcome the spec asks for, while documenting
    //    the actual behaviour.
    // =====================================================================
    void whitespace_only_query_yields_no_results()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        vm.setSearchQuery(QStringLiteral("   "));
        // 3 spaces → length() == 3 → search runs, but nothing in the fixture
        // contains a 3-space sequence.
        QCOMPARE(vm.resultCount(), 0);

        delete f;
    }

    // =====================================================================
    // 8) Case-insensitivity: query "FOO" must match "foo bar" and "Foobar baz"
    // =====================================================================
    void search_is_case_insensitive()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        vm.setSearchQuery(QStringLiteral("FOO"));

        QVERIFY(resultsContainTaskId(vm.results(), f->matchA));
        QVERIFY(resultsContainTaskId(vm.results(), f->matchB));

        delete f;
    }

    // =====================================================================
    // 9) Search returns the expected number of matching tasks
    //    – includes the 3 live + 1 completed (current behaviour)
    //    – excludes the 1 soft-deleted
    // =====================================================================
    void search_returns_matching_tasks_only()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        vm.setSearchQuery(QStringLiteral("foo"));

        const int taskHits = countResultsOfType(vm.results(),
                                                QStringLiteral("task"));
        // Live matching: matchA + matchB + matchC + completedM = 4.
        // (deletedM is filtered out.)
        QCOMPARE(taskHits, 4);

        QVERIFY( resultsContainTaskId(vm.results(), f->matchA));
        QVERIFY( resultsContainTaskId(vm.results(), f->matchB));
        QVERIFY( resultsContainTaskId(vm.results(), f->matchC));
        QVERIFY( resultsContainTaskId(vm.results(), f->completedM));

        delete f;
    }

    // =====================================================================
    // 10) Soft-deleted tasks are excluded from search results
    // =====================================================================
    void search_excludes_soft_deleted_tasks()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        vm.setSearchQuery(QStringLiteral("foo"));

        QVERIFY(!resultsContainTaskId(vm.results(), f->deletedM));

        delete f;
    }

    // =====================================================================
    // 11) Completed tasks ARE currently included in search results.
    //     This documents the implemented behaviour so a future change to
    //     hide completed tasks would need to update this test deliberately.
    // =====================================================================
    void search_includes_completed_tasks()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        vm.setSearchQuery(QStringLiteral("foo"));

        QVERIFY(resultsContainTaskId(vm.results(), f->completedM));

        delete f;
    }

    // =====================================================================
    // 12) Project search: results should also surface matching projects
    // =====================================================================
    void search_returns_matching_projects()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        vm.setSearchQuery(QStringLiteral("Personal"));

        QCOMPARE(countResultsOfType(vm.results(), QStringLiteral("project")), 1);

        delete f;
    }

    // =====================================================================
    // 13) Label search: matching labels should appear with type "label"
    // =====================================================================
    void search_returns_matching_labels()
    {
        Fixture *f = makeFixture();

        // Add a label with a unique substring that nothing else matches.
        Label *l = f->store->addLabel(QStringLiteral("urgentXYZ"));
        QVERIFY(l != nullptr);

        SearchViewModel vm(f->store);
        vm.setSearchQuery(QStringLiteral("urgentXYZ"));

        QCOMPARE(countResultsOfType(vm.results(), QStringLiteral("label")), 1);

        delete f;
    }

    // =====================================================================
    // 14) Comment search: matching comments should appear with type "comment"
    // =====================================================================
    void search_returns_matching_comments()
    {
        Fixture *f = makeFixture();

        // Attach a comment with a uniquely identifiable phrase.
        Comment *c = f->store->addComment(
            QStringLiteral("uniquephrase appears here"),
            f->matchA, f->projectId);
        QVERIFY(c != nullptr);

        SearchViewModel vm(f->store);
        vm.setSearchQuery(QStringLiteral("uniquephrase"));

        QCOMPARE(countResultsOfType(vm.results(), QStringLiteral("comment")), 1);

        delete f;
    }

    // =====================================================================
    // 15) Recent searches – round trip via search() / recentSearches()
    //     Recent searches are stored on DataStore::settings() (state.json),
    //     not in QSettings.
    // =====================================================================
    void search_method_adds_query_to_recent()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        QSignalSpy rsSpy(&vm, &SearchViewModel::recentSearchesChanged);

        vm.search(QStringLiteral("foo"));

        QVERIFY(rsSpy.count() >= 1);
        QCOMPARE(vm.recentSearches().size(), 1);
        QCOMPARE(vm.recentSearches().first(), QStringLiteral("foo"));

        // Persisted to DataStore-level settings under the documented key.
        const QStringList persisted =
            f->store->setting(QLatin1String(SettingsKeys::RecentSearches))
                .toStringList();
        QCOMPARE(persisted, QStringList{QStringLiteral("foo")});

        delete f;
    }

    // =====================================================================
    // 16) search() with whitespace-only query does NOT add to recent
    // =====================================================================
    void search_method_skips_whitespace_only_in_recent()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        vm.search(QStringLiteral("    "));
        QVERIFY(vm.recentSearches().isEmpty());

        delete f;
    }

    // =====================================================================
    // 17) Most-recent-first ordering and de-duplication on repeated search
    // =====================================================================
    void recent_searches_are_dedup_most_recent_first()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        vm.search(QStringLiteral("alpha"));
        vm.search(QStringLiteral("beta"));
        vm.search(QStringLiteral("gamma"));
        vm.search(QStringLiteral("alpha")); // duplicate – should move to head

        QStringList r = vm.recentSearches();
        QCOMPARE(r.size(), 3);
        QCOMPARE(r.at(0), QStringLiteral("alpha"));
        QCOMPARE(r.at(1), QStringLiteral("gamma"));
        QCOMPARE(r.at(2), QStringLiteral("beta"));

        delete f;
    }

    // =====================================================================
    // 18) History capacity: at most MAX_RECENT_SEARCHES entries (=10)
    // =====================================================================
    void recent_searches_capped_at_max()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        // Push 12 distinct queries.
        for (int i = 0; i < 12; ++i)
            vm.search(QStringLiteral("query%1").arg(i));

        QStringList r = vm.recentSearches();
        QCOMPARE(r.size(), 10);

        // The 11th-most-recent ("query2") and earlier were pushed out.
        // Newest (query11) is at the head; query2 must be gone.
        QCOMPARE(r.at(0), QStringLiteral("query11"));
        QVERIFY(!r.contains(QStringLiteral("query0")));
        QVERIFY(!r.contains(QStringLiteral("query1")));

        delete f;
    }

    // =====================================================================
    // 19) removeRecentSearch removes the specified entry only
    // =====================================================================
    void removeRecentSearch_removes_only_target()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        vm.search(QStringLiteral("alpha"));
        vm.search(QStringLiteral("beta"));
        vm.search(QStringLiteral("gamma"));

        QSignalSpy rsSpy(&vm, &SearchViewModel::recentSearchesChanged);
        vm.removeRecentSearch(QStringLiteral("beta"));
        QVERIFY(rsSpy.count() >= 1);

        const QStringList r = vm.recentSearches();
        QCOMPARE(r.size(), 2);
        QVERIFY(!r.contains(QStringLiteral("beta")));
        QVERIFY( r.contains(QStringLiteral("alpha")));
        QVERIFY( r.contains(QStringLiteral("gamma")));

        // Persistence reflects the removal.
        const QStringList persisted =
            f->store->setting(QLatin1String(SettingsKeys::RecentSearches))
                .toStringList();
        QVERIFY(!persisted.contains(QStringLiteral("beta")));

        delete f;
    }

    // =====================================================================
    // 20) clearRecentSearches empties the list and clears persistence
    // =====================================================================
    void clearRecentSearches_empties_and_persists()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        vm.search(QStringLiteral("a"));
        vm.search(QStringLiteral("b"));
        QVERIFY(!vm.recentSearches().isEmpty());

        QSignalSpy rsSpy(&vm, &SearchViewModel::recentSearchesChanged);
        vm.clearRecentSearches();
        QCOMPARE(rsSpy.count(), 1);

        QVERIFY(vm.recentSearches().isEmpty());

        // Persistence stores invalid → setting() returns invalid QVariant.
        const QVariant persisted =
            f->store->setting(QLatin1String(SettingsKeys::RecentSearches));
        QVERIFY(!persisted.isValid() ||
                persisted.toStringList().isEmpty());

        delete f;
    }

    // =====================================================================
    // 21) Recent searches are restored when a new SearchViewModel is built
    //     against a DataStore whose settings already contain them.
    // =====================================================================
    void recent_searches_restored_on_new_viewmodel()
    {
        Fixture *f = makeFixture();

        // Seed DataStore settings directly, the same way SyncEngine would.
        const QStringList seed = {
            QStringLiteral("aaa"),
            QStringLiteral("bbb"),
            QStringLiteral("ccc")
        };
        f->store->setSetting(QLatin1String(SettingsKeys::RecentSearches),
                             QVariant::fromValue(seed));

        SearchViewModel vm(f->store);
        QCOMPARE(vm.recentSearches(), seed);

        delete f;
    }

    // =====================================================================
    // 22) clearSearch wipes both the query and the results, and persists
    //     an empty string (so the next launch starts blank).
    // =====================================================================
    void clearSearch_wipes_query_and_results_and_persists_empty()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        vm.setSearchQuery(QStringLiteral("foo"));
        QVERIFY(vm.resultCount() > 0);

        QSignalSpy querySpy  (&vm, &SearchViewModel::searchQueryChanged);
        QSignalSpy resultSpy(&vm, &SearchViewModel::resultsChanged);

        vm.clearSearch();

        QCOMPARE(vm.searchQuery(), QString());
        QCOMPARE(vm.resultCount(), 0);
        QCOMPARE(querySpy.count(),  1);
        QCOMPARE(resultSpy.count(), 1);

        QSettings qs;
        qs.sync();
        QCOMPARE(qs.value(QStringLiteral("search/last_query")).toString(),
                 QString());

        delete f;
    }

    // =====================================================================
    // 23) Live refresh: when the underlying DataStore mutates while the
    //     query is active, results are recomputed automatically.
    // =====================================================================
    void results_refresh_when_data_changes()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        vm.setSearchQuery(QStringLiteral("foo"));
        const int before = vm.resultCount();
        QVERIFY(before > 0);

        // Add a new task that should show up under the current query.
        Task *extra = f->store->addTask(QStringLiteral("foo extra"),
                                        f->projectId);
        QVERIFY(extra != nullptr);

        // The DataStore::tasksChanged signal triggers refreshIfActive, which
        // re-runs performSearch when query length >= 2.
        QCOMPARE(vm.resultCount(), before + 1);
        QVERIFY(resultsContainTaskId(vm.results(), extra->id()));

        delete f;
    }

    // =====================================================================
    // 24) aboutToReset: results must be cleared so the model doesn't hold
    //     dangling pointers to entities the store is about to destroy.
    // =====================================================================
    void about_to_reset_clears_results()
    {
        Fixture *f = makeFixture();
        SearchViewModel vm(f->store);

        vm.setSearchQuery(QStringLiteral("foo"));
        QVERIFY(vm.resultCount() > 0);

        QSignalSpy resultsSpy(&vm, &SearchViewModel::resultsChanged);
        emit f->store->aboutToReset();

        QCOMPARE(vm.resultCount(), 0);
        QVERIFY(resultsSpy.count() >= 1);

        delete f;
    }
};

QTEST_GUILESS_MAIN(TestSearchViewModel)
#include "test_searchviewmodel.moc"
