cmake_minimum_required(VERSION 3.20)
project(graphite VERSION 0.0.1 LANGUAGES C)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(GRAPHITE_BUILD_EXAMPLES "Build examples" ON)
option(GRAPHITE_BUILD_CONFORMANCE "Build conformance tools" ON)
option(GRAPHITE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
option(GRAPHITE_GTK "Build the GTK 4.22 backend" OFF)

if(WIN32 AND BUILD_SHARED_LIBS)
    # Keep build-tree executables beside their DLL dependencies.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
endif()

add_subdirectory(src)

if(APPLE)
    enable_language(OBJC)
    add_subdirectory(src/platforms/macos)
endif()

if(WIN32)
    enable_language(CXX)
    add_subdirectory(src/platforms/windows)
endif()

if(GRAPHITE_GTK)
    add_subdirectory(src/platforms/gtk)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(GRAPHITE_BUILD_CONFORMANCE)
    add_subdirectory(conformance)
endif()

if(GRAPHITE_BUILD_EXAMPLES)
    add_subdirectory(examples/demo)
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
        DEPENDS graphite_resource_tests graphite_scene_tests graphite_text_tests format-check
        USES_TERMINAL)
    if(TARGET graphite_conformance_tests)
        add_dependencies(check graphite_conformance_tests)
    endif()
    if(TARGET graphite_demo_scenes)
        add_dependencies(check graphite_demo_scenes)
    endif()
    if(TARGET graphite_windows_tests)
        add_dependencies(check graphite_windows_tests)
    endif()
    if(TARGET graphite_windows_conformance)
        add_dependencies(check graphite_windows_conformance)
    endif()
    if(TARGET graphite_gtk_tests)
        add_dependencies(check graphite_gtk_tests)
    endif()
    if(TARGET graphite_gtk_conformance)
        add_dependencies(check graphite_gtk_conformance)
    endif()
    if(TARGET graphite_demo)
        add_dependencies(check graphite_demo)
    endif()
    if(TARGET graphite_macos_tests)
        add_dependencies(check graphite_macos_tests)
    endif()
    if(TARGET graphite_macos_conformance)
        add_dependencies(check graphite_macos_conformance)
    endif()
endif()

include(cmake/Install.cmake)
