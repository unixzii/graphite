cmake_minimum_required(VERSION 3.20)
project(graphite VERSION 0.0.1 LANGUAGES C)

include(CTest)
option(GRAPHITE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
add_subdirectory(src)
if(APPLE)
    enable_language(OBJC)
    add_subdirectory(src/platforms/macos)
endif()
option(GRAPHITE_GTK "Build the GTK 4.22 backend" OFF)
if(GRAPHITE_GTK)
    add_subdirectory(src/platforms/gtk)
endif()
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
add_subdirectory(conformance)
add_subdirectory(examples/demo)

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
    add_dependencies(check graphite_conformance_tests graphite_demo_scenes)
    if(TARGET graphite_gtk_conformance)
        add_dependencies(check graphite_gtk_conformance graphite_gtk_tests)
    endif()
    if(TARGET graphite_demo)
        add_dependencies(check graphite_demo)
    endif()
    if(TARGET graphite_macos_conformance)
        add_dependencies(check graphite_macos_conformance graphite_macos_tests)
    endif()
endif()
