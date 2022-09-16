cmake_minimum_required(VERSION 3.23)
project(multicast C)

set(CMAKE_C_STANDARD 99)

if (CMAKE_SYSTEM_NAME MATCHES "CYGWIN")
    add_compile_definitions(_WIN32)
endif()

add_executable(multicast main.c)
