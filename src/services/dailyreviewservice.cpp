#include "dailyreviewservice.h"
#include "storage/datastore.h"
#include "services/notificationservice.h"
#include "model/task.h"
#include "utils/datetime.h"
#include <QDateTime>
#include <QSettings>
#include <climits>

DailyReviewService::DailyReviewService(DataStore *store,
                                       NotificationService *notif,
                                       QObject *parent)
    : QObject(parent), m_store(store), m_notif(notif)
{
    // Restore last-notified date from QSettings so a same-day app restart
    // doesn't double-fire.
    QSettings s;
    m_lastNotifiedDate = QDate::fromString(
        s.value(QStringLiteral("daily_review/last_notified")).toString(),
        Qt::ISODate);

    // Load the user-configured firing time (default 18:00).
    loadTimeFromSettings();

    connect(&m_timer, &QTimer::timeout, this, &DailyReviewService::checkAndNotify);
    m_timer.setSingleShot(true);
    rescheduleNextTrigger();
}

void DailyReviewService::loadTimeFromSettings()
{
    // Qt.labs.settings stores QML values under the "category" group, so the
    // key path matches what the QML Settings element writes:
    //   [productivity]
    //   dailyReviewHour=...
    //   dailyReviewMinute=...
    QSettings s;
    const int rawHour = s.value(QStringLiteral("productivity/dailyReviewHour"), 18).toInt();
    const int rawMin  = s.value(QStringLiteral("productivity/dailyReviewMinute"), 0).toInt();
    m_hour   = qBound(0, rawHour, 23);
    m_minute = qBound(0, rawMin,  59);
}

void DailyReviewService::rescheduleFromSettings()
{
    loadTimeFromSettings();
    rescheduleNextTrigger();
}

QDateTime DailyReviewService::nextTrigger() const
{
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime todayTarget(now.date(), QTime(m_hour, m_minute));
    if (now >= todayTarget)
        return todayTarget.addDays(1);
    return todayTarget;
}

void DailyReviewService::rescheduleNextTrigger()
{
    const QDateTime target = nextTrigger();
    const qint64 ms = QDateTime::currentDateTime().msecsTo(target);
    // Cap at INT_MAX (~24.8 days) — single-shot timer signature requires int.
    m_timer.setInterval(static_cast<int>(qBound<qint64>(qint64(1000), ms, qint64(INT_MAX))));
    m_timer.start();
}

void DailyReviewService::checkAndNotify()
{
    // 1. Setting must be enabled — read directly from DataStore
    //    (Infra layer doesn't depend on SettingsViewModel which lives in App).
    if (m_store) {
        const QVariant v = m_store->setting(QStringLiteral("daily_review_enabled"));
        // Default ON: missing key counts as true
        if (v.isValid() && !v.toBool()) {
            rescheduleNextTrigger();
            return;
        }
    }

    // 2. Don't double-fire on the same date
    const QDate today = QDate::currentDate();
    if (m_lastNotifiedDate == today) {
        rescheduleNextTrigger();
        return;
    }

    // 3. Count today's tasks — total (incl. completed) and remaining.
    //    We can't use DataStore::tasksForToday() because it filters out
    //    isCompleted() (datastore.cpp). Mirror its date logic here so the
    //    "Все задачи на сегодня выполнены" branch below is reachable when
    //    every today-task is done.
    int remaining = 0;
    int total = 0;
    const QString todayStr = DateTimeHelper::toDateString(today);
    for (const Task *t : m_store->allTasks()) {
        if (t->isDeleted()) continue;

        bool isToday = false;
        if (t->dueDate() == todayStr) {
            isToday = true;
        } else if (t->dueDate().isEmpty() && !t->dueDatetime().isEmpty()) {
            const QDateTime dt = DateTimeHelper::fromIsoString(t->dueDatetime());
            if (dt.isValid() && dt.date() == today) isToday = true;
        }
        if (!isToday) continue;

        total++;
        if (!t->isCompleted()) remaining++;
    }

    // 4. Skip if no tasks today (don't be annoying)
    if (total == 0) {
        rescheduleNextTrigger();
        return;
    }

    // 5. Compose body — Russian plural agreement
    QString body;
    if (remaining == 0) {
        body = QStringLiteral("Все задачи на сегодня выполнены — отличная работа!");
    } else {
        // Plural "задача / задачи / задач"
        const int m100 = remaining % 100;
        const int m10  = remaining % 10;
        QString word;
        if (m100 >= 11 && m100 <= 14)      word = QStringLiteral("задач");
        else if (m10 == 1)                 word = QStringLiteral("задача");
        else if (m10 >= 2 && m10 <= 4)    word = QStringLiteral("задачи");
        else                               word = QStringLiteral("задач");
        body = QStringLiteral("Сегодня осталось %1 %2 — самое время подвести итог.")
                   .arg(remaining).arg(word);
    }

    if (m_notif) {
        m_notif->showNotification(QStringLiteral("Итог дня"), body, QString());
    }

    m_lastNotifiedDate = today;
    QSettings s;
    s.setValue(QStringLiteral("daily_review/last_notified"), today.toString(Qt::ISODate));

    rescheduleNextTrigger();
}
