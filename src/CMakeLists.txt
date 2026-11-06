add_library(graphite STATIC
    core/base.c
    core/resource.c
    core/path.c
    core/canvas.c
    core/scene.c
    core/layer.c
    core/resolve.c
)
add_library(graphite::graphite ALIAS graphite)
target_include_directories(graphite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(graphite PUBLIC c_std_11)
set_target_properties(graphite PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(NOT MSVC)
    target_compile_options(graphite PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_libraries(graphite PUBLIC m)
endif()
if(GRAPHITE_SANITIZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "GRAPHITE_SANITIZERS requires Clang or GCC")
    endif()
    target_compile_options(graphite PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(graphite PUBLIC -fsanitize=address,undefined)
endif()

if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    add_library(graphite_test_harness STATIC tests/test.c)
    target_link_libraries(graphite_test_harness PUBLIC graphite)
    target_include_directories(graphite_test_harness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    if(NOT MSVC)
        target_compile_options(graphite_test_harness PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    function(graphite_add_suite target suite)
        add_executable(${target} ${ARGN})
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core)
        target_link_libraries(${target} PRIVATE graphite_test_harness Threads::Threads)
        if(NOT MSVC)
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME graphite.${suite} COMMAND ${target})
    endfunction()

    graphite_add_suite(graphite_resource_tests resources tests/resources.c)
    graphite_add_suite(graphite_scene_tests scene tests/scene.c review/lifecycle.c)
endif()
