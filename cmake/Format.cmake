if(NOT DEFINED GRAPHITE_ROOT)
    message(FATAL_ERROR "GRAPHITE_ROOT is required")
endif()
if(NOT CLANG_FORMAT)
    unset(CLANG_FORMAT)
    unset(CLANG_FORMAT CACHE)
    find_program(CLANG_FORMAT NAMES clang-format-18 clang-format)
endif()
if(NOT CLANG_FORMAT)
    message(FATAL_ERROR "Install clang-format 18 or set CLANG_FORMAT to its executable")
endif()
execute_process(COMMAND "${CLANG_FORMAT}" --version OUTPUT_VARIABLE version RESULT_VARIABLE result)
if(NOT result EQUAL 0 OR NOT version MATCHES "clang-format version 18\\.")
    message(FATAL_ERROR "Expected clang-format 18, got: ${version}")
endif()
set(sources)
foreach(directory src include tests conformance examples/demo benchmarks)
    file(GLOB_RECURSE files
        "${GRAPHITE_ROOT}/${directory}/*.c" "${GRAPHITE_ROOT}/${directory}/*.h"
        "${GRAPHITE_ROOT}/${directory}/*.hpp" "${GRAPHITE_ROOT}/${directory}/*.cpp" "${GRAPHITE_ROOT}/${directory}/*.m"
        "${GRAPHITE_ROOT}/${directory}/*.mm")
    list(APPEND sources ${files})
endforeach()
list(SORT sources)
if(FORMAT_WRITE)
    set(mode -i)
else()
    set(mode --dry-run --Werror)
endif()
execute_process(
    COMMAND "${CLANG_FORMAT}" ${mode} ${sources}
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "C formatting failed. Run the format target with clang-format 18.")
endif()

# Blank lines clang-format cannot enforce: after the include guard, after the
# leading include block, around extern "C" wrappers and before the guard's #endif.
function(graphite_layout input output)
    set(text "${input}")
    set(include_line "#(include|import) [^\n]*\n")
    # Include guard opening.
    string(REGEX REPLACE "(^|\n)(#ifndef [A-Z0-9_]+_HP?P?\n#define [A-Z0-9_]+_HP?P?\n)([^\n])" "\\1\\2\n\\3"
        text "${text}")
    # Configuration macros (such as NOMINMAX) before the includes.
    string(REGEX REPLACE "(^|\n)(#(define|endif)[^\n]*\n)(#(include|import) (<|\"[^\n\"]*\\.hp?p?\"))" "\\1\\2\n\\4"
        text "${text}")
    # Includes followed by code, a macro definition or an extern "C" wrapper.
    string(REGEX REPLACE "(^|\n)(${include_line})([^#\n])" "\\1\\2\n\\4" text "${text}")
    string(REGEX REPLACE "(^|\n)(#(include|import) (<[^\n>]*>|\"[^\n\"]*\\.hp?p?\")\n)(#(define|undef|pragma|ifdef __cplusplus))"
        "\\1\\2\n\\5" text "${text}")
    # Conditional includes followed by code.
    string(REGEX REPLACE "(\n${include_line}#endif[^\n]*\n)([^#\n])" "\\1\n\\3" text "${text}")
    # extern "C" wrappers.
    string(REGEX REPLACE "([^\n]\n)(#ifdef __cplusplus\nextern \"C\")" "\\1\n\\2" text "${text}")
    string(REGEX REPLACE "(extern \"C\" {\n#endif\n)([^\n])" "\\1\n\\2" text "${text}")
    string(REGEX REPLACE "([^\n]\n)(#ifdef __cplusplus\n}\n#endif)" "\\1\n\\2" text "${text}")
    string(REGEX REPLACE "(\n#ifdef __cplusplus\n}\n#endif\n)([^\n])" "\\1\n\\2" text "${text}")
    # Include guard closing.
    if(text MATCHES "(^|\n)#ifndef [A-Z0-9_]+_HP?P?\n#define [A-Z0-9_]+_HP?P?\n")
        string(REGEX REPLACE "([^\n]\n)(#endif[^\n]*\n*)$" "\\1\n\\2" text "${text}")
    endif()
    set(${output} "${text}" PARENT_SCOPE)
endfunction()

set(unformatted)
foreach(source IN LISTS sources)
    file(READ "${source}" original)
    string(REPLACE "\r\n" "\n" original "${original}")
    graphite_layout("${original}" fixed)
    if(NOT fixed STREQUAL original)
        if(FORMAT_WRITE)
            file(WRITE "${source}" "${fixed}")
        else()
            file(RELATIVE_PATH relative "${GRAPHITE_ROOT}" "${source}")
            list(APPEND unformatted "${relative}")
        endif()
    endif()
endforeach()
if(unformatted)
    list(JOIN unformatted "\n  " unformatted)
    message(FATAL_ERROR "Blank-line layout differs in:\n  ${unformatted}\nRun the format target with clang-format 18.")
endif()
