// Tests for the Q_GADGET value types nested inside the Comment / Reminder
// entities: AttachmentObject, ReminderDue, LocationObject.
//
// These types are intentionally lightweight (no signals, no parent QObject) —
// they exist so QML can read/write structured sub-values via Q_PROPERTY MEMBER
// bindings on the parent Q_OBJECT. Coverage focus areas:
//   - Default-constructed isNull() / observable-state defaults
//   - Field round-trip through the Q_GADGET's own toJson / fromJson
//   - Round-trip through the parent entity's toJson / fromJson
//   - Default-constructed equality + copy/assign value semantics
//   - QML-style property access through QMetaProperty (proves the MEMBER
//     bindings actually expose readable properties, not just stubs)
//
// IMPORTANT: avoid raw string literals R"(...)" — moc has historically
// choked on them in this project; see test_comment_extended.cpp for the
// same caveat.

#include <QtTest>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMetaProperty>
#include <QVariant>

#include "model/comment.h"
#include "model/reminder.h"

class TestQGadgets : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() {}
    void cleanupTestCase() {}

    // ── AttachmentObject ──────────────────────────────────────────────────────

    void attachment_default_is_null()
    {
        AttachmentObject a;
        QVERIFY(a.isNull());
        QVERIFY(a.fileName.isEmpty());
        QVERIFY(a.fileType.isEmpty());
        QVERIFY(a.fileUrl.isEmpty());
        QCOMPARE(a.fileSize, 0);
    }

    void attachment_fields_stored()
    {
        AttachmentObject a;
        a.fileName = QStringLiteral("budget.xlsx");
        a.fileType = QStringLiteral("application/vnd.openxmlformats");
        a.fileUrl  = QStringLiteral("https://files.example.com/123");
        a.fileSize = 4096;

        QVERIFY(!a.isNull());
        QCOMPARE(a.fileName, QStringLiteral("budget.xlsx"));
        QCOMPARE(a.fileType, QStringLiteral("application/vnd.openxmlformats"));
        QCOMPARE(a.fileUrl,  QStringLiteral("https://files.example.com/123"));
        QCOMPARE(a.fileSize, 4096);
    }

    void attachment_json_roundtrip_direct()
    {
        AttachmentObject a;
        a.fileName = QStringLiteral("notes.pdf");
        a.fileType = QStringLiteral("application/pdf");
        a.fileUrl  = QStringLiteral("https://drive.example.com/abc");
        a.fileSize = 12345;

        const QJsonObject obj = a.toJson();
        // Field names follow the snake_case JSON convention noted in CLAUDE.md.
        QVERIFY(obj.contains(QStringLiteral("file_name")));
        QVERIFY(obj.contains(QStringLiteral("file_type")));
        QVERIFY(obj.contains(QStringLiteral("file_url")));
        QVERIFY(obj.contains(QStringLiteral("file_size")));
        QCOMPARE(obj.value(QStringLiteral("file_size")).toInt(), 12345);

        AttachmentObject restored = AttachmentObject::fromJson(obj);
        QCOMPARE(restored.fileName, a.fileName);
        QCOMPARE(restored.fileType, a.fileType);
        QCOMPARE(restored.fileUrl,  a.fileUrl);
        QCOMPARE(restored.fileSize, a.fileSize);
        QVERIFY(!restored.isNull());
    }

    void attachment_roundtrip_via_comment()
    {
        // Comment::toJson omits the attachment subobject entirely when the
        // attachment is null, and embeds it as an "attachment" object when
        // not null. Round-trip via the parent.
        Comment c;
        c.setContent(QStringLiteral("Look at this file"));

        AttachmentObject a;
        a.fileName = QStringLiteral("photo.png");
        a.fileType = QStringLiteral("image/png");
        a.fileUrl  = QStringLiteral("https://img.example.com/p");
        a.fileSize = 7777;
        c.setAttachment(a);

        const QJsonObject obj = c.toJson();
        QVERIFY(obj.contains(QStringLiteral("attachment")));

        Comment *restored = Comment::fromJson(obj);
        QVERIFY(restored != nullptr);
        AttachmentObject ra = restored->attachment();
        QVERIFY(!ra.isNull());
        QCOMPARE(ra.fileName, QStringLiteral("photo.png"));
        QCOMPARE(ra.fileType, QStringLiteral("image/png"));
        QCOMPARE(ra.fileUrl,  QStringLiteral("https://img.example.com/p"));
        QCOMPARE(ra.fileSize, 7777);

        // A Comment whose attachment is left null must NOT serialise an
        // attachment field — important because consumers gate on its absence.
        Comment empty;
        empty.setContent(QStringLiteral("plain"));
        const QJsonObject emptyObj = empty.toJson();
        QVERIFY(!emptyObj.contains(QStringLiteral("attachment")));

        delete restored;
    }

    void attachment_copy_and_equality_semantics()
    {
        // Two default-constructed objects represent the same empty state.
        // The Q_GADGETs do not declare operator==, so we compare by field.
        AttachmentObject a;
        AttachmentObject b;
        QCOMPARE(a.fileName, b.fileName);
        QCOMPARE(a.fileType, b.fileType);
        QCOMPARE(a.fileUrl,  b.fileUrl);
        QCOMPARE(a.fileSize, b.fileSize);

        // Differ once one of them carries data.
        a.fileName = QStringLiteral("doc.txt");
        QVERIFY(a.fileName != b.fileName);

        // Copy ctor: fields preserved, source unchanged afterwards.
        AttachmentObject copy(a);
        QCOMPARE(copy.fileName, a.fileName);
        copy.fileSize = 999;
        QCOMPARE(a.fileSize, 0);   // mutating copy must not touch original

        // Assignment: same story, deep value copy.
        AttachmentObject assigned;
        assigned = a;
        QCOMPARE(assigned.fileName, a.fileName);
        assigned.fileName = QStringLiteral("other.txt");
        QCOMPARE(a.fileName, QStringLiteral("doc.txt"));
    }

    void attachment_qml_property_access()
    {
        // Confirm the Q_PROPERTY MEMBER bindings actually publish the fields
        // to the meta-object system — that is what makes them readable from
        // QML (`comment.attachment.fileName`).
        AttachmentObject a;
        a.fileName = QStringLiteral("x.bin");
        a.fileSize = 42;
        const QMetaObject *mo = &AttachmentObject::staticMetaObject;

        QVariant fileNameVariant = mo->property(mo->indexOfProperty("fileName"))
                                       .readOnGadget(&a);
        QVariant fileSizeVariant = mo->property(mo->indexOfProperty("fileSize"))
                                       .readOnGadget(&a);
        QCOMPARE(fileNameVariant.toString(), QStringLiteral("x.bin"));
        QCOMPARE(fileSizeVariant.toInt(),    42);
        QVERIFY(mo->indexOfProperty("fileType") >= 0);
        QVERIFY(mo->indexOfProperty("fileUrl")  >= 0);
    }

    // ── ReminderDue ───────────────────────────────────────────────────────────

    void reminder_due_default_is_null()
    {
        ReminderDue d;
        QVERIFY(d.isNull());
        QVERIFY(d.date.isEmpty());
        QVERIFY(d.datetime.isEmpty());
        QVERIFY(d.timezone.isEmpty());
        // lang has a default value — verify what the header declares.
        QCOMPARE(d.lang, QStringLiteral("en"));
    }

    void reminder_due_date_only_is_not_null()
    {
        ReminderDue d;
        d.date = QStringLiteral("2026-05-09");
        QVERIFY(!d.isNull());
        QCOMPARE(d.date, QStringLiteral("2026-05-09"));
        QVERIFY(d.datetime.isEmpty());
    }

    void reminder_due_datetime_only_is_not_null()
    {
        ReminderDue d;
        d.datetime = QStringLiteral("2026-05-09T09:00:00Z");
        QVERIFY(!d.isNull());
        QVERIFY(d.date.isEmpty());
        QCOMPARE(d.datetime, QStringLiteral("2026-05-09T09:00:00Z"));
    }

    void reminder_due_both_fields_stored()
    {
        ReminderDue d;
        d.date     = QStringLiteral("2026-05-09");
        d.datetime = QStringLiteral("2026-05-09T15:30:00+02:00");
        d.timezone = QStringLiteral("Europe/Berlin");
        d.lang     = QStringLiteral("ru");
        QVERIFY(!d.isNull());

        QJsonObject obj = d.toJson();
        QVERIFY(obj.contains(QStringLiteral("date")));
        QVERIFY(obj.contains(QStringLiteral("datetime")));
        QVERIFY(obj.contains(QStringLiteral("timezone")));
        QVERIFY(obj.contains(QStringLiteral("lang")));

        ReminderDue back = ReminderDue::fromJson(obj);
        QCOMPARE(back.date,     d.date);
        QCOMPARE(back.datetime, d.datetime);
        QCOMPARE(back.timezone, d.timezone);
        QCOMPARE(back.lang,     d.lang);
    }

    void reminder_due_roundtrip_via_reminder()
    {
        // Reminder::toJson only embeds "due" when type=="absolute" AND the
        // due is not null. That is the only path real callers exercise.
        Reminder r;
        r.setType(QStringLiteral("absolute"));
        ReminderDue d;
        d.datetime = QStringLiteral("2026-12-31T23:59:00Z");
        d.timezone = QStringLiteral("UTC");
        d.lang     = QStringLiteral("en");
        r.setDue(d);

        QJsonObject obj = r.toJson();
        QVERIFY(obj.contains(QStringLiteral("due")));

        Reminder *restored = Reminder::fromJson(obj);
        QVERIFY(restored != nullptr);
        ReminderDue back = restored->due();
        QCOMPARE(back.datetime, QStringLiteral("2026-12-31T23:59:00Z"));
        QCOMPARE(back.timezone, QStringLiteral("UTC"));
        QCOMPARE(back.lang,     QStringLiteral("en"));
        QVERIFY(!back.isNull());

        // A null due on an absolute reminder must NOT be serialised — that
        // contract underlies ReminderScheduler skipping unschedulable items.
        Reminder empty;
        empty.setType(QStringLiteral("absolute"));
        const QJsonObject emptyObj = empty.toJson();
        QVERIFY(!emptyObj.contains(QStringLiteral("due")));

        delete restored;
    }

    void reminder_due_timezone_field_preserved()
    {
        ReminderDue d;
        d.datetime = QStringLiteral("2026-05-09T08:00:00+09:00");
        d.timezone = QStringLiteral("Asia/Tokyo");

        ReminderDue back = ReminderDue::fromJson(d.toJson());
        QCOMPARE(back.timezone, QStringLiteral("Asia/Tokyo"));
        QCOMPARE(back.datetime, QStringLiteral("2026-05-09T08:00:00+09:00"));
    }

    void reminder_due_lang_field_preserved_and_defaults_to_en()
    {
        // Empty input → fromJson supplies the documented "en" default.
        ReminderDue empty = ReminderDue::fromJson(QJsonObject{});
        QCOMPARE(empty.lang, QStringLiteral("en"));

        // Explicit non-default value survives the round-trip.
        ReminderDue d;
        d.date = QStringLiteral("2026-05-09");
        d.lang = QStringLiteral("ru");
        ReminderDue back = ReminderDue::fromJson(d.toJson());
        QCOMPARE(back.lang, QStringLiteral("ru"));
    }

    void reminder_due_copy_and_default_equality()
    {
        ReminderDue a;
        ReminderDue b;
        // Default-constructed: same observable state.
        QCOMPARE(a.date,     b.date);
        QCOMPARE(a.datetime, b.datetime);
        QCOMPARE(a.timezone, b.timezone);
        QCOMPARE(a.lang,     b.lang);

        a.date = QStringLiteral("2026-05-09");
        ReminderDue copy(a);
        QCOMPARE(copy.date, QStringLiteral("2026-05-09"));
        copy.date = QStringLiteral("2027-01-01");
        QCOMPARE(a.date, QStringLiteral("2026-05-09")); // copy is detached
    }

    // ── LocationObject ────────────────────────────────────────────────────────

    void location_default_is_null_and_has_documented_defaults()
    {
        LocationObject l;
        QVERIFY(l.isNull());
        QVERIFY(l.name.isEmpty());
        QCOMPARE(l.latitude,  0.0);
        QCOMPARE(l.longitude, 0.0);
        // The header declares radius default = 100 meters and trigger = "on_enter".
        QCOMPARE(l.radius,  100);
        QCOMPARE(l.trigger, QStringLiteral("on_enter"));
    }

    void location_lat_lng_makes_it_not_null()
    {
        LocationObject l;
        l.latitude  = 52.5200;
        l.longitude = 13.4050;       // Berlin
        QVERIFY(!l.isNull());

        LocationObject named;
        named.name = QStringLiteral("Office");
        // Name alone (without coords) is also enough — isNull requires
        // empty name AND zero coords.
        QVERIFY(!named.isNull());
    }

    void location_radius_default_value_round_trips()
    {
        LocationObject l;
        l.latitude  = 1.0;
        l.longitude = 1.0;
        // Don't touch radius — it should serialise as the documented default.

        QJsonObject obj = l.toJson();
        QCOMPARE(obj.value(QStringLiteral("radius")).toInt(), 100);

        // fromJson with a missing radius value must also fall back to 100,
        // per the explicit toInt(100) default in LocationObject::fromJson.
        QJsonObject minimal;
        minimal[QStringLiteral("latitude")]  = 1.0;
        minimal[QStringLiteral("longitude")] = 2.0;
        LocationObject back = LocationObject::fromJson(minimal);
        QCOMPARE(back.radius, 100);
    }

    void location_trigger_round_trips_on_enter_and_on_leave()
    {
        // "on_enter" is the constructor default — verify it survives.
        LocationObject onEnter;
        onEnter.latitude  = 10.0;
        onEnter.longitude = 20.0;
        LocationObject backEnter = LocationObject::fromJson(onEnter.toJson());
        QCOMPARE(backEnter.trigger, QStringLiteral("on_enter"));

        // "on_leave" is the other supported value.
        LocationObject onLeave;
        onLeave.latitude  = 10.0;
        onLeave.longitude = 20.0;
        onLeave.trigger   = QStringLiteral("on_leave");
        LocationObject backLeave = LocationObject::fromJson(onLeave.toJson());
        QCOMPARE(backLeave.trigger, QStringLiteral("on_leave"));

        // Missing trigger field in JSON must default to "on_enter" per
        // LocationObject::fromJson's toString("on_enter") fallback.
        QJsonObject minimal;
        minimal[QStringLiteral("latitude")]  = 5.0;
        minimal[QStringLiteral("longitude")] = 6.0;
        LocationObject defaulted = LocationObject::fromJson(minimal);
        QCOMPARE(defaulted.trigger, QStringLiteral("on_enter"));
    }

    void location_negative_latitude_southern_hemisphere()
    {
        // Sydney: a real Southern hemisphere coordinate. Negative latitudes
        // and large longitudes must round-trip with full double precision.
        LocationObject l;
        l.name      = QStringLiteral("Sydney Office");
        l.latitude  = -33.8688;
        l.longitude = 151.2093;
        l.radius    = 250;
        l.trigger   = QStringLiteral("on_leave");

        LocationObject back = LocationObject::fromJson(l.toJson());
        QCOMPARE(back.name,      l.name);
        QCOMPARE(back.latitude,  -33.8688);
        QCOMPARE(back.longitude, 151.2093);
        QCOMPARE(back.radius,    250);
        QCOMPARE(back.trigger,   QStringLiteral("on_leave"));
        QVERIFY(!back.isNull());
    }

    void location_out_of_range_longitude_round_trips_unchanged()
    {
        // The data type performs no validation — out-of-range geographic
        // coordinates are accepted and round-trip verbatim. This test
        // documents that behaviour so a future caller (LocationService)
        // knows it cannot rely on the model to reject bad input.
        LocationObject l;
        l.latitude  = 95.0;     // outside [-90, 90]
        l.longitude = 250.0;    // outside [-180, 180]
        l.radius    = -5;       // negative radius — also unvalidated
        QVERIFY(!l.isNull());

        QJsonObject obj = l.toJson();
        LocationObject back = LocationObject::fromJson(obj);
        QCOMPARE(back.latitude,  95.0);
        QCOMPARE(back.longitude, 250.0);
        QCOMPARE(back.radius,    -5);
    }

    void location_roundtrip_via_reminder()
    {
        // Reminder::toJson only embeds "location" when type=="location" and
        // the LocationObject is not null.
        Reminder r;
        LocationObject l;
        l.name      = QStringLiteral("Home");
        l.latitude  = 48.8566;
        l.longitude = 2.3522;        // Paris
        l.radius    = 175;
        l.trigger   = QStringLiteral("on_enter");
        r.setLocation(l);
        r.setType(QStringLiteral("location"));

        QJsonObject obj = r.toJson();
        QVERIFY(obj.contains(QStringLiteral("location")));

        Reminder *restored = Reminder::fromJson(obj);
        QVERIFY(restored != nullptr);
        LocationObject back = restored->location();
        QCOMPARE(back.name,      QStringLiteral("Home"));
        QCOMPARE(back.latitude,  48.8566);
        QCOMPARE(back.longitude, 2.3522);
        QCOMPARE(back.radius,    175);
        QCOMPARE(back.trigger,   QStringLiteral("on_enter"));

        // Conversely: location-typed Reminder with default (null) location
        // must NOT serialise the location key.
        Reminder empty;
        empty.setType(QStringLiteral("location"));
        const QJsonObject emptyObj = empty.toJson();
        QVERIFY(!emptyObj.contains(QStringLiteral("location")));

        delete restored;
    }

    void location_copy_and_qml_property_access()
    {
        // Q_PROPERTY MEMBER bindings → reachable through the static meta-object.
        LocationObject l;
        l.name      = QStringLiteral("Cabin");
        l.latitude  = 60.0;
        l.longitude = 30.0;
        l.radius    = 500;

        const QMetaObject *mo = &LocationObject::staticMetaObject;
        QCOMPARE(mo->property(mo->indexOfProperty("name")).readOnGadget(&l).toString(),
                 QStringLiteral("Cabin"));
        QCOMPARE(mo->property(mo->indexOfProperty("latitude")).readOnGadget(&l).toDouble(),
                 60.0);
        QCOMPARE(mo->property(mo->indexOfProperty("longitude")).readOnGadget(&l).toDouble(),
                 30.0);
        QCOMPARE(mo->property(mo->indexOfProperty("radius")).readOnGadget(&l).toInt(),
                 500);
        QVERIFY(mo->indexOfProperty("trigger") >= 0);

        // Value-copy semantics: mutating the copy must not affect the original.
        LocationObject copy(l);
        copy.name = QStringLiteral("Boat");
        QCOMPARE(l.name, QStringLiteral("Cabin"));

        LocationObject assigned;
        assigned = l;
        assigned.latitude = 99.0;
        QCOMPARE(l.latitude, 60.0);
    }
};

QTEST_GUILESS_MAIN(TestQGadgets)
#include "test_q_gadgets.moc"
