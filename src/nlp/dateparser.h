#pragma once

#include <QObject>
#include <QString>
#include <QDate>
#include <QTime>
#include <QDateTime>
#include <QQmlEngine>

struct DateParseResult {
    Q_GADGET
    Q_PROPERTY(QString date MEMBER date)
    Q_PROPERTY(QString datetime MEMBER datetime)
    Q_PROPERTY(bool isRecurring MEMBER isRecurring)
    Q_PROPERTY(QString recurrenceRule MEMBER recurrenceRule)
    Q_PROPERTY(QString humanString MEMBER humanString)
    Q_PROPERTY(bool valid MEMBER valid)
public:
    QString date;           // YYYY-MM-DD
    QString datetime;       // full ISO datetime if time was specified
    bool isRecurring = false;
    QString recurrenceRule; // internal recurrence descriptor
    QString humanString;    // human-readable string for display
    bool valid = false;
};

// Language-specific rule function type
using DateRuleFunc = DateParseResult(*)(const QString &input, const QDate &reference);

class DateParser : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit DateParser(QObject *parent = nullptr);

    // night iter-147: nextWeekDay (1=Mon..7=Sun, default Monday) wires the
    // previously-dead `next_week_day` setting — it selects which weekday
    // "next week" / "следующая неделя" resolves to. Default 1 preserves the
    // original next-Monday behaviour for every existing caller.
    Q_INVOKABLE DateParseResult parse(const QString &input, const QString &lang = QStringLiteral("en"), int nextWeekDay = 1);

    static DateParseResult parseEnglish(const QString &input, const QDate &reference = QDate::currentDate(), int nextWeekDay = 1);
    static DateParseResult parseRussian(const QString &input, const QDate &reference = QDate::currentDate(), int nextWeekDay = 1);

private:
    static DateParseResult tryParseTime(const QString &input, DateParseResult result);
    // First date >= reference whose weekday is listed in the rule's BYDAY
    // part; `reference` itself when the rule has no (parseable) BYDAY.
    static QDate firstBydayOnOrAfter(const QString &recurrenceRule, const QDate &reference);
    static QString formatDate(const QDate &date);
    static QString formatDateTime(const QDateTime &dt);
};
