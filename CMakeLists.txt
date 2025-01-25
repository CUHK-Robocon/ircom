cmake_minimum_required(VERSION 3.20)

project(ircom)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()
find_package(Boost REQUIRED)

find_package(spdlog REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ircom STATIC
    src/discovery.cpp
    src/ircom.cpp
)
target_include_directories(ircom
    PUBLIC
        include
    PRIVATE
        src
)
target_link_libraries(ircom
    avahi-client
    avahi-common
    Boost::headers
    spdlog::spdlog_header_only
)

add_executable(ircom_test test/main.cpp)
target_link_libraries(ircom_test ircom)
