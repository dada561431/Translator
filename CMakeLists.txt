cmake_minimum_required(VERSION 3.20)

project(LunaTranslatorQt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(LunaTranslatorQt
    src/main.cpp
    src/gui/MainWindow.cpp
    src/gui/MainWindow.h
)

target_include_directories(LunaTranslatorQt PRIVATE src)

target_link_libraries(LunaTranslatorQt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

set_target_properties(LunaTranslatorQt PROPERTIES
    WIN32_EXECUTABLE TRUE
)
