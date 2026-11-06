cmake_minimum_required(VERSION 3.20)
project(graphite VERSION 0.0.1 LANGUAGES C)

include(CTest)
option(GRAPHITE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

set(CLANG_FORMAT "" CACHE FILEPATH "Path to clang-format 18")
add_custom_target(format-check
    COMMAND ${CMAKE_COMMAND} -DGRAPHITE_ROOT=${PROJECT_SOURCE_DIR}
        -DCLANG_FORMAT=${CLANG_FORMAT} -P ${PROJECT_SOURCE_DIR}/cmake/Format.cmake
    VERBATIM)
add_custom_target(format
    COMMAND ${CMAKE_COMMAND} -DGRAPHITE_ROOT=${PROJECT_SOURCE_DIR}
        -DCLANG_FORMAT=${CLANG_FORMAT} -DFORMAT_WRITE=ON
        -P ${PROJECT_SOURCE_DIR}/cmake/Format.cmake
    VERBATIM)
if(BUILD_TESTING)
    add_test(NAME graphite.format
        COMMAND ${CMAKE_COMMAND} -DGRAPHITE_ROOT=${PROJECT_SOURCE_DIR}
            -DCLANG_FORMAT=${CLANG_FORMAT} -P ${PROJECT_SOURCE_DIR}/cmake/Format.cmake)
    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --verbose --output-on-failure
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
        DEPENDS graphite_resource_tests graphite_scene_tests format-check
        USES_TERMINAL)
endif()
