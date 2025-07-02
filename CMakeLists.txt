cmake_minimum_required(VERSION 3.10)
project(network)
set(CMAKE_CXX_STANDARD 17)

include_directories(${PROJECT_SOURCE_DIR}/include)

file(GLOB_RECURSE SOURCES "src/*.cpp")

set(CMAKE_CXX_FLAGS "-Wno-error -Wno-format-truncation -g")

# set(CMAKE_CXX_FLAGS "-DRELEASE")

add_library(${PROJECT_NAME} STATIC ${SOURCES})

# Find and link pthread library
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} Threads::Threads)

file(GLOB_RECURSE EXAMPLE "example/*.cxx")
foreach(src IN LISTS EXAMPLE)
  string(REGEX REPLACE "^.*/([^/.]+).cxx" "\\1" target ${src})
  message(STATUS "target: " ${target})
  add_executable(${target} ${src})
  target_link_libraries(${target} ${PROJECT_NAME})
endforeach(src)
