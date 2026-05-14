# QtQuickTest aggregator: one executable runs every tst_*.qml in this directory.
qt_add_executable(qmltest_runner main.cpp)
target_link_libraries(qmltest_runner PRIVATE
    OpenTodoApp
    Qt6::QuickTest
    Qt6::Quick
    Qt6::QuickControls2
)

# The QML test runner needs to know where the `OpenTodo` QML module's qmldir
# lives — it sits next to the main app's build artefacts (top of build tree).
#
# B-7.qml-hang: On Windows, QT_QPA_PLATFORM=offscreen causes the qmltest_runner
# to hang indefinitely (the offscreen platform plugin never completes window
# initialisation in this static-library configuration).  Use the native Windows
# platform plugin instead; tests are still headless because QUICK_TEST_MAIN
# doesn't require a visible display.
if(WIN32)
    set(_QML_QPA "windows")
else()
    set(_QML_QPA "offscreen")
endif()

add_test(NAME qml_components
         COMMAND qmltest_runner -input ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(qml_components PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=${_QML_QPA};QML2_IMPORT_PATH=${CMAKE_BINARY_DIR};QML_IMPORT_PATH=${CMAKE_BINARY_DIR}"
)
