// QApplication (not QGuiApplication) is required because:
//   * WindowsNotificationService uses QSystemTrayIcon (a QtWidgets type) —
//     constructing/showing it under a QGuiApplication crashes the process
//     on the first showMessage() call. The "Послать тестовое" diagnostic
//     button on the About settings page hits exactly that path.
//   * Several QtQuick.Dialogs implementations on Windows fall back to native
//     widget dialogs that also need QApplication.
// Linking Qt6::Widgets alone is not enough — the application object itself
// must be QApplication.
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QIcon>
#include <QTimer>
#include <QHash>
#include <QLocale>
#include <QTranslator>
#include <QLibraryInfo>
#include <QMetaEnum>
#include <memory>

#include "utils/settingskeys.h"

#include "storage/datastore.h"
#include "storage/filemanager.h"
#include "sync/syncengine.h"
#include "sync/gdrive.h"
#include "nlp/dateparser.h"
#include "filter/filterparser.h"
#include "filter/filtereval.h"

#include "viewmodels/taskviewmodel.h"
#include "viewmodels/projectviewmodel.h"
#include "viewmodels/sidebarviewmodel.h"
#include "viewmodels/filterviewmodel.h"
#include "viewmodels/searchviewmodel.h"
#include "viewmodels/settingsviewmodel.h"
#include "viewmodels/karmaviewmodel.h"
#include "viewmodels/syncviewmodel.h"
#include "viewmodels/notificationsviewmodel.h"

// Platform services
#include "services/notificationservice.h"
#include "services/hotkeyservice.h"
#include "services/reminderscheduler.h"
#include "services/windowchrome.h"
#include "services/undostack.h"
#include "services/aiassistant.h"
#include "services/updatechecker.h"
#include "services/crashlogger.h"
#include "services/telemetry.h"
#include "services/taskexporter.h"
#include "services/dailyreviewservice.h"
#include "services/pomodoroservice.h"

#include "import/todoistimporter.h"

#if defined(Q_OS_WASM)
#include "services/notificationservice_wasm.h"
#elif defined(Q_OS_WIN)
#include "services/notificationservice_win.h"
#elif defined(Q_OS_ANDROID)
#include "services/notificationservice_android.h"
#else
// Linux, macOS and any other platform: header-only stub (B-9.6)
#include "services/notificationservice_stub.h"
#endif

#ifdef HAS_POSITIONING
#include "services/locationservice.h"
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // CrashLogger must be installed before everything else so that startup
    // messages (translations, icon loading, VM construction) are captured.
    auto *crashLogger = new CrashLogger(&app);
    crashLogger->install();

    app.setApplicationName(QStringLiteral("OpenTodo"));
    app.setOrganizationName(QStringLiteral("OpenTodo"));
    app.setOrganizationDomain(QStringLiteral("opentodo.org"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    // Load Qt's bundled translations so standardButtons in dialogs (Cancel,
    // OK, Yes, No, etc.) appear in the user's locale. With a static Qt build
    // QLibraryInfo::TranslationsPath points to <exe>/translations rather than
    // the Qt install dir, so we probe several candidate paths.
    {
        const QLocale loc = QLocale::system();
        const QStringList trPaths = {
            QCoreApplication::applicationDirPath() + QStringLiteral("/translations"),
            QLibraryInfo::path(QLibraryInfo::TranslationsPath),
#ifdef OPENTODO_QT_TRANSLATIONS_DIR
            QStringLiteral(OPENTODO_QT_TRANSLATIONS_DIR),
#endif
        };
        QTranslator *qtTr = new QTranslator(&app);
        bool loaded = false;
        for (const QString &path : trPaths) {
            // Try qtbase_<lang> first (Qt 6 split-component name), then qt_<lang>.
            if (qtTr->load(loc, QStringLiteral("qtbase"), QStringLiteral("_"), path) ||
                qtTr->load(loc, QStringLiteral("qt"),     QStringLiteral("_"), path)) {
                loaded = true;
                break;
            }
        }
        if (loaded) {
            app.installTranslator(qtTr);
        } else {
            qWarning("OpenTodo: failed to load Qt translations for locale %s; tried %s",
                     qPrintable(loc.name()), qPrintable(trPaths.join(QStringLiteral(", "))));
        }
    }

    // Window icon (taskbar, alt-tab, title bar). Qt picks the right size
    // automatically because QIcon is initialised from multi-size PNGs +
    // the master SVG.
    QIcon appIcon;
    appIcon.addFile(QStringLiteral(":/icons/resources/icon.svg"));
    for (int size : {16, 32, 48, 64, 128, 256, 512})
        appIcon.addFile(QStringLiteral(":/icons/resources/icons/icon_%1.png").arg(size));
    app.setWindowIcon(appIcon);

    // Material Style
    QQuickStyle::setStyle(QStringLiteral("Material"));
#ifdef Q_OS_ANDROID
    qputenv("QT_QUICK_CONTROLS_MATERIAL_VARIANT", "Normal");
#else
    qputenv("QT_QUICK_CONTROLS_MATERIAL_VARIANT", "Dense");
#endif

    // Core services
    auto *dataStore = new DataStore(&app);
    auto *fileManager = new FileManager(dataStore, &app);
    auto *syncEngine = new SyncEngine(dataStore, &app);

    // Load data from disk
    fileManager->load();

    // Flush any pending in-memory changes when the app is quitting.
    // Without this the 1 s autosave debounce gets killed on shutdown and
    // every edit made in the last second before quit is silently lost —
    // this was the root cause of "data doesn't reach the disk" complaints
    // when users toggled a setting and closed the window quickly.
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     fileManager, [fileManager] {
        fileManager->saveImmediate();
    });

    // Platform services — notifications
    NotificationService *notificationService = nullptr;
#if defined(Q_OS_WASM)
    // WASM: log to browser console + fire signals for QML in-app banners.
    notificationService = new WasmNotificationService(&app);
#elif defined(Q_OS_WIN)
    notificationService = new WindowsNotificationService(&app);
#elif defined(Q_OS_ANDROID)
    notificationService = new AndroidNotificationService(&app);
#else
    // Linux, macOS: log to qDebug() + emit inAppBannerRequested (B-9.6)
    notificationService = new StubNotificationService(&app);
#endif

    // Reminder scheduler — bridges DataStore reminders to platform notifications.
    if (notificationService)
        new ReminderScheduler(dataStore, notificationService, &app);

    // Platform services — global hotkey (desktop only)
    auto *hotkeyService = new HotkeyService(&app);
#ifndef Q_OS_ANDROID
    // Surface registration failures via the in-app banner so the user knows
    // their global hotkey isn't actually live. The historical failure mode
    // was a silent qWarning("Назначенная клавиша уже зарегистрирована") —
    // the user pressed Ctrl+Alt+A, nothing happened, and they had no idea
    // another process held the binding. Q2.19: route the message through
    // NotificationService::inAppBannerRequested so the banner stack shows
    // it just like a reminder.
    if (notificationService) {
        QObject::connect(hotkeyService, &HotkeyService::registrationFailed,
                         notificationService,
            [notificationService](const QString &shortcut, const QString &reason) {
                Q_UNUSED(reason)
                const QString title = QStringLiteral("Горячая клавиша недоступна");
                const QString body  = QStringLiteral(
                    "Не удалось назначить %1 — она уже занята другой программой. "
                    "Измените сочетание в настройках.").arg(shortcut);
                // In-app banner is the right surface here: the QML stack
                // shows it inside the OpenTodo window without competing with
                // OS-level focus, and users on Linux/WASM (where there's no
                // tray toast) still see the warning. The QML side is
                // already wired up to NotificationService::inAppBannerRequested
                // (see qml/Main.qml).
                notificationService->showInAppBanner(title, body, QString());
            });
    }
    hotkeyService->registerQuickAdd();
#endif

    // Platform services — location (if Qt Positioning available)
#ifdef HAS_POSITIONING
    auto *locationService = new LocationService(&app);
#endif

    // Undo stack — shared service available to ViewModels and QML
    auto *undoStack = new UndoStack(&app);

    // ViewModels (all take DataStore* or engine pointers)
    auto *taskVM = new TaskViewModel(dataStore, &app);
    taskVM->setUndoStack(undoStack);
    auto *projectVM = new ProjectViewModel(dataStore, &app);
    auto *sidebarVM = new SidebarViewModel(dataStore, projectVM, &app);
    auto *filterVM = new FilterViewModel(dataStore, &app);
    auto *searchVM = new SearchViewModel(dataStore, &app);
    auto *settingsVM = new SettingsViewModel(dataStore, &app);
    auto *karmaVM = new KarmaViewModel(dataStore, &app);
    auto *syncVM = new SyncViewModel(syncEngine, &app);
    auto *notificationsVM = new NotificationsViewModel(dataStore, syncEngine, &app);

    // Wire cross-VM signals
    QObject::connect(sidebarVM, &SidebarViewModel::navigationRequested,
                     taskVM, [taskVM](const QString &view, const QString &itemId) {
        taskVM->setFilterMode(TaskViewModel::filterModeFromString(view));
        if (view == QStringLiteral("project"))
            taskVM->setCurrentProjectId(itemId);
        else if (view == QStringLiteral("label") || view == QStringLiteral("filter"))
            taskVM->setCurrentFilter(itemId);
    });

    // Karma: track task completion state transitions (complete + un-complete).
    // We keep a previous-state map so a re-save of an already-completed task
    // doesn't double-count, and un-completing correctly decrements the score.
    {
        // B-9.10: use shared_ptr so the lambda captures keep the map alive
        // without leaking — it is destroyed when the last lambda is destroyed.
        auto prevCompleted = std::make_shared<QHash<QString, bool>>();
        // Pre-seed with current state so boot doesn't fire spurious events.
        for (auto *t : dataStore->allTasks())
            prevCompleted->insert(t->id(), t->isCompleted());

        QObject::connect(taskVM, &TaskViewModel::taskChanged, karmaVM,
                         [karmaVM, dataStore, prevCompleted](const QString &id) {
            auto *task = dataStore->taskById(id);
            if (!task) return;
            const bool nowDone = task->isCompleted();
            const bool wasDone = prevCompleted->value(id, false);
            if (nowDone && !wasDone)
                karmaVM->onTaskCompleted();
            else if (!nowDone && wasDone)
                karmaVM->onTaskUncompleted();
            prevCompleted->insert(id, nowDone);
        });

        // Keep map tidy: remove entries for deleted tasks.
        QObject::connect(taskVM, &TaskViewModel::taskRemoved, karmaVM,
                         [prevCompleted](const QString &id) {
            prevCompleted->remove(id);
        });
    }

    // Karma → Notifications: surface milestone achievements in the notification centre.
    QObject::connect(karmaVM, &KarmaViewModel::achievementUnlocked,
                     notificationsVM, &NotificationsViewModel::onAchievementUnlocked);

    // ReminderScheduler → Notifications: log every fired reminder in the
    // in-app notification centre so the user can review past reminders even
    // after the OS toast disappears. Only wired when a NotificationService is
    // present (it's nullptr in unsupported configurations).
    if (notificationService) {
        QObject::connect(notificationService, &NotificationService::reminderFired,
                         notificationsVM, &NotificationsViewModel::onReminderFired);
    }

    // Restore the last view state from the previous session.
    //
    // Strategy: if LastFilterMode was saved, restore that mode (and the
    // associated project/filter ids, and the selected task). If nothing was
    // saved — or the saved filter mode string is unrecognisable — fall back
    // to the user's configured startPage, which is the old behaviour.
    //
    // The restore block runs after fileManager->load() (DataStore is populated)
    // and before engine.loadFromModule() (QML first render reflects the result).
    {
        const QString lastTaskId    = dataStore->setting(QLatin1String(SettingsKeys::LastViewedTaskId)).toString();
        const QString lastModeStr   = dataStore->setting(QLatin1String(SettingsKeys::LastFilterMode)).toString();
        const QString lastProjectId = dataStore->setting(QLatin1String(SettingsKeys::LastProjectId)).toString();
        const QString lastFilter    = dataStore->setting(QLatin1String(SettingsKeys::LastFilter)).toString();

        bool restored = false;
        if (!lastModeStr.isEmpty()) {
            const QMetaEnum me = QMetaEnum::fromType<TaskViewModel::FilterMode>();
            bool ok = false;
            const int v = me.keyToValue(qPrintable(lastModeStr), &ok);
            if (ok) {
                const auto mode = static_cast<TaskViewModel::FilterMode>(v);
                taskVM->setFilterMode(mode);
                if (!lastProjectId.isEmpty())
                    taskVM->setCurrentProjectId(lastProjectId);
                if (!lastFilter.isEmpty())
                    taskVM->setCurrentFilter(lastFilter);

                // Keep the sidebar selection in sync with the restored mode.
                const QString viewName = lastModeStr.toLower();
                sidebarVM->setCurrentView(viewName);
                if (!lastProjectId.isEmpty())
                    sidebarVM->setCurrentItemId(lastProjectId);
                else if (!lastFilter.isEmpty())
                    sidebarVM->setCurrentItemId(lastFilter);

                restored = true;
            }
        }

        if (!restored) {
            // Fallback: honour the user's explicit startPage setting.
            const QString sp = settingsVM->startPage();
            if (sp == QStringLiteral("inbox") ||
                sp == QStringLiteral("today") ||
                sp == QStringLiteral("upcoming")) {
                sidebarVM->setCurrentView(sp);
            }
        }

        // Restore the open task editor — only if the task still exists in
        // DataStore (it may have been deleted or synced away since last run).
        if (!lastTaskId.isEmpty()) {
            if (Task *t = dataStore->taskById(lastTaskId))
                taskVM->setSelectedTask(t);
            // If taskById returns nullptr the task is gone — silently skip,
            // selectedTask stays null and the editor panel stays closed.
        }
    }

    // Sync interval from settings — keep the timer in sync with the saved
    // value, AND start it once at boot. Without the boot kick, the first
    // sync would only happen if the user opened Settings and changed the
    // interval (which fires the signal). New users never saw anything
    // appear on Google Drive because the auto-sync timer never started.
    QObject::connect(settingsVM, &SettingsViewModel::syncIntervalChanged, syncVM,
                     [syncVM, settingsVM]() {
        syncVM->setSyncInterval(settingsVM->syncInterval());
    });
    {
        const int initialInterval = settingsVM->syncInterval();
        // B-9.34: respect 0 as "auto-sync disabled" — pass it through as-is.
        // The SyncViewModel treats 0 (or negative) as "stop the sync timer".
        // Only override when the setting has never been written (returns -1 or
        // some sentinel); a deliberate 0 from the user must be honoured.
        // If the saved value is negative (truly unset), default to 300 s.
        syncVM->setSyncInterval(initialInterval >= 0 ? initialInterval : 300);
    }

    // Push to Drive whenever data changes — debounced 5 s so a burst of
    // edits collapses into one upload. Without this the user had to
    // manually click "Синхронизировать" after every change.
    {
        auto *driveSyncTimer = new QTimer(&app);
        driveSyncTimer->setSingleShot(true);
        driveSyncTimer->setInterval(5000);
        // B-9.13: don't re-arm the drive-sync debounce while syncEngine is
        // actively pulling/applying a remote state — that would create a
        // feedback loop where an inbound sync triggers an immediate push
        // of the just-applied state.
        QObject::connect(dataStore, &DataStore::dataChanged, driveSyncTimer,
                         [driveSyncTimer, syncEngine]() {
            if (syncEngine->status() == SyncEngine::Syncing)
                return;
            driveSyncTimer->start();
        });
        QObject::connect(driveSyncTimer, &QTimer::timeout, syncVM, [syncVM] {
            // No-op when the user hasn't connected Drive — push() exits
            // with status=Offline.
            syncVM->push();
        });
    }

    // Force a flush to Drive on quit too, so changes made just before
    // closing aren't lost in the 5-second debounce.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, syncVM, [syncVM]() {
        syncVM->push();
    });

    // Persist current view state so the next launch can restore exactly where
    // the user left off. We save even when no task is open (empty string clears
    // the restored selection gracefully on next startup).
    QObject::connect(&app, &QCoreApplication::aboutToQuit, dataStore,
                     [dataStore, taskVM]() {
        // Task id — empty when no editor is open.
        QString lastTaskId;
        if (auto *t = qobject_cast<Task *>(taskVM->selectedTask()))
            lastTaskId = t->id();
        dataStore->setSetting(QLatin1String(SettingsKeys::LastViewedTaskId), lastTaskId);

        // Filter mode as the QMetaEnum key string (e.g. "Inbox", "Today", "Project").
        const QMetaEnum me = QMetaEnum::fromType<TaskViewModel::FilterMode>();
        const char *modeKey = me.valueToKey(static_cast<int>(taskVM->filterMode()));
        dataStore->setSetting(QLatin1String(SettingsKeys::LastFilterMode),
                              modeKey ? QString::fromUtf8(modeKey) : QString());

        // Auxiliary ids — only meaningful for Project/Board and Label/Filter modes
        // respectively; stored unconditionally and ignored on restore when empty.
        dataStore->setSetting(QLatin1String(SettingsKeys::LastProjectId),
                              taskVM->currentProjectId());
        dataStore->setSetting(QLatin1String(SettingsKeys::LastFilter),
                              taskVM->currentFilter());
    });

    // AI Assistant
    auto *aiAssistant = new AIAssistant(dataStore, &app);

    // Todoist importer
    auto *todoistImporter = new TodoistImporter(dataStore, &app);

    // Auto-updater
    auto *updateChecker = new UpdateChecker(&app);

    // Task exporter (CSV / Markdown)
    auto *taskExporter = new TaskExporter(dataStore, &app);

    // Daily review reminder — fires at 18:00 local time if the user has any tasks today.
    // Reads daily_review_enabled setting directly from DataStore (no SettingsVM dep).
    new DailyReviewService(dataStore, notificationService, &app);

    // Pomodoro timer — classic 25/5 min work/break cycles with notifications.
    auto *pomodoroService = new PomodoroService(notificationService, &app);

    // Telemetry (opt-in; default disabled — nothing is sent until user explicitly consents)
    auto *telemetry = new Telemetry(&app);

    // Wire telemetry into the VMs that emit trackEvent calls. Each VM checks
    // m_telemetry for null before emitting, so this is safe to call exactly once.
    taskVM->setTelemetry(telemetry);
    syncVM->setTelemetry(telemetry);
    projectVM->setTelemetry(telemetry);
    filterVM->setTelemetry(telemetry);

    // NotificationService: route notificationClicked(taskId) → open the task editor.
    // QML handles raising the window in its Connections block; the C++ side
    // only needs to bridge the signal to TaskViewModel.
    if (notificationService) {
        QObject::connect(notificationService, &NotificationService::notificationClicked,
                         taskVM, [taskVM](const QString &taskId) {
            taskVM->openTaskById(taskId);
        });
        // Record that the user has responded to the notification permission prompt,
        // regardless of whether they granted or denied it, so we don't re-prompt.
        QObject::connect(notificationService, &NotificationService::permissionGranted,
                         settingsVM, [settingsVM]() {
            settingsVM->setNotificationPermissionAsked(true);
        });
        QObject::connect(notificationService, &NotificationService::permissionDenied,
                         settingsVM, [settingsVM]() {
            settingsVM->setNotificationPermissionAsked(true);
        });
    }

#ifdef HAS_POSITIONING
    // LocationService: start polling and register any pre-loaded geofence reminders.
    // DataStore has no allReminders(); walk every task's reminders instead.
    locationService->start();

    auto reloadGeofences = [locationService, dataStore]() {
        locationService->clearGeofences();
        for (const auto *task : dataStore->allTasks()) {
            for (auto *r : dataStore->remindersForTask(task->id())) {
                if (r->isDeleted()) continue;
                if (r->type() != QStringLiteral("location")) continue;
                const auto loc = r->location();
                if (loc.isNull()) continue;
                locationService->addGeofence(r->id(), loc.latitude, loc.longitude,
                                              static_cast<double>(loc.radius));
            }
        }
    };
    reloadGeofences();
    QObject::connect(dataStore, &DataStore::remindersChanged, locationService, reloadGeofences);

    // Fire notification when user enters a geofenced area.
    QObject::connect(locationService, &LocationService::enteredGeofence,
                     locationService, [dataStore, notificationService](const QString &reminderId) {
        auto *reminder = dataStore->reminderById(reminderId);
        if (!reminder) return;
        auto *task = dataStore->taskById(reminder->taskId());
        const QString title = task ? task->content() : QStringLiteral("Напоминание");
        if (notificationService)
            notificationService->showNotification(title, QStringLiteral("Вы рядом!"), reminder->taskId());
    });

    // F-9.20: also fire notification when the user leaves a geofenced area,
    // for reminders that have a "on_leave" location trigger.
    QObject::connect(locationService, &LocationService::leftGeofence,
                     locationService, [dataStore, notificationService](const QString &reminderId) {
        auto *reminder = dataStore->reminderById(reminderId);
        if (!reminder) return;
        if (reminder->type() != QStringLiteral("location")) return;
        // Only trigger on-leave reminders when explicitly configured via LocationObject::trigger.
        if (reminder->location().trigger != QStringLiteral("on_leave")) return;
        auto *task = dataStore->taskById(reminder->taskId());
        const QString title = task ? task->content() : QStringLiteral("Напоминание");
        if (notificationService)
            notificationService->showNotification(title, QStringLiteral("Вы покинули место!"), reminder->taskId());
    });
#endif

    // Hotkey: global Quick Add shortcut opens the quick-add bar.
    // QML connects directly to hotkeyService.quickAddRequested (hotkeyService is
    // registered as a context property below) — no C++ relay needed.

    // QML engine
    QQmlApplicationEngine engine;

    // Add the deployed Qt.labs.* plugins directory to the QML import path.
    // CMake's POST_BUILD step copies qmlsettingsplugin.dll + qmldir into
    // <exe>/qt/qml/Qt/labs/settings/ — but Qt only finds them if the engine
    // import path includes <exe>/qt/qml/. Without this addImportPath call the
    // runtime emits "module Qt.labs.settings is not installed" and any QML
    // file that imports it fails to load (DesktopLayout black-screen).
    engine.addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qt/qml"));

    // Register all as context properties (accessible from QML)
    auto *ctx = engine.rootContext();
    ctx->setContextProperty(QStringLiteral("dataStore"), dataStore);
    ctx->setContextProperty(QStringLiteral("fileManager"), fileManager);
    ctx->setContextProperty(QStringLiteral("syncEngine"), syncEngine);
    ctx->setContextProperty(QStringLiteral("gdriveClient"), syncEngine->gdriveClient());
    ctx->setContextProperty(QStringLiteral("taskViewModel"), taskVM);
    ctx->setContextProperty(QStringLiteral("projectViewModel"), projectVM);
    ctx->setContextProperty(QStringLiteral("sidebarViewModel"), sidebarVM);
    ctx->setContextProperty(QStringLiteral("filterViewModel"), filterVM);
    ctx->setContextProperty(QStringLiteral("searchViewModel"), searchVM);
    ctx->setContextProperty(QStringLiteral("settingsViewModel"), settingsVM);
    ctx->setContextProperty(QStringLiteral("karmaViewModel"), karmaVM);
    ctx->setContextProperty(QStringLiteral("syncViewModel"), syncVM);
    ctx->setContextProperty(QStringLiteral("notificationsViewModel"), notificationsVM);
    ctx->setContextProperty(QStringLiteral("undoStack"), undoStack);
    ctx->setContextProperty(QStringLiteral("aiAssistant"), aiAssistant);
    ctx->setContextProperty(QStringLiteral("updateChecker"), updateChecker);
    ctx->setContextProperty(QStringLiteral("todoistImporter"), todoistImporter);
    ctx->setContextProperty(QStringLiteral("taskExporter"), taskExporter);
    ctx->setContextProperty(QStringLiteral("telemetry"), telemetry);
    ctx->setContextProperty(QStringLiteral("pomodoroService"), pomodoroService);
    ctx->setContextProperty(QStringLiteral("crashLogger"), crashLogger);

    // Platform services exposed to QML
    if (notificationService)
        ctx->setContextProperty(QStringLiteral("notificationService"), notificationService);
    ctx->setContextProperty(QStringLiteral("hotkeyService"), hotkeyService);
    ctx->setContextProperty(QStringLiteral("WindowChrome"), new WindowChrome(&app));
#ifdef HAS_POSITIONING
    ctx->setContextProperty(QStringLiteral("locationService"), locationService);
#endif

    // F-9.15: TaskViewModel has QML_ELEMENT + Q_ENUM(FilterMode) in its header,
    // so qt_add_qml_module already registers it and exposes FilterMode to QML.
    // The explicit qmlRegisterUncreatableType call below was a double-registration
    // that caused Qt 6 warnings; it has been removed.
    //
    // AIAssistant is similarly exposed via QML_UNCREATABLE in its header.
    // If a Wave-2 agent removes that annotation, restore the call here.

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("OpenTodoUI", "Main");

    return app.exec();
}
