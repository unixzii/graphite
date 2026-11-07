# Each example is its own executable built on the shared layer in shared/:
# graphite_example (scene helpers and lifecycle) and graphite_example_shell
# (command line, selftest, snapshot and the native window for this platform).
add_subdirectory(shared)

# graphite_add_example(<name> <sources>...) builds the executable <name> and
# registers its selftest. Without a native shell the sources still compile.
function(graphite_add_example name)
    set(target graphite_example_${name})
    if(TARGET graphite_example_shell)
        add_executable(${target} ${ARGN})
        target_link_libraries(${target} PRIVATE graphite_example_shell)
        set_target_properties(${target} PROPERTIES OUTPUT_NAME ${name})
        if(BUILD_TESTING)
            add_test(NAME graphite.examples.${name} COMMAND ${target} --selftest)
            set_tests_properties(graphite.examples.${name} PROPERTIES RUN_SERIAL TRUE TIMEOUT 20)
        endif()
    else()
        add_library(${target} OBJECT ${ARGN})
        target_link_libraries(${target} PRIVATE graphite_example)
    endif()
    set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    set_property(GLOBAL APPEND PROPERTY GRAPHITE_EXAMPLE_TARGETS ${target})
endfunction()

add_subdirectory(showcase)
add_subdirectory(ui)
add_subdirectory(stress)
add_subdirectory(glyphs)
add_subdirectory(input)
