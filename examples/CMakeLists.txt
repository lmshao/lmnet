# Examples
cmake_minimum_required(VERSION 3.10)

add_subdirectory(tcp)
add_subdirectory(udp)
add_subdirectory(benchmark)

# Unix domain socket examples are only supported on Unix-like systems
if(NOT WIN32)
    add_subdirectory(unix)
endif()

# Network utilities demo
add_executable(network_utils_demo network_utils_demo.cpp)
target_link_libraries(network_utils_demo lmnet)
message(STATUS "lmnet Example target: network_utils_demo")
