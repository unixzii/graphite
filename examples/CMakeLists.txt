# Each example is its own executable built on the shared layer in shared/:
# graphite_example (scene helpers and lifecycle) and graphite_example_shell
# (command line, selftest, snapshot and the native window for this platform).
add_subdirectory(shared)

# Selftests run the executable itself; on the Web they run its script in
# headless Chromium, once tests/web has its packages (npm ci).
set(graphite_example_selftests ${BUILD_TESTING})
set(graphite_example_runner)
if(TARGET graphite_web)
    set(graphite_example_runner ${GRAPHITE_NODE} ${PROJECT_SOURCE_DIR}/tests/web/run.mjs)
    if(NOT GRAPHITE_NODE OR NOT EXISTS ${PROJECT_SOURCE_DIR}/tests/web/node_modules/playwright)
        set(graphite_example_selftests OFF)
    endif()
endif()

# graphite_add_example(<name> <sources>...) builds the executable <name> and
# registers its selftest. Without a native shell the sources still compile.
function(graphite_add_example name)
    set(target graphite_example_${name})
    if(TARGET graphite_example_shell)
        add_executable(${target} ${ARGN})
        target_link_libraries(${target} PRIVATE graphite_example_shell)
        set_target_properties(${target} PROPERTIES OUTPUT_NAME ${name})
    else()
        add_library(${target} OBJECT ${ARGN})
        target_link_libraries(${target} PRIVATE graphite_example)
    endif()
    set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    set_property(GLOBAL APPEND PROPERTY GRAPHITE_EXAMPLE_TARGETS ${target})
    if(NOT TARGET graphite_example_shell)
        return()
    endif()

    if(TARGET graphite_web)
        # <name>.js runs in <name>.html, which the browser opens straight from the build directory.
        set(GRAPHITE_EXAMPLE_NAME ${name})
        configure_file(${PROJECT_SOURCE_DIR}/examples/shared/platform/web.html.in
            ${CMAKE_CURRENT_BINARY_DIR}/${name}.html @ONLY)
    endif()
    if(graphite_example_selftests)
        add_test(NAME graphite.examples.${name}
            COMMAND ${graphite_example_runner} $<TARGET_FILE:${target}> --selftest)
        set_tests_properties(graphite.examples.${name} PROPERTIES RUN_SERIAL TRUE TIMEOUT 20)
    endif()
endfunction()

add_subdirectory(showcase)
add_subdirectory(ui)
add_subdirectory(stress)
add_subdirectory(glyphs)
add_subdirectory(input)
add_subdirectory(mines)
