cmake_minimum_required(VERSION 3.21)
project(OpenTodo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ── Static analysis: clang-tidy (opt-in, per-TU) ─────────────────────────────
# Enable with `cmake -DOPENTODO_ENABLE_CLANG_TIDY=ON ...`. The toolchain then
# runs clang-tidy alongside the C++ compiler for every translation unit and
# reports findings as warnings (no build failure — see WarningsAsErrors: '' in
# .clang-tidy). If clang-tidy is not on PATH the flag is a warning, not an
# error, so the build still configures cleanly on machines without it.
option(OPENTODO_ENABLE_CLANG_TIDY "Run clang-tidy during build" OFF)
if(OPENTODO_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
        message(STATUS "clang-tidy: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING
            "OPENTODO_ENABLE_CLANG_TIDY=ON but clang-tidy was not found on PATH. "
            "Install LLVM/clang-tidy or use the Qt Creator clang bundle "
            "(C:/Qt/Tools/QtCreator/bin/clang/bin on Windows).")
    endif()
endif()

# ── Static analysis: cppcheck (opt-in, separate target) ──────────────────────
# Configure: standard CMake. Run: `cmake --build <build> --target cppcheck`.
# Findings are streamed to stderr in the build window and to cppcheck.log via
# the wrapper scripts under tools/. Like clang-tidy, this is informational and
# never fails the build. See tools/run-cppcheck.{bat,sh} for the standalone
# (no-CMake) entry point used by CI / pre-commit hooks.
find_program(CPPCHECK_EXE NAMES cppcheck)
if(CPPCHECK_EXE)
    # --library=qt loads cppcheck's bundled qt.cfg so Q_PROPERTY / Q_OBJECT /
    # `slots:` etc. don't trigger unknownMacro noise. Suppressions match those
    # in tools/run-cppcheck.{bat,sh} — keep them in sync.
    add_custom_target(cppcheck
        COMMAND "${CPPCHECK_EXE}"
            --enable=all
            --inconclusive
            --library=qt
            --suppress=missingIncludeSystem
            --suppress=unusedFunction
            --suppress=useStlAlgorithm
            --suppress=cstyleCast
            --suppress=unmatchedSuppression
            --inline-suppr
            --std=c++20
            --language=c++
            -I "${CMAKE_SOURCE_DIR}/src"
            "${CMAKE_SOURCE_DIR}/src"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Running cppcheck on src/"
        VERBATIM
    )
    message(STATUS "cppcheck: ${CPPCHECK_EXE} (target: cppcheck)")
endif()

# ── WASM detection ────────────────────────────────────────────────────────────
# EMSCRIPTEN is set automatically when the emscripten toolchain is loaded.
# We also normalise a convenience variable used in comments/messages.
if(EMSCRIPTEN)
    message(STATUS "OpenTodo: configuring for WebAssembly (Emscripten)")
endif()

# Qt::Widgets is not meaningful in a WASM/QML-only build but it IS available
# in Qt WASM builds (statically bundled).  We keep it in REQUIRED so that
# the same CMakeLists.txt works unchanged for desktop, Android, and WASM.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Network Widgets QuickControls2 Svg)
find_package(Qt6 OPTIONAL_COMPONENTS CorePrivate)

# Qt.labs.settings is an OPTIONAL plugin — without an explicit dependency the
# build does NOT deploy qmlsettingsplugin.dll into qt/qml/Qt/labs/settings/,
# and runtime imports of "Qt.labs.settings" silently fail. For most QML files
# Qt then no-ops the Settings blocks (UI continues to render, persistence
# breaks); but for Loaders and singletons the resolution failure is fatal and
# the parent file fails to load (DesktopLayout.qml black-screen, May 2026).
# Linking Qt6::LabsSettings forces deployment.
find_package(Qt6 OPTIONAL_COMPONENTS LabsSettings)

# Positioning is optional — used by LocationService for geo-reminders.
# Skip on WASM: Qt Positioning is not shipped in the wasm_singlethread kit.
if(NOT EMSCRIPTEN)
    find_package(Qt6 OPTIONAL_COMPONENTS Positioning)
endif()

qt_standard_project_setup(REQUIRES 6.5)

# ── Code coverage (opt-in) ───────────────────────────────────────────────────
# Off by default. Per-compiler: GCC → --coverage (gcovr), Clang → source-based
# (-fprofile-instr-generate / llvm-cov), MSVC → use OpenCppCoverage externally.
# Helper scripts: tools/coverage.{bat,sh}. Full workflow in docs/BUILDING.md.
option(OPENTODO_ENABLE_COVERAGE "Enable code coverage instrumentation (off by default; see docs/BUILDING.md)" OFF)

if(OPENTODO_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(STATUS "OpenTodo: code coverage enabled — GCC (gcov)")
        add_compile_options(--coverage -O0 -g)
        add_link_options(--coverage)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(STATUS "OpenTodo: code coverage enabled — Clang (source-based)")
        add_compile_options(-fprofile-instr-generate -fcoverage-mapping -O0 -g)
        add_link_options(-fprofile-instr-generate)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(WARNING
            "OPENTODO_ENABLE_COVERAGE=ON requested but MSVC has no built-in "
            "line coverage. Use OpenCppCoverage against a normal Debug build.")
    else()
        message(WARNING
            "OPENTODO_ENABLE_COVERAGE=ON but compiler '${CMAKE_CXX_COMPILER_ID}' "
            "is not recognised — no instrumentation flags applied.")
    endif()
endif()

# ── QtKeychain (OS keyring backend for SecretStore) ───────────────────────────
# Sec-1: replace XOR-obfuscated QSettings storage of the Claude API key with the
# native OS keyring (Windows Credential Manager / macOS Keychain / libsecret /
# kwallet). On Android and WASM there's no OS-level keyring we can target, so
# the SecretStore falls back to QSettings (still suboptimal but consistent with
# existing behavior on those platforms).
#
# FetchContent keeps the dependency vendored — no system-install requirement on
# build machines. v0.14.0 is the latest published tag as of 2026-05.
# QTKEYCHAIN_STATIC=ON avoids generating a separate .dll that we'd have to
# deploy next to the exe.
if(NOT EMSCRIPTEN AND NOT ANDROID)
    include(FetchContent)
    set(BUILD_WITH_QT6 ON CACHE BOOL "" FORCE)
    set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)
    set(BUILD_TRANSLATIONS OFF CACHE BOOL "" FORCE)
    set(QTKEYCHAIN_STATIC ON CACHE BOOL "" FORCE)
    set(QT_DEFAULT_MAJOR_VERSION 6 CACHE STRING "" FORCE)
    FetchContent_Declare(qtkeychain
        GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
        GIT_TAG v0.14.0
    )
    FetchContent_MakeAvailable(qtkeychain)
    if(TARGET qt6keychain)
        set(OPENTODO_QTKEYCHAIN_TARGET qt6keychain)
    elseif(TARGET Qt6Keychain)
        set(OPENTODO_QTKEYCHAIN_TARGET Qt6Keychain)
    elseif(TARGET qtkeychain)
        set(OPENTODO_QTKEYCHAIN_TARGET qtkeychain)
    endif()
endif()

# ── Domain layer ─────────────────────────────────────────────────────────────
# model + filter + nlp + utils — zero Qt::Quick dependency
set(DOMAIN_SOURCES
    src/model/task.cpp
    src/model/project.cpp
    src/model/section.cpp
    src/model/label.cpp
    src/model/filter.cpp
    src/model/reminder.cpp
    src/model/comment.cpp
    src/filter/filterparser.cpp
    src/nlp/dateparser.cpp
    src/nlp/rules_en.cpp
    src/nlp/rules_ru.cpp
    src/nlp/recurrenceengine.cpp
    src/nlp/quickaddparser.cpp
    src/utils/textsearch.cpp
)

set(DOMAIN_HEADERS
    src/model/task.h
    src/model/project.h
    src/model/section.h
    src/model/label.h
    src/model/filter.h
    src/model/reminder.h
    src/model/comment.h
    src/filter/tokens.h
    src/filter/filterparser.h
    src/nlp/dateparser.h
    src/nlp/recurrenceengine.h
    src/nlp/quickaddparser.h
    src/utils/uuid.h
    src/utils/datetime.h
    src/utils/textsearch.h
)

qt_add_library(OpenTodoDomain STATIC ${DOMAIN_SOURCES} ${DOMAIN_HEADERS})

target_include_directories(OpenTodoDomain PUBLIC
    src
    src/model
    src/filter
    src/nlp
    src/utils
)

target_link_libraries(OpenTodoDomain PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
)

# ── Infrastructure layer ──────────────────────────────────────────────────────
# storage + sync + services — depends on Domain
set(INFRA_SOURCES
    src/filter/filtereval.cpp
    src/storage/datastore.cpp
    src/storage/filemanager.cpp
    src/cli/clicore.cpp
    src/sync/commit.cpp
    src/sync/syncengine.cpp
    src/sync/merger.cpp
    src/sync/hlc.cpp
    src/sync/crdt_meta.cpp
    src/sync/gdrive.cpp
    src/sync/oauth_client.cpp
    src/sync/drive_api.cpp
    src/sync/snapshot.cpp
    src/sync/localdirremote.cpp
    src/sync/httpremote.cpp
    src/sync/synchttpserver.cpp
    src/services/crashlogger.cpp
    src/services/dailyreviewservice.cpp
    src/services/pomodoroservice.cpp
    src/services/reminderscheduler.cpp
    src/services/windowchrome.cpp
    src/services/undostack.cpp
    src/services/aiassistant.cpp
    src/services/llmprovider.cpp
    src/services/secretstore.cpp
    src/sync/sharemanager.cpp
    src/services/updatechecker.cpp
    src/services/telemetry.cpp
    src/services/taskexporter.cpp
    src/services/icsexporter.cpp
    src/services/claudecodelauncher.cpp
    src/services/tintedsvgimageprovider.cpp
    src/import/todoist_csv_parser.cpp
    src/import/todoist_api_client.cpp
    src/import/todoistimporter.cpp
)

set(INFRA_HEADERS
    src/filter/filtereval.h
    src/storage/datastore.h
    src/storage/filemanager.h
    src/sync/commit.h
    src/sync/syncengine.h
    src/sync/merger.h
    src/sync/hlc.h
    src/sync/crdt_meta.h
    src/sync/remotestore.h
    src/sync/gdrive.h
    src/sync/oauth_client.h
    src/sync/drive_api.h
    src/sync/snapshot.h
    src/sync/localdirremote.h
    src/sync/httpremote.h
    src/sync/synchttpserver.h
    src/services/notificationservice.h
    src/services/hotkeyservice.h
    src/services/pomodoroservice.h
    src/services/reminderscheduler.h
    src/services/windowchrome.h
    src/services/undostack.h
    src/services/aiassistant.h
    src/services/llmprovider.h
    src/services/secretstore.h
    src/sync/sharemanager.h
    src/services/crashlogger.h
    src/services/dailyreviewservice.h
    src/services/updatechecker.h
    src/services/telemetry.h
    src/services/taskexporter.h
    src/services/icsexporter.h
    src/services/claudecodelauncher.h
    src/services/tintedsvgimageprovider.h
    src/import/todoist_types.h
    src/import/todoist_csv_parser.h
    src/import/todoist_api_client.h
    src/import/todoistimporter.h
)

# Platform-specific services
if(EMSCRIPTEN)
    # WebAssembly: no native hotkeys, no OS-level notifications.
    # WASM notification service logs to the browser console and fires signals
    # so QML can show in-app banners.  HotkeyService uses the existing stub.
    list(APPEND INFRA_SOURCES
        src/services/notificationservice_wasm.cpp
        src/services/hotkeyservice_stub.cpp
    )
    list(APPEND INFRA_HEADERS
        src/services/notificationservice_wasm.h
    )
elseif(WIN32)
    list(APPEND INFRA_SOURCES
        src/services/notificationservice_win.cpp
        src/services/hotkeyservice_win.cpp
        src/services/trayicon.cpp
    )
    list(APPEND INFRA_HEADERS
        src/services/notificationservice_win.h
        src/services/trayicon.h
    )
elseif(ANDROID)
    list(APPEND INFRA_SOURCES
        src/services/notificationservice_android.cpp
        src/services/hotkeyservice_stub.cpp
    )
    list(APPEND INFRA_HEADERS
        src/services/notificationservice_android.h
    )
else()
    # Linux, macOS, and any other platform without a native notification or hotkey backend.
    # StubNotificationService is a header-only no-op that logs to qDebug() and emits
    # inAppBannerRequested so the QML layer can show in-app toasts.
    # HotkeyService uses the existing stub (already compiles on all non-Windows platforms).
    # TrayIconService is built here too — QSystemTrayIcon works on Linux/macOS
    # (with the GNOME extension on most Wayland setups); the runtime guard on
    # isSystemTrayAvailable() handles the rare environment where it does not.
    list(APPEND INFRA_SOURCES
        src/services/hotkeyservice_stub.cpp
        src/services/trayicon.cpp
    )
    list(APPEND INFRA_HEADERS
        src/services/notificationservice_stub.h
        src/services/trayicon.h
    )
endif()

# LocationService is cross-platform (uses Qt Positioning)
if(Qt6Positioning_FOUND)
    list(APPEND INFRA_SOURCES src/services/locationservice.cpp)
    list(APPEND INFRA_HEADERS src/services/locationservice.h)
endif()

qt_add_library(OpenTodoInfra STATIC ${INFRA_SOURCES} ${INFRA_HEADERS})

target_include_directories(OpenTodoInfra PUBLIC
    src/filter
    src/storage
    src/sync
    src/services
    src/import
)

target_link_libraries(OpenTodoInfra PUBLIC
    OpenTodoDomain
    Qt6::Core
    Qt6::Network
    Qt6::Widgets
    Qt6::Quick
    Qt6::Svg
)

# Qt6::CorePrivate is optional — it is only available when Qt was built with
# developer headers (i.e. the -developer-build configure flag or the matching
# Qt dev package is installed).  Guard the link so the build still succeeds
# on standard Qt installations. (B-9.7)
if(TARGET Qt6::CorePrivate)
    target_link_libraries(OpenTodoInfra PUBLIC Qt6::CorePrivate)
endif()

# zlib is needed for the in-process ZIP reader used by the Todoist importer.
# Qt bundles zlib internally but doesn't export all symbols, so we look for
# a system zlib first and fall back to Qt's headers (linked via Qt6::Core).
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    target_link_libraries(OpenTodoInfra PUBLIC ZLIB::ZLIB)
else()
    # MinGW ships zlib1 alongside Qt — link it by name.
    target_link_libraries(OpenTodoInfra PUBLIC z)
endif()

# Windows: dwmapi for the dark title bar (DwmSetWindowAttribute).
# Not available / not needed on WASM.
if(WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(OpenTodoInfra PUBLIC dwmapi)
endif()

if(Qt6Positioning_FOUND)
    target_link_libraries(OpenTodoInfra PUBLIC Qt6::Positioning)
    target_compile_definitions(OpenTodoInfra PUBLIC HAS_POSITIONING)
endif()

# Sec-1: link QtKeychain on platforms where it is available. SecretStore
# guards its includes with HAS_QTKEYCHAIN; without the macro it compiles
# against the QSettings fallback path only.
if(OPENTODO_QTKEYCHAIN_TARGET)
    target_link_libraries(OpenTodoInfra PUBLIC ${OPENTODO_QTKEYCHAIN_TARGET})
    target_include_directories(OpenTodoInfra PUBLIC
        "${qtkeychain_SOURCE_DIR}"
        "${qtkeychain_BINARY_DIR}"
    )
    target_compile_definitions(OpenTodoInfra PUBLIC HAS_QTKEYCHAIN)
endif()

# ── Application layer ─────────────────────────────────────────────────────────
# viewmodels — depends on Infra (+ transitive Domain)
set(APP_SOURCES
    src/viewmodels/taskviewmodel.cpp
    src/viewmodels/projectviewmodel.cpp
    src/viewmodels/sidebarviewmodel.cpp
    src/viewmodels/filterviewmodel.cpp
    src/viewmodels/searchviewmodel.cpp
    src/viewmodels/settingsviewmodel.cpp
    src/viewmodels/karmaviewmodel.cpp
    src/viewmodels/syncviewmodel.cpp
    src/viewmodels/notificationsviewmodel.cpp
    src/app/clibridge.cpp
)

set(APP_HEADERS
    src/viewmodels/taskviewmodel.h
    src/viewmodels/projectviewmodel.h
    src/viewmodels/sidebarviewmodel.h
    src/viewmodels/filterviewmodel.h
    src/viewmodels/searchviewmodel.h
    src/viewmodels/settingsviewmodel.h
    src/viewmodels/karmaviewmodel.h
    src/viewmodels/syncviewmodel.h
    src/viewmodels/notificationsviewmodel.h
    src/app/clibridge.h
)

qt_add_library(OpenTodoApp STATIC ${APP_SOURCES} ${APP_HEADERS})

target_include_directories(OpenTodoApp PUBLIC
    src/viewmodels
)

target_link_libraries(OpenTodoApp PUBLIC
    OpenTodoInfra
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
)

# Pull in Qt.labs.settings plugin so qmlsettingsplugin.dll is deployed into
# qt/qml/Qt/labs/settings/. Without this the runtime "Qt.labs.settings is
# not installed" warning fires and DesktopLayout.qml fails to render
# (May 2026 black-screen incident — the plugin is "optional" in qmldir,
# so silent no-op for most QML files but FATAL for Loaders/singletons).
if(TARGET Qt6::LabsSettings)
    target_link_libraries(OpenTodoApp PRIVATE Qt6::LabsSettings)
endif()

# ── QML files ────────────────────────────────────────────────────────────────
set(QML_FILES
    qml/Main.qml
    qml/layouts/DesktopLayout.qml
    qml/layouts/MobileLayout.qml
    qml/components/IconSvg.qml
    qml/components/SidebarContent.qml
    qml/components/TaskItem.qml
    qml/components/TaskListView.qml
    qml/components/TaskMoveDialog.qml
    qml/components/TaskEditor.qml
    qml/components/editor/EditorHeader.qml
    qml/components/editor/EditorContentField.qml
    qml/components/editor/EditorDescriptionField.qml
    qml/components/editor/EditorMetaRow.qml
    qml/components/editor/EditorRemindersSection.qml
    qml/components/editor/EditorSubtasksSection.qml
    qml/components/editor/EditorCommentsSection.qml
    qml/components/editor/EditorActionBar.qml
    qml/components/QuickAdd.qml
    qml/components/UpcomingCalendarPanel.qml
    qml/components/DatePicker.qml
    qml/components/DesignPill.qml
    qml/components/DesignSelect.qml
    qml/components/DesignSwitch.qml
    qml/components/PriorityPicker.qml
    qml/components/LabelChip.qml
    qml/components/SectionHeader.qml
    qml/components/SearchBar.qml
    qml/components/CommandMenu.qml
    qml/components/SyncIndicator.qml
    qml/components/ConfettiOverlay.qml
    qml/components/InAppBanner.qml
    qml/components/QuickJumpDialog.qml
    qml/components/ReadingModeOverlay.qml
    qml/components/SubtaskGeneratorDialog.qml
    qml/components/WhatsNewDialog.qml
    qml/components/ShareLinkDialog.qml
    # B-7.26: ProjectItem.qml deleted — was dead code, never referenced by any Loader or createComponent
    qml/components/FilterQueryInput.qml
    qml/components/EmptyState.qml
    qml/components/ProjectIconPicker.qml
    # ── Mobile Android redesign components ──────────────────────────────
    qml/components/mobile/MDensity.qml
    qml/components/mobile/MIconButton.qml
    qml/components/mobile/MCheck.qml
    qml/components/mobile/MChip.qml
    qml/components/mobile/MSeg.qml
    qml/components/mobile/MFab.qml
    qml/components/mobile/MSection.qml
    qml/components/mobile/MSheet.qml
    qml/components/mobile/MTopBar.qml
    qml/components/mobile/MSnackbar.qml
    qml/components/mobile/MCalendar.qml
    qml/components/mobile/MNavItem.qml
    qml/components/mobile/MBottomNav.qml
    qml/components/mobile/MPinRow.qml
    qml/components/mobile/MPinSheet.qml
    qml/components/mobile/MTaskRow.qml
    qml/components/mobile/MOptionSheet.qml
    qml/components/mobile/MContextSheet.qml
    qml/components/mobile/MQuickAdd.qml
    qml/components/mobile/MQuickAddChip.qml
    qml/components/mobile/MEditorMetaRow.qml
    qml/components/mobile/MEditorHistoryRow.qml
    qml/components/mobile/MEditorDateSheet.qml
    qml/components/mobile/MEditorScreen.qml
    qml/components/mobile/MCalendarScreen.qml
    qml/components/mobile/MTrashScreen.qml
    qml/components/mobile/MFiltersScreen.qml
    qml/components/mobile/MNotificationsScreen.qml
    qml/components/mobile/MProductivityScreen.qml
    qml/components/mobile/MLabelsScreen.qml
    qml/components/mobile/MSwitch.qml
    qml/components/mobile/MSettingsSectionLabel.qml
    qml/components/mobile/MSettingsRow.qml
    qml/components/mobile/MSettingsScreen.qml
    qml/components/mobile/MDrawerItem.qml
    qml/components/mobile/MDrawerContent.qml
    qml/components/mobile/MEmptyState.qml
    qml/components/mobile/MBoardScreen.qml
    qml/components/mobile/MSearchScreen.qml
    qml/components/mobile/MAIScreen.qml
    qml/components/mobile/MSyncScreen.qml
    qml/views/ProjectView.qml
    qml/views/BoardView.qml
    qml/views/CalendarView.qml
    qml/views/SettingsView.qml
    qml/views/TrashView.qml
    qml/components/settings/SettingsSectionDefer.qml
    qml/components/settings/SettingsSectionGeneral.qml
    qml/components/settings/SettingsSectionLanguage.qml
    qml/components/settings/SettingsSectionAppearance.qml
    qml/components/settings/SettingsSectionProductivity.qml
    qml/components/settings/SettingsSectionReminders.qml
    qml/components/settings/SettingsSectionSidebar.qml
    qml/components/settings/SettingsSectionSync.qml
    qml/components/settings/SettingsSectionBackup.qml
    qml/components/settings/SettingsSectionAbout.qml
    qml/components/settings/SettingsSectionSupport.qml
    qml/components/settings/SettingsSectionAI.qml
    qml/views/ProductivityView.qml
    qml/views/NotificationsView.qml
    qml/views/AIView.qml
    qml/themes/ThemeManager.qml
    qml/themes/Spacing.qml
    qml/themes/FontSize.qml
    qml/themes/Radius.qml
    qml/themes/Duration.qml
    qml/themes/DateFormatter.qml
    # Standalone palette files (TodoistLight.qml, TodoistDark.qml, Kale.qml,
    # Blueberry.qml, Lavender.qml, Raspberry.qml, Graphite.qml, Sunflower.qml)
    # were deleted in B-8.1 cleanup — all palette data is inlined in ThemeManager.qml.
)

# ── QML module — backed by the App library, not the exe ──────────────────────
# Attaching qt_add_qml_module to the OpenTodoApp *library* (not the exe) is
# required on Android: androiddeployqt resolves "import OpenTodoUI" via the
# library's standard QML output path.  When the module was on the exe target,
# androiddeployqt reported "Import found outside of import paths" because the
# QML output dir for an executable does not sit on any searched import path.
#
# NO_RESOURCE_TARGET_PATH is only valid for executable targets; we use an
# explicit RESOURCE_PREFIX "/" instead (same effect for resource loading).

# Mark theme singletons so other components can reference them directly after
# `import OpenTodoUI`.  ThemeManager was already a singleton; the four token
# files (Spacing / FontSize / Radius / Duration) are new.
set_source_files_properties(
    qml/themes/ThemeManager.qml
    qml/themes/Spacing.qml
    qml/themes/FontSize.qml
    qml/themes/Radius.qml
    qml/themes/Duration.qml
    qml/themes/DateFormatter.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(OpenTodoApp
    URI OpenTodoUI
    VERSION 1.0
    QML_FILES ${QML_FILES}
    RESOURCE_PREFIX "/qt/qml"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qt/qml/OpenTodoUI"
)

# ── Executable ───────────────────────────────────────────────────────────────
qt_add_executable(OpenTodo WIN32 src/main.cpp)

# Link the App library (which now carries the QML module) and the generated
# plugin target.  qt_add_qml_module on a library creates a companion plugin
# target named <target>plugin; linking it ensures the module is initialised
# before QQmlApplicationEngine::loadFromModule runs.
target_link_libraries(OpenTodo PRIVATE OpenTodoApp OpenTodoAppplugin)

# ── opentodo-cli (console front-end) ──────────────────────────────────────────
# Headless CLI over the same task store. Reuses the Infra layer (DataStore /
# FileManager / clicore) so there is zero logic duplication. No WIN32, so it is
# a console app with working stdout.
#
# NB: the binary stays named "opentodo-cli", NOT "opentodo". Windows paths are
# case-insensitive, so an "opentodo.exe" output would resolve to the SAME file
# as the GUI app's "OpenTodo.exe" in this build directory — and the linker
# cannot overwrite it while the app is running. Keeping the "-cli" suffix also
# lets it sit beside the app's windeployqt'd Qt DLLs, so it runs with no extra
# deployment. Alias it to `opentodo` on PATH for the shorter command.
qt_add_executable(opentodo-cli src/cli/main_cli.cpp)
target_link_libraries(opentodo-cli PRIVATE OpenTodoInfra)

# Explicitly copy the Qt.labs.settings plugin into the exe's qml/ tree.
# Even with target_link_libraries(Qt6::LabsSettings) above, Qt's CMake glue
# does not deploy the plugin .dll into the binary directory for desktop
# (non-installed) runs, so QML import resolution at runtime fails. We copy
# qmlsettingsplugin.dll + qmldir + plugins.qmltypes from the Qt installation
# into ${CMAKE_BINARY_DIR}/qt/qml/Qt/labs/settings/ at build time so that
# `import Qt.labs.settings 1.0` resolves correctly.
if(WIN32 AND NOT EMSCRIPTEN)
    set(_OPENTODO_LABS_SETTINGS_SRC "${Qt6_DIR}/../../../qml/Qt/labs/settings")
    set(_OPENTODO_LABS_SETTINGS_DST "${CMAKE_BINARY_DIR}/qt/qml/Qt/labs/settings")
    set(_OPENTODO_QT_BIN          "${Qt6_DIR}/../../../bin")
    if(EXISTS "${_OPENTODO_LABS_SETTINGS_SRC}/qmlsettingsplugin.dll")
        add_custom_command(TARGET OpenTodo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_OPENTODO_LABS_SETTINGS_DST}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_OPENTODO_LABS_SETTINGS_SRC}/qmlsettingsplugin.dll"
                    "${_OPENTODO_LABS_SETTINGS_SRC}/qmldir"
                    "${_OPENTODO_LABS_SETTINGS_SRC}/plugins.qmltypes"
                    "${_OPENTODO_LABS_SETTINGS_DST}/"
            COMMENT "Deploying Qt.labs.settings plugin into qml/Qt/labs/settings/"
            VERBATIM
        )
    endif()
    # qmlsettingsplugin.dll depends on Qt6LabsSettings.dll. Qt's CMake glue
    # copies Qt6Core/Qt6Quick/etc. automatically (they come from REQUIRED
    # find_package), but OPTIONAL_COMPONENTS aren't auto-copied — the .dll
    # ends up referenced by the plugin without being deployed, and the
    # runtime fails with "Не найден указанный модуль" (DLL load failure
    # masquerading as a missing module). Force-copy it next to the exe.
    if(EXISTS "${_OPENTODO_QT_BIN}/Qt6LabsSettings.dll")
        add_custom_command(TARGET OpenTodo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_OPENTODO_QT_BIN}/Qt6LabsSettings.dll"
                    "$<TARGET_FILE_DIR:OpenTodo>/"
            COMMENT "Deploying Qt6LabsSettings.dll alongside the executable"
            VERBATIM
        )
    endif()
endif()

# Deploy QtCore QML plugin (Qt 6.5+ replacement for Qt.labs.settings).
# Same rationale as the Qt.labs.settings deploy above: Qt's CMake glue does
# not deploy the plugin .dll into the binary directory for desktop
# (non-installed) runs, so `import QtCore` fails to resolve at runtime
# unless we copy qtqmlcoreplugin.dll + qmldir + plugins.qmltypes from the
# Qt installation into ${CMAKE_BINARY_DIR}/qt/qml/QtCore/ at build time.
if(WIN32 AND NOT EMSCRIPTEN)
    set(_OPENTODO_QTCORE_QML_SRC "${Qt6_DIR}/../../../qml/QtCore")
    set(_OPENTODO_QTCORE_QML_DST "${CMAKE_BINARY_DIR}/qt/qml/QtCore")
    if(EXISTS "${_OPENTODO_QTCORE_QML_SRC}/qtqmlcoreplugin.dll")
        add_custom_command(TARGET OpenTodo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_OPENTODO_QTCORE_QML_DST}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_OPENTODO_QTCORE_QML_SRC}/qtqmlcoreplugin.dll"
                    "${_OPENTODO_QTCORE_QML_SRC}/qmldir"
                    "${_OPENTODO_QTCORE_QML_SRC}/plugins.qmltypes"
                    "${_OPENTODO_QTCORE_QML_DST}/"
            COMMENT "Deploying QtCore QML plugin into qml/QtCore/"
            VERBATIM
        )
    endif()
    # qtqmlcoreplugin.dll depends on Qt6QmlCore.dll (the runtime that backs
    # the QtCore QML module). Same DLL-load failure mode as Qt6LabsSettings:
    # if the dependency is not next to the exe, the plugin appears as
    # "module not found" at runtime. Force-copy it.
    if(EXISTS "${_OPENTODO_QT_BIN}/Qt6QmlCore.dll")
        add_custom_command(TARGET OpenTodo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_OPENTODO_QT_BIN}/Qt6QmlCore.dll"
                    "$<TARGET_FILE_DIR:OpenTodo>/"
            COMMENT "Deploying Qt6QmlCore.dll alongside the executable"
            VERBATIM
        )
    endif()
endif()

# Bake Qt's translations directory into the binary so it can fall back when
# QLibraryInfo::TranslationsPath (which under static Qt points at <exe>/translations)
# is not populated. Qt6_DIR resolves to the cmake/Qt6 dir; …/../../translations
# is the canonical install layout for both online installer and aqtinstall.
if(NOT EMSCRIPTEN AND NOT ANDROID)
    get_filename_component(_OPENTODO_QT_TR_DIR "${Qt6_DIR}/../../../translations" ABSOLUTE)
    target_compile_definitions(OpenTodo PRIVATE
        OPENTODO_QT_TRANSLATIONS_DIR="${_OPENTODO_QT_TR_DIR}"
    )
endif()

# ── Platform-specific configuration ──────────────────────────────────────────
if(ANDROID)
    # Bundle OpenSSL 3 so Qt's TLS backend can load. Without libcrypto_3.so /
    # libssl_3.so the qopensslbackend plugin fails at runtime
    # ("No TLS backend is available") and ALL HTTPS — including Google Drive
    # sync — is dead on Android. Prebuilt per-ABI libs (KDAB android_openssl,
    # the Qt-recommended source) live under third_party/android_openssl/<abi>/.
    set(_OPENTODO_ANDROID_SSL_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/android_openssl/${ANDROID_ABI}")
    if(EXISTS "${_OPENTODO_ANDROID_SSL_DIR}/libssl_3.so")
        set_property(TARGET OpenTodo APPEND PROPERTY
            QT_ANDROID_EXTRA_LIBS
                "${_OPENTODO_ANDROID_SSL_DIR}/libcrypto_3.so"
                "${_OPENTODO_ANDROID_SSL_DIR}/libssl_3.so"
        )
    else()
        message(WARNING
            "OpenSSL libs for ABI '${ANDROID_ABI}' not found under "
            "third_party/android_openssl/ — TLS/HTTPS (Google Drive sync) will "
            "NOT work in this Android build.")
    endif()
    set_target_properties(OpenTodo PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    )

    # The app's own libOpenTodo_<abi>.so ships UNSTRIPPED on this toolchain:
    # Gradle's stripDebugSymbols task fails ("Unable to strip … packaging them
    # as they are"), so ~120 MB of symbols + DWARF debug info ride into the APK
    # (the single biggest entry, dwarfing all Qt libs combined). Strip the app
    # lib at link time for non-Debug configs so the packaged lib is ~10 MB and
    # the APK drops from ~195 MB to ~75 MB. .dynsym (the exported `main` Qt
    # dlsym()s at startup) is preserved; only .symtab + debug sections go.
    # Debug builds keep symbols so native debugging still works.
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_link_options(OpenTodo PRIVATE "LINKER:--strip-all")
    endif()
endif()

if(EMSCRIPTEN)
    # Allow heap to grow beyond the initial 64 MB reservation.
    # The Qt WASM runtime + QML engine + app data can easily exceed 32 MB.
    target_link_options(OpenTodo PRIVATE
        -sINITIAL_MEMORY=67108864   # 64 MB
        -sALLOW_MEMORY_GROWTH=1
        # Export the IDBFS file-system so the app can persist state.json
        # across page reloads via Emscripten's IndexedDB FS mount.
        -lidbfs.js
    )
    # Qt WASM output is always named after the target; rename to lower-case
    # for the conventional /<appname>.html URL people expect.
    set_target_properties(OpenTodo PROPERTIES OUTPUT_NAME "OpenTodo")

    # Post-build: drop favicon next to OpenTodo.html and inject a <link rel="icon">
    # tag into the Qt-generated HTML so browsers/tabs/bookmarks show our icon.
    add_custom_command(TARGET OpenTodo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/icon_32.png"
                "$<TARGET_FILE_DIR:OpenTodo>/favicon.png"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.svg"
                "$<TARGET_FILE_DIR:OpenTodo>/favicon.svg"
        COMMAND ${CMAKE_COMMAND}
                -DHTML="$<TARGET_FILE_DIR:OpenTodo>/OpenTodo.html"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_wasm_favicon.cmake"
        VERBATIM
    )
endif()

# ── Resources ────────────────────────────────────────────────────────────────
# F-9.27: qtquickcontrols2.conf was removed because main.cpp already sets the
# Material style programmatically via QQuickStyle::setStyle() and qputenv().
# Keeping a stale conf file with conflicting Theme/Primary/Accent keys would
# override those runtime settings on some Qt versions.

qt_add_resources(OpenTodo "icons"
    PREFIX "/icons"
    FILES
        resources/icon.svg
        resources/icons/icon_16.png
        resources/icons/icon_32.png
        resources/icons/icon_48.png
        resources/icons/icon_64.png
        resources/icons/icon_128.png
        resources/icons/icon_256.png
        resources/icons/icon_512.png
)

# Task 17cea65b: UI icon set used via qml/components/IconSvg.qml.
# Adding new icons here is the only registration needed — IconSvg loads
# them as "qrc:/icons/resources/icons/ui/<name>.svg".
#
# Q4.01 (redesign): the legacy 18-icon set was expanded into a single
# stroke-style pack (24×24 viewBox, stroke=currentColor, stroke-width 1.6,
# round caps) so MultiEffect colorisation in IconSvg.qml can drive the
# colour from the theme. Source of truth for the geometry lives in the
# design bundle at .design-temp/opentodo/project/icons.jsx (regenerated by
# .design-temp/_build_icons.py). Replacing emojis in the sidebar / chips /
# section headers is the user-visible motivation — see chat1.md in the
# design bundle for context.
qt_add_resources(OpenTodo "ui_icons"
    PREFIX "/icons"
    FILES
        resources/icons/ui/alarm.svg
        resources/icons/ui/archive.svg
        resources/icons/ui/bell.svg
        resources/icons/ui/bitcoin.svg
        resources/icons/ui/board.svg
        resources/icons/ui/calendar.svg
        resources/icons/ui/check.svg
        resources/icons/ui/check-bold.svg
        resources/icons/ui/check-circle.svg
        resources/icons/ui/chevron-down.svg
        resources/icons/ui/chevron-left.svg
        resources/icons/ui/chevron-right.svg
        resources/icons/ui/circle.svg
        resources/icons/ui/claude.svg
        resources/icons/ui/clock.svg
        resources/icons/ui/close.svg
        resources/icons/ui/cube.svg
        resources/icons/ui/drag-handle.svg
        resources/icons/ui/ethereum.svg
        resources/icons/ui/eye.svg
        resources/icons/ui/eye-off.svg
        resources/icons/ui/filter.svg
        resources/icons/ui/flag.svg
        resources/icons/ui/flame.svg
        resources/icons/ui/folder.svg
        resources/icons/ui/hash.svg
        resources/icons/ui/inbox.svg
        resources/icons/ui/label.svg
        resources/icons/ui/link.svg
        resources/icons/ui/list.svg
        resources/icons/ui/menu.svg
        resources/icons/ui/more.svg
        resources/icons/ui/more-horizontal.svg
        resources/icons/ui/note.svg
        resources/icons/ui/offline.svg
        resources/icons/ui/opentodo-mark.svg
        resources/icons/ui/paperclip.svg
        resources/icons/ui/pause.svg
        resources/icons/ui/pin.svg
        resources/icons/ui/plus.svg
        resources/icons/ui/play.svg
        resources/icons/ui/productivity.svg
        resources/icons/ui/repeat.svg
        resources/icons/ui/reset.svg
        resources/icons/ui/search.svg
        resources/icons/ui/send.svg
        resources/icons/ui/settings.svg
        resources/icons/ui/sidebar.svg
        resources/icons/ui/skip.svg
        resources/icons/ui/sparkles.svg
        resources/icons/ui/star-filled.svg
        resources/icons/ui/star.svg
        resources/icons/ui/success-badge.svg
        resources/icons/ui/sync.svg
        resources/icons/ui/tag.svg
        resources/icons/ui/today.svg
        resources/icons/ui/ton.svg
        resources/icons/ui/trash.svg
        resources/icons/ui/upcoming.svg
        resources/icons/ui/usdt.svg
        resources/icons/ui/warning.svg
)

# Q4.01 (redesign): project-category icons. Loaded by IconSvg with
# `name: "projects/<glyph>"` once a project icon picker is in place; for now
# they're registered so the assets ship with the binary and future work can
# wire up the picker without re-touching the build.
qt_add_resources(OpenTodo "project_icons"
    PREFIX "/icons"
    FILES
        resources/icons/projects/basket.svg
        resources/icons/projects/bed.svg
        resources/icons/projects/bike.svg
        resources/icons/projects/book.svg
        resources/icons/projects/brain.svg
        resources/icons/projects/briefcase.svg
        resources/icons/projects/broom.svg
        resources/icons/projects/building.svg
        resources/icons/projects/camera.svg
        resources/icons/projects/car.svg
        resources/icons/projects/cart.svg
        resources/icons/projects/chart.svg
        resources/icons/projects/coffee.svg
        resources/icons/projects/compass.svg
        resources/icons/projects/cube.svg
        resources/icons/projects/dumbbell.svg
        resources/icons/projects/flag.svg
        resources/icons/projects/fork-knife.svg
        resources/icons/projects/gamepad.svg
        resources/icons/projects/gift.svg
        resources/icons/projects/graduation-cap.svg
        resources/icons/projects/hammer.svg
        resources/icons/projects/heart.svg
        resources/icons/projects/house.svg
        resources/icons/projects/key.svg
        resources/icons/projects/layers.svg
        resources/icons/projects/leaf.svg
        resources/icons/projects/lightbulb.svg
        resources/icons/projects/map-pin.svg
        resources/icons/projects/monitor.svg
        resources/icons/projects/moon.svg
        resources/icons/projects/mountain.svg
        resources/icons/projects/music.svg
        resources/icons/projects/palette.svg
        resources/icons/projects/paw.svg
        resources/icons/projects/pencil.svg
        resources/icons/projects/plane.svg
        resources/icons/projects/plant.svg
        resources/icons/projects/printer.svg
        resources/icons/projects/puzzle.svg
        resources/icons/projects/rocket.svg
        resources/icons/projects/runner.svg
        resources/icons/projects/screwdriver.svg
        resources/icons/projects/shirt.svg
        resources/icons/projects/shopping-bag.svg
        resources/icons/projects/smile.svg
        resources/icons/projects/sofa.svg
        resources/icons/projects/sun.svg
        resources/icons/projects/target.svg
        resources/icons/projects/user.svg
        resources/icons/projects/wrench.svg
)

# Windows: embed the multi-resolution .ico as the EXE's file icon (visible in
# Explorer, the taskbar before the QML window opens, and the alt-tab list).
#
# CMake's RC support pipes the .rc through cc1.exe, which inherits all C++
# include paths from the target. With a project path containing spaces
# ("Smart Home/opentodo") windres preprocessing breaks. We sidestep all of
# that by compiling the .rc with windres directly (no preprocessor), then
# linking the resulting .obj as a target source.
if(WIN32 AND NOT EMSCRIPTEN)
    # windres + cc1.exe mangle paths with embedded spaces (the project lives
    # under "Smart Home/..."). Workaround: stage the .rc + .ico into a
    # space-free temp directory and compile from there. The resulting .obj
    # is then linked directly into the executable.
    find_program(WINDRES_EXE NAMES windres x86_64-w64-mingw32-windres REQUIRED)
    set(_rc_stage "$ENV{TEMP}/opentodo_rc")
    file(MAKE_DIRECTORY "${_rc_stage}")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows.rc"
              "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/icon.ico"
         DESTINATION "${_rc_stage}")
    # Patch the .rc to reference the .ico in the same dir, not "icons/icon.ico".
    file(WRITE "${_rc_stage}/windows.rc" "IDI_ICON1 ICON DISCARDABLE \"icon.ico\"\n")
    set(_rc_obj "${_rc_stage}/windows_icon.rc.obj")
    add_custom_command(
        OUTPUT "${_rc_obj}"
        COMMAND "${WINDRES_EXE}" -O coff -i "${_rc_stage}/windows.rc" -o "${_rc_obj}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/windows.rc"
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/icon.ico"
        WORKING_DIRECTORY "${_rc_stage}"
        VERBATIM
    )
    target_sources(OpenTodo PRIVATE "${_rc_obj}")
endif()

qt_add_resources(OpenTodo "templates"
    PREFIX "/templates"
    FILES
        resources/templates/meeting_agenda.json
        resources/templates/personal_routine.json
        resources/templates/shopping_list.json
        resources/templates/dev_pipeline.json
)

# ── Application translations ─────────────────────────────────────────────────
# Russian is the source language (literal Russian strings sit inside qsTr() in
# QML and tr() in C++). The .ts files in i18n/ define translations into the
# five supported locales: ru / en / de / uk / be. Each is compiled to a binary
# .qm and embedded into the Qt resource system under prefix /i18n/, where
# main.cpp loads it via QTranslator::load(":/i18n/opentodo_<lang>.qm").
#
# We deliberately use qt_add_lrelease() (compile-only) rather than
# qt_add_translations() because the latter would try to run lupdate during the
# normal CMake configure, which scans every C++/QML source file and would
# clobber the hand-edited .ts files on every reconfigure. lupdate is meant to
# be run manually by translators; here we only need .qm output.
set(OPENTODO_TS_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/opentodo_ru.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/opentodo_en.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/opentodo_de.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/opentodo_uk.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/opentodo_be.ts
)

# Find lrelease — it's part of Qt6::LinguistTools.
find_package(Qt6 OPTIONAL_COMPONENTS LinguistTools)
if(TARGET Qt6::lrelease)
    qt_add_lrelease(OpenTodo
        TS_FILES ${OPENTODO_TS_FILES}
        QM_FILES_OUTPUT_VARIABLE OPENTODO_QM_FILES
    )
    # Embed the compiled .qm files into the application resources so the
    # runtime can load them via ":/i18n/opentodo_<lang>.qm" regardless of
    # install layout (WASM / Android / static-deployed desktop builds).
    qt_add_resources(OpenTodo "translations"
        PREFIX "/i18n"
        BASE   "${CMAKE_CURRENT_BINARY_DIR}"
        FILES  ${OPENTODO_QM_FILES}
    )
else()
    message(WARNING "OpenTodo: Qt6::lrelease not found — translations will not be compiled. "
                    "Install the Qt Linguist tools (qttools package) and reconfigure.")
endif()

# ── Install ──────────────────────────────────────────────────────────────────
install(TARGETS OpenTodo
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ── Tests ────────────────────────────────────────────────────────────────────
option(BUILD_TESTING "Build the OpenTodo test suite" ON)

# libFuzzer harnesses for FilterParser / DateParser / QuickAddParser. Opt-in
# because the fuzzer is Clang-only (-fsanitize=fuzzer is an LLVM compiler-rt
# feature unavailable under MSVC/GCC). See tests/fuzz/README.md.
option(OPENTODO_ENABLE_FUZZ "Build libFuzzer parser harnesses (requires Clang)" OFF)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Coverage report target (opt-in, requires OPENTODO_ENABLE_COVERAGE=ON) ────
# `cmake --build <build> --target coverage_report` runs ctest with coverage
# instrumentation and builds an HTML report at <build>/coverage/index.html.
# Tool discovery via find_program (gcovr / llvm-profdata + llvm-cov) — fresh
# checkouts configure cleanly even without the optional tooling installed.
if(OPENTODO_ENABLE_COVERAGE AND BUILD_TESTING)
    set(_OPENTODO_COVERAGE_DIR "${CMAKE_BINARY_DIR}/coverage")

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        find_program(GCOVR_EXE NAMES gcovr)
        if(GCOVR_EXE)
            add_custom_target(coverage_report
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_OPENTODO_COVERAGE_DIR}"
                COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
                COMMAND "${GCOVR_EXE}"
                        --root "${CMAKE_SOURCE_DIR}"
                        --html --html-details
                        --output "${_OPENTODO_COVERAGE_DIR}/index.html"
                        --exclude "${CMAKE_SOURCE_DIR}/tests/"
                        --exclude "${CMAKE_SOURCE_DIR}/build/"
                        --exclude "${CMAKE_BINARY_DIR}/"
                        --print-summary
                        "${CMAKE_BINARY_DIR}"
                WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
                COMMENT "Running ctest and generating gcovr HTML report under ${_OPENTODO_COVERAGE_DIR}/"
                VERBATIM)
            add_dependencies(coverage_report OpenTodo)
            message(STATUS "Coverage report target available (gcovr at ${GCOVR_EXE})")
        else()
            add_custom_target(coverage_report
                COMMAND ${CMAKE_COMMAND} -E echo
                        "ERROR: coverage_report requires gcovr. Install with: pip install gcovr"
                COMMAND ${CMAKE_COMMAND} -E false
                VERBATIM)
            message(WARNING "OPENTODO_ENABLE_COVERAGE=ON but gcovr not found. pip install gcovr.")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        find_program(LLVM_PROFDATA_EXE NAMES llvm-profdata)
        find_program(LLVM_COV_EXE      NAMES llvm-cov)
        if(LLVM_PROFDATA_EXE AND LLVM_COV_EXE)
            add_custom_target(coverage_report
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_OPENTODO_COVERAGE_DIR}"
                COMMAND ${CMAKE_COMMAND} -E env
                        "LLVM_PROFILE_FILE=${_OPENTODO_COVERAGE_DIR}/profile-%p.profraw"
                        ${CMAKE_CTEST_COMMAND} --output-on-failure
                COMMAND "${LLVM_PROFDATA_EXE}" merge -sparse
                        -o "${_OPENTODO_COVERAGE_DIR}/coverage.profdata"
                        "${_OPENTODO_COVERAGE_DIR}/profile-*.profraw"
                COMMAND "${LLVM_COV_EXE}" show
                        "$<TARGET_FILE:OpenTodo>"
                        "-instr-profile=${_OPENTODO_COVERAGE_DIR}/coverage.profdata"
                        -format=html
                        "-output-dir=${_OPENTODO_COVERAGE_DIR}"
                        "-ignore-filename-regex=(tests/|build/|${CMAKE_BINARY_DIR})"
                WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
                VERBATIM)
            add_dependencies(coverage_report OpenTodo)
            message(STATUS "Coverage report target available (llvm-cov at ${LLVM_COV_EXE})")
        else()
            add_custom_target(coverage_report
                COMMAND ${CMAKE_COMMAND} -E echo
                        "ERROR: coverage_report requires llvm-profdata + llvm-cov from LLVM toolchain"
                COMMAND ${CMAKE_COMMAND} -E false
                VERBATIM)
            message(WARNING "OPENTODO_ENABLE_COVERAGE=ON but LLVM tools not found.")
        endif()
    endif()
endif()

# ── Q3.11: Packaging (CPack) ─────────────────────────────────────────────────
# Build user-facing distributables — a portable .zip and (if NSIS is
# installed) a Windows .exe installer.
#
# Quick run-down:
#   1. Build release as usual:  cmake --build build/release
#   2. Generate packages:        cd build/release ; cpack
#   3. Outputs land in build/release/_CPack_Packages/ and at the build root.
#
# Driver script: tools/package.bat (Windows) — wraps the full flow.
# See docs/PACKAGING.md for the cheat-sheet, NSIS install instructions,
# and notes on the unsigned-binary SmartScreen warning users will see.
#
# Only Windows desktop is wired up right now. WASM and Android have their
# own deployment paths (Qt for WebAssembly produces a static html bundle;
# androiddeployqt produces an .apk via Gradle) — both out of scope here.
if(WIN32 AND NOT EMSCRIPTEN AND NOT ANDROID)

    # ── install() rules: lay the runtime out next to the exe ────────────────
    # Everything goes to the install prefix root (DESTINATION ".") so the
    # resulting tree is the same shape a portable .zip user would expect:
    #
    #   OpenTodo/
    #     OpenTodo.exe
    #     Qt6Core.dll  Qt6Gui.dll  Qt6Quick.dll  ...
    #     platforms/qwindows.dll
    #     styles/qmodernwindowsstyle.dll
    #     qml/QtQuick/...
    #     LICENSE  NOTICE  README.md
    #
    install(TARGETS OpenTodo
        RUNTIME DESTINATION .
        BUNDLE  DESTINATION .
        LIBRARY DESTINATION .
    )
    install(FILES
        "${CMAKE_SOURCE_DIR}/LICENSE"
        "${CMAKE_SOURCE_DIR}/NOTICE"
        "${CMAKE_SOURCE_DIR}/README.md"
        DESTINATION .
    )

    # windeployqt — bundles Qt6Core.dll, the platforms/ + styles/ plugins,
    # the QML module tree under qml/, and (when relevant) the runtime
    # MinGW DLLs. Without this the installed/zipped exe will not launch.
    # We register an install(CODE) hook so windeployqt runs against the
    # *staged* exe at install time — that way the same recipe drives both
    # `cmake --install` (for local testing) and `cpack` (for distribution).
    find_program(OPENTODO_WINDEPLOYQT
        NAMES windeployqt
        HINTS "${Qt6_DIR}/../../../bin"
              "${Qt6_DIR}/../../../../bin"
        DOC "Qt windeployqt — bundles Qt runtime DLLs and QML modules next to the exe.")
    if(OPENTODO_WINDEPLOYQT)
        message(STATUS "Packaging: windeployqt at ${OPENTODO_WINDEPLOYQT}")
        install(CODE "
            message(STATUS \"Running windeployqt against \${CMAKE_INSTALL_PREFIX}/OpenTodo.exe\")
            execute_process(
                COMMAND \"${OPENTODO_WINDEPLOYQT}\"
                    --release
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --qmldir \"${CMAKE_SOURCE_DIR}/qml\"
                    \"\${CMAKE_INSTALL_PREFIX}/OpenTodo.exe\"
                RESULT_VARIABLE _wq_rc
            )
            if(NOT _wq_rc EQUAL 0)
                message(FATAL_ERROR \"windeployqt failed (rc=\${_wq_rc})\")
            endif()
        ")
    else()
        message(WARNING "Packaging: windeployqt not found — `cpack` will produce a "
                        "package that probably cannot launch. Add \${Qt6_DIR}/../../../bin "
                        "to PATH or set OPENTODO_WINDEPLOYQT explicitly.")
    endif()

    # ── CPack metadata ──────────────────────────────────────────────────────
    set(CPACK_PACKAGE_NAME              "OpenTodo")
    set(CPACK_PACKAGE_VENDOR            "OpenTodo contributors")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Cross-platform task manager — local-first, Drive-syncable, MIT-licensed")
    set(CPACK_PACKAGE_VERSION_MAJOR     ${PROJECT_VERSION_MAJOR})
    set(CPACK_PACKAGE_VERSION_MINOR     ${PROJECT_VERSION_MINOR})
    set(CPACK_PACKAGE_VERSION_PATCH     ${PROJECT_VERSION_PATCH})
    set(CPACK_PACKAGE_VERSION           "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "OpenTodo")
    set(CPACK_PACKAGE_FILE_NAME
        "OpenTodo-${PROJECT_VERSION}-win64")
    set(CPACK_RESOURCE_FILE_LICENSE     "${CMAKE_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_README      "${CMAKE_SOURCE_DIR}/README.md")
    set(CPACK_PACKAGE_EXECUTABLES       "OpenTodo;OpenTodo")
    # Generators: always ZIP (portable bundle works without third-party
    # tools). NSIS is conditionally enabled below when makensis is on PATH.
    set(CPACK_GENERATOR                 "ZIP")
    set(CPACK_SOURCE_GENERATOR          "ZIP")
    set(CPACK_SOURCE_PACKAGE_FILE_NAME
        "OpenTodo-${PROJECT_VERSION}-source")
    set(CPACK_SOURCE_IGNORE_FILES
        "/\\\\.git/"
        "/build/"
        "/build-.*/"
        "/\\\\.claude/"
        "/\\\\.vscode/"
        "/_CPack_Packages/"
        "/cppcheck.*log"
        "\\\\.user$"
    )

    # ── NSIS installer (Windows .exe) — opt-in via makensis on PATH ─────────
    # NSIS produces a real Windows installer with a Start menu entry, an
    # uninstaller, and a desktop-shortcut option. To enable:
    #   winget install --id NSIS.NSIS  (recommended)
    # then re-configure with `cmake -B build/release` and re-run cpack.
    #
    # The installer is unsigned — first-run users will see Microsoft
    # SmartScreen's "Windows protected your PC" warning. Code-signing is
    # tracked separately in the backlog; see docs/PACKAGING.md.
    find_program(OPENTODO_MAKENSIS NAMES makensis
        HINTS "C:/Program Files (x86)/NSIS"
              "C:/Program Files/NSIS"
        DOC "NSIS makensis — if present, cpack also produces a Windows .exe installer.")
    if(OPENTODO_MAKENSIS)
        message(STATUS "Packaging: NSIS at ${OPENTODO_MAKENSIS} — installer enabled")
        list(APPEND CPACK_GENERATOR "NSIS")
        set(CPACK_NSIS_DISPLAY_NAME            "OpenTodo")
        set(CPACK_NSIS_PACKAGE_NAME            "OpenTodo")
        set(CPACK_NSIS_HELP_LINK
            "https://github.com/opentodo/opentodo")
        set(CPACK_NSIS_URL_INFO_ABOUT
            "https://github.com/opentodo/opentodo")
        set(CPACK_NSIS_CONTACT                 "opentodo@example.invalid")
        set(CPACK_NSIS_MODIFY_PATH             OFF)
        set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
        # Start menu shortcut — name shown ; relative .exe path.
        set(CPACK_NSIS_MENU_LINKS              "OpenTodo.exe" "OpenTodo")
        set(CPACK_NSIS_CREATE_ICONS_EXTRA
            "CreateShortCut '$DESKTOP\\\\OpenTodo.lnk' '$INSTDIR\\\\OpenTodo.exe'")
        set(CPACK_NSIS_DELETE_ICONS_EXTRA
            "Delete '$DESKTOP\\\\OpenTodo.lnk'")
        # Installer + uninstaller icons.
        set(_OPENTODO_NSIS_ICO
            "${CMAKE_SOURCE_DIR}/resources/icons/icon.ico")
        if(EXISTS "${_OPENTODO_NSIS_ICO}")
            # CPack's NSIS generator wants Windows-style backslashes here.
            file(TO_NATIVE_PATH "${_OPENTODO_NSIS_ICO}" _OPENTODO_NSIS_ICO_NATIVE)
            string(REPLACE "\\" "\\\\" _OPENTODO_NSIS_ICO_ESCAPED
                "${_OPENTODO_NSIS_ICO_NATIVE}")
            set(CPACK_NSIS_MUI_ICON       "${_OPENTODO_NSIS_ICO_ESCAPED}")
            set(CPACK_NSIS_MUI_UNIICON    "${_OPENTODO_NSIS_ICO_ESCAPED}")
            set(CPACK_NSIS_INSTALLED_ICON_NAME "OpenTodo.exe")
        endif()
    else()
        message(STATUS "Packaging: NSIS not on PATH — only ZIP will be produced. "
                       "Install with `winget install NSIS.NSIS` for an .exe installer.")
    endif()

    include(CPack)
endif()
