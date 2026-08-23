cmake_minimum_required(VERSION 3.10)
project(loader CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(loader loader.cpp)
target_link_libraries(loader PRIVATE user32 advapi32)
