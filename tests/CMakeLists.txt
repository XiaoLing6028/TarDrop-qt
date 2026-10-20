# Unit tests for tardrop-core, plus the install controller's approve/deny flow.
# Run with `ctest --test-dir build --output-on-failure`.

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

# Adds one QtTest executable built with the same warnings and string-safety defines as the app.
function(tardrop_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES" ${ARGN})
    add_executable(${name} ${name}.cpp ArchiveBuilder.cpp ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE
        tardrop-core Qt6::Test KF6::I18n PkgConfig::LibArchive ${ARG_LIBRARIES})
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(${name} PRIVATE
        QT_NO_CAST_FROM_ASCII QT_NO_URL_CAST_FROM_STRING QT_USE_QSTRINGBUILDER)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

tardrop_add_test(tst_riskassessment)
tardrop_add_test(tst_archiveinspect)
tardrop_add_test(tst_datapaths LIBRARIES KF6::CoreAddons)
# The controller has no widget dependency, so its queue logic is tested against the real core.
tardrop_add_test(tst_installflow
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/gui/InstallController.cpp
    LIBRARIES Qt6::Concurrent)
# The warning dialog itself; it needs the same widget and KDE libraries as the application.
tardrop_add_test(tst_securitydialog
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/gui/Dialogs.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/gui/Widgets.cpp
    LIBRARIES Qt6::Widgets KF6::WidgetsAddons KF6::ColorScheme KF6::ConfigGui KF6::KIOCore
              KF6::KIOGui KF6::KIOWidgets KF6::Service)
