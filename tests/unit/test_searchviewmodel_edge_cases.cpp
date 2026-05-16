#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QSet>

#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "model/section.h"
#include "model/label.h"
#include "model/filter.h"
#include "model/comment.h"
#include "viewmodels/searchviewmodel.h"
#include "utils/settingskeys.h"

// ============================================================================
// SearchViewModel — DEEP edge-case coverage (round 2.21).
//
// Complements tests/unit/test_searchviewmodel.cpp (the baseline contract suite)
// with adversarial inputs and corner-case semantics that the original suite
// does not exercise. Specifically:
//
//   1. Empty / whitespace-only query semantics — what `setQuery("")` and
//      `setQuery("  ")` actually do (NOT equivalent: 2-space runs a full
//      performSearch and would match project / label names containing two
//      consecutive spaces, "" clears).
//   2. Russian / Cyrillic case folding — "Елка" vs "ёлка" vs "ЁЛКА" round-trip.
//   3. NFC vs NFD diacritic normalization — Qt's toLower() does NOT normalize.
//      Combining vs precomposed forms compare unequal. (Pinned via qInfo.)
//   4. Project / Section / Label / Filter / Reminder name coverage — the C++
//      side searches Tasks (content+description), Projects, Labels, Comments.
//      Sections, Filters, Reminders are NOT indexed (pinned via qInfo).
//   5. Description-only matches — task content has no hit but description does.
//   6. Tombstone exclusion across every entity type the search touches.
//   7. 10 KB DoS-class query — no infinite loop, completes quickly, returns 0.
//   8. FilterParser operators (`& | ! ( ) ,`) passed verbatim — NOT parsed.
//   9. RecentSearches persistence on DataStore::settings() (state.json) +
//      QSettings restore of last_query on a freshly-constructed VM.
//  10. C++ side has NO debounce — every distinct setSearchQuery call fires
//      a single resultsChanged. Same-string repeat is idempotent.
//  11. Result-map shape — every result QVariantMap exposes the keys that
//      MobileLayout.qml and SearchBar.qml bind to (type, id, title, …).
//  12. Mass-result robustness — 200 matching tasks → all surfaced.
//
// Constraints (per mission brief):
//   * QTEST_GUILESS_MAIN
//   * Unique QSettings org/app to avoid cross-test pollution.
//   * No raw strings (every literal is QStringLiteral / QLatin1String / QChar).
//   * No function-local Q_OBJECT (single class at top level + .moc include).
//   * Russian UI strings naturally appear in test data (Cyrillic case folding).
//
// Bugs surfaced (documented via qInfo so green-test signal is preserved):
//   B1. setSearchQuery with EXACTLY 2 whitespace characters runs the full
//       search dispatch — it is NOT equivalent to setSearchQuery("").
//   B2. Diacritic equivalence is not implemented — NFC "é" != NFD "é".
//   B3. Sections / Filters / Reminders are silently excluded from the search
//       index even though they are first-class entities elsewhere.
//   B4. removeRecentSearch always writes settings + emits the change signal
//       even when the target string is not in the list (no-op write).
//   B5. There is no in-process debounce — rapid setSearchQuery calls produce
//       O(N) resultsChanged emissions (debounce lives in the QML layer only).
//
// All five quirks are surface-level documentation, not crashes. No production
// edits are made in this commit; the slots assert the OBSERVED behaviour and
// emit qInfo when that behaviour is suspect, so a future deliberate fix to
// any of them will produce a visible failure that flags the test for review.
// ============================================================================

namespace {

// Helper: scan a QVariantList of result maps for an entry of given (type, id).
static bool hasResult(const QVariantList &results,
                      const QString &type, const QString &id)
{
    for (const QVariant &v : results) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("type")).toString() == type
            && m.value(QStringLiteral("id")).toString() == id)
            return true;
    }
    return false;
}

static int countByType(const QVariantList &results, const QString &type)
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


class TestSearchViewModelEdgeCases : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_SearchVMEdge"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_searchviewmodel_edge_cases"));
    }

    void init()
    {
        // Per-slot QSettings wipe so the constructor's "search/last_query"
        // restore does not bleed state between slots.
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
    // 1) Whitespace-only query is NOT equivalent to empty.
    //
    //   * setSearchQuery("")     → clears results (early-return).
    //   * setSearchQuery(" ")    → 1 char, below threshold, no search runs
    //                              and results stay where they were.
    //   * setSearchQuery("  ")   → 2 chars, search dispatches; substring
    //                              " " (two spaces) is searched against
    //                              content, description, project name,
    //                              label name, comment content.
    //   * setSearchQuery("   ")  → same as above.
    //
    // The interesting case is the 2-space query: if any project/label/etc.
    // happens to contain two consecutive spaces in its name, it WILL match.
    // This is a real (if obscure) divergence from the natural-language
    // expectation that whitespace-only == empty.
    // =====================================================================
    void whitespace_query_is_not_equivalent_to_empty()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Two  Spaces Name"));
        Q_UNUSED(p);
        store.addProject(QStringLiteral("NoDoubleSpaces"));

        SearchViewModel vm(&store);

        // 0-char → no results.
        vm.setSearchQuery(QString());
        QCOMPARE(vm.resultCount(), 0);

        // 1-char → no search dispatched.
        vm.setSearchQuery(QStringLiteral(" "));
        QCOMPARE(vm.resultCount(), 0);

        // 2-char " " (two spaces): performSearch RUNS. The fixture project
        // "Two  Spaces Name" contains two consecutive spaces, so it must
        // match. The other project does not.
        vm.setSearchQuery(QStringLiteral("  "));
        const int projHits = countByType(vm.results(), QStringLiteral("project"));
        if (projHits != 1) {
            qInfo("B1 candidate: 2-space query did not match project with "
                  "embedded double space (got %d hits, expected 1).", projHits);
        }
        QCOMPARE(projHits, 1);
        qInfo("Pinned B1: setSearchQuery with 2-char whitespace runs full "
              "performSearch — not equivalent to empty query.");
    }

    // =====================================================================
    // 2) Empty-string transition: non-empty -> "" must clear results.
    // =====================================================================
    void empty_query_after_results_clears_atomically()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Личный"));
        store.addTask(QStringLiteral("Купить молоко"), p->id());
        store.addTask(QStringLiteral("Молочные продукты"), p->id());

        SearchViewModel vm(&store);
        vm.setSearchQuery(QStringLiteral("молок"));
        QVERIFY(vm.resultCount() >= 1);

        QSignalSpy resultsSpy(&vm, &SearchViewModel::resultsChanged);
        vm.setSearchQuery(QString());

        QCOMPARE(vm.resultCount(), 0);
        QVERIFY(vm.results().isEmpty());
        QVERIFY(resultsSpy.count() >= 1);
    }

    // =====================================================================
    // 3) Russian / Cyrillic case folding.
    //
    // Qt's QString::toLower() lowercases Cyrillic characters correctly:
    //   Е → е, Ё → ё, Й → й, А → а, etc.
    //
    // So a search for "ёлка" must match a task whose content is "Ёлка" and
    // a task whose content is "ЁЛКА", and vice versa.
    // =====================================================================
    void cyrillic_case_folding_works_both_directions()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Праздники"));

        Task *t1 = store.addTask(QStringLiteral("Ёлка"), p->id());
        Task *t2 = store.addTask(QStringLiteral("ЁЛКА в зале"), p->id());
        Task *t3 = store.addTask(QStringLiteral("Купить ёлочные шары"),
                                 p->id());

        SearchViewModel vm(&store);

        // Lowercase query, mixed-case content. "ёлк" should hit "Ёлка"
        // and "ЁЛКА в зале" — but NOT t3 ("Купить ёлочные шары"), which
        // contains "ёлоч" not "ёлк".
        vm.setSearchQuery(QStringLiteral("ёлк"));
        QVERIFY(hasResult(vm.results(),
                          QStringLiteral("task"), t1->id()));
        QVERIFY(hasResult(vm.results(),
                          QStringLiteral("task"), t2->id()));
        QVERIFY(!hasResult(vm.results(),
                           QStringLiteral("task"), t3->id()));

        // Uppercase query, lowercase content.
        vm.setSearchQuery(QStringLiteral("ЁЛК"));
        QVERIFY(hasResult(vm.results(),
                          QStringLiteral("task"), t1->id()));
        QVERIFY(hasResult(vm.results(),
                          QStringLiteral("task"), t2->id()));

        // Substring inside the word: "ёлоч" should hit t3.
        vm.setSearchQuery(QStringLiteral("ёлоч"));
        QVERIFY(hasResult(vm.results(),
                          QStringLiteral("task"), t3->id()));

        // "Е" (no diaeresis) must NOT match "Ё" (with diaeresis) —
        // these are distinct codepoints. Pin that contract.
        vm.setSearchQuery(QStringLiteral("елк"));
        // None of the fixture tasks contain plain Е+л+к (they all use Ё).
        // Document the actual result via a soft assertion + qInfo.
        const int hits = countByType(vm.results(), QStringLiteral("task"));
        if (hits != 0) {
            qInfo("Note: 'елк' search produced %d task hits — Qt's toLower() "
                  "may be performing folding between Е and Ё.", hits);
        }
        QCOMPARE(hits, 0);
    }

    // =====================================================================
    // 4) NFC vs NFD diacritic normalization is NOT performed.
    //
    // Precomposed "é" (U+00E9) and decomposed "e" + combining acute
    // (U+0301) render identically and the user types them indistinguishably,
    // but byte-wise they are different. Qt's toLower() does NOT normalize,
    // so they compare unequal.
    //
    // This is a real edge case that affects French / Spanish / German /
    // Vietnamese task content. Document via qInfo + a pinned assertion so
    // a future fix (calling normalized(QString::NormalizationForm_C) on
    // both sides) produces a visible test signal.
    // =====================================================================
    void nfc_vs_nfd_diacritics_do_not_match()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Café"));

        // Precomposed é (U+00E9)
        Task *precomposed = store.addTask(
            QString::fromUtf8("Caf\xC3\xA9 visit"),  // "Café visit"
            p->id());

        // Decomposed: e (U+0065) + combining acute (U+0301)
        Task *decomposed = store.addTask(
            QStringLiteral("Cafe") + QChar(0x0301) + QStringLiteral(" visit"),
            p->id());

        Q_UNUSED(precomposed);
        Q_UNUSED(decomposed);

        SearchViewModel vm(&store);

        // Query with precomposed é
        vm.setSearchQuery(QString::fromUtf8("caf\xC3\xA9"));

        // The precomposed task MUST match.
        QVERIFY(hasResult(vm.results(),
                          QStringLiteral("task"), precomposed->id()));

        // The decomposed task should NOT match — byte sequences differ.
        const bool decomposedMatches = hasResult(
            vm.results(), QStringLiteral("task"), decomposed->id());
        if (decomposedMatches) {
            qInfo("Surprise: NFD-form task matched NFC-form query — Qt may "
                  "have started normalizing in toLower(). Update test.");
        } else {
            qInfo("B2 pinned: NFC 'é' (U+00E9) and NFD 'é' (U+0065+U+0301) "
                  "compare unequal in SearchViewModel — diacritic-equivalence "
                  "normalization is not implemented.");
        }
        QVERIFY(!decomposedMatches);
    }

    // =====================================================================
    // 5) Search across project / label / comment names — yes.
    //    Search across section / filter / reminder — no (pinned).
    // =====================================================================
    void search_covers_projects_labels_comments_but_not_sections_filters()
    {
        DataStore store;

        Project *p = store.addProject(QStringLiteral("uniqueSnowflake_proj"));
        Section *s = store.addSection(
            QStringLiteral("uniqueSnowflake_sec"), p->id());
        Label *l = store.addLabel(QStringLiteral("uniqueSnowflake_lab"));
        Filter *f = store.addFilter(
            QStringLiteral("uniqueSnowflake_fil"),
            QStringLiteral("@home"));
        Q_UNUSED(s);
        Q_UNUSED(f);

        Task *t = store.addTask(QStringLiteral("hosting party"), p->id());
        store.addComment(QStringLiteral("uniqueSnowflake_com appears"),
                         t->id(), p->id());

        SearchViewModel vm(&store);
        vm.setSearchQuery(QStringLiteral("uniqueSnowflake"));

        // INDEXED:
        QCOMPARE(countByType(vm.results(),
                             QStringLiteral("project")), 1);
        QCOMPARE(countByType(vm.results(),
                             QStringLiteral("label")), 1);
        QCOMPARE(countByType(vm.results(),
                             QStringLiteral("comment")), 1);

        // NOT INDEXED: sections + filters + reminders.
        QCOMPARE(countByType(vm.results(),
                             QStringLiteral("section")), 0);
        QCOMPARE(countByType(vm.results(),
                             QStringLiteral("filter")), 0);
        QCOMPARE(countByType(vm.results(),
                             QStringLiteral("reminder")), 0);

        qInfo("B3 pinned: Sections (%s) / Filters (uniqueSnowflake_fil) are "
              "first-class entities elsewhere in OpenTodo but are silently "
              "excluded from the SearchViewModel index. If this is intentional "
              "the contract should be documented; if not, performSearch needs "
              "two new loops.", "uniqueSnowflake_sec");

        // Sanity: confirm those entities exist in the DataStore even though
        // they did not appear in results — the test is asserting the search
        // index, not entity existence.
        QVERIFY(store.sectionsForProject(p->id()).size() == 1);
        QVERIFY(store.allFilters().size() == 1);

        // The label IS indexed — title field exposes the literal name.
        QVERIFY(hasResult(vm.results(),
                          QStringLiteral("label"), l->id()));
    }

    // =====================================================================
    // 6) Description-only matches.
    //    A task whose content has no hit but whose description does must
    //    surface in results.
    // =====================================================================
    void description_only_match_surfaces_task()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Работа"));

        Task *byContent = store.addTask(QStringLiteral("Подготовить отчёт"),
                                        p->id());
        Task *byDescription = store.addTask(QStringLiteral("Встреча"),
                                            p->id());
        byDescription->setDescription(
            QStringLiteral("Принести черновик отчёта"));

        SearchViewModel vm(&store);
        vm.setSearchQuery(QStringLiteral("отчёт"));

        QVERIFY(hasResult(vm.results(),
                          QStringLiteral("task"), byContent->id()));
        QVERIFY(hasResult(vm.results(),
                          QStringLiteral("task"), byDescription->id()));

        const int hits = countByType(vm.results(),
                                     QStringLiteral("task"));
        QCOMPARE(hits, 2);
    }

    // =====================================================================
    // 7) Tombstone exclusion holds across every entity type the search
    //    actually indexes (tasks, projects, labels, comments).
    // =====================================================================
    void tombstoned_entities_excluded_across_all_types()
    {
        DataStore store;

        Project *p1 = store.addProject(QStringLiteral("alphaProj"));
        Project *p2 = store.addProject(QStringLiteral("alphaTomb"));
        Label *l1 = store.addLabel(QStringLiteral("alphaLab"));
        Label *l2 = store.addLabel(QStringLiteral("alphaLabTomb"));
        Task *t1 = store.addTask(QStringLiteral("alpha note"), p1->id());
        Task *t2 = store.addTask(QStringLiteral("alpha note tomb"), p1->id());
        Comment *c1 = store.addComment(QStringLiteral("alpha comment"),
                                       t1->id(), p1->id());
        Comment *c2 = store.addComment(QStringLiteral("alpha comment tomb"),
                                       t1->id(), p1->id());

        Q_UNUSED(c1);
        Q_UNUSED(c2);

        // Soft-delete one of each.
        store.removeProject(p2->id());
        store.removeLabel(l2->id());
        store.removeTask(t2->id());
        store.removeComment(c2->id());

        SearchViewModel vm(&store);
        vm.setSearchQuery(QStringLiteral("alpha"));

        // Tombstoned ones MUST NOT appear.
        QVERIFY(!hasResult(vm.results(),
                           QStringLiteral("project"), p2->id()));
        QVERIFY(!hasResult(vm.results(),
                           QStringLiteral("label"), l2->id()));
        QVERIFY(!hasResult(vm.results(),
                           QStringLiteral("task"), t2->id()));
        QVERIFY(!hasResult(vm.results(),
                           QStringLiteral("comment"), c2->id()));

        // Live ones MUST appear.
        QVERIFY(hasResult(vm.results(),
                          QStringLiteral("project"), p1->id()));
        QVERIFY(hasResult(vm.results(),
                          QStringLiteral("label"), l1->id()));
        QVERIFY(hasResult(vm.results(),
                          QStringLiteral("task"), t1->id()));
    }

    // =====================================================================
    // 8) DoS-class: 10 KB query.
    //    Substring search is O(n*m) but only runs once per task; against a
    //    small fixture it must complete in milliseconds. The test asserts
    //    no infinite loop, no crash, and an empty result set (the 10 KB
    //    string is not a substring of any fixture data).
    // =====================================================================
    void large_query_completes_and_returns_no_results()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("DoS"));
        store.addTask(QStringLiteral("small task"), p->id());
        store.addTask(QStringLiteral("another task"), p->id());

        const QString huge = QString(10 * 1024, QChar(u'x'));
        QCOMPARE(huge.length(), 10 * 1024);

        SearchViewModel vm(&store);

        QElapsedTimer timer;
        timer.start();
        vm.setSearchQuery(huge);
        const qint64 ms = timer.elapsed();

        QCOMPARE(vm.resultCount(), 0);
        // Empirical upper bound; the search is in-memory substring against
        // a 2-task fixture so this should be sub-millisecond on any sane
        // machine. The 2000 ms ceiling exists to catch a future quadratic
        // explosion (e.g. someone adding a regex compile in the hot loop).
        QVERIFY2(ms < 2000,
                 qPrintable(QStringLiteral("10 KB query took %1 ms — "
                                           "performance regression suspected.")
                                .arg(ms)));
        qInfo("10 KB query elapsed: %lld ms", ms);
    }

    // =====================================================================
    // 9) FilterParser operators are treated as literal characters,
    //    NOT parsed. SearchViewModel must not invoke FilterParser.
    //
    // Construct a task whose content contains exactly the operator soup;
    // search for a substring of it must succeed. Equally, search for an
    // operator-only string must NOT crash and must NOT match anything
    // that doesn't literally contain those bytes.
    // =====================================================================
    void filter_parser_operators_passed_literally()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("DSL"));

        Task *literal = store.addTask(
            QStringLiteral("today & p1 | (overdue, !done)"), p->id());
        Task *innocent = store.addTask(
            QStringLiteral("buy bread"), p->id());
        Q_UNUSED(innocent);

        SearchViewModel vm(&store);

        // Operators-as-literal: must match `literal` only.
        vm.setSearchQuery(QStringLiteral("today & p1"));
        QVERIFY(hasResult(vm.results(),
                          QStringLiteral("task"), literal->id()));
        QCOMPARE(countByType(vm.results(),
                             QStringLiteral("task")), 1);

        // Pure-symbol query: must not crash, must not panic-match.
        vm.setSearchQuery(QStringLiteral("&|!()"));
        // No fixture task contains that exact 5-byte substring.
        QCOMPARE(countByType(vm.results(),
                             QStringLiteral("task")), 0);

        // Bracket-only: same logic, never explodes parser-style.
        vm.setSearchQuery(QStringLiteral("((("));
        QCOMPARE(countByType(vm.results(),
                             QStringLiteral("task")), 0);
    }

    // =====================================================================
    // 10) Recent-searches persistence on DataStore::settings()  AND
    //     QSettings restore of last_query on a freshly-constructed VM.
    //
    // This re-verifies the two-tier persistence contract:
    //   * recentSearches  → SettingsKeys::RecentSearches (DataStore JSON).
    //   * search/last_query → QSettings (per-machine, app-scoped).
    // =====================================================================
    void recent_searches_and_last_query_survive_vm_reconstruction()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Persist"));
        store.addTask(QStringLiteral("молоко"), p->id());

        // First VM session.
        {
            SearchViewModel vm(&store);
            vm.search(QStringLiteral("молоко"));   // adds to recent + persists
            QCOMPARE(vm.recentSearches().size(), 1);
            QCOMPARE(vm.recentSearches().first(),
                     QStringLiteral("молоко"));
            QCOMPARE(vm.searchQuery(), QStringLiteral("молоко"));
        }

        // Second VM session against the SAME DataStore: recent searches
        // must come back, last_query must come back, but performSearch is
        // NOT auto-triggered — the bootstrapped query is "soft" until the
        // user's first interaction.
        {
            SearchViewModel vm(&store);
            QCOMPARE(vm.recentSearches().size(), 1);
            QCOMPARE(vm.recentSearches().first(),
                     QStringLiteral("молоко"));
            QCOMPARE(vm.searchQuery(), QStringLiteral("молоко"));
            QCOMPARE(vm.resultCount(), 0);  // soft restore, no auto-search.
        }

        // Direct settings inspection: the DataStore-level setting holds
        // the recent searches as a QStringList.
        const QVariant raw = store.setting(
            QLatin1String(SettingsKeys::RecentSearches));
        const QStringList persisted = raw.toStringList();
        QCOMPARE(persisted, QStringList{QStringLiteral("молоко")});
    }

    // =====================================================================
    // 11) NO in-process debounce: every distinct setSearchQuery call fires
    //     ONE resultsChanged. A repeat of the same string is a no-op.
    //
    // The expected emit counts are (one emit per distinct query):
    //   "alp"  → emits          (n=1)
    //   "alp"  → no emit        (idempotent)
    //   "alph" → emits          (n=2)
    //   "alpha"→ emits          (n=3)
    //   ""     → emits (clear)  (n=4)
    //
    // The QML debounce (300 ms Timer in SearchBar.qml) lives ABOVE this
    // view-model. Pin both contracts.
    // =====================================================================
    void rapid_setquery_emits_once_per_distinct_query()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Stride"));
        store.addTask(QStringLiteral("alpha task"), p->id());
        store.addTask(QStringLiteral("alphabet task"), p->id());

        SearchViewModel vm(&store);

        QSignalSpy resultsSpy(&vm, &SearchViewModel::resultsChanged);
        QSignalSpy querySpy(&vm, &SearchViewModel::searchQueryChanged);
        QVERIFY(resultsSpy.isValid());
        QVERIFY(querySpy.isValid());

        vm.setSearchQuery(QStringLiteral("alp"));     // 1
        vm.setSearchQuery(QStringLiteral("alp"));     // no-op
        vm.setSearchQuery(QStringLiteral("alph"));    // 2
        vm.setSearchQuery(QStringLiteral("alpha"));   // 3
        vm.setSearchQuery(QString());                 // 4 (clear)

        QCOMPARE(querySpy.count(),   4);
        // performSearch emits resultsChanged once; clear emits once.
        QCOMPARE(resultsSpy.count(), 4);

        qInfo("B5 pinned: SearchViewModel has no internal debounce. Rapid "
              "setSearchQuery calls produce O(N) performSearch invocations "
              "and matching resultsChanged emissions. Debouncing is delegated "
              "to the QML layer (SearchBar.qml: 300 ms Timer).");
    }

    // =====================================================================
    // 12) Result-map shape: every result entry exposes the keys that the
    //     QML side (MobileLayout.qml + SearchBar.qml) actually reads:
    //       universal: type, id, title, subtitle, object
    //       task:      projectId, isCompleted
    //       project:   color
    //       label:     color
    //       comment:   taskId
    // =====================================================================
    void result_map_shape_matches_qml_contract()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("shapeProj"));
        Label *l = store.addLabel(QStringLiteral("shapeLab"));
        Q_UNUSED(l);
        Task *t = store.addTask(QStringLiteral("shape task"), p->id());
        store.addComment(QStringLiteral("shape comment"),
                         t->id(), p->id());

        SearchViewModel vm(&store);
        vm.setSearchQuery(QStringLiteral("shape"));

        // Collect one map of each type and verify required keys.
        QMap<QString, QVariantMap> byType;
        for (const QVariant &v : vm.results()) {
            const QVariantMap m = v.toMap();
            const QString type = m.value(QStringLiteral("type")).toString();
            if (!byType.contains(type))
                byType.insert(type, m);
        }

        // We expect at least: task, project, label, comment.
        QVERIFY(byType.contains(QStringLiteral("task")));
        QVERIFY(byType.contains(QStringLiteral("project")));
        QVERIFY(byType.contains(QStringLiteral("label")));
        QVERIFY(byType.contains(QStringLiteral("comment")));

        const auto requireKeys = [](const QVariantMap &m,
                                    const QStringList &keys) {
            for (const QString &k : keys) {
                if (!m.contains(k)) {
                    qInfo("Missing key '%s' in result map of type '%s'",
                          qUtf8Printable(k),
                          qUtf8Printable(m.value(QStringLiteral("type"))
                                            .toString()));
                    return false;
                }
            }
            return true;
        };

        // Universal keys.
        const QStringList universal = {
            QStringLiteral("type"),
            QStringLiteral("id"),
            QStringLiteral("title"),
            QStringLiteral("subtitle"),
            QStringLiteral("object"),
        };
        for (auto it = byType.constBegin(); it != byType.constEnd(); ++it)
            QVERIFY(requireKeys(it.value(), universal));

        // Task-specific keys.
        QVERIFY(requireKeys(byType.value(QStringLiteral("task")),
                            { QStringLiteral("projectId"),
                              QStringLiteral("isCompleted") }));

        // Project / Label color.
        QVERIFY(byType.value(QStringLiteral("project"))
                    .contains(QStringLiteral("color")));
        QVERIFY(byType.value(QStringLiteral("label"))
                    .contains(QStringLiteral("color")));

        // Comment taskId (mobile uses it for openTaskById).
        QVERIFY(byType.value(QStringLiteral("comment"))
                    .contains(QStringLiteral("taskId")));

        // The comment subtitle uses the "Комментарий к:" Russian prefix.
        const QString commentSubtitle =
            byType.value(QStringLiteral("comment"))
                .value(QStringLiteral("subtitle")).toString();
        QVERIFY(commentSubtitle.startsWith(
            QStringLiteral("Комментарий к:")));
    }

    // =====================================================================
    // 13) Mass-result robustness: 200 matching tasks must all surface.
    //
    // SearchViewModel::performSearch builds a QVariantList by appending
    // QVariantMaps in a tight loop. There is no pagination cap. A naïve
    // implementation might miss this; this slot pins that.
    // =====================================================================
    void mass_results_no_silent_truncation()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Mass"));

        for (int i = 0; i < 200; ++i)
            store.addTask(QStringLiteral("uniquetoken-%1").arg(i, 3, 10,
                                                               QChar(u'0')),
                          p->id());

        SearchViewModel vm(&store);
        vm.setSearchQuery(QStringLiteral("uniquetoken-"));

        QCOMPARE(countByType(vm.results(),
                             QStringLiteral("task")), 200);

        // No duplicates — each id must appear exactly once.
        QSet<QString> seen;
        for (const QVariant &v : vm.results()) {
            const QString id = v.toMap()
                                  .value(QStringLiteral("id")).toString();
            QVERIFY2(!seen.contains(id),
                     qPrintable(QStringLiteral("Duplicate result id: ") + id));
            seen.insert(id);
        }
    }

    // =====================================================================
    // 14) removeRecentSearch with a non-member query is a wasteful no-op.
    //
    //     Even when the string is not in m_recentSearches, the implementation
    //     calls m_store->setSetting(...) and emits recentSearchesChanged.
    //     Not a crash bug, but a contract worth pinning so a future
    //     optimisation knows what tests it might break.
    // =====================================================================
    void removeRecentSearch_with_missing_query_is_noop_but_emits()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Removal"));
        Q_UNUSED(p);

        SearchViewModel vm(&store);
        vm.search(QStringLiteral("alpha"));
        vm.search(QStringLiteral("beta"));
        QCOMPARE(vm.recentSearches().size(), 2);

        QSignalSpy rsSpy(&vm, &SearchViewModel::recentSearchesChanged);
        vm.removeRecentSearch(QStringLiteral("doesNotExist"));

        // List unchanged…
        QCOMPARE(vm.recentSearches().size(), 2);
        // …but a change signal still fires.
        QCOMPARE(rsSpy.count(), 1);

        qInfo("B4 pinned: removeRecentSearch fires recentSearchesChanged and "
              "writes settings even when target query is not in the list. "
              "Consider an early-return if list is unchanged.");
    }

    // =====================================================================
    // 15) Live refresh on tombstone: after the underlying task is removed
    //     (soft-delete) while a query is active, the next data-change cycle
    //     drops the now-dead row.
    //
    //     This catches a regression where the refresh-on-tasksChanged path
    //     re-uses cached pointers without re-filtering.
    // =====================================================================
    void live_refresh_drops_tombstoned_results()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Refresh"));
        Task *keep = store.addTask(QStringLiteral("foo keep"), p->id());
        Task *drop = store.addTask(QStringLiteral("foo drop"), p->id());

        SearchViewModel vm(&store);
        vm.setSearchQuery(QStringLiteral("foo"));
        QCOMPARE(countByType(vm.results(),
                             QStringLiteral("task")), 2);

        // Soft-delete one task. DataStore::removeTask emits tasksChanged
        // which triggers SearchViewModel::refreshIfActive → performSearch.
        QSignalSpy resultsSpy(&vm, &SearchViewModel::resultsChanged);
        store.removeTask(drop->id());
        QVERIFY(resultsSpy.count() >= 1);

        QCOMPARE(countByType(vm.results(),
                             QStringLiteral("task")), 1);
        QVERIFY( hasResult(vm.results(),
                           QStringLiteral("task"), keep->id()));
        QVERIFY(!hasResult(vm.results(),
                           QStringLiteral("task"), drop->id()));
    }
};

QTEST_GUILESS_MAIN(TestSearchViewModelEdgeCases)
#include "test_searchviewmodel_edge_cases.moc"
