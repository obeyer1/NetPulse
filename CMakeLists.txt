cmake_minimum_required(VERSION 3.21)

project(NetPulse
    VERSION 1.0.0
    DESCRIPTION "Local Network Health Monitor"
    HOMEPAGE_URL "https://github.com/obeyer1/NetPulse"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network Charts Sql Concurrent Test)

qt_standard_project_setup()

set(NETPULSE_CORE_SOURCES
    src/core/Types.h
    src/core/IcmpPinger.h
    src/core/IcmpPinger.cpp
    src/core/Probes.h
    src/core/Probes.cpp
    src/core/ProbeScheduler.h
    src/core/ProbeScheduler.cpp
    src/core/SystemInfo.h
    src/core/SystemInfo.cpp
    src/core/Diagnostics.h
    src/core/Diagnostics.cpp
)

set(NETPULSE_APP_SOURCES
    src/main.cpp
    src/storage/Storage.h
    src/storage/Storage.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/TargetsModel.h
    src/ui/TargetsModel.cpp
    src/ui/TargetDialog.h
    src/ui/TargetDialog.cpp
    src/ui/ChartsPanel.h
    src/ui/ChartsPanel.cpp
    src/ui/Panels.h
    src/ui/Panels.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
)

qt_add_executable(NetPulse
    ${NETPULSE_CORE_SOURCES}
    ${NETPULSE_APP_SOURCES}
)

target_include_directories(NetPulse PRIVATE src)

target_link_libraries(NetPulse PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Charts
    Qt6::Sql
    Qt6::Concurrent
)

set_target_properties(NetPulse PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "NetPulse"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.obeyer1.netpulse"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

# ---- Tests ----------------------------------------------------------------
enable_testing()

qt_add_executable(test_diagnostics
    tests/test_diagnostics.cpp
    src/core/Types.h
    src/core/Diagnostics.h
    src/core/Diagnostics.cpp
)
target_include_directories(test_diagnostics PRIVATE src)
target_link_libraries(test_diagnostics PRIVATE Qt6::Test Qt6::Network)
set_target_properties(test_diagnostics PROPERTIES MACOSX_BUNDLE FALSE)
add_test(NAME diagnostics COMMAND test_diagnostics)
