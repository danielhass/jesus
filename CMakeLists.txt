cmake_minimum_required(VERSION 3.0.2)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

project(efm)

include(GNUInstallDirs)

option(Debug "Show the debug screen" OFF)

enable_testing()
set(CMAKE_BUILD_TYPE DEBUG)

add_definitions ("-Wall -Wextra -fvisibility=hidden -std=gnu99")

find_package(Eina REQUIRED)
find_package(Eo 1.15 REQUIRED)
find_package(Eolian 1.15 REQUIRED)
find_package(Eet REQUIRED)
find_package(Efl REQUIRED)
find_package(Eldbus REQUIRED)
find_package(Efreet COMPONENTS mime trash)
find_package(Eio REQUIRED)
find_package(Evas REQUIRED)
find_package(Edje REQUIRED)
find_package(Ecore COMPONENTS file)
find_package(Elementary 1.17.0 REQUIRED)
find_package(LibArchive REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(Mount REQUIRED mount)
pkg_check_modules(Check REQUIRED check)

if (${Eolian_VERSION} VERSION_LESS 1.16.99)
    message("Using legecy eo_helper")
    include(eo_helper)
endif (${Eolian_VERSION} VERSION_LESS 1.16.99)

add_subdirectory(data/theme/elm_ext)
add_subdirectory(src/lib/emous)
add_subdirectory(src/lib/emous/modules/udisk)
add_subdirectory(src/lib/efm)
add_subdirectory(src/widgets)
add_subdirectory(src/test)
add_subdirectory(src/bin)

INSTALL(FILES ${CMAKE_SOURCE_DIR}/data/jesus.desktop
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/applications/)
