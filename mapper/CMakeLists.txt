cmake_minimum_required(VERSION 3.10)
project(mapper CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(SOURCES
    main.cpp
    kdmapper.cpp
    portable_executable.cpp
    intel_driver.cpp
    service.cpp
    utils.cpp
)

add_executable(mapper ${SOURCES})
