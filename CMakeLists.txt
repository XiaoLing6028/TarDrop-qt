# TarDrop — a user-local installer for portable application archives.
# Linux/KDE Plasma only: there is deliberately no cross-platform abstraction layer.

cmake_minimum_required(VERSION 3.28)

project(tardrop
    VERSION 0.2.0
    DESCRIPTION "A safe, user-local installer for portable application archives"
    LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "TarDrop targets Linux only.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent DBus)

# The KDE frameworks are located individually so that the build does not require
# extra-cmake-modules (which only ships the `KF6` umbrella package config).
# Note: these are the *package* names; each exports several KF6:: targets
# (e.g. KF6Config provides KF6::ConfigCore and KF6::ConfigGui).
foreach(framework CoreAddons I18n WidgetsAddons Config ConfigWidgets
                  ColorScheme IconThemes Notifications Crash KIO Service)
    find_package(KF6${framework} 6.0 REQUIRED)
endforeach()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LibArchive REQUIRED IMPORTED_TARGET libarchive>=3.6)

qt_standard_project_setup(REQUIRES 6.6)

# ---------------------------------------------------------------------------
# tardrop-core: all security-sensitive logic. No widget/GUI dependency, so the
# safety rules cannot be weakened by presentation code.
# ---------------------------------------------------------------------------
add_library(tardrop-core STATIC
    src/core/Paths.cpp
    src/core/Security.cpp
    src/core/ArchiveReader.cpp
    src/core/DesktopFile.cpp
    src/core/IconFinder.cpp
    src/core/Installer.cpp
    src/core/Records.cpp
    src/core/Providers.cpp
    src/core/Updater.cpp
)
target_include_directories(tardrop-core PUBLIC src)
target_link_libraries(tardrop-core
    PUBLIC  Qt6::Core Qt6::Network
    PRIVATE PkgConfig::LibArchive KF6::CoreAddons KF6::I18n
)
set_target_properties(tardrop-core PROPERTIES POSITION_INDEPENDENT_CODE ON AUTOMOC ON)

# ---------------------------------------------------------------------------
# tardrop: the Qt Widgets / KDE Plasma front end.
# ---------------------------------------------------------------------------
qt_add_executable(tardrop
    src/main.cpp
    src/gui/MainWindow.cpp
    src/gui/DropZone.cpp
    src/gui/InstallPage.cpp
    src/gui/RecordsPage.cpp
    src/gui/SettingsPage.cpp
    src/gui/InstallController.cpp
    src/gui/UpdateController.cpp
    src/gui/Dialogs.cpp
    src/gui/Widgets.cpp
)
set_target_properties(tardrop PROPERTIES AUTOMOC ON)
target_link_libraries(tardrop PRIVATE
    tardrop-core
    Qt6::Widgets Qt6::Concurrent Qt6::DBus
    KF6::CoreAddons KF6::I18n KF6::WidgetsAddons KF6::ConfigCore KF6::ConfigGui
    KF6::ConfigWidgets KF6::ColorScheme KF6::IconThemes KF6::Notifications
    KF6::Crash KF6::KIOCore KF6::KIOGui KF6::KIOWidgets KF6::Service
)

target_compile_options(tardrop-core PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(tardrop PRIVATE -Wall -Wextra -Wpedantic)

# Hardening: TarDrop processes untrusted archives, so keep the usual mitigations on.
foreach(target tardrop-core tardrop)
    target_compile_options(${target} PRIVATE
        -fstack-protector-strong -fstack-clash-protection -fno-plt)
    target_compile_definitions(${target} PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=3>
        QT_NO_CAST_FROM_ASCII QT_NO_URL_CAST_FROM_STRING QT_USE_QSTRINGBUILDER
        TARDROP_VERSION="${PROJECT_VERSION}")
endforeach()
target_link_options(tardrop PRIVATE -Wl,-z,relro -Wl,-z,now)

install(TARGETS tardrop RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.tardrop.TarDrop.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
install(FILES data/org.tardrop.TarDrop.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)
