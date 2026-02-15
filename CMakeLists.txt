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

# json-glib
pkg_check_modules(JSON_GLIB REQUIRED IMPORTED_TARGET json-glib-1.0)

# Find glib-compile-resources
find_program(GLIB_COMPILE_RESOURCES NAMES glib-compile-resources REQUIRED)

# Resource compilation
set(RESOURCE_FILE ${CMAKE_SOURCE_DIR}/resources.gresource.xml)
set(RESOURCE_OUTPUT ${CMAKE_BINARY_DIR}/resources.c)
set(RESOURCE_HEADER ${CMAKE_BINARY_DIR}/resources.h)

# Generate both .c and .h files
add_custom_command(
    OUTPUT ${RESOURCE_OUTPUT} ${RESOURCE_HEADER}
    COMMAND ${GLIB_COMPILE_RESOURCES}
            --target=${RESOURCE_OUTPUT}
            --generate-source
            --sourcedir=${CMAKE_SOURCE_DIR}
            ${RESOURCE_FILE}
    COMMAND ${GLIB_COMPILE_RESOURCES}
            --target=${RESOURCE_HEADER}
            --generate-header
            --sourcedir=${CMAKE_SOURCE_DIR}
            ${RESOURCE_FILE}
    DEPENDS ${RESOURCE_FILE}
            ${CMAKE_SOURCE_DIR}/styles.css
            ${CMAKE_SOURCE_DIR}/ui/init.ui
            ${CMAKE_SOURCE_DIR}/ui/help.ui
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Compiling GLib resources"
)

add_executable(WayPicker
    main.c
    color_history.c
    ${RESOURCE_OUTPUT}
)

# Add build directory to include path so we can find resources.h
target_include_directories(WayPicker PRIVATE ${CMAKE_BINARY_DIR})

target_link_libraries(WayPicker
    PRIVATE
        PkgConfig::GTK4
        PkgConfig::PORTAL
        PkgConfig::JSON_GLIB
)
