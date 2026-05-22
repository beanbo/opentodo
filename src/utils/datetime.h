#pragma once

#include <QString>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <QTimeZone>

namespace DateTimeHelper {

inline QString toIsoString(const QDateTime &dt)
{
    return dt.toUTC().toString(Qt::ISODateWithMs);
}

inline QDateTime fromIsoString(const QString &s)
{
    return QDateTime::fromString(s, Qt::ISODateWithMs);
}

inline QString toDateString(const QDate &date)
{
    return date.toString(QStringLiteral("yyyy-MM-dd"));
}

inline QDate fromDateString(const QString &s)
{
    return QDate::fromString(s, QStringLiteral("yyyy-MM-dd"));
}

inline QString toTimeString(const QTime &time)
{
    return time.toString(QStringLiteral("HH:mm"));
}

inline QTime fromTimeString(const QString &s)
{
    return QTime::fromString(s, QStringLiteral("HH:mm"));
}

inline QDateTime nowUtc()
{
    return QDateTime::currentDateTimeUtc();
}

inline QDate today()
{
    return QDate::currentDate();
}

inline QDate tomorrow()
{
    return QDate::currentDate().addDays(1);
}

inline bool isOverdue(const QString &dateStr)
{
    if (dateStr.isEmpty()) return false;
    const QDate d = fromDateString(dateStr);
    return d.isValid() && d < QDate::currentDate();
}

// B-1.28: overload that accepts both a date string and a datetime string.
// If dateStr is set it takes precedence (matching OpenTodo semantics).
// If only datetimeStr is set, compares against the current moment.
inline bool isOverdue(const QString &dateStr, const QString &datetimeStr)
{
    if (!dateStr.isEmpty())
        return isOverdue(dateStr);
    if (!datetimeStr.isEmpty()) {
        const QDateTime dt = fromIsoString(datetimeStr);
        return dt.isValid() && dt < QDateTime::currentDateTime();
    }
    return false;
}

inline bool isToday(const QString &dateStr)
{
    if (dateStr.isEmpty()) return false;
    return fromDateString(dateStr) == QDate::currentDate();
}

inline QString formatRelativeDate(const QDate &date)
{
    const QDate now = QDate::currentDate();
    const qint64 days = now.daysTo(date);

    if (days == 0) return QStringLiteral("Today");
    if (days == 1) return QStringLiteral("Tomorrow");
    if (days == -1) return QStringLiteral("Yesterday");
    if (days > 1 && days <= 7) return date.toString(QStringLiteral("dddd"));
    return date.toString(QStringLiteral("MMM d"));
}

} // namespace DateTimeHelper
