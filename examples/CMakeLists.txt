# Examples
cmake_minimum_required(VERSION 3.10)

add_subdirectory(tcp)
add_subdirectory(udp)

# Unix domain socket examples are only supported on Unix-like systems
if(NOT WIN32)
    add_subdirectory(unix)
endif()
