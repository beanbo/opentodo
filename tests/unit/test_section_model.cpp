// Section model + DataStore section operations coverage.
//
// Complements tests/unit/test_models.cpp (basic Section round-trip) and
// tests/unit/test_datastore.cpp (handful of section ops). This file targets
// edge cases and behaviours not yet covered:
//
//   * Section: invalid JSON, idempotent setters, sortOrder/order round-trip,
//     timestamps preserved through round-trip.
//   * DataStore: addSection signals, sortOrder preservation across multiple
//     sections, soft-delete excludes from sectionsForProject(), tasks
//     associated by sectionId, project removal cascade to sections, _meta
//     CRDT blob present after addSection.
//
// QTEST_GUILESS_MAIN — no QGuiApplication needed.

#include <QtTest>
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

#include "model/section.h"
#include "model/project.h"
#include "model/task.h"
#include "storage/datastore.h"
#include "utils/uuid.h"

class TestSectionModel : public QObject
{
    Q_OBJECT

private:
    // Set the QSettings org/app so HLC/deviceId helpers don't pollute the
    // default registry hive on Windows. Must be called from initTestCase.
    static void configureSettings()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenTodoTest_SectionModel"));
        QCoreApplication::setApplicationName(QStringLiteral("test_section_model"));
    }

private slots:
    void initTestCase() { configureSettings(); }
    void cleanupTestCase() {}

    // ── Section entity ────────────────────────────────────────────────────────

    void section_default_construction_sets_id_and_timestamps()
    {
        Section s;
        QVERIFY2(!s.id().isEmpty(), "Default-constructed Section must have an id");
        QVERIFY2(UuidHelper::isValid(s.id()),
                 "Default Section id must be a valid UUID");
        QVERIFY(s.createdAt().isValid());
        QVERIFY(s.updatedAt().isValid());
        // createdAt and updatedAt are seeded to the same instant.
        QCOMPARE(s.createdAt(), s.updatedAt());
        QVERIFY(!s.isDeleted());
        QVERIFY(!s.isCollapsed());
        QVERIFY(!s.isArchived());
        QCOMPARE(s.order(), 0);
        QVERIFY(s.name().isEmpty());
        QVERIFY(s.projectId().isEmpty());
    }

    void section_two_default_constructions_have_unique_ids()
    {
        Section a, b;
        QVERIFY(a.id() != b.id());
    }

    void section_to_json_round_trip_preserves_all_fields()
    {
        Section orig;
        orig.setName(QStringLiteral("Sprint 1"));
        orig.setProjectId(QStringLiteral("proj-uuid-123"));
        orig.setOrder(7);
        orig.setIsCollapsed(true);
        orig.setIsArchived(true);
        orig.setIsDeleted(false);

        const QString id = orig.id();
        const QDateTime created = orig.createdAt();

        QJsonObject json = orig.toJson();

        // Schema sanity — JSON uses snake_case (project conventions).
        QVERIFY(json.contains(QStringLiteral("id")));
        QVERIFY(json.contains(QStringLiteral("name")));
        QVERIFY(json.contains(QStringLiteral("project_id")));
        QVERIFY(json.contains(QStringLiteral("order")));
        QVERIFY(json.contains(QStringLiteral("is_collapsed")));
        QVERIFY(json.contains(QStringLiteral("is_archived")));
        QVERIFY(json.contains(QStringLiteral("is_deleted")));
        QVERIFY(json.contains(QStringLiteral("created_at")));
        QVERIFY(json.contains(QStringLiteral("updated_at")));

        QScopedPointer<Section> r(Section::fromJson(json));
        QVERIFY(r);
        QCOMPARE(r->id(), id);
        QCOMPARE(r->name(), QStringLiteral("Sprint 1"));
        QCOMPARE(r->projectId(), QStringLiteral("proj-uuid-123"));
        QCOMPARE(r->order(), 7);
        QVERIFY(r->isCollapsed());
        QVERIFY(r->isArchived());
        QVERIFY(!r->isDeleted());
        QCOMPARE(r->createdAt(), created);
    }

    void section_from_json_invalid_does_not_crash()
    {
        // Empty object: parser should produce a Section with empty id/name
        // and default flags — *not* crash or return null. The contract is
        // "be lenient on input"; the loader in DataStore relies on this.
        QJsonObject empty;
        QScopedPointer<Section> s(Section::fromJson(empty));
        QVERIFY(s);
        QVERIFY(s->id().isEmpty());
        QVERIFY(s->name().isEmpty());
        QVERIFY(!s->isDeleted());
        QCOMPARE(s->order(), 0);

        // Garbage object with wrong types — toString() returns "" and toInt()
        // returns 0 silently, so we end up with a default-shaped Section.
        QJsonObject garbage;
        garbage[QStringLiteral("id")] = 42;                  // wrong type
        garbage[QStringLiteral("name")] = QJsonArray();      // wrong type
        garbage[QStringLiteral("order")] = QStringLiteral("hello"); // wrong type
        QScopedPointer<Section> g(Section::fromJson(garbage));
        QVERIFY(g);
        QVERIFY(g->id().isEmpty());
        QCOMPARE(g->order(), 0);
    }

    void section_setName_emits_signal_and_updates()
    {
        Section s;
        QSignalSpy spy(&s, &Section::nameChanged);
        s.setName(QStringLiteral("New Section"));
        QCOMPARE(s.name(), QStringLiteral("New Section"));
        QCOMPARE(spy.count(), 1);
    }

    void section_setName_idempotent_no_signal()
    {
        // Setting the same value should not re-emit.
        Section s;
        s.setName(QStringLiteral("Same"));
        QSignalSpy spy(&s, &Section::nameChanged);
        s.setName(QStringLiteral("Same"));
        QCOMPARE(spy.count(), 0);
    }

    void section_setOrder_round_trip()
    {
        Section s;
        QSignalSpy spy(&s, &Section::orderChanged);
        s.setOrder(42);
        QCOMPARE(s.order(), 42);
        QCOMPARE(spy.count(), 1);

        // Serialise → restore preserves the order field.
        QJsonObject j = s.toJson();
        QCOMPARE(j[QStringLiteral("order")].toInt(), 42);

        QScopedPointer<Section> r(Section::fromJson(j));
        QCOMPARE(r->order(), 42);
    }

    void section_setUpdatedAt_round_trip()
    {
        // Section model itself does NOT auto-bump updatedAt on setters
        // (DataStore is responsible for that via renameSection /
        // stampEntityField). Verify setUpdatedAt is honoured explicitly.
        Section s;
        const QDateTime then = QDateTime::fromString(
            QStringLiteral("2026-01-15T12:34:56.789Z"), Qt::ISODateWithMs);
        QSignalSpy spy(&s, &Section::updatedAtChanged);
        s.setUpdatedAt(then);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(s.updatedAt(), then);

        QJsonObject j = s.toJson();
        QScopedPointer<Section> r(Section::fromJson(j));
        QCOMPARE(r->updatedAt(), then);
    }

    void section_soft_delete_round_trip_preserved()
    {
        Section s;
        s.setName(QStringLiteral("Trash"));
        s.setIsDeleted(true);
        QJsonObject j = s.toJson();
        QVERIFY(j[QStringLiteral("is_deleted")].toBool());
        QScopedPointer<Section> r(Section::fromJson(j));
        QVERIFY(r->isDeleted());
        QCOMPARE(r->name(), QStringLiteral("Trash"));
    }

    // ── DataStore section ops ─────────────────────────────────────────────────

    void datastore_addSection_appears_in_sectionsForProject()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Work"));
        Section *s = ds.addSection(QStringLiteral("Sprint 1"), p->id());
        QVERIFY(s != nullptr);
        QVERIFY(!s->id().isEmpty());
        QCOMPARE(s->name(), QStringLiteral("Sprint 1"));
        QCOMPARE(s->projectId(), p->id());

        const auto sections = ds.sectionsForProject(p->id());
        QCOMPARE(sections.size(), 1);
        QCOMPARE(sections.first(), s);
    }

    void datastore_addSection_emits_sectionsChanged_and_dataChanged()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Work"));
        QSignalSpy secSpy(&ds, &DataStore::sectionsChanged);
        QSignalSpy dataSpy(&ds, &DataStore::dataChanged);
        ds.addSection(QStringLiteral("Sprint"), p->id());
        QCOMPARE(secSpy.count(), 1);
        QVERIFY2(dataSpy.count() >= 1, "addSection must emit dataChanged for autosave");
    }

    void datastore_renameSection_updates_name_and_signals()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("P"));
        Section *s = ds.addSection(QStringLiteral("Old Name"), p->id());

        QSignalSpy secSpy(&ds, &DataStore::sectionsChanged);
        QSignalSpy dataSpy(&ds, &DataStore::dataChanged);
        const bool ok = ds.renameSection(s->id(), QStringLiteral("New Name"));
        QVERIFY(ok);
        QCOMPARE(s->name(), QStringLiteral("New Name"));
        QCOMPARE(secSpy.count(), 1);
        QVERIFY(dataSpy.count() >= 1);
    }

    void datastore_renameSection_unknown_id_returns_false()
    {
        DataStore ds;
        QVERIFY(!ds.renameSection(QStringLiteral("no-such-id"),
                                  QStringLiteral("Whatever")));
    }

    void datastore_renameSection_empty_name_rejected()
    {
        // renameSection trims the new name and rejects empty strings.
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("P"));
        Section *s = ds.addSection(QStringLiteral("Keep Me"), p->id());
        QVERIFY(!ds.renameSection(s->id(), QStringLiteral("   ")));
        QCOMPARE(s->name(), QStringLiteral("Keep Me"));
    }

    void datastore_removeSection_soft_deletes_and_excludes_from_enumeration()
    {
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("P"));
        Section *s = ds.addSection(QStringLiteral("Doomed"), p->id());

        QSignalSpy secSpy(&ds, &DataStore::sectionsChanged);
        const bool ok = ds.removeSection(s->id());
        QVERIFY(ok);
        QVERIFY2(s->isDeleted(),
                 "removeSection must mark the entity is_deleted (soft-delete)");
        QCOMPARE(secSpy.count(), 1);

        // Soft-deleted sections are excluded from sectionsForProject().
        QCOMPARE(ds.sectionsForProject(p->id()).size(), 0);

        // Index still resolves the id (object lives, just tombstoned).
        QVERIFY(ds.sectionById(s->id()) == s);
    }

    void datastore_removeSection_unknown_returns_false()
    {
        DataStore ds;
        QVERIFY(!ds.removeSection(QStringLiteral("ghost-id")));
    }

    void datastore_sectionsForProject_preserves_addition_order()
    {
        // Multiple sections under the same project should appear in the
        // order in which they were added (DataStore appends to m_sections
        // and sectionsForProject iterates that list).
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("P"));
        Section *s1 = ds.addSection(QStringLiteral("First"), p->id());
        Section *s2 = ds.addSection(QStringLiteral("Second"), p->id());
        Section *s3 = ds.addSection(QStringLiteral("Third"), p->id());

        // addSection assigns increasing sortOrder values via
        // setOrder(sectionsForProject(projectId).size()) — verify.
        QCOMPARE(s1->order(), 0);
        QCOMPARE(s2->order(), 1);
        QCOMPARE(s3->order(), 2);

        const auto list = ds.sectionsForProject(p->id());
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.at(0), s1);
        QCOMPARE(list.at(1), s2);
        QCOMPARE(list.at(2), s3);
    }

    void datastore_sectionsForProject_filters_by_project_id()
    {
        DataStore ds;
        Project *p1 = ds.addProject(QStringLiteral("Alpha"));
        Project *p2 = ds.addProject(QStringLiteral("Beta"));
        ds.addSection(QStringLiteral("A1"), p1->id());
        ds.addSection(QStringLiteral("A2"), p1->id());
        ds.addSection(QStringLiteral("B1"), p2->id());

        QCOMPARE(ds.sectionsForProject(p1->id()).size(), 2);
        QCOMPARE(ds.sectionsForProject(p2->id()).size(), 1);
        QCOMPARE(ds.sectionsForProject(QStringLiteral("nobody")).size(), 0);
    }

    void datastore_tasksForSection_returns_only_section_tasks()
    {
        DataStore ds;
        // Inbox so addTask("...", "") would work too, but here we use a real project.
        Project *p = ds.addProject(QStringLiteral("Work"));
        Section *s1 = ds.addSection(QStringLiteral("S1"), p->id());
        Section *s2 = ds.addSection(QStringLiteral("S2"), p->id());

        Task *t1 = ds.addTask(QStringLiteral("In S1"), p->id());
        Task *t2 = ds.addTask(QStringLiteral("In S2"), p->id());
        Task *t3 = ds.addTask(QStringLiteral("Sectionless"), p->id());
        t1->setSectionId(s1->id());
        t2->setSectionId(s2->id());
        // t3 stays sectionless.

        QCOMPARE(ds.tasksForSection(s1->id()).size(), 1);
        QCOMPARE(ds.tasksForSection(s1->id()).first(), t1);
        QCOMPARE(ds.tasksForSection(s2->id()).size(), 1);
        QCOMPARE(ds.tasksForSection(s2->id()).first(), t2);

        // Sectionless task is not surfaced by tasksForSection() at all.
        QCOMPARE(ds.tasksForSection(QString()).size(), 1);
        QCOMPARE(ds.tasksForSection(QString()).first(), t3);
    }

    void datastore_removeProject_cascades_soft_delete_to_sections()
    {
        // Documented behaviour (datastore.cpp::removeProject lines 453-461):
        // removing a project tombstones every Section whose projectId
        // matches. Sections become invisible to sectionsForProject() but the
        // index lookup still resolves them so sync can propagate the
        // tombstone.
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("Goodbye"));
        Section *a = ds.addSection(QStringLiteral("A"), p->id());
        Section *b = ds.addSection(QStringLiteral("B"), p->id());

        QVERIFY(!a->isDeleted());
        QVERIFY(!b->isDeleted());

        QVERIFY(ds.removeProject(p->id()));
        QVERIFY2(a->isDeleted(),
                 "Sections of removed project must be tombstoned (cascade)");
        QVERIFY2(b->isDeleted(),
                 "Sections of removed project must be tombstoned (cascade)");
        QCOMPARE(ds.sectionsForProject(p->id()).size(), 0);
    }

    // ── CRDT _meta blob support ───────────────────────────────────────────────

    void datastore_addSection_stamps_crdt_meta_in_serialised_state()
    {
        // The Section model itself does not carry a _meta blob — the per-
        // entity metadata lives in DataStore::m_sectionMeta and is injected
        // into the JSON during DataStore::toJson() (see datastore.cpp
        // injectMeta). Verify that after addSection() the _meta is present
        // for that section in the serialised state.
        //
        // SHAPE: single-level. obj["_meta"][fieldName] == { ts, dev }.
        // (Earlier builds had a buggy double-wrap obj["_meta"]["_meta"]
        //  [fieldName] = ...; that was fixed alongside a v1->v2 schema
        //  migration in DataStore::migrateJson.)
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("P"));
        Section *s = ds.addSection(QStringLiteral("Sprint"), p->id());

        const QJsonObject root = ds.toJson();
        QVERIFY(root.contains(QStringLiteral("sections")));
        const QJsonArray sectionsArr = root.value(QStringLiteral("sections")).toArray();
        QCOMPARE(sectionsArr.size(), 1);

        const QJsonObject obj = sectionsArr.at(0).toObject();
        QCOMPARE(obj.value(QStringLiteral("id")).toString(), s->id());
        QVERIFY2(obj.contains(QStringLiteral("_meta")),
                 "addSection must produce a CRDT _meta blob in serialised state");

        const QJsonObject meta = obj.value(QStringLiteral("_meta")).toObject();
        QVERIFY2(!meta.contains(QStringLiteral("_meta")),
                 "_meta must not contain a nested '_meta' key (that would be "
                 "the legacy double-wrap bug)");

        // The initialFields list in DataStore::addSection stamps these fields.
        QVERIFY2(meta.contains(QStringLiteral("name")),
                 "_meta must include 'name' field stamped by addSection");
        QVERIFY2(meta.contains(QStringLiteral("project_id")),
                 "_meta must include 'project_id' field stamped by addSection");
        QVERIFY2(meta.contains(QStringLiteral("is_deleted")),
                 "_meta must include 'is_deleted' field stamped by addSection");

        // Each field's meta is { ts, dev } shape.
        const QJsonObject nameMeta = meta.value(QStringLiteral("name")).toObject();
        QVERIFY(nameMeta.contains(QStringLiteral("ts")));
        QVERIFY(nameMeta.contains(QStringLiteral("dev")));
        QVERIFY(!nameMeta.value(QStringLiteral("ts")).toString().isEmpty());
        QVERIFY(!nameMeta.value(QStringLiteral("dev")).toString().isEmpty());
    }

    void datastore_renameSection_stamps_name_field_meta()
    {
        // After rename, the per-field meta for "name" must be re-stamped
        // (so the rename wins on merge against an older edit on a peer).
        // Single-level "_meta"[field] shape — see addSection test note.
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("P"));
        Section *s = ds.addSection(QStringLiteral("Old"), p->id());

        const QString origNameTs = ds.toJson()
            .value(QStringLiteral("sections")).toArray()
            .at(0).toObject()
            .value(QStringLiteral("_meta")).toObject()
            .value(QStringLiteral("name")).toObject()
            .value(QStringLiteral("ts")).toString();
        QVERIFY(!origNameTs.isEmpty());

        QVERIFY(ds.renameSection(s->id(), QStringLiteral("New")));

        const QString newNameTs = ds.toJson()
            .value(QStringLiteral("sections")).toArray()
            .at(0).toObject()
            .value(QStringLiteral("_meta")).toObject()
            .value(QStringLiteral("name")).toObject()
            .value(QStringLiteral("ts")).toString();
        QVERIFY(!newNameTs.isEmpty());
        QVERIFY2(newNameTs > origNameTs,
                 "renameSection must advance the HLC ts on the 'name' field");
    }

    void datastore_removeSection_stamps_tombstone_field_meta()
    {
        // Single-level "_meta"[field] shape — see addSection test note.
        DataStore ds;
        Project *p = ds.addProject(QStringLiteral("P"));
        Section *s = ds.addSection(QStringLiteral("Doomed"), p->id());

        const QString origDelTs = ds.toJson()
            .value(QStringLiteral("sections")).toArray()
            .at(0).toObject()
            .value(QStringLiteral("_meta")).toObject()
            .value(QStringLiteral("is_deleted")).toObject()
            .value(QStringLiteral("ts")).toString();
        QVERIFY(!origDelTs.isEmpty());

        QVERIFY(ds.removeSection(s->id()));

        const QString newDelTs = ds.toJson()
            .value(QStringLiteral("sections")).toArray()
            .at(0).toObject()
            .value(QStringLiteral("_meta")).toObject()
            .value(QStringLiteral("is_deleted")).toObject()
            .value(QStringLiteral("ts")).toString();
        QVERIFY2(newDelTs > origDelTs,
                 "removeSection must re-stamp the is_deleted field's HLC ts");
    }
};

QTEST_GUILESS_MAIN(TestSectionModel)
#include "test_section_model.moc"
