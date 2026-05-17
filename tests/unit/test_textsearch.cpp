#include <QtTest>
#include <QString>
#include <QChar>

#include "utils/textsearch.h"
#include "utils/settingskeys.h"
#include "storage/datastore.h"
#include "model/task.h"
#include "model/project.h"
#include "viewmodels/searchviewmodel.h"
#include "filter/filtereval.h"

// ============================================================================
// Q2.26-B: TextSearch normalization + integration with SearchViewModel +
//          FilterEval `search:` predicate.
//
// Spec acceptance checklist:
//   - "café" finds "café" (NFC + NFD on either side normalize to the same
//     NFKC form, then combining acute is stripped when stripDiacritics=true
//     so even pre-Unicode-aware queries succeed).
//   - "Москва" finds "москва" (case-insensitive via Unicode case fold).
//   - "ё" finds "е" ONLY when accent-insensitive is on (the diacritic on Ё
//     decomposes to a combining diaeresis U+0308 which we drop).
//   - "fiance" finds "ﬁancé" — NFKC folds the LATIN SMALL LIGATURE FI
//     (U+FB01) into "fi", and accent strip drops the acute on é.
//   - On/off switch via SettingsKeys::SearchAccentSensitive.
// ============================================================================

namespace {

// Helper: scan a QVariantList of {type,id,...} result maps for a hit.
static bool hasTaskResult(const QVariantList &results, const QString &taskId)
{
    for (const QVariant &v : results) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("type")).toString() == QLatin1String("task")
            && m.value(QStringLiteral("id")).toString() == taskId)
            return true;
    }
    return false;
}

} // anonymous namespace


class TestTextSearch : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(
            QStringLiteral("OpenTodoTest_TextSearch"));
        QCoreApplication::setApplicationName(
            QStringLiteral("test_textsearch"));
    }

    // -----------------------------------------------------------------------
    // Pure normalization helpers — direct unit tests on TextSearch.
    // -----------------------------------------------------------------------

    void normalize_empty_returns_empty()
    {
        QCOMPARE(TextSearch::normalize(QString()), QString());
        QCOMPARE(TextSearch::normalize(QString(), false), QString());
    }

    void normalize_nfc_and_nfd_cafe_collapse_to_same_form()
    {
        // Precomposed: é = U+00E9
        const QString nfc = QString::fromUtf8("caf\xC3\xA9");
        // Decomposed: e + U+0301 combining acute
        const QString nfd = QStringLiteral("cafe") + QChar(0x0301);

        const QString aNoStrip = TextSearch::normalize(nfc, false);
        const QString bNoStrip = TextSearch::normalize(nfd, false);
        QCOMPARE(aNoStrip, bNoStrip);
        QVERIFY(!aNoStrip.isEmpty());

        // With diacritics stripped, both collapse to plain "cafe".
        QCOMPARE(TextSearch::normalize(nfc, true), QStringLiteral("cafe"));
        QCOMPARE(TextSearch::normalize(nfd, true), QStringLiteral("cafe"));
    }

    void normalize_case_folds_to_lowercase()
    {
        QCOMPARE(TextSearch::normalize(QStringLiteral("Hello WORLD"), false),
                 QStringLiteral("hello world"));
        QCOMPARE(TextSearch::normalize(QStringLiteral("Москва"), false),
                 QStringLiteral("москва"));
    }

    void normalize_nfkc_folds_ligature_fi()
    {
        // ﬁ = U+FB01 LATIN SMALL LIGATURE FI
        const QString lig = QStringLiteral("ﬁance");
        QCOMPARE(TextSearch::normalize(lig, true), QStringLiteral("fiance"));
        QCOMPARE(TextSearch::normalize(lig, false), QStringLiteral("fiance"));
    }

    void normalize_strips_combining_diaeresis_from_yo()
    {
        // Ё = U+0401 decomposes to Е (U+0415) + ◌̈ (U+0308).
        const QString yoUpper = QString(QChar(0x0401));
        const QString yoLower = QString(QChar(0x0451));

        // With stripping: both collapse to plain е.
        QCOMPARE(TextSearch::normalize(yoUpper, true), QStringLiteral("е"));
        QCOMPARE(TextSearch::normalize(yoLower, true), QStringLiteral("е"));

        // Without stripping: Ё/ё are preserved (NFKC keeps them composed).
        const QString preservedUpper = TextSearch::normalize(yoUpper, false);
        const QString preservedLower = TextSearch::normalize(yoLower, false);
        QCOMPARE(preservedUpper, preservedLower); // case-fold to same form
        QVERIFY(preservedUpper != QStringLiteral("е")); // diacritic kept
    }

    void contains_empty_needle_is_true()
    {
        QVERIFY(TextSearch::contains(QStringLiteral("anything"), QString()));
    }

    void contains_cafe_matches_cafe_nfc_nfd_and_ascii_under_default()
    {
        const QString needleNfc   = QString::fromUtf8("caf\xC3\xA9");
        const QString needleNfd   = QStringLiteral("cafe") + QChar(0x0301);
        const QString needleAscii = QStringLiteral("cafe");

        const QString hayNfc = QString::fromUtf8("My Caf\xC3\xA9 trip");
        const QString hayNfd = QStringLiteral("My Cafe") + QChar(0x0301) +
                               QStringLiteral(" trip");

        // Default: stripDiacritics=true.
        QVERIFY(TextSearch::contains(hayNfc, needleNfc));
        QVERIFY(TextSearch::contains(hayNfc, needleNfd));
        QVERIFY(TextSearch::contains(hayNfc, needleAscii));
        QVERIFY(TextSearch::contains(hayNfd, needleNfc));
        QVERIFY(TextSearch::contains(hayNfd, needleNfd));
        QVERIFY(TextSearch::contains(hayNfd, needleAscii));

        // Accent-sensitive: ASCII "cafe" does NOT match composed "café".
        QVERIFY(TextSearch::contains(hayNfc, needleNfc, /*strip=*/false));
        QVERIFY(TextSearch::contains(hayNfc, needleNfd, /*strip=*/false));
        QVERIFY(!TextSearch::contains(hayNfc, needleAscii, /*strip=*/false));
    }

    void contains_fiance_matches_ligated_fianc_with_acute()
    {
        // Task content uses ligature ﬁ + composed é
        const QString hay = QString::fromUtf8("\xEF\xAC\x81") + // ﬁ
                            QString::fromUtf8("anc\xC3\xA9");   // ancé

        // Query "fiance" must match under default (stripDiacritics=true).
        QVERIFY(TextSearch::contains(hay, QStringLiteral("fiance")));
        // Accent-sensitive: "fiance" is NOT a substring of "fiancé" because
        // the trailing 'e' in the needle does not equal U+00E9 (composed é)
        // in the haystack. QString::contains is exact-substring, not prefix-
        // tolerant — same contract as the analogous "cafe" vs "café" pin in
        // contains_cafe_matches_cafe_nfc_nfd_and_ascii_under_default above.
        QVERIFY(!TextSearch::contains(hay, QStringLiteral("fiance"),
                                      /*strip=*/false));
        // Query with the acute "fiancé" still matches under both modes.
        const QString withAcute = QString::fromUtf8("fianc\xC3\xA9");
        QVERIFY(TextSearch::contains(hay, withAcute));
        QVERIFY(TextSearch::contains(hay, withAcute, /*strip=*/false));
    }

    void contains_moscow_case_insensitive_cyrillic()
    {
        QVERIFY(TextSearch::contains(QStringLiteral("Я живу в Москве"),
                                     QStringLiteral("москва")) == false);
        // ^ the haystack form is "Москве" (locative), not "Москва", so a
        // substring "москва" does NOT appear. Use the actual substring:
        QVERIFY(TextSearch::contains(QStringLiteral("город Москва — столица"),
                                     QStringLiteral("москва")));
        QVERIFY(TextSearch::contains(QStringLiteral("город Москва — столица"),
                                     QStringLiteral("МОСКВА")));
    }

    void contains_yo_matches_e_only_when_accent_insensitive()
    {
        // Haystack contains "ёлка"; needle is plain "е..."
        const QString hay = QStringLiteral("Купить ёлку");
        QVERIFY(TextSearch::contains(hay, QStringLiteral("ёлк"))); // default
        QVERIFY(TextSearch::contains(hay, QStringLiteral("елк"))); // default strips diaeresis

        // Accent-sensitive: must NOT cross the ё/е boundary.
        QVERIFY(TextSearch::contains(hay, QStringLiteral("ёлк"),
                                     /*strip=*/false));
        QVERIFY(!TextSearch::contains(hay, QStringLiteral("елк"),
                                      /*strip=*/false));
    }

    void contains_pure_diacritic_query_collapses_to_empty()
    {
        // A query consisting solely of combining marks normalizes to "" under
        // stripDiacritics=true → matches everything (empty-needle semantics).
        const QString combiningOnly = QString(QChar(0x0301)) +
                                      QString(QChar(0x0308));
        QVERIFY(TextSearch::contains(QStringLiteral("anything"), combiningOnly));
        QCOMPARE(TextSearch::normalize(combiningOnly, true), QString());
    }

    // -----------------------------------------------------------------------
    // Integration with SearchViewModel: full QML-facing path.
    // -----------------------------------------------------------------------

    void searchviewmodel_cafe_finds_nfc_nfd_and_ascii()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Trips"));

        Task *nfc = store.addTask(QString::fromUtf8("Caf\xC3\xA9 Paris"),
                                  p->id());
        Task *nfd = store.addTask(
            QStringLiteral("Cafe") + QChar(0x0301) + QStringLiteral(" Lyon"),
            p->id());
        Task *unrelated = store.addTask(QStringLiteral("Boulangerie Marseille"),
                                        p->id());
        Q_UNUSED(unrelated);

        SearchViewModel vm(&store);

        // ASCII query under default — finds both
        vm.setSearchQuery(QStringLiteral("cafe"));
        QVERIFY(hasTaskResult(vm.results(), nfc->id()));
        QVERIFY(hasTaskResult(vm.results(), nfd->id()));
        QVERIFY(!hasTaskResult(vm.results(), unrelated->id()));

        // Composed-é query
        vm.setSearchQuery(QString::fromUtf8("caf\xC3\xA9"));
        QVERIFY(hasTaskResult(vm.results(), nfc->id()));
        QVERIFY(hasTaskResult(vm.results(), nfd->id()));

        // Decomposed-é query
        vm.setSearchQuery(QStringLiteral("cafe") + QChar(0x0301));
        QVERIFY(hasTaskResult(vm.results(), nfc->id()));
        QVERIFY(hasTaskResult(vm.results(), nfd->id()));
    }

    void searchviewmodel_moskva_case_insensitive()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Путешествия"));

        Task *t = store.addTask(QStringLiteral("Билеты Москва — Питер"),
                                p->id());
        SearchViewModel vm(&store);

        vm.setSearchQuery(QStringLiteral("москва"));
        QVERIFY(hasTaskResult(vm.results(), t->id()));

        vm.setSearchQuery(QStringLiteral("МОСКВА"));
        QVERIFY(hasTaskResult(vm.results(), t->id()));

        vm.setSearchQuery(QStringLiteral("Москва"));
        QVERIFY(hasTaskResult(vm.results(), t->id()));
    }

    void searchviewmodel_fiance_matches_ligature_and_diacritic()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Personal"));

        // Task content uses ligature ﬁ + composed é
        Task *t = store.addTask(QString::fromUtf8("\xEF\xAC\x81") +
                                QString::fromUtf8("anc\xC3\xA9 list"),
                                p->id());
        SearchViewModel vm(&store);

        vm.setSearchQuery(QStringLiteral("fiance"));
        QVERIFY(hasTaskResult(vm.results(), t->id()));

        vm.setSearchQuery(QString::fromUtf8("fianc\xC3\xA9"));
        QVERIFY(hasTaskResult(vm.results(), t->id()));
    }

    void searchviewmodel_accent_sensitive_keeps_strict_match()
    {
        DataStore store;
        store.setSetting(
            QLatin1String(SettingsKeys::SearchAccentSensitive), true);

        Project *p = store.addProject(QStringLiteral("Trips"));
        Task *withAccent = store.addTask(
            QString::fromUtf8("Caf\xC3\xA9 Paris"), p->id());
        Q_UNUSED(withAccent);

        SearchViewModel vm(&store);

        // ASCII "cafe" must NOT match "café" in sensitive mode.
        vm.setSearchQuery(QStringLiteral("cafe"));
        QVERIFY(!hasTaskResult(vm.results(), withAccent->id()));

        // Composed-é still matches.
        vm.setSearchQuery(QString::fromUtf8("caf\xC3\xA9"));
        QVERIFY(hasTaskResult(vm.results(), withAccent->id()));
    }

    void searchviewmodel_setting_toggles_live_results()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Trips"));
        Task *t = store.addTask(QString::fromUtf8("Caf\xC3\xA9 trip"),
                                p->id());
        SearchViewModel vm(&store);

        // Default insensitive: "cafe" matches.
        vm.setSearchQuery(QStringLiteral("cafe"));
        QVERIFY(hasTaskResult(vm.results(), t->id()));

        // Flip to sensitive — same query no longer matches when re-run.
        store.setSetting(
            QLatin1String(SettingsKeys::SearchAccentSensitive), true);
        vm.setSearchQuery(QString()); // clear
        vm.setSearchQuery(QStringLiteral("cafe"));
        QVERIFY(!hasTaskResult(vm.results(), t->id()));

        // Flip back.
        store.setSetting(
            QLatin1String(SettingsKeys::SearchAccentSensitive), false);
        vm.setSearchQuery(QString());
        vm.setSearchQuery(QStringLiteral("cafe"));
        QVERIFY(hasTaskResult(vm.results(), t->id()));
    }

    // -----------------------------------------------------------------------
    // Integration with FilterEvaluator `search:` predicate.
    // -----------------------------------------------------------------------

    void filtereval_search_predicate_uses_textsearch()
    {
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Trips"));

        Task *nfc = store.addTask(QString::fromUtf8("Caf\xC3\xA9 Paris"),
                                  p->id());
        Task *nfd = store.addTask(
            QStringLiteral("Cafe") + QChar(0x0301) + QStringLiteral(" Lyon"),
            p->id());
        Task *unrelated = store.addTask(QStringLiteral("Boulangerie"),
                                        p->id());
        Q_UNUSED(unrelated);

        FilterEvaluator eval(&store);
        const auto matches = eval.evaluate(QStringLiteral("search: cafe"));

        QSet<QString> ids;
        for (auto *t : matches) ids.insert(t->id());

        QVERIFY(ids.contains(nfc->id()));
        QVERIFY(ids.contains(nfd->id()));
        QVERIFY(!ids.contains(unrelated->id()));
    }

    void filtereval_search_predicate_respects_accent_sensitive_setting()
    {
        DataStore store;
        store.setSetting(
            QLatin1String(SettingsKeys::SearchAccentSensitive), true);

        Project *p = store.addProject(QStringLiteral("Trips"));
        Task *t = store.addTask(QString::fromUtf8("Caf\xC3\xA9 visit"),
                                p->id());

        FilterEvaluator eval(&store);

        // Accent-sensitive: "cafe" misses, "café" hits.
        QSet<QString> ascii;
        for (auto *x : eval.evaluate(QStringLiteral("search: cafe")))
            ascii.insert(x->id());
        QVERIFY(!ascii.contains(t->id()));

        QSet<QString> composed;
        for (auto *x : eval.evaluate(
                 QString::fromUtf8("search: caf\xC3\xA9")))
            composed.insert(x->id());
        QVERIFY(composed.contains(t->id()));
    }

    void filtereval_identifier_fallback_also_normalized()
    {
        // Bare identifier (no `search:` prefix) falls through to content
        // substring match — it should get the same TextSearch treatment.
        DataStore store;
        Project *p = store.addProject(QStringLiteral("Trips"));
        Task *t = store.addTask(QString::fromUtf8("Caf\xC3\xA9 visit"),
                                p->id());

        FilterEvaluator eval(&store);
        QSet<QString> ids;
        for (auto *x : eval.evaluate(QStringLiteral("cafe")))
            ids.insert(x->id());
        QVERIFY(ids.contains(t->id()));
    }
};

QTEST_GUILESS_MAIN(TestTextSearch)
#include "test_textsearch.moc"
