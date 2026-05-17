#include "icsexporter.h"
#include "storage/datastore.h"
#include "model/task.h"

#include <QSaveFile>
#include <QDateTime>
#include <QUrl>
#include <QRegularExpression>

// ── RFC 5545 helpers ─────────────────────────────────────────────────────────
//
// We keep all the spec mechanics in this file. Three pieces matter:
//   (1) Text-value escaping (§3.3.11): backslash / semicolon / comma / newline.
//   (2) Line folding (§3.1): any content line longer than 75 octets must be
//       split, with each continuation starting CRLF + single space.
//   (3) Line endings (§3.1): CRLF only; never bare LF.

// Escape one RFC 5545 TEXT value.  The order matters: backslash must be
// substituted first, otherwise the substitution for `;` etc. would be
// re-escaped into `\\;`.
static QString icsEscapeText(const QString &in)
{
    QString s = in;
    s.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    s.replace(QLatin1Char(';'),  QStringLiteral("\\;"));
    s.replace(QLatin1Char(','),  QStringLiteral("\\,"));
    // Collapse CRLF, then bare CR, then LF — all into the literal `\n`
    // sequence that RFC 5545 recognises inside a TEXT value.
    s.replace(QStringLiteral("\r\n"), QStringLiteral("\\n"));
    s.replace(QLatin1Char('\r'),       QStringLiteral("\\n"));
    s.replace(QLatin1Char('\n'),       QStringLiteral("\\n"));
    return s;
}

// Fold a single logical content line per RFC 5545 §3.1. The spec talks about
// octets, not characters — for UTF-8 we have to count bytes. We measure the
// already-encoded UTF-8 representation, slice on byte boundaries, then take
// care not to split a multi-byte sequence: every continuation byte (10xxxxxx)
// is folded with the byte that follows it (we back up while the next byte is
// a continuation). The line-fold threshold is 75 octets per RFC 5545:
//
//   "Lines of text SHOULD NOT be longer than 75 octets, excluding the line
//    break."
//
// Continuation lines are CRLF + a single SP + payload — and the leading SP
// counts toward the 75-octet budget of the continuation line itself. So the
// first segment can be up to 75 octets, but every continuation segment must
// fit in 74 payload octets (74 payload + 1 SP = 75 line octets).
static QByteArray foldLine(const QString &logical)
{
    constexpr int kFirstLineMax = 75;
    constexpr int kContinuationPayloadMax = 74; // 1 octet reserved for SP
    const QByteArray utf8 = logical.toUtf8();
    QByteArray out;
    int pos = 0;
    bool firstSegment = true;
    while (pos < utf8.size()) {
        const int remaining = utf8.size() - pos;
        const int budget = firstSegment ? kFirstLineMax : kContinuationPayloadMax;
        int take = qMin(remaining, budget);
        // Don't split a multi-byte UTF-8 sequence — back up while the byte
        // at `pos + take` is a continuation byte (top two bits == 10).
        // No risk of underflow: `take` is at most 75, and a single UTF-8
        // codepoint is at most 4 bytes wide.
        while (take > 0 && pos + take < utf8.size()
               && (static_cast<unsigned char>(utf8[pos + take]) & 0xC0) == 0x80) {
            --take;
        }
        out.append(utf8.constData() + pos, take);
        pos += take;
        if (pos < utf8.size()) {
            // Continuation line: CRLF + single space. The leading space is
            // part of the unfold marker (RFC 5545 §3.1) and is stripped by
            // the receiver before reassembling the logical line.
            out.append("\r\n ");
            firstSegment = false;
        }
    }
    return out;
}

// Append `key:value` as a single folded, CRLF-terminated content line.
static void appendProp(QByteArray &out, const QString &key, const QString &value)
{
    QString logical = key;
    logical.append(QLatin1Char(':'));
    logical.append(value);
    out.append(foldLine(logical));
    out.append("\r\n");
}

// Same as appendProp() but for a property with parameters, e.g.
// `DUE;VALUE=DATE:20260517`. The caller passes the entire left-hand side
// up to and including the parameter list, and we append the value after `:`.
static void appendPropRaw(QByteArray &out, const QString &lhs, const QString &value)
{
    QString logical = lhs;
    logical.append(QLatin1Char(':'));
    logical.append(value);
    out.append(foldLine(logical));
    out.append("\r\n");
}

// ── DUE formatting ───────────────────────────────────────────────────────────
//
// OpenTodo stores due either as an ISO date (`YYYY-MM-DD`) or as an ISO
// datetime (`YYYY-MM-DDTHH:MM:SS[+TZ]`). We translate both to the
// corresponding iCalendar TEXT forms.  We treat datetimes as floating local
// time (no TZID parameter, no Z suffix) — that's the most predictable round-
// trip across Apple/Google/Outlook when we don't ship a VTIMEZONE block.
//
// Both helpers return empty when the task has neither a date nor a datetime.
static QString icsDateOnly(const QString &isoDate)
{
    // "YYYY-MM-DD" -> "YYYYMMDD".  QDate::toString gets the same result, but
    // the simple replace path avoids round-tripping invalid strings into a
    // null QDate (which would silently emit an empty value).
    QString s = isoDate;
    s.remove(QLatin1Char('-'));
    return s;
}

static QString icsDateTimeFloating(const QString &isoDt)
{
    // "YYYY-MM-DDTHH:MM:SS[+TZ]" -> "YYYYMMDDTHHMMSS" (drop offset).
    QDateTime dt = QDateTime::fromString(isoDt, Qt::ISODate);
    if (!dt.isValid()) return {};
    return dt.toString(QStringLiteral("yyyyMMdd'T'HHmmss"));
}

// ── PRIORITY mapping ─────────────────────────────────────────────────────────
//
// Todoist priority is 1-best..4-default; OpenTodo follows the same convention.
// RFC 5545 PRIORITY is 0=undefined, 1=highest, 9=lowest. The mapping below
// stays compatible with what Apple Calendar and Outlook surface in their UIs.
static int icsPriority(int taskPriority)
{
    switch (taskPriority) {
        case 1: return 1;  // p1 → highest
        case 2: return 3;  // p2 → medium-high
        case 3: return 5;  // p3 → medium
        case 4: default: return 9; // p4 (default) → lowest
    }
}

// ── Recurrence mapping ───────────────────────────────────────────────────────
//
// We accept either an explicit RFC 5545 RRULE string (already produced by the
// upstream NLP parser — see DueObject::recurrenceRule) or the human-language
// string that the user originally typed. The explicit RRULE wins because it's
// already in the right format; we just normalise the prefix.
//
// For the human-string fallback we cover the common cases listed in the spec
// and silently skip anything else. The caller will then fall back to
// prepending the human string to DESCRIPTION (see exportTasks below).
static QString mapRecurrenceToRrule(const QString &rruleField,
                                    const QString &humanString)
{
    // 1. Already an RFC 5545 RRULE? Strip the "RRULE:" prefix if present —
    //    we add the property name ourselves in exportTasks.
    if (!rruleField.isEmpty()) {
        QString r = rruleField.trimmed();
        if (r.startsWith(QStringLiteral("RRULE:"), Qt::CaseInsensitive))
            r = r.mid(6);
        return r;
    }

    // 2. Parse the human string. Russian or English; lower-cased, trimmed.
    const QString s = humanString.trimmed().toLower();
    if (s.isEmpty()) return {};

    // "every weekday" / "по будням"
    if (s == QStringLiteral("every weekday")
        || s == QStringLiteral("каждый будний день")
        || s == QStringLiteral("по будням")) {
        return QStringLiteral("FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR");
    }

    // "every <weekday>" — English form.
    struct Wd { const char *en; const char *ru; const char *byday; };
    static constexpr Wd kWeekdays[] = {
        {"monday",    "понедельник", "MO"},
        {"tuesday",   "вторник",     "TU"},
        {"wednesday", "среда",       "WE"},
        {"thursday",  "четверг",     "TH"},
        {"friday",    "пятница",     "FR"},
        {"saturday",  "суббота",     "SA"},
        {"sunday",    "воскресенье", "SU"},
    };
    for (const auto &wd : kWeekdays) {
        const QString en = QStringLiteral("every ") + QString::fromUtf8(wd.en);
        const QString ru = QStringLiteral("каждый ") + QString::fromUtf8(wd.ru);
        if (s == en || s == ru) {
            return QStringLiteral("FREQ=WEEKLY;BYDAY=") + QString::fromLatin1(wd.byday);
        }
    }

    // "every N days/weeks/months/years" — with INTERVAL.
    // Both English and Russian use a leading number; the unit is what we
    // dispatch on.
    static const QRegularExpression kEveryN(
        QStringLiteral("^(?:every|каждые)\\s+(\\d+)\\s+"
                       "(day|days|week|weeks|month|months|year|years|"
                       "день|дня|дней|неделю|недели|недель|месяц|месяца|месяцев|"
                       "год|года|лет)$"));
    const auto m = kEveryN.match(s);
    if (m.hasMatch()) {
        const int interval = qMax(1, m.captured(1).toInt());
        const QString unit = m.captured(2);
        QString freq;
        if (unit.startsWith(QLatin1Char('d')) || unit.startsWith(QStringLiteral("день"))
            || unit.startsWith(QStringLiteral("дн")))                              freq = QStringLiteral("DAILY");
        else if (unit.startsWith(QLatin1Char('w')) || unit.startsWith(QStringLiteral("недел")))
            freq = QStringLiteral("WEEKLY");
        else if (unit.startsWith(QLatin1Char('m')) || unit.startsWith(QStringLiteral("месяц")))
            freq = QStringLiteral("MONTHLY");
        else if (unit.startsWith(QLatin1Char('y')) || unit.startsWith(QStringLiteral("год"))
                 || unit.startsWith(QStringLiteral("лет")))                        freq = QStringLiteral("YEARLY");
        if (freq.isEmpty()) return {};
        return QStringLiteral("FREQ=%1;INTERVAL=%2").arg(freq).arg(interval);
    }

    // Bare frequency words.
    if (s == QStringLiteral("every day")    || s == QStringLiteral("daily")
        || s == QStringLiteral("каждый день")) return QStringLiteral("FREQ=DAILY");
    if (s == QStringLiteral("every week")   || s == QStringLiteral("weekly")
        || s == QStringLiteral("каждую неделю")) return QStringLiteral("FREQ=WEEKLY");
    if (s == QStringLiteral("every month")  || s == QStringLiteral("monthly")
        || s == QStringLiteral("каждый месяц")) return QStringLiteral("FREQ=MONTHLY");
    if (s == QStringLiteral("every year")   || s == QStringLiteral("yearly")
        || s == QStringLiteral("annually")  || s == QStringLiteral("каждый год"))
        return QStringLiteral("FREQ=YEARLY");

    // Unrecognised — caller falls back to prepending the human string to
    // DESCRIPTION.
    return {};
}

// ── Public API ───────────────────────────────────────────────────────────────

IcsExporter::IcsExporter(DataStore *store, QObject *parent)
    : QObject(parent), m_store(store)
{
}

QString IcsExporter::exportTasks(const QList<Task *> &tasks)
{
    QByteArray out;
    out.reserve(1024 + 256 * tasks.size());

    // VCALENDAR header. PRODID identifies the producer per RFC 5545 §3.7.3;
    // the X-WR-CALNAME extension is widely supported and gives subscribers a
    // sensible display name in Apple Calendar / Outlook.
    out.append("BEGIN:VCALENDAR\r\n");
    appendProp(out, QStringLiteral("VERSION"),  QStringLiteral("2.0"));
    appendProp(out, QStringLiteral("PRODID"),   QStringLiteral("-//OpenTodo//OpenTodo 1.0//EN"));
    appendProp(out, QStringLiteral("CALSCALE"), QStringLiteral("GREGORIAN"));
    appendProp(out, QStringLiteral("METHOD"),   QStringLiteral("PUBLISH"));
    appendProp(out, QStringLiteral("X-WR-CALNAME"), QStringLiteral("OpenTodo Tasks"));

    const QString dtstamp =
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));

    for (const Task *t : tasks) {
        if (!t || t->isDeleted()) continue;

        out.append("BEGIN:VTODO\r\n");

        // UID is stable across re-exports — subscribers update the same VTODO
        // in place rather than accumulating duplicates.
        appendProp(out, QStringLiteral("UID"),
                   t->id() + QStringLiteral("@opentodo"));
        appendProp(out, QStringLiteral("DTSTAMP"), dtstamp);

        // SUMMARY is the task title; required.
        appendProp(out, QStringLiteral("SUMMARY"), icsEscapeText(t->content()));

        // DESCRIPTION optional, but we may also prepend the human recurrence
        // string when we couldn't translate it to an RRULE.
        const DueObject due = t->due();
        const QString rrule = mapRecurrenceToRrule(due.recurrenceRule, due.string);
        QString description = t->description();
        if (due.isRecurring && rrule.isEmpty() && !due.string.isEmpty()) {
            const QString prefix = QStringLiteral("Recurrence: ") + due.string;
            description = description.isEmpty() ? prefix
                                                 : prefix + QLatin1Char('\n') + description;
        }
        if (!description.isEmpty()) {
            appendProp(out, QStringLiteral("DESCRIPTION"), icsEscapeText(description));
        }

        appendProp(out, QStringLiteral("PRIORITY"),
                   QString::number(icsPriority(t->priority())));
        appendProp(out, QStringLiteral("STATUS"),
                   t->isCompleted() ? QStringLiteral("COMPLETED")
                                    : QStringLiteral("NEEDS-ACTION"));

        // DUE: prefer datetime if present, else date-only with VALUE=DATE
        // parameter. Skip when neither is set — VTODO doesn't require a DUE.
        const QString dt = icsDateTimeFloating(due.datetime);
        if (!dt.isEmpty()) {
            appendProp(out, QStringLiteral("DUE"), dt);
        } else if (!due.date.isEmpty()) {
            appendPropRaw(out, QStringLiteral("DUE;VALUE=DATE"),
                          icsDateOnly(due.date));
        }

        if (!rrule.isEmpty()) {
            appendProp(out, QStringLiteral("RRULE"), rrule);
        }

        // COMPLETED — emitted in UTC per RFC 5545 §3.8.2.1.
        if (t->isCompleted() && t->completedAt().isValid()) {
            appendProp(out, QStringLiteral("COMPLETED"),
                       t->completedAt().toUTC()
                           .toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'")));
        }

        // CATEGORIES from labels (RFC 5545 supports comma-separated text).
        if (!t->labels().isEmpty()) {
            QStringList escaped;
            escaped.reserve(t->labels().size());
            for (const QString &l : t->labels()) escaped << icsEscapeText(l);
            appendProp(out, QStringLiteral("CATEGORIES"),
                       escaped.join(QLatin1Char(',')));
        }

        out.append("END:VTODO\r\n");
    }

    out.append("END:VCALENDAR\r\n");
    return QString::fromUtf8(out);
}

bool IcsExporter::exportToFile(const QList<Task *> &tasks, const QString &path)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = exportTasks(tasks).toUtf8();
    if (f.write(bytes) != bytes.size()) return false;
    return f.commit();
}

QString IcsExporter::exportToIcs(const QString &path)
{
    if (!m_store) {
        emit exportFailed(QStringLiteral("DataStore не инициализирован"));
        return {};
    }

    // Accept either a filesystem path or a file:// URL (QFileDialog default).
    QString localPath = path;
    if (localPath.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        localPath = QUrl(localPath).toLocalFile();
    }
    if (localPath.isEmpty()) {
        emit exportFailed(QStringLiteral("Пустой путь для экспорта"));
        return {};
    }

    if (!exportToFile(m_store->allTasks(), localPath)) {
        emit exportFailed(QStringLiteral("Не удалось записать ICS-файл: ") + localPath);
        return {};
    }
    emit exportSucceeded(localPath);
    return localPath;
}
