cmake_minimum_required(VERSION 4.0)
project(WayPicker C)

set(CMAKE_C_STANDARD 11)

find_package(PkgConfig REQUIRED)

# --- GTK4 ---
pkg_check_modules(GTK4 REQUIRED gtk4)
include_directories(${GTK4_INCLUDE_DIRS})
link_directories(${GTK4_LIBRARY_DIRS})
add_definitions(${GTK4_CFLAGS_OTHER})

# --- libportal-gtk4 ---
pkg_check_modules(PORTAL REQUIRED libportal-gtk4)
include_directories(${PORTAL_INCLUDE_DIRS})
link_directories(${PORTAL_LIBRARY_DIRS})
add_definitions(${PORTAL_CFLAGS_OTHER})

# --- Build executable ---
add_executable(WayPicker main.c)

# Link both GTK4 and portal libraries
target_link_libraries(WayPicker ${GTK4_LIBRARIES} ${PORTAL_LIBRARIES})

add_custom_command(TARGET WayPicker POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/styles.css
        $<TARGET_FILE_DIR:WayPicker>/styles.css)