cmake_minimum_required(VERSION 3.23)
project(multicast C)

set(CMAKE_C_STANDARD 99)

add_executable(multicast main.c)

if (CMAKE_SYSTEM_NAME MATCHES "CYGWIN")
    target_compile_definitions(multicast PUBLIC _WIN32)
    target_sources(multicast PRIVATE win.c)
    target_link_libraries(multicast PRIVATE iphlpapi)
else()
    target_sources(multicast PRIVATE linux.c)
endif()