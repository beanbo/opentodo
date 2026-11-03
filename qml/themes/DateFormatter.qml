pragma Singleton
import QtQuick

// DateFormatter — singleton helper that centralises date rendering across the
// app, so all UI components display dates in the user-selected format.
//
// Three formats are supported:
//   "dd.MM.yyyy"  → European  (31.12.2026)
//   "yyyy-MM-dd"  → ISO       (2026-12-31)
//   "MM/dd/yyyy"  → US        (12/31/2026)
//
// Persistence intentionally lives OUTSIDE this singleton — Qt.labs.settings
// can't be imported here (Qt resolves singleton imports very early in engine
// startup, before optional QML modules are mounted, which produced
// "module Qt.labs.settings is not installed" failures cascading through
// every consumer of this file). Instead, SettingsSectionGeneral writes the
// chosen format into the QSettings store AND mirrors it onto
// DateFormatter.currentFormat at startup + on change. Consumers just call
// DateFormatter.format(...) — they don't need to know about persistence.
//
// Usage from QML:
//   Text { text: DateFormatter.format(task.due) }
//   Text { text: DateFormatter.formatWithTime(reminder.fireAt) }
//   var pat = DateFormatter.pattern()      // for Qt.formatDate(...) elsewhere
QtObject {
    id: dateFormatter

    // Active format. Default = European DD.MM.YYYY. SettingsSectionGeneral
    // pushes the user's choice into this property on every change.
    property string currentFormat: "dd.MM.yyyy"

    // Apply the persisted choice ("general/dateFormat") at startup. Before,
    // only SettingsSectionGeneral's Component.onCompleted did this, so the
    // saved format was ignored until the desktop Settings page was opened
    // (and never on mobile). The Settings object is created at RUNTIME, when
    // the singleton is first used — a static `import QtCore` here is what
    // broke engine startup (see the header note) — and any failure leaves
    // the default in place.
    property QtObject _store: null
    Component.onCompleted: {
        try {
            _store = Qt.createQmlObject(
                'import QtCore\nSettings { category: "general" }',
                dateFormatter, "DateFormatterSettings")
            if (_store) {
                var saved = _store.value("dateFormat", "")
                if (saved) setFormat(String(saved))
            }
        } catch (e) {
            // Settings unavailable (e.g. stripped QtCore QML module): keep default.
        }
    }

    // Returns the active QML/JS-compatible pattern string.
    // Falls back to the European format if currentFormat is invalid.
    function pattern() {
        if (currentFormat === "dd.MM.yyyy" || currentFormat === "yyyy-MM-dd" || currentFormat === "MM/dd/yyyy")
            return currentFormat
        return "dd.MM.yyyy"
    }

    // Writable accessor used by SettingsSectionGeneral after each ComboBox
    // change. Validates the input; rejects invalid patterns.
    function setFormat(f) {
        if (f === "dd.MM.yyyy" || f === "yyyy-MM-dd" || f === "MM/dd/yyyy") {
            currentFormat = f
            return true
        }
        return false
    }

    // Coerces an arbitrary input (Date object, ISO string, or QML date) into
    // a JS Date. Returns an invalid Date on failure so callers can detect it
    // via isNaN(d.getTime()).
    function _toDate(input) {
        if (input === null || input === undefined || input === "")
            return new Date(NaN)
        if (input instanceof Date)
            return input
        // String inputs: try ISO first, fall back to Date()'s native parser.
        if (typeof input === "string") {
            // QDate.toString(Qt.ISODate) yields "yyyy-MM-dd"; QDateTime is
            // "yyyy-MM-ddTHH:mm:ss". Date() handles both natively.
            //
            // BUG-FIX (C-2): a bare "yyyy-MM-dd" is parsed by JS as UTC
            // midnight per ECMA-262, so users in UTC- timezones (e.g.
            // Hawaii) see the *previous* day rendered. Append "T00:00:00"
            // for the date-only form so Date() parses it as local midnight
            // — matches the convention already in DesktopLayout.qml:843
            // and TaskItem.qml:851.
            if (/^\d{4}-\d{2}-\d{2}$/.test(input))
                return new Date(input + "T00:00:00")
            return new Date(input)
        }
        // Numeric (epoch ms) or any other coercible value.
        return new Date(input)
    }

    // Formats a date value (Date, ISO string, or QML date) using the active
    // pattern. Returns "" for invalid / empty input so callers can fall back
    // to a placeholder cleanly.
    function format(dateOrDateString) {
        var d = _toDate(dateOrDateString)
        if (isNaN(d.getTime()))
            return ""
        return Qt.formatDate(d, pattern())
    }

    // Same as format(), but appends " HH:mm" — used for reminders, due-time
    // displays, etc. Time portion is always 24-hour to keep parsing trivial;
    // the dedicated 12h/24h time-format setting is independent of date format.
    function formatWithTime(dateOrDateString) {
        var d = _toDate(dateOrDateString)
        if (isNaN(d.getTime()))
            return ""
        return Qt.formatDateTime(d, pattern() + " HH:mm")
    }
}
