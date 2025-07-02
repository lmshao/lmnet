cmake_minimum_required(VERSION 3.10)
project(network)
set(CMAKE_CXX_STANDARD 17)

include_directories(${PROJECT_SOURCE_DIR}/include/network)

file(GLOB_RECURSE SOURCES "src/*.cpp")

set(CMAKE_CXX_FLAGS "-Wno-error -Wno-format-truncation -g")

# set(CMAKE_CXX_FLAGS "-DRELEASE")

add_library(${PROJECT_NAME} STATIC ${SOURCES})

# Find and link pthread library
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} Threads::Threads)

# Add examples subdirectory
option(BUILD_EXAMPLES "Build examples" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
