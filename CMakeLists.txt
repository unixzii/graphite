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
option(GRAPHITE_LTO "Enable link-time optimization outside Debug builds" OFF)

if(WIN32 AND BUILD_SHARED_LIBS)
    # Keep build-tree executables beside their DLL dependencies.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
endif()

# Only GR_API functions are exported from shared libraries (see include/graphite/types.h).
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_OBJC_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Optimized builds drop unreferenced functions and data at link time. Mach-O
# strips per symbol already; ELF and MSVC need one section per function/datum.
set(graphite_optimized "$<CONFIG:Release,RelWithDebInfo,MinSizeRel>")
if(MSVC)
    add_compile_options("$<${graphite_optimized}:/Gy;/Gw>")
    add_link_options("$<${graphite_optimized}:/OPT:REF;/OPT:ICF>")
elseif(APPLE)
    add_link_options("$<${graphite_optimized}:LINKER:-dead_strip>")
else()
    add_compile_options("$<${graphite_optimized}:-ffunction-sections;-fdata-sections>")
    add_link_options("$<${graphite_optimized}:LINKER:--gc-sections>")
endif()

if(GRAPHITE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT graphite_lto_supported OUTPUT graphite_lto_output LANGUAGES C)
    if(NOT graphite_lto_supported)
        message(FATAL_ERROR "GRAPHITE_LTO is not supported by this toolchain:\n${graphite_lto_output}")
    endif()
    foreach(config RELEASE RELWITHDEBINFO MINSIZEREL)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_${config} ON)
    endforeach()
    if(APPLE)
        # CMake has no IPO flags for Objective-C; the macOS backend reuses the C compiler's.
        add_compile_options("$<$<AND:$<COMPILE_LANGUAGE:OBJC>,${graphite_optimized}>:${CMAKE_C_COMPILE_OPTIONS_IPO}>")
    endif()
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
    add_subdirectory(examples)
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
    get_property(graphite_example_targets GLOBAL PROPERTY GRAPHITE_EXAMPLE_TARGETS)
    if(graphite_example_targets)
        add_dependencies(check ${graphite_example_targets})
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
    if(TARGET graphite_macos_tests)
        add_dependencies(check graphite_macos_tests)
    endif()
    if(TARGET graphite_macos_conformance)
        add_dependencies(check graphite_macos_conformance)
    endif()
endif()

include(cmake/Install.cmake)
