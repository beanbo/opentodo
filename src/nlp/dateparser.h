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

    Q_INVOKABLE DateParseResult parse(const QString &input, const QString &lang = QStringLiteral("en"));

    static DateParseResult parseEnglish(const QString &input, const QDate &reference = QDate::currentDate());
    static DateParseResult parseRussian(const QString &input, const QDate &reference = QDate::currentDate());

private:
    static DateParseResult tryParseTime(const QString &input, DateParseResult result);
    static QString formatDate(const QDate &date);
    static QString formatDateTime(const QDateTime &dt);
};
