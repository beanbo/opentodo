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
#include <QFont>
#include <QFontDatabase>
#include <QTimer>
#include <QHash>
#include <QLocale>
#include <QSettings>
#include <QTranslator>
#include <QLibraryInfo>
#include <QMetaEnum>
#include <QWindow>
#include <memory>

#include "utils/settingskeys.h"

#include "storage/datastore.h"
#include "storage/filemanager.h"
#include "sync/syncengine.h"
#include "sync/gdrive.h"
#include "sync/sharemanager.h"
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
#if !defined(Q_OS_ANDROID) && !defined(__EMSCRIPTEN__)
#include "services/trayicon.h"
#endif
#include "services/reminderscheduler.h"
#include "services/windowchrome.h"
#include "services/undostack.h"
#include "services/aiassistant.h"
#include "services/secretstore.h"
#include "services/updatechecker.h"
#include "services/crashlogger.h"
#include "services/telemetry.h"
#include "services/taskexporter.h"
#include "services/icsexporter.h"
#include "services/dailyreviewservice.h"
#include "services/pomodoroservice.h"
#include "services/claudecodelauncher.h"
#include "services/tintedsvgimageprovider.h"
#include "app/clibridge.h"

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

    // Q4.01 pixel-perfect: the design canvas
    // (.design-temp/opentodo/project/OpenTodo Redesign.html) uses Inter as the
    // primary type face. Try the system-installed Inter first (Microsoft Edge
    // ships it on Windows 11, many Linux distros pull it in via webfonts
    // packages, and modern macOS users frequently install it from Google
    // Fonts). If the family is not available, QFont silently falls back to
    // the platform's default sans-serif (Segoe UI on Windows, San Francisco
    // on macOS, the configured default on Linux) — Qt never aborts on a
    // missing family, so this is safe at every install.
    //
    // QFont::setStyleHint(SansSerif) makes the fallback explicit rather than
    // relying on Qt's generic family matcher, and matches the design's
    // `font-family: 'Inter', -apple-system, system-ui, sans-serif;` cascade.
    {
        const bool interAvailable =
            QFontDatabase::families().contains(QStringLiteral("Inter"),
                                               Qt::CaseInsensitive);
        QFont appFont(interAvailable ? QStringLiteral("Inter")
                                     : QGuiApplication::font().family());
        appFont.setStyleHint(QFont::SansSerif);
        // Pixel size 13 mirrors the design canvas body baseline. Individual
        // QML components that set their own pixelSize keep overriding this
        // (they read ThemeManager / hard-coded values), so this only affects
        // controls that inherit the application font (most QtQuick.Controls
        // text rendering).
        appFont.setPixelSize(13);
        QGuiApplication::setFont(appFont);
        if (!interAvailable) {
            qInfo("OpenTodo: 'Inter' font not installed; using platform default '%s' "
                  "as the application font. Install Inter from "
                  "https://fonts.google.com/specimen/Inter for design-canvas parity.",
                  qPrintable(appFont.family()));
        }
    }

    // Load translations. We have two layers:
    //   1. Qt's own translations (qtbase_<lang>.qm) — fills in standardButtons
    //      in dialogs ("Cancel", "OK", "Yes", "No", …).
    //   2. OpenTodo's own translations (opentodo_<lang>.qm) — every UI string
    //      we have wrapped in qsTr() / tr().
    //
    // The active locale is resolved as:
    //   * settings/language if the user has explicitly picked one (one of
    //     "ru", "en", "de", "uk", "be") — even before SettingsViewModel is
    //     constructed, because QSettings is process-wide here.
    //   * Otherwise QLocale::system() — but only the *language* part: a user
    //     in en_US still gets en.qm, not en_US.qm (which we don't ship).
    {
        // Read settings/language directly from QSettings. We cannot use the
        // SettingsViewModel here — it isn't constructed yet, and we need the
        // translators in place before any QML loads.
        const QString cfgLang = []() -> QString {
            QSettings s;
            const QString v = s.value(QStringLiteral("language")).toString();
            return v;
        }();
        const QStringList supported = {
            QStringLiteral("ru"), QStringLiteral("en"),
            QStringLiteral("de"), QStringLiteral("uk"),
            QStringLiteral("be"),
        };

        QString lang = cfgLang;
        if (lang.isEmpty() || !supported.contains(lang)) {
            const QString sysLang = QLocale::system().name().section(QChar('_'), 0, 0);
            lang = supported.contains(sysLang) ? sysLang : QStringLiteral("en");
        }
        const QLocale loc(lang);

        // (1) Qt's bundled translations
        const QStringList trPaths = {
            QCoreApplication::applicationDirPath() + QStringLiteral("/translations"),
            QLibraryInfo::path(QLibraryInfo::TranslationsPath),
#ifdef OPENTODO_QT_TRANSLATIONS_DIR
            QStringLiteral(OPENTODO_QT_TRANSLATIONS_DIR),
#endif
        };
        QTranslator *qtTr = new QTranslator(&app);
        bool qtLoaded = false;
        for (const QString &path : trPaths) {
            if (qtTr->load(loc, QStringLiteral("qtbase"), QStringLiteral("_"), path) ||
                qtTr->load(loc, QStringLiteral("qt"),     QStringLiteral("_"), path)) {
                qtLoaded = true;
                break;
            }
        }
        if (qtLoaded) {
            app.installTranslator(qtTr);
        } else {
            // Demoted from qWarning to qInfo (Q3.07-followup): Qt does not
            // bundle qtbase_be.qm for Belarusian (and historically also
            // missing for some Ukrainian builds). Our own opentodo_<lang>.qm
            // still loads via the resource prefix below, so this only means
            // standard-dialog button labels (OK/Cancel/...) fall back to
            // English. Not an error worth a warning-level log line.
            qInfo("OpenTodo: Qt standard translations not bundled for locale %s; using English fallback for built-in dialogs",
                  qPrintable(loc.name()));
            delete qtTr;
        }

        // (2) OpenTodo's own translations. The .qm files are embedded under
        // the :/i18n/ resource prefix by qt_add_lrelease + qt_add_resources
        // in CMakeLists.txt. Russian is the *source* language; loading
        // opentodo_ru is therefore a no-op as far as message lookup goes,
        // but we still install it to keep the active-translator state
        // consistent (and to surface the .qm being missing as a hard error
        // during development).
        QTranslator *appTr = new QTranslator(&app);
        const QString appQmRes = QStringLiteral(":/i18n/opentodo_%1.qm").arg(lang);
        if (appTr->load(appQmRes)) {
            app.installTranslator(appTr);
        } else {
            qWarning("OpenTodo: failed to load app translations from %s",
                     qPrintable(appQmRes));
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

    // ── Sync scheduling — gated on syncMode ─────────────────────────────────
    //
    // Three modes (stored in settings as "sync_mode"):
    //   "after_action" (default) — 5 s debounce after every dataChanged;
    //                              periodic timer is disabled (interval = 0).
    //   "interval"               — periodic timer per syncInterval seconds;
    //                              debounce is disabled.
    //   "manual"                 — no automatic sync at all.
    //
    // Migration: existing users have only syncInterval set (no syncMode).
    // SettingsViewModel::syncMode() returns "after_action" for them, so their
    // experience is unchanged — 5 s debounce still fires, periodic timer stops.

    // 5-second debounced after-action push. Only armed when syncMode ==
    // "after_action". The connection to dataChanged is always live; we check
    // the mode at the moment the timer fires so mode changes take effect
    // immediately without disconnecting/reconnecting signals.
    auto *driveSyncTimer = new QTimer(&app);
    driveSyncTimer->setSingleShot(true);
    driveSyncTimer->setInterval(5000);

    // B-9.13: don't re-arm the drive-sync debounce while syncEngine is
    // actively pulling/applying a remote state — that would create a
    // feedback loop where an inbound sync triggers an immediate push
    // of the just-applied state.
    QObject::connect(dataStore, &DataStore::dataChanged, driveSyncTimer,
                     [driveSyncTimer, syncEngine, settingsVM]() {
        if (settingsVM->syncMode() != QStringLiteral("after_action"))
            return;
        if (syncEngine->status() == SyncEngine::Syncing)
            return;
        driveSyncTimer->start();
    });
    QObject::connect(driveSyncTimer, &QTimer::timeout, syncVM, [syncVM] {
        // No-op when the user hasn't connected Drive — push() exits
        // with status=Offline.
        syncVM->push();
    });

    // Helper: apply the current mode + interval to the SyncViewModel periodic timer.
    // Called at startup and whenever syncMode or syncInterval changes.
    auto applyIntervalTimer = [syncVM, settingsVM]() {
        const QString mode = settingsVM->syncMode();
        if (mode == QStringLiteral("interval")) {
            // Use the saved interval; fall back to 300 s if never set.
            const int secs = settingsVM->syncInterval();
            syncVM->setSyncInterval(secs >= 0 ? secs : 300);
        } else {
            // "after_action" or "manual": disable the periodic timer.
            syncVM->setSyncInterval(0);
        }
    };

    // Keep the periodic timer in sync when the user changes syncMode or syncInterval.
    QObject::connect(settingsVM, &SettingsViewModel::syncModeChanged,
                     &app, applyIntervalTimer);
    QObject::connect(settingsVM, &SettingsViewModel::syncIntervalChanged,
                     &app, applyIntervalTimer);

    // Boot kick — apply once at startup so neither a fresh install nor an
    // existing user requires the Settings screen to be opened first.
    applyIntervalTimer();

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

    // Sec-1: SecretStore — backs the Claude API key with the OS keyring
    // (Windows Credential Manager / macOS Keychain / libsecret / kwallet via
    // QtKeychain). On Android/WASM falls back to QSettings under "secrets/"
    // since no real OS keyring is available there. SecretStore is created
    // before AIAssistant so the migration of any pre-existing plaintext key
    // happens on the first setSecretStore() wiring below.
    auto *secretStore = new SecretStore(&app);

    // AI Assistant
    auto *aiAssistant = new AIAssistant(dataStore, &app);
    // Sec-1: wire the secret store. This also runs the one-time migration:
    // any XOR-obfuscated key still living under "ai/k" in QSettings is
    // decoded, pushed into the SecretStore, and removed from QSettings.
    aiAssistant->setSecretStore(secretStore);

    // Todoist importer
    auto *todoistImporter = new TodoistImporter(dataStore, &app);

    // Auto-updater
    auto *updateChecker = new UpdateChecker(&app);

    // Task exporter (CSV / Markdown)
    auto *taskExporter = new TaskExporter(dataStore, &app);

    // Share manager — publishes project / task snapshots to Google Drive
    // as anonymous-reader public links. Uses the existing GDriveClient
    // (drive.file scope already grants permission to share files this app
    // created).
    auto *shareManager = new ShareManager(dataStore, syncEngine->gdriveClient(), &app);

    // iCalendar (.ics) exporter — lets users subscribe their tasks from
    // Apple Calendar / Google Calendar / Outlook by saving the file and
    // either importing it once or hosting it for "subscribe to URL" use.
    auto *icsExporter = new IcsExporter(dataStore, &app);

    // Daily review reminder — fires at 18:00 local time if the user has any tasks today.
    // Reads daily_review_enabled setting directly from DataStore (no SettingsVM dep).
    new DailyReviewService(dataStore, notificationService, &app);

    // Pomodoro timer — classic 25/5 min work/break cycles with notifications.
    auto *pomodoroService = new PomodoroService(notificationService, &app);

    // Claude Code launcher — spawns the claude CLI in a detached terminal
    // rooted at the project's linkedRepoPath. Exposed to QML so task context
    // menus can trigger it directly.
    auto *claudeCodeLauncher = new ClaudeCodeLauncher(&app);

    // CLI bridge — lets the `opentodo` command-line tool drive THIS running
    // instance over a local socket, so changes from the CLI (and from Claude
    // Code running in a linked repo) flow through the real ViewModel path:
    // karma, the 1-second autosave, and Drive-sync fire exactly as if the user
    // had acted in the UI. Parented to `app`; no QML exposure needed.
    new CliBridge(dataStore, taskVM, &app);

    // Flush the in-flight session to disk on clean shutdown so a paused-then-
    // closed session restores at its exact countdown. The state-mutation paths
    // (start/pause/resume/skip/reset/enterPhase) already persist, so this is
    // belt-and-braces for the "user closed window while running" path — the
    // anchor (started_at) is already on disk; this just refreshes updated_at.
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     pomodoroService, [pomodoroService] {
        pomodoroService->flushStateToDisk();
    });

    // Telemetry (opt-in; default disabled — nothing is sent until user explicitly consents)
    auto *telemetry = new Telemetry(&app);

    // Wire telemetry into the VMs that emit trackEvent calls. Each VM checks
    // m_telemetry for null before emitting, so this is safe to call exactly once.
    taskVM->setTelemetry(telemetry);
    syncVM->setTelemetry(telemetry);
    projectVM->setTelemetry(telemetry);
    filterVM->setTelemetry(telemetry);
    // Sync layer also emits telemetry for reliability events (corrupted
    // commits, dangling HEAD, tombstone-absorbed edits) — propagate through
    // the engine, which forwards to Merger as well.
    syncEngine->setTelemetry(telemetry);

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

    // Register the SVG-tinting image provider so that QML's IconSvg.qml can
    // request runtime-coloured icons via image://svg-tinted/<name>?color=<rgb>.
    // Ownership transfers to the engine.
    // Why this exists: Qt's QSvgRenderer renders stroke="currentColor" as
    // black (no CSS context), and QtQuick.Effects.MultiEffect's colorization
    // preserves luminance — so black stays black no matter the
    // colorizationColor. We replace currentColor in the SVG text at fetch
    // time, which produces correctly-tinted icons with zero shader plumbing.
    // See src/services/tintedsvgimageprovider.{h,cpp} and qml/components/IconSvg.qml.
    engine.addImageProvider(QStringLiteral("svg-tinted"),
                            new TintedSvgImageProvider());

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
    // Sec-1: SecretStore exposed for future direct-from-QML secret access
    // (currently unused — AIAssistant handles its own key internally).
    ctx->setContextProperty(QStringLiteral("secretStore"), secretStore);
    ctx->setContextProperty(QStringLiteral("updateChecker"), updateChecker);
    ctx->setContextProperty(QStringLiteral("todoistImporter"), todoistImporter);
    ctx->setContextProperty(QStringLiteral("taskExporter"), taskExporter);
    ctx->setContextProperty(QStringLiteral("shareManager"), shareManager);
    ctx->setContextProperty(QStringLiteral("icsExporter"), icsExporter);
    ctx->setContextProperty(QStringLiteral("telemetry"), telemetry);
    ctx->setContextProperty(QStringLiteral("pomodoroService"), pomodoroService);
    ctx->setContextProperty(QStringLiteral("crashLogger"), crashLogger);
    ctx->setContextProperty(QStringLiteral("claudeCodeLauncher"), claudeCodeLauncher);

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

#if !defined(Q_OS_ANDROID) && !defined(__EMSCRIPTEN__)
    // ── System tray icon (desktop only) ─────────────────────────────────────
    // The tray icon offers Open / Quick Add / Settings / Quit and survives
    // the main window being hidden. It is constructed after engine.loadFromModule
    // so that engine.rootObjects() already contains the QML ApplicationWindow,
    // which the "Open" / DoubleClick handlers reach into via raise +
    // requestActivate.
    //
    // Visibility is gated on settingsViewModel.showTrayIcon (default true);
    // a toggle from the settings UI shows/hides the icon in real time without
    // restart.
    auto *trayIcon = new TrayIconService(&app);

    if (trayIcon->isAvailable()) {
        // Initial visibility honours the user's saved preference (default on).
        trayIcon->setVisible(settingsVM->showTrayIcon());

#ifdef Q_OS_WIN
        // Hand THIS single tray icon to the Windows notification service so its
        // toast balloons anchor here instead of registering a second icon in
        // the notification area (the "two tray icons" bug). qobject_cast safely
        // returns nullptr if the platform service isn't the Windows one. Done
        // before app.exec() — no notification can fire until the event loop
        // runs, so the shared icon is always in place by the first toast.
        if (auto *winNotif =
                qobject_cast<WindowsNotificationService *>(notificationService)) {
            winNotif->setSharedTrayIcon(trayIcon->systemTrayIcon());
        }
#endif

        // Reflect the live tray-icon toggle (Settings UI) without restart.
        QObject::connect(settingsVM, &SettingsViewModel::showTrayIconChanged,
                         trayIcon, [trayIcon, settingsVM]() {
            trayIcon->setVisible(settingsVM->showTrayIcon());
        });

        // openWindowRequested + settingsRequested are now handled QML-side
        // (Main.qml has a Connections block listening to `trayIcon`). The
        // earlier C++ lambda directly poked `engine.rootObjects().first()` →
        // QWindow::showNormal/raise/requestActivate and could crash when the
        // ApplicationWindow had been hidden/destroyed by the user closing
        // the X button while the tray icon was still live. Routing via QML
        // lets the engine resolve `root` through its normal binding system
        // instead of an out-of-band cast, which Qt handles gracefully even
        // when the window is in transient states.

        // Quick Add from the tray re-uses the existing hotkey wiring: emit
        // the SAME signal that the Win32 RegisterHotKey path emits, so the
        // QML side (qml/Main.qml) runs its raise + focusInput handler
        // unchanged. No separate code path means no risk of the two entry
        // points drifting apart (Quick Add modal opening differently from
        // menu vs hotkey was the kind of bug we want to make structurally
        // impossible).
        QObject::connect(trayIcon, &TrayIconService::quickAddRequested,
                         hotkeyService, [hotkeyService]() {
            emit hotkeyService->quickAddRequested();
        });

        // settingsRequested: navigation only — the raise/activate part is
        // handled QML-side (Main.qml's Connections block), so this lambda
        // only has to advance the sidebar selection.
        QObject::connect(trayIcon, &TrayIconService::settingsRequested,
                         sidebarVM, [sidebarVM]() {
            sidebarVM->navigateTo(QStringLiteral("settings"));
        });

        // Quit: route through QCoreApplication::quit() so aboutToQuit fires
        // (autosave flush + Drive push + last-view persistence all hang off
        // it). A bare app.exit() would skip those.
        QObject::connect(trayIcon, &TrayIconService::quitRequested,
                         &app, &QCoreApplication::quit);

        // Re-translate the tray menu when the user switches UI language at
        // runtime. tr() captures the active translator at the time the
        // action was constructed; without this hook the menu would keep the
        // previous locale until restart.
        QObject::connect(settingsVM, &SettingsViewModel::languageChanged,
                         trayIcon, &TrayIconService::retranslate);

        // ── Accent-tinted tray badge ────────────────────────────────────────
        //
        // The tray icon paints a small circular badge in the bottom-right
        // corner whenever there are unread items in the notification centre.
        // Color comes from SettingsViewModel::accentColor (which mirrors the
        // QML ThemeManager.accentColor / customAccentHex), count comes from
        // NotificationsViewModel::unreadCount. We seed both at startup so the
        // very first paint already reflects current state, then keep them in
        // sync via NOTIFY signals.
        trayIcon->setBadgeAccentColor(settingsVM->accentColor());
        trayIcon->setBadgeCount(notificationsVM->unreadCount());

        QObject::connect(settingsVM, &SettingsViewModel::accentColorChanged,
                         trayIcon, [trayIcon, settingsVM]() {
            trayIcon->setBadgeAccentColor(settingsVM->accentColor());
        });

        QObject::connect(notificationsVM, &NotificationsViewModel::unreadCountChanged,
                         trayIcon, [trayIcon, notificationsVM]() {
            trayIcon->setBadgeCount(notificationsVM->unreadCount());
        });
    }

    // Expose the tray service to QML too — used by the settings UI to read
    // .available and bind the visibility switch.
    ctx->setContextProperty(QStringLiteral("trayIcon"), trayIcon);
#endif

    return app.exec();
}
