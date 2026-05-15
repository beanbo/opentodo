cmake_minimum_required(VERSION 3.21)
project(OpenTodo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ── WASM detection ────────────────────────────────────────────────────────────
# EMSCRIPTEN is set automatically when the emscripten toolchain is loaded.
# We also normalise a convenience variable used in comments/messages.
if(EMSCRIPTEN)
    message(STATUS "OpenTodo: configuring for WebAssembly (Emscripten)")
endif()

# Qt::Widgets is not meaningful in a WASM/QML-only build but it IS available
# in Qt WASM builds (statically bundled).  We keep it in REQUIRED so that
# the same CMakeLists.txt works unchanged for desktop, Android, and WASM.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml Network Widgets QuickControls2)
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
    src/sync/commit.cpp
    src/sync/syncengine.cpp
    src/sync/merger.cpp
    src/sync/hlc.cpp
    src/sync/crdt_meta.cpp
    src/sync/gdrive.cpp
    src/sync/oauth_client.cpp
    src/sync/drive_api.cpp
    src/sync/snapshot.cpp
    src/services/crashlogger.cpp
    src/services/dailyreviewservice.cpp
    src/services/pomodoroservice.cpp
    src/services/reminderscheduler.cpp
    src/services/windowchrome.cpp
    src/services/undostack.cpp
    src/services/aiassistant.cpp
    src/services/updatechecker.cpp
    src/services/telemetry.cpp
    src/services/taskexporter.cpp
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
    src/sync/gdrive.h
    src/sync/oauth_client.h
    src/sync/drive_api.h
    src/sync/snapshot.h
    src/services/notificationservice.h
    src/services/hotkeyservice.h
    src/services/pomodoroservice.h
    src/services/reminderscheduler.h
    src/services/windowchrome.h
    src/services/undostack.h
    src/services/aiassistant.h
    src/services/crashlogger.h
    src/services/dailyreviewservice.h
    src/services/updatechecker.h
    src/services/telemetry.h
    src/services/taskexporter.h
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
    )
    list(APPEND INFRA_HEADERS
        src/services/notificationservice_win.h
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
    list(APPEND INFRA_SOURCES
        src/services/hotkeyservice_stub.cpp
    )
    list(APPEND INFRA_HEADERS
        src/services/notificationservice_stub.h
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
    qml/components/DatePicker.qml
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
    # B-7.26: ProjectItem.qml deleted — was dead code, never referenced by any Loader or createComponent
    qml/components/FilterQueryInput.qml
    qml/components/EmptyState.qml
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
qt_add_executable(OpenTodo src/main.cpp)

# Link the App library (which now carries the QML module) and the generated
# plugin target.  qt_add_qml_module on a library creates a companion plugin
# target named <target>plugin; linking it ensures the module is initialised
# before QQmlApplicationEngine::loadFromModule runs.
target_link_libraries(OpenTodo PRIVATE OpenTodoApp OpenTodoAppplugin)

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
    set_property(TARGET OpenTodo APPEND PROPERTY
        QT_ANDROID_EXTRA_LIBS ""
    )
    set_target_properties(OpenTodo PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    )
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
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
