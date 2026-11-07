set(graphite_install_targets graphite)
set(GRAPHITE_PACKAGE_GTK OFF)

foreach(platform macos gtk windows)
    if(TARGET graphite_${platform})
        list(APPEND graphite_install_targets graphite_${platform})
        set_target_properties(graphite_${platform} PROPERTIES EXPORT_NAME ${platform})
        if(platform STREQUAL "gtk")
            set(GRAPHITE_PACKAGE_GTK ON)
        endif()
    endif()
endforeach()

foreach(target IN LISTS graphite_install_targets)
    if(APPLE)
        set_target_properties(${target} PROPERTIES INSTALL_RPATH "@loader_path")
    elseif(UNIX)
        set_target_properties(${target} PROPERTIES INSTALL_RPATH "$ORIGIN")
    endif()
    set_target_properties(${target} PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
endforeach()

install(TARGETS ${graphite_install_targets} EXPORT graphiteTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/graphite DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

set(graphite_package_dir ${CMAKE_INSTALL_LIBDIR}/cmake/graphite)
configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/graphiteConfig.cmake.in
    ${PROJECT_BINARY_DIR}/graphiteConfig.cmake
    INSTALL_DESTINATION ${graphite_package_dir})
write_basic_package_version_file(${PROJECT_BINARY_DIR}/graphiteConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(EXPORT graphiteTargets NAMESPACE graphite:: DESTINATION ${graphite_package_dir})
install(FILES ${PROJECT_BINARY_DIR}/graphiteConfig.cmake
    ${PROJECT_BINARY_DIR}/graphiteConfigVersion.cmake DESTINATION ${graphite_package_dir})
