#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QQmlEngine>

class LocationObject {
    Q_GADGET
    Q_PROPERTY(QString name MEMBER name)
    Q_PROPERTY(double latitude MEMBER latitude)
    Q_PROPERTY(double longitude MEMBER longitude)
    Q_PROPERTY(int radius MEMBER radius)
    Q_PROPERTY(QString trigger MEMBER trigger)
public:
    QString name;
    double latitude = 0.0;
    double longitude = 0.0;
    int radius = 100;
    QString trigger = QStringLiteral("on_enter");

    QJsonObject toJson() const;
    static LocationObject fromJson(const QJsonObject &json);
    bool isNull() const { return name.isEmpty() && latitude == 0.0 && longitude == 0.0; }
};

class ReminderDue {
    Q_GADGET
    Q_PROPERTY(QString date MEMBER date)
    Q_PROPERTY(QString datetime MEMBER datetime)
    Q_PROPERTY(QString timezone MEMBER timezone)
    Q_PROPERTY(QString lang MEMBER lang)
public:
    QString date;
    QString datetime;
    QString timezone;
    QString lang = QStringLiteral("en");

    QJsonObject toJson() const;
    static ReminderDue fromJson(const QJsonObject &json);
    bool isNull() const { return date.isEmpty() && datetime.isEmpty(); }
};

class Reminder : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString id READ id WRITE setId NOTIFY idChanged)
    Q_PROPERTY(QString taskId READ taskId WRITE setTaskId NOTIFY taskIdChanged)
    Q_PROPERTY(QString type READ type WRITE setType NOTIFY typeChanged)
    Q_PROPERTY(ReminderDue due READ due WRITE setDue NOTIFY dueChanged)
    Q_PROPERTY(int minuteOffset READ minuteOffset WRITE setMinuteOffset NOTIFY minuteOffsetChanged)
    Q_PROPERTY(LocationObject location READ location WRITE setLocation NOTIFY locationChanged)
    Q_PROPERTY(QDateTime createdAt READ createdAt WRITE setCreatedAt NOTIFY createdAtChanged)
    Q_PROPERTY(QDateTime updatedAt READ updatedAt WRITE setUpdatedAt NOTIFY updatedAtChanged)
    Q_PROPERTY(bool isDeleted READ isDeleted WRITE setIsDeleted NOTIFY isDeletedChanged)
    Q_PROPERTY(QString recurrenceRule READ recurrenceRule WRITE setRecurrenceRule NOTIFY recurrenceRuleChanged)

public:
    explicit Reminder(QObject *parent = nullptr);

    QString id() const;
    void setId(const QString &id);

    QString taskId() const;
    void setTaskId(const QString &taskId);

    QString type() const;
    void setType(const QString &type);

    QString recurrenceRule() const;
    void setRecurrenceRule(const QString &rule);

    ReminderDue due() const;
    void setDue(const ReminderDue &due);

    int minuteOffset() const;
    void setMinuteOffset(int offset);

    LocationObject location() const;
    void setLocation(const LocationObject &loc);

    QDateTime createdAt() const;
    void setCreatedAt(const QDateTime &dt);

    QDateTime updatedAt() const;
    void setUpdatedAt(const QDateTime &dt);

    bool isDeleted() const;
    void setIsDeleted(bool deleted);

    Q_INVOKABLE QJsonObject toJson() const;
    static Reminder *fromJson(const QJsonObject &json, QObject *parent = nullptr);

    // QML-callable convenience setters: ReminderDue / LocationObject are
    // Q_GADGETs and not directly constructable from QML, so we expose plain
    // primitives here. EditorRemindersSection.qml drives reminders through
    // these methods after the parent task calls dataStore.addReminder().
    Q_INVOKABLE void setDueDatetime(const QString &isoDatetime,
                                    const QString &timezone = QString(),
                                    const QString &lang = QString());
    Q_INVOKABLE void setRepeatRule(const QString &rule);
    Q_INVOKABLE void setLocationCoords(double lat, double lon, int radiusMeters,
                                       const QString &name = QString(),
                                       const QString &trigger = QString());

signals:
    void idChanged();
    void taskIdChanged();
    void typeChanged();
    void dueChanged();
    void minuteOffsetChanged();
    void locationChanged();
    void createdAtChanged();
    void updatedAtChanged();
    void isDeletedChanged();
    void recurrenceRuleChanged();

private:
    QString m_id;
    QString m_taskId;
    QString m_type = QStringLiteral("absolute");
    ReminderDue m_due;
    int m_minuteOffset = 0;
    LocationObject m_location;
    QDateTime m_createdAt;
    QDateTime m_updatedAt;
    bool m_isDeleted = false;
    QString m_recurrenceRule;
};
