# Tests
cmake_minimum_required(VERSION 3.10)

# Enable testing
enable_testing()

# Use simple assert-based test framework
message(STATUS "Using simple assert-based test framework")

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/src)

# Add test subdirectories
add_subdirectory(unit)