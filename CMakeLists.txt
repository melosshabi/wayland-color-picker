cmake_minimum_required(VERSION 3.16)

project(WayPicker LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)

# GTK4
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)

# libportal-gtk4
pkg_check_modules(PORTAL REQUIRED IMPORTED_TARGET libportal-gtk4)

add_executable(WayPicker
    main.c
)

target_link_libraries(WayPicker
    PRIVATE
        PkgConfig::GTK4
        PkgConfig::PORTAL
)

# Copy runtime files
add_custom_command(
    TARGET WayPicker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/styles.css
            $<TARGET_FILE_DIR:WayPicker>/styles.css
)

file(COPY ${CMAKE_SOURCE_DIR}/ui
     DESTINATION ${CMAKE_BINARY_DIR})
