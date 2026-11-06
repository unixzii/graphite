add_library(graphite
    core/base.c
    core/resource.c
    core/path.c
    core/canvas.c
    core/scene.c
    core/layer.c
    core/resolve.c
)
add_library(graphite::graphite ALIAS graphite)
target_include_directories(graphite PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(graphite PUBLIC c_std_11)
set_target_properties(graphite PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(graphite PUBLIC /utf-8 "$<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>")
endif()
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
