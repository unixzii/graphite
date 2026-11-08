add_library(graphite_web backend.c host.c text.c)
add_library(graphite::web ALIAS graphite_web)
target_link_libraries(graphite_web PUBLIC graphite)
target_compile_options(graphite_web PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_include_directories(graphite_web PRIVATE ${PROJECT_SOURCE_DIR}/src/core)
target_compile_definitions(graphite_web PRIVATE GRAPHITE_WEB_BUILDING)
set_target_properties(graphite_web PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
# The DOM side of the backend; every executable linking graphite_web links it too.
target_link_options(graphite_web PUBLIC
    "SHELL:--js-library $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/runtime.js>$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_DATADIR}/graphite/graphite_web.js>"
    "SHELL:--js-library $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/text.js>$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${CMAKE_INSTALL_DATADIR}/graphite/graphite_web_text.js>")
set_property(TARGET graphite_web APPEND PROPERTY INTERFACE_LINK_DEPENDS
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/runtime.js> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/text.js>)
install(FILES runtime.js DESTINATION ${CMAKE_INSTALL_DATADIR}/graphite RENAME graphite_web.js)
install(FILES text.js DESTINATION ${CMAKE_INSTALL_DATADIR}/graphite RENAME graphite_web_text.js)

if(BUILD_TESTING)
    # Runs in a browser: tests/web drives Chromium through Playwright (npm ci there first).
    add_executable(graphite_web_tests ${PROJECT_SOURCE_DIR}/tests/web.c)
    target_compile_options(graphite_web_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_libraries(graphite_web_tests PRIVATE graphite_web graphite_test_harness)
    target_link_options(graphite_web_tests PRIVATE -sSINGLE_FILE=1 -sEXIT_RUNTIME=1 -sENVIRONMENT=web
        "SHELL:--js-library ${PROJECT_SOURCE_DIR}/tests/web.js")
    set_property(TARGET graphite_web_tests APPEND PROPERTY LINK_DEPENDS ${PROJECT_SOURCE_DIR}/tests/web.js)
    find_program(GRAPHITE_NODE node)
    if(GRAPHITE_NODE AND EXISTS ${PROJECT_SOURCE_DIR}/tests/web/node_modules/playwright)
        add_test(NAME graphite.web
            COMMAND ${GRAPHITE_NODE} ${PROJECT_SOURCE_DIR}/tests/web/run.mjs $<TARGET_FILE:graphite_web_tests>)
    else()
        message(STATUS "graphite.web browser test skipped: run `npm ci` in tests/web")
    endif()
endif()
