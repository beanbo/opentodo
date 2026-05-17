# libFuzzer harnesses for user-input parsers.
#
# Opt-in: -DOPENTODO_ENABLE_FUZZ=ON.  Requires Clang because the fuzzer is
# tightly coupled to LLVM's instrumentation (-fsanitize=fuzzer).  MSVC and
# GCC will not build these targets.
#
# Harnesses:
#   fuzz_filterparser     — FilterParser::parse + isValid
#   fuzz_dateparser       — DateParser::parseEnglish / parseRussian / parse
#   fuzz_quickaddparser   — QuickAddParser::parse (also drives DateParser)
#
# These targets are NEVER added to the default build — they are conditionally
# created here and excluded from `all` so a normal `cmake --build` still works
# under MSVC/GCC/MinGW.

if(NOT OPENTODO_ENABLE_FUZZ)
    return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "OPENTODO_ENABLE_FUZZ requires Clang (got '${CMAKE_CXX_COMPILER_ID}'). "
        "Re-configure with -DCMAKE_CXX_COMPILER=clang++ "
        "(or clang-cl on Windows). libFuzzer is not available with MSVC/GCC.")
endif()

message(STATUS "OpenTodo: building libFuzzer harnesses "
               "(compiler=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION})")

# libFuzzer + ASan + UBSan. ASan catches OOB / UAF, UBSan catches signed
# overflow / unaligned access — both relevant to the integer-parsing paths
# in dateparser (rules_en.cpp does `n * 7` on a regex-captured int).
set(_OPENTODO_FUZZ_FLAGS -fsanitize=fuzzer,address,undefined)

# Parsers all live in the Domain layer (OpenTodoDomain). Linking the
# smallest viable Qt surface keeps fuzz binaries lean and avoids dragging
# in the QML engine just to exercise a string -> AST/struct parser.
foreach(_parser IN ITEMS filterparser dateparser quickaddparser)
    set(_tgt fuzz_${_parser})
    add_executable(${_tgt} EXCLUDE_FROM_ALL fuzz_${_parser}.cpp)
    target_link_libraries(${_tgt} PRIVATE OpenTodoDomain)
    target_compile_options(${_tgt} PRIVATE ${_OPENTODO_FUZZ_FLAGS})
    target_link_options(${_tgt} PRIVATE ${_OPENTODO_FUZZ_FLAGS})
    # Use the same C++20 standard as the rest of the project.
    set_target_properties(${_tgt} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )
endforeach()

# Convenience aggregate: `cmake --build build-fuzz --target fuzz_all` builds
# every harness in one invocation.
add_custom_target(fuzz_all
    DEPENDS fuzz_filterparser fuzz_dateparser fuzz_quickaddparser
    COMMENT "Built all OpenTodo libFuzzer harnesses"
)
