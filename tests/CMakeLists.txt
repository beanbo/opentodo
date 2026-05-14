find_package(Qt6 REQUIRED COMPONENTS Test QuickTest)

# Per-test executable convenience: each test is a single .cpp linked against
# the appropriate layer library + Qt6::Test, registered with ctest.
# Usage: opentodo_add_unit_test(<name> <layer>)
#   layer is one of: Domain, Infra, App
function(opentodo_add_unit_test name layer)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE OpenTodo${layer} Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    # Tests must be able to find Qt platform plugin etc.; CTest runs from $<BUILD>.
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_subdirectory(unit)
add_subdirectory(qml)
