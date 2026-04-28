cmake_minimum_required(VERSION 3.16)
project(CPUSim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    set(QT_VERSION_MAJOR 6)
    message(STATUS "Using Qt6")
else()
    find_package(Qt5 COMPONENTS Widgets REQUIRED)
    set(QT_VERSION_MAJOR 5)
    message(STATUS "Using Qt5")
endif()

set(CORE_SOURCES
    dram.cpp
    cache.cpp
    directmemif.cpp
    pipeline.cpp
)

add_executable(sim_gui
    main_gui.cpp
    mainwindow.cpp
    ${CORE_SOURCES}
)

target_include_directories(sim_gui PRIVATE ${CMAKE_SOURCE_DIR})

if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(sim_gui PRIVATE Qt6::Widgets)
else()
    target_link_libraries(sim_gui PRIVATE Qt5::Widgets)
endif()
