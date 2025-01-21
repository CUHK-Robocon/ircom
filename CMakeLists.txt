cmake_minimum_required(VERSION 3.20)

project(ircom)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ircom STATIC
    src/discovery.cpp
)
target_include_directories(ircom
    PUBLIC
        include
    PRIVATE
        src
)
target_link_libraries(ircom avahi-client avahi-common)

add_executable(ircom_test test/main.cpp)
target_link_libraries(ircom_test ircom)
