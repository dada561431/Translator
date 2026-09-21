cmake_minimum_required(VERSION 3.20)

project(Translator VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(Translator
    src/main.cpp
    src/config/SettingsManager.cpp
    src/config/SettingsManager.h
    src/gui/MainWindow.cpp
    src/gui/MainWindow.h
)

target_include_directories(Translator PRIVATE src)

target_link_libraries(Translator PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

set_target_properties(Translator PROPERTIES
    WIN32_EXECUTABLE TRUE
)

include(CTest)

if(BUILD_TESTING)
    add_executable(TranslatorPhase2Tests
        tests/Phase2UiTest.cpp
        src/config/SettingsManager.cpp
        src/config/SettingsManager.h
        src/gui/MainWindow.cpp
        src/gui/MainWindow.h
    )

    target_include_directories(TranslatorPhase2Tests PRIVATE src)

    target_link_libraries(TranslatorPhase2Tests PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )

    add_test(NAME phase2_ui_and_settings COMMAND TranslatorPhase2Tests)
    set_tests_properties(phase2_ui_and_settings PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()
