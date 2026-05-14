#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QQmlEngine>

class DueObject {
    Q_GADGET
    Q_PROPERTY(QString date MEMBER date)
    Q_PROPERTY(QString datetime MEMBER datetime)
    Q_PROPERTY(QString timezone MEMBER timezone)
    Q_PROPERTY(QString string MEMBER string)
    Q_PROPERTY(bool isRecurring MEMBER isRecurring)
    Q_PROPERTY(QString lang MEMBER lang)
    Q_PROPERTY(QString recurrenceRule MEMBER recurrenceRule)
public:
    QString date;
    QString datetime;
    QString timezone;
    QString string;
    bool isRecurring = false;
    QString lang = QStringLiteral("en");
    // B-4.60 follow-up: RFC 5545-ish RRULE string (e.g. "FREQ=WEEKLY;BYDAY=MO,WE,FR").
    // Empty for non-recurring tasks or legacy data — RecurrenceEngine falls back
    // to humanString parsing when this is empty.
    QString recurrenceRule;

    QJsonObject toJson() const;
    static DueObject fromJson(const QJsonObject &json);
    bool isNull() const { return date.isEmpty() && datetime.isEmpty(); }
};

class DurationObject {
    Q_GADGET
    Q_PROPERTY(int amount MEMBER amount)
    Q_PROPERTY(QString unit MEMBER unit)
public:
    int amount = 0;
    QString unit;

    QJsonObject toJson() const;
    static DurationObject fromJson(const QJsonObject &json);
    bool isNull() const { return amount == 0; }
};

class Task : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString id READ id WRITE setId NOTIFY idChanged)
    Q_PROPERTY(QString content READ content WRITE setContent NOTIFY contentChanged)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY descriptionChanged)
    Q_PROPERTY(QString projectId READ projectId WRITE setProjectId NOTIFY projectIdChanged)
    Q_PROPERTY(QString sectionId READ sectionId WRITE setSectionId NOTIFY sectionIdChanged)
    Q_PROPERTY(QString parentId READ parentId WRITE setParentId NOTIFY parentIdChanged)
    Q_PROPERTY(int order READ order WRITE setOrder NOTIFY orderChanged)
    Q_PROPERTY(int priority READ priority WRITE setPriority NOTIFY priorityChanged)
    Q_PROPERTY(QStringList labels READ labels WRITE setLabels NOTIFY labelsChanged)
    Q_PROPERTY(bool isCompleted READ isCompleted WRITE setIsCompleted NOTIFY isCompletedChanged)
    Q_PROPERTY(QDateTime completedAt READ completedAt WRITE setCompletedAt NOTIFY completedAtChanged)
    Q_PROPERTY(QString dueDate READ dueDate WRITE setDueDate NOTIFY dueChanged)
    Q_PROPERTY(QString dueDatetime READ dueDatetime WRITE setDueDatetime NOTIFY dueChanged)
    Q_PROPERTY(QString dueTimezone READ dueTimezone WRITE setDueTimezone NOTIFY dueChanged)
    Q_PROPERTY(QString dueString READ dueString WRITE setDueString NOTIFY dueChanged)
    Q_PROPERTY(bool dueIsRecurring READ dueIsRecurring WRITE setDueIsRecurring NOTIFY dueChanged)
    Q_PROPERTY(QString dueLang READ dueLang WRITE setDueLang NOTIFY dueChanged)
    Q_PROPERTY(QString dueRecurrenceRule READ dueRecurrenceRule WRITE setDueRecurrenceRule NOTIFY dueChanged)
    Q_PROPERTY(QString deadline READ deadline WRITE setDeadline NOTIFY deadlineChanged)
    Q_PROPERTY(int durationAmount READ durationAmount WRITE setDurationAmount NOTIFY durationChanged)
    Q_PROPERTY(QString durationUnit READ durationUnit WRITE setDurationUnit NOTIFY durationChanged)
    Q_PROPERTY(QDateTime createdAt READ createdAt WRITE setCreatedAt NOTIFY createdAtChanged)
    Q_PROPERTY(QDateTime updatedAt READ updatedAt WRITE setUpdatedAt NOTIFY updatedAtChanged)
    Q_PROPERTY(bool isDeleted READ isDeleted WRITE setIsDeleted NOTIFY isDeletedChanged)
    Q_PROPERTY(int dayOrder READ dayOrder WRITE setDayOrder NOTIFY dayOrderChanged)
    Q_PROPERTY(QString color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(bool isPinned READ isPinned WRITE setIsPinned NOTIFY isPinnedChanged)
    Q_PROPERTY(int estimateMinutes READ estimateMinutes WRITE setEstimateMinutes NOTIFY estimateMinutesChanged)

public:
    explicit Task(QObject *parent = nullptr);

    QString id() const;
    void setId(const QString &id);

    QString content() const;
    void setContent(const QString &content);

    QString description() const;
    void setDescription(const QString &description);

    QString projectId() const;
    void setProjectId(const QString &projectId);

    QString sectionId() const;
    void setSectionId(const QString &sectionId);

    QString parentId() const;
    void setParentId(const QString &parentId);

    int order() const;
    void setOrder(int order);

    int priority() const;
    void setPriority(int priority);

    QStringList labels() const;
    void setLabels(const QStringList &labels);

    bool isCompleted() const;
    void setIsCompleted(bool completed);

    QDateTime completedAt() const;
    void setCompletedAt(const QDateTime &completedAt);

    DueObject due() const;
    void setDue(const DueObject &due);

    QString dueDate() const;
    void setDueDate(const QString &date);
    QString dueDatetime() const;
    void setDueDatetime(const QString &dt);
    QString dueTimezone() const;
    void setDueTimezone(const QString &tz);
    QString dueString() const;
    void setDueString(const QString &s);
    bool dueIsRecurring() const;
    void setDueIsRecurring(bool r);
    QString dueLang() const;
    void setDueLang(const QString &l);

    QString dueRecurrenceRule() const;
    void setDueRecurrenceRule(const QString &rrule);

    QString deadline() const;
    void setDeadline(const QString &deadline);

    int durationAmount() const;
    void setDurationAmount(int amount);
    QString durationUnit() const;
    void setDurationUnit(const QString &unit);

    DurationObject duration() const;
    void setDuration(const DurationObject &dur);

    QDateTime createdAt() const;
    void setCreatedAt(const QDateTime &createdAt);

    QDateTime updatedAt() const;
    void setUpdatedAt(const QDateTime &updatedAt);

    bool isDeleted() const;
    void setIsDeleted(bool deleted);

    int dayOrder() const;
    void setDayOrder(int dayOrder);

    QString color() const;            // hex string like "#db4c3f", "" = no color
    void setColor(const QString &c);

    bool isPinned() const;
    void setIsPinned(bool pinned);

    int estimateMinutes() const;
    void setEstimateMinutes(int minutes);

    Q_INVOKABLE QJsonObject toJson() const;
    static Task *fromJson(const QJsonObject &json, QObject *parent = nullptr);

signals:
    void idChanged();
    void contentChanged();
    void descriptionChanged();
    void projectIdChanged();
    void sectionIdChanged();
    void parentIdChanged();
    void orderChanged();
    void priorityChanged();
    void labelsChanged();
    void isCompletedChanged();
    void completedAtChanged();
    void dueChanged();
    void deadlineChanged();
    void durationChanged();
    void createdAtChanged();
    void updatedAtChanged();
    void isDeletedChanged();
    void dayOrderChanged();
    void colorChanged();
    void isPinnedChanged();
    void estimateMinutesChanged();

private:
    QString m_id;
    QString m_content;
    QString m_description;
    QString m_projectId;
    QString m_sectionId;
    QString m_parentId;
    int m_order = 0;
    int m_priority = 1;
    QStringList m_labels;
    bool m_isCompleted = false;
    QDateTime m_completedAt;
    DueObject m_due;
    QString m_deadline;
    DurationObject m_duration;
    QDateTime m_createdAt;
    QDateTime m_updatedAt;
    bool m_isDeleted = false;
    int m_dayOrder = 0;
    QString m_color;
    bool m_isPinned = false;
    int m_estimateMinutes = 0;
};
