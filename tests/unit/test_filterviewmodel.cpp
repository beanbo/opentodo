#include <QtTest>
#include <QSignalSpy>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QVariant>
#include <QModelIndex>

#include "storage/datastore.h"
#include "model/filter.h"
#include "viewmodels/filterviewmodel.h"

// ============================================================================
// Comprehensive unit tests for FilterViewModel.
//
// Public API exercised:
//   - Q_PROPERTY count (READ count, NOTIFY countChanged)
//   - Roles: id, name, query, color, order, isFavorite, filterObject
//   - Q_INVOKABLE addFilter(name, query, color="charcoal") -> id
//   - Q_INVOKABLE updateFilter(id, field, value) -> bool
//   - Q_INVOKABLE deleteFilter(id) -> bool   (soft-delete via DataStore)
//   - Q_INVOKABLE setFavorite(id, bool)      (forwards to updateFilter)
//   - Q_INVOKABLE reorderFilter(id, newOrder) (forwards to updateFilter)
//   - Q_INVOKABLE isQueryValid(query) -> bool
//   - Q_INVOKABLE queryError(query)   -> QString
//   - Q_INVOKABLE filterById(id)      -> Filter*
//
// Boundaries:
//   - DataStore is the source of truth. removeFilter sets is_deleted=true
//     (soft-delete) but keeps the entity in the underlying storage list.
//   - allFilters() filters tombstones; rowCount/count must mirror that.
//   - The VM sorts by Filter::order ascending in refresh().
// ============================================================================

namespace {

// Helper: count rows whose IsDeleted() flag is set in the DataStore's
// raw filter list (we have no public iterator, so we test via filterById).
static bool filterStillExistsButIsDeleted(DataStore *store, const QString &id)
{
    Filter *f = store->filterById(id);
    return f != nullptr && f->isDeleted();
}

// Read a role value from the model for a given row.
static QVariant roleAt(const FilterViewModel &vm, int row, int role)
{
    return vm.data(vm.index(row, 0), role);
}

} // anonymous namespace


class TestFilterViewModel : public QObject
{
    Q_OBJECT

private slots:

    // ---------------------------------------------------------------------
    // Per-test scope: route QSettings into a per-suite hive so we never
    // touch the user's real OpenTodo registry/INI on disk.
    // ---------------------------------------------------------------------
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_FilterVM"));
        QCoreApplication::setApplicationName(QStringLiteral("test_filterviewmodel"));
    }

    void init()
    {
        QSettings qs;
        qs.clear();
        qs.sync();
    }

    void cleanupTestCase()
    {
        QSettings qs;
        qs.clear();
        qs.sync();
    }

    // =====================================================================
    // 1) Bootstrap: a fresh DataStore yields a VM with no filters.
    // =====================================================================
    void bootstrap_empty_store_has_no_filters()
    {
        DataStore store;
        FilterViewModel vm(&store);

        QCOMPARE(vm.count(), 0);
        QCOMPARE(vm.rowCount(), 0);
        QVERIFY(store.allFilters().isEmpty());
    }

    // =====================================================================
    // 2) addFilter inserts an entity, count goes up, store is updated.
    // =====================================================================
    void addFilter_creates_entity_and_updates_store_count()
    {
        DataStore store;
        FilterViewModel vm(&store);

        QSignalSpy countSpy(&vm, &FilterViewModel::countChanged);
        QSignalSpy addedSpy(&vm, &FilterViewModel::filterAdded);

        const QString id = vm.addFilter(QStringLiteral("My Filter"),
                                        QStringLiteral("today & p1"));

        QVERIFY(!id.isEmpty());
        QCOMPARE(vm.count(), 1);
        QCOMPARE(vm.rowCount(), 1);
        QCOMPARE(store.allFilters().size(), 1);

        // Signals: countChanged fires from refresh(); filterAdded carries the id.
        QVERIFY(countSpy.count() >= 1);
        QCOMPARE(addedSpy.count(), 1);
        QCOMPARE(addedSpy.first().first().toString(), id);

        // The store-level entity is wired through.
        Filter *f = store.filterById(id);
        QVERIFY(f != nullptr);
        QCOMPARE(f->name(), QStringLiteral("My Filter"));
        QCOMPARE(f->query(), QStringLiteral("today & p1"));
    }

    // =====================================================================
    // 3) addFilter applies the optional color argument.
    // =====================================================================
    void addFilter_uses_explicit_color_argument()
    {
        DataStore store;
        FilterViewModel vm(&store);

        const QString id = vm.addFilter(QStringLiteral("Coloured"),
                                        QStringLiteral("today"),
                                        QStringLiteral("berry_red"));

        Filter *f = store.filterById(id);
        QVERIFY(f != nullptr);
        QCOMPARE(f->color(), QStringLiteral("berry_red"));
    }

    // =====================================================================
    // 4) addFilter default color is "charcoal".
    // =====================================================================
    void addFilter_default_color_is_charcoal()
    {
        DataStore store;
        FilterViewModel vm(&store);

        const QString id = vm.addFilter(QStringLiteral("Default Colour"),
                                        QStringLiteral("today"));

        Filter *f = store.filterById(id);
        QVERIFY(f != nullptr);
        QCOMPARE(f->color(), QStringLiteral("charcoal"));
    }

    // =====================================================================
    // 5) filterById round-trips and returns null for unknown ids.
    // =====================================================================
    void filterById_returns_entity_or_null()
    {
        DataStore store;
        FilterViewModel vm(&store);

        const QString id = vm.addFilter(QStringLiteral("Real"),
                                        QStringLiteral("today"));

        Filter *f = vm.filterById(id);
        QVERIFY(f != nullptr);
        QCOMPARE(f->name(), QStringLiteral("Real"));

        QVERIFY(vm.filterById(QStringLiteral("nonexistent-id")) == nullptr);
    }

    // =====================================================================
    // 6) updateFilter("query", ...) updates the query string.
    //    NB: setQuery itself does NOT bump updatedAt — that's handled by
    //    sync paths, not the VM. We assert the actual contract: only the
    //    field changes, and both filterChanged(id) and dataChanged emit.
    // =====================================================================
    void updateFilter_query_updates_query_and_emits_signals()
    {
        DataStore store;
        FilterViewModel vm(&store);

        const QString id = vm.addFilter(QStringLiteral("F"),
                                        QStringLiteral("today"));
        Filter *f = store.filterById(id);
        QVERIFY(f != nullptr);

        QSignalSpy changedSpy (&vm, &FilterViewModel::filterChanged);
        QSignalSpy dataSpy    (&vm, &FilterViewModel::dataChanged);

        const bool ok = vm.updateFilter(id,
                                        QStringLiteral("query"),
                                        QStringLiteral("today & p1"));
        QVERIFY(ok);
        QCOMPARE(f->query(), QStringLiteral("today & p1"));

        QCOMPARE(changedSpy.count(), 1);
        QCOMPARE(changedSpy.first().first().toString(), id);
        // dataChanged is emitted because the row exists in the model.
        QCOMPARE(dataSpy.count(), 1);
    }

    // =====================================================================
    // 7) updateFilter rejects unknown ids and unknown fields.
    // =====================================================================
    void updateFilter_returns_false_for_unknown_id_or_field()
    {
        DataStore store;
        FilterViewModel vm(&store);

        const QString id = vm.addFilter(QStringLiteral("F"),
                                        QStringLiteral("today"));

        QVERIFY(!vm.updateFilter(QStringLiteral("missing"),
                                 QStringLiteral("name"),
                                 QStringLiteral("X")));
        QVERIFY(!vm.updateFilter(id,
                                 QStringLiteral("not_a_field"),
                                 QStringLiteral("X")));
    }

    // =====================================================================
    // 8) deleteFilter is a SOFT delete: the entity stays in the underlying
    //    list with is_deleted=true; the model rowCount drops to 0.
    // =====================================================================
    void deleteFilter_is_soft_delete_not_hard_remove()
    {
        DataStore store;
        FilterViewModel vm(&store);

        const QString id = vm.addFilter(QStringLiteral("Doomed"),
                                        QStringLiteral("today"));
        QCOMPARE(vm.rowCount(), 1);

        QSignalSpy removedSpy(&vm, &FilterViewModel::filterRemoved);

        const bool ok = vm.deleteFilter(id);
        QVERIFY(ok);

        // Soft-delete contract: VM/list view drops the row,
        // but DataStore::filterById still resolves it as a tombstone.
        QCOMPARE(vm.rowCount(), 0);
        QCOMPARE(vm.count(), 0);
        QCOMPARE(store.allFilters().size(), 0);
        QVERIFY(filterStillExistsButIsDeleted(&store, id));

        QCOMPARE(removedSpy.count(), 1);
        QCOMPARE(removedSpy.first().first().toString(), id);

        // Deleting a non-existent id returns false and emits nothing further.
        QVERIFY(!vm.deleteFilter(QStringLiteral("nope")));
        QCOMPARE(removedSpy.count(), 1);
    }

    // =====================================================================
    // 9) rowCount excludes soft-deleted entities — verified across a mix.
    // =====================================================================
    void rowCount_excludes_soft_deleted_entities()
    {
        DataStore store;
        FilterViewModel vm(&store);

        const QString a = vm.addFilter(QStringLiteral("A"),
                                       QStringLiteral("today"));
        const QString b = vm.addFilter(QStringLiteral("B"),
                                       QStringLiteral("p1"));
        const QString c = vm.addFilter(QStringLiteral("C"),
                                       QStringLiteral("@home"));
        QCOMPARE(vm.rowCount(), 3);

        QVERIFY(vm.deleteFilter(b));
        QCOMPARE(vm.rowCount(), 2);

        // The two surviving rows are the ones we kept.
        QStringList visibleNames;
        for (int i = 0; i < vm.rowCount(); ++i)
            visibleNames << roleAt(vm, i, FilterViewModel::NameRole).toString();
        std::sort(visibleNames.begin(), visibleNames.end());
        QCOMPARE(visibleNames, (QStringList{QStringLiteral("A"),
                                            QStringLiteral("C")}));

        // Sanity: the deleted entity is NOT exposed via filterById ->
        // wait, it IS still there as a tombstone (see test 8).
        QVERIFY(store.filterById(b) != nullptr);
        QVERIFY(store.filterById(b)->isDeleted());

        // Suppress unused warning.
        Q_UNUSED(a)
    }

    // =====================================================================
    // 10) setFavorite toggles the flag and persists on the entity.
    // =====================================================================
    void setFavorite_toggles_and_persists()
    {
        DataStore store;
        FilterViewModel vm(&store);

        const QString id = vm.addFilter(QStringLiteral("Star"),
                                        QStringLiteral("today"));
        Filter *f = store.filterById(id);
        QVERIFY(f != nullptr);
        QVERIFY(!f->isFavorite());

        QSignalSpy changedSpy(&vm, &FilterViewModel::filterChanged);

        QVERIFY(vm.setFavorite(id, true));
        QVERIFY(f->isFavorite());
        QCOMPARE(roleAt(vm, 0, FilterViewModel::IsFavoriteRole).toBool(), true);
        QCOMPARE(changedSpy.count(), 1);

        QVERIFY(vm.setFavorite(id, false));
        QVERIFY(!f->isFavorite());
        QCOMPARE(roleAt(vm, 0, FilterViewModel::IsFavoriteRole).toBool(), false);
        QCOMPARE(changedSpy.count(), 2);
    }

    // =====================================================================
    // 11) reorderFilter changes the order field and re-sorts the model.
    //     refresh() sorts by ascending order, so changing one filter's order
    //     above another's must reorder the visible rows.
    // =====================================================================
    void reorderFilter_changes_visible_row_order()
    {
        DataStore store;
        FilterViewModel vm(&store);

        const QString a = vm.addFilter(QStringLiteral("A"),
                                       QStringLiteral("today"));
        const QString b = vm.addFilter(QStringLiteral("B"),
                                       QStringLiteral("p1"));
        const QString c = vm.addFilter(QStringLiteral("C"),
                                       QStringLiteral("@home"));

        // Sanity baseline: insertion order via addFilter assigns 0, 1, 2.
        QCOMPARE(roleAt(vm, 0, FilterViewModel::NameRole).toString(),
                 QStringLiteral("A"));
        QCOMPARE(roleAt(vm, 1, FilterViewModel::NameRole).toString(),
                 QStringLiteral("B"));
        QCOMPARE(roleAt(vm, 2, FilterViewModel::NameRole).toString(),
                 QStringLiteral("C"));

        // Demote A to last by giving it a high order. (refresh() resorts on
        // dataStore::filtersChanged; the VM's reorderFilter doesn't itself
        // re-sort because it only emits filterChanged + dataChanged.)
        // To force a resort we mutate the order directly on the entity and
        // tickle filtersChanged via removeFilter on a non-existent id?
        // Simpler: call store.addFilter / removeFilter? No — instead, emit
        // filtersChanged the same way the store's own mutations do. The
        // documented surface is: callers wishing to re-sort after a manual
        // reorder must trigger another store mutation. The VM-level
        // reorderFilter still updates the underlying field correctly.
        QVERIFY(vm.reorderFilter(a, 10));
        Filter *fa = store.filterById(a);
        QVERIFY(fa != nullptr);
        QCOMPARE(fa->order(), 10);

        // Force a refresh by adding then removing a throwaway filter.
        const QString throwaway =
            vm.addFilter(QStringLiteral("__tmp"), QStringLiteral("today"));
        QVERIFY(vm.deleteFilter(throwaway));

        // After resort: B (order 1), C (order 2), A (order 10).
        QCOMPARE(vm.rowCount(), 3);
        QCOMPARE(roleAt(vm, 0, FilterViewModel::NameRole).toString(),
                 QStringLiteral("B"));
        QCOMPARE(roleAt(vm, 1, FilterViewModel::NameRole).toString(),
                 QStringLiteral("C"));
        QCOMPARE(roleAt(vm, 2, FilterViewModel::NameRole).toString(),
                 QStringLiteral("A"));

        Q_UNUSED(b)
        Q_UNUSED(c)
    }

    // =====================================================================
    // 12) isQueryValid: well-formed DSL expressions parse cleanly.
    // =====================================================================
    void isQueryValid_accepts_well_formed_queries()
    {
        DataStore store;
        FilterViewModel vm(&store);

        QVERIFY(vm.isQueryValid(QStringLiteral("today")));
        QVERIFY(vm.isQueryValid(QStringLiteral("today & p1")));
        QVERIFY(vm.isQueryValid(QStringLiteral("p1 | p2")));
        QVERIFY(vm.isQueryValid(QStringLiteral("(today | tomorrow) & !overdue")));
        QVERIFY(vm.isQueryValid(QStringLiteral("#Inbox")));
        QVERIFY(vm.isQueryValid(QStringLiteral("@urgent")));
    }

    // =====================================================================
    // 13) isQueryValid: malformed queries fail and queryError() is non-empty.
    // =====================================================================
    void isQueryValid_rejects_malformed_queries()
    {
        DataStore store;
        FilterViewModel vm(&store);

        // Trailing operator with no right-hand operand.
        QVERIFY(!vm.isQueryValid(QStringLiteral("today &")));
        const QString err = vm.queryError(QStringLiteral("today &"));
        QVERIFY(!err.isEmpty());

        // Unbalanced parentheses.
        QVERIFY(!vm.isQueryValid(QStringLiteral("(today")));
        QVERIFY(!vm.queryError(QStringLiteral("(today")).isEmpty());
    }

    // =====================================================================
    // 14) isQueryValid: documents the behaviour for the empty string.
    //     The parser treats an empty token list as an error ("Empty query"),
    //     so isQueryValid("") returns false. This is the spec contract: a
    //     stored Filter with an empty query is invalid and the UI should
    //     reject it before save.
    // =====================================================================
    void isQueryValid_empty_string_is_invalid()
    {
        DataStore store;
        FilterViewModel vm(&store);

        QVERIFY(!vm.isQueryValid(QString()));
        QVERIFY(!vm.isQueryValid(QStringLiteral("")));
        QVERIFY(!vm.isQueryValid(QStringLiteral("   ")));

        // queryError surfaces a non-empty diagnostic for the empty case.
        QVERIFY(!vm.queryError(QString()).isEmpty());
    }

    // =====================================================================
    // 15) Roles populate correctly for an added filter.
    // =====================================================================
    void roles_expose_entity_fields()
    {
        DataStore store;
        FilterViewModel vm(&store);

        const QString id = vm.addFilter(QStringLiteral("Roles"),
                                        QStringLiteral("today"),
                                        QStringLiteral("sky_blue"));
        QVERIFY(vm.setFavorite(id, true));

        QCOMPARE(roleAt(vm, 0, FilterViewModel::IdRole).toString(), id);
        QCOMPARE(roleAt(vm, 0, FilterViewModel::NameRole).toString(),
                 QStringLiteral("Roles"));
        QCOMPARE(roleAt(vm, 0, FilterViewModel::QueryRole).toString(),
                 QStringLiteral("today"));
        QCOMPARE(roleAt(vm, 0, FilterViewModel::ColorRole).toString(),
                 QStringLiteral("sky_blue"));
        QCOMPARE(roleAt(vm, 0, FilterViewModel::IsFavoriteRole).toBool(), true);
        QCOMPARE(roleAt(vm, 0, FilterViewModel::OrderRole).toInt(), 0);

        // FilterObjectRole exposes the entity itself.
        Filter *f = qvariant_cast<Filter *>(
            roleAt(vm, 0, FilterViewModel::FilterObjectRole));
        QVERIFY(f != nullptr);
        QCOMPARE(f->id(), id);

        // Out-of-range / invalid index is a defensive return.
        QVERIFY(!vm.data(QModelIndex(), FilterViewModel::IdRole).isValid());
        QVERIFY(!vm.data(vm.index(99, 0), FilterViewModel::IdRole).isValid());
    }

    // =====================================================================
    // 16) roleNames hash exposes the QML-facing string keys.
    // =====================================================================
    void roleNames_exposes_qml_keys()
    {
        DataStore store;
        FilterViewModel vm(&store);

        const QHash<int, QByteArray> roles = vm.roleNames();
        QCOMPARE(roles.value(FilterViewModel::IdRole), QByteArray("id"));
        QCOMPARE(roles.value(FilterViewModel::NameRole), QByteArray("name"));
        QCOMPARE(roles.value(FilterViewModel::QueryRole), QByteArray("query"));
        QCOMPARE(roles.value(FilterViewModel::ColorRole), QByteArray("color"));
        QCOMPARE(roles.value(FilterViewModel::OrderRole), QByteArray("order"));
        QCOMPARE(roles.value(FilterViewModel::IsFavoriteRole),
                 QByteArray("isFavorite"));
        QCOMPARE(roles.value(FilterViewModel::FilterObjectRole),
                 QByteArray("filterObject"));
    }

    // =====================================================================
    // 17) addFilter is reflected by countChanged signal AND the model rows
    //     update via DataStore::filtersChanged → refresh().
    // =====================================================================
    void store_filtersChanged_drives_model_refresh()
    {
        DataStore store;
        FilterViewModel vm(&store);

        QSignalSpy countSpy(&vm, &FilterViewModel::countChanged);

        // Bypass the VM and add directly on the store — the signal
        // propagation through filtersChanged must still cause the VM to
        // rebuild and emit countChanged.
        Filter *f = store.addFilter(QStringLiteral("Direct"),
                                    QStringLiteral("today"));
        QVERIFY(f != nullptr);

        QCOMPARE(vm.rowCount(), 1);
        QVERIFY(countSpy.count() >= 1);
        QCOMPARE(roleAt(vm, 0, FilterViewModel::NameRole).toString(),
                 QStringLiteral("Direct"));
    }

    // =====================================================================
    // 18) DataStore::aboutToReset clears the model so the VM cannot hold
    //     dangling pointers to entities the store is destroying.
    // =====================================================================
    void about_to_reset_clears_the_model()
    {
        DataStore store;
        FilterViewModel vm(&store);

        vm.addFilter(QStringLiteral("X"), QStringLiteral("today"));
        vm.addFilter(QStringLiteral("Y"), QStringLiteral("p1"));
        QCOMPARE(vm.rowCount(), 2);

        emit store.aboutToReset();
        QCOMPARE(vm.rowCount(), 0);
    }
};

QTEST_GUILESS_MAIN(TestFilterViewModel)
#include "test_filterviewmodel.moc"
