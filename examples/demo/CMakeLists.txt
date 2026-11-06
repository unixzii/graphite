add_library(graphite_demo_scenes STATIC
    scenes/util.c scenes/images.c scenes/ui.c
    scenes/showcase/showcase.c scenes/showcase/transforms.c scenes/showcase/canvas.c
    scenes/showcase/shapes.c scenes/showcase/masks.c scenes/showcase/clipping.c
    scenes/showcase/compositing.c)
target_include_directories(graphite_demo_scenes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(graphite_demo_scenes PUBLIC graphite)
set_target_properties(graphite_demo_scenes PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(NOT MSVC)
    target_compile_options(graphite_demo_scenes PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(APPLE)
    add_executable(graphite_demo main.c platform/macos.m)
    target_link_libraries(graphite_demo PRIVATE graphite_demo_scenes graphite_macos "-framework ImageIO")
    target_compile_options(graphite_demo PRIVATE -Wall -Wextra -Wpedantic -Werror
        "$<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>" "$<$<COMPILE_LANGUAGE:OBJC>:-fblocks>")
elseif(WIN32)
    add_executable(graphite_demo main.c platform/windows.cpp)
    target_compile_features(graphite_demo PRIVATE cxx_std_20)
    target_compile_definitions(graphite_demo PRIVATE NOMINMAX UNICODE _UNICODE)
    target_link_libraries(graphite_demo PRIVATE graphite_demo_scenes graphite_windows windowscodecs)
    target_compile_options(graphite_demo PRIVATE /W4 /WX /EHsc)
elseif(TARGET graphite_gtk)
    add_executable(graphite_demo main.c platform/gtk.c)
    target_link_libraries(graphite_demo PRIVATE graphite_demo_scenes graphite_gtk)
    target_compile_options(graphite_demo PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(TARGET graphite_demo)
    set_target_properties(graphite_demo PROPERTIES OUTPUT_NAME demo C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    if(BUILD_TESTING)
        add_test(NAME graphite.demo.showcase COMMAND graphite_demo --selftest)
        add_test(NAME graphite.demo.ui COMMAND graphite_demo --scene ui --selftest)
        set_tests_properties(graphite.demo.showcase graphite.demo.ui PROPERTIES RUN_SERIAL TRUE TIMEOUT 20)
    endif()
endif()
