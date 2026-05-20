#pragma once

#include <QObject>
#include <QDate>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QQmlEngine>

class DataStore;
class ProjectViewModel;

class SidebarViewModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("SidebarViewModel is constructed in C++ and exposed via context property.")

    Q_PROPERTY(int inboxCount READ inboxCount NOTIFY countsChanged)
    Q_PROPERTY(int todayCount READ todayCount NOTIFY countsChanged)
    Q_PROPERTY(int upcomingCount READ upcomingCount NOTIFY countsChanged)
    Q_PROPERTY(QString currentView READ currentView WRITE setCurrentView NOTIFY currentViewChanged)
    Q_PROPERTY(QString currentItemId READ currentItemId WRITE setCurrentItemId NOTIFY currentItemIdChanged)
    Q_PROPERTY(QVariantList favorites READ favorites NOTIFY favoritesChanged)
    Q_PROPERTY(QVariantList labels READ labels NOTIFY labelsChanged)
    Q_PROPERTY(bool sidebarVisible READ sidebarVisible WRITE setSidebarVisible NOTIFY sidebarVisibleChanged)

public:
    explicit SidebarViewModel(DataStore *store, ProjectViewModel *projectVM, QObject *parent = nullptr);

    int inboxCount() const;
    int todayCount() const;
    int upcomingCount() const;

    QString currentView() const;
    void setCurrentView(const QString &view);

    QString currentItemId() const;
    void setCurrentItemId(const QString &id);

    QVariantList favorites() const;
    QVariantList labels() const;

    bool sidebarVisible() const;
    void setSidebarVisible(bool visible);

    Q_INVOKABLE void navigateTo(const QString &view, const QString &itemId = {});
    Q_INVOKABLE void toggleSidebar();
    Q_INVOKABLE void navigateToInbox();
    Q_INVOKABLE void navigateToToday();
    Q_INVOKABLE void navigateToUpcoming();
    Q_INVOKABLE void navigateToProject(const QString &projectId);
    Q_INVOKABLE void navigateToLabel(const QString &labelName);
    Q_INVOKABLE void navigateToFilter(const QString &filterId);

    Q_INVOKABLE QStringList labelOrder() const;
    Q_INVOKABLE void setLabelOrder(const QStringList &order);

    // Test hook: simulate a calendar-day rollover for testing the midnight
    // refresh path without freezing the system clock.  Flips
    // m_lastSeenDate to yesterday and re-runs the timer lambda so the
    // body of the rollover branch executes exactly as in production at
    // midnight.  Not exposed in QML for end users.
    Q_INVOKABLE void simulateMidnightRolloverForTesting();

    // Test helper: expose the midnight timer's interval so tests can
    // verify the timer is configured / running without poking the
    // implementation directly.
    Q_INVOKABLE int midnightCheckIntervalMsForTesting() const;
    Q_INVOKABLE bool midnightCheckTimerActiveForTesting() const;

signals:
    void countsChanged();
    void currentViewChanged();
    void currentItemIdChanged();
    void favoritesChanged();
    void labelsChanged();
    void labelOrderChanged();
    void sidebarVisibleChanged();
    void navigationRequested(const QString &view, const QString &itemId);

private slots:
    void onDataChanged();

private:
    void updateCounts();
    void onMidnightTimerTick();

    DataStore *m_store;
    ProjectViewModel *m_projectVM; // reserved for per-project task-count display
    int m_inboxCount = 0;
    int m_todayCount = 0;
    int m_upcomingCount = 0;
    QString m_currentView = QStringLiteral("inbox");
    QString m_currentItemId;
    bool m_sidebarVisible = true;

    // Midnight rollover guard: if the app stays open across midnight with
    // no data mutations, the "Сегодня" counter would otherwise reflect
    // yesterday's task set.  Tick once a minute and recompute when the
    // calendar date changes.
    QDate m_lastSeenDate;
    QTimer m_midnightCheckTimer;

    // Q3.24: debounce rapid tasksChanged bursts (e.g. bulk complete, sync import)
    // so updateCounts() runs at most once per 50 ms instead of once per task.
    QTimer m_countsDebounce;
};
