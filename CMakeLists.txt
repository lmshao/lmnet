cmake_minimum_required(VERSION 3.10)
project(network VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

# Determine target platform
if(WIN32)
    message(FATAL_ERROR "Windows platform is not supported yet")
elseif(APPLE)
    message(FATAL_ERROR "macOS platform is not supported yet")
elseif(UNIX)
    set(PLATFORM_DIR "platforms/linux")
    set(PLATFORM_NAME "Linux")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include/network)
include_directories(${PROJECT_SOURCE_DIR}/src/common)
include_directories(${PROJECT_SOURCE_DIR}/src/${PLATFORM_DIR})

message(STATUS "Building for platform: ${PLATFORM_NAME}")
message(STATUS "Platform source directory: src/${PLATFORM_DIR}")

# Collect source files
file(GLOB_RECURSE COMMON_SOURCES "src/common/*.cpp")
file(GLOB_RECURSE PLATFORM_SOURCES "src/${PLATFORM_DIR}/*.cpp")

# Check if platform sources exist
list(LENGTH PLATFORM_SOURCES PLATFORM_SOURCES_COUNT)
if(PLATFORM_SOURCES_COUNT EQUAL 0)
    message(FATAL_ERROR "No platform-specific sources found in src/${PLATFORM_DIR}/")
endif()

set(SOURCES ${COMMON_SOURCES} ${PLATFORM_SOURCES})

# Print source file summary
list(LENGTH COMMON_SOURCES COMMON_COUNT)
list(LENGTH PLATFORM_SOURCES PLATFORM_COUNT)
message(STATUS "Found ${COMMON_COUNT} common source files")
message(STATUS "Found ${PLATFORM_COUNT} platform-specific source files")

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error -Wno-format-truncation")

# Debug-specific flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# Release-specific flags  
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -DRELEASE")

# Print build configuration
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ flags: ${CMAKE_CXX_FLAGS}")
message(STATUS "C++ flags for ${CMAKE_BUILD_TYPE}: ${CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}}")

# Library build options
option(BUILD_STATIC_LIBS "Build static libraries" ON)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

if(NOT BUILD_STATIC_LIBS AND NOT BUILD_SHARED_LIBS)
    message(FATAL_ERROR "At least one of BUILD_STATIC_LIBS or BUILD_SHARED_LIBS must be ON")
endif()

# Find and link pthread library
find_package(Threads REQUIRED)

# Static library
if(BUILD_STATIC_LIBS)
    add_library(${PROJECT_NAME}_static STATIC ${SOURCES})
    target_include_directories(${PROJECT_NAME}_static PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(${PROJECT_NAME}_static Threads::Threads)
    set_target_properties(${PROJECT_NAME}_static PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
    set_target_properties(${PROJECT_NAME}_static PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
    
    # Create alias for static library (for internal use)
    add_library(${PROJECT_NAME}::static ALIAS ${PROJECT_NAME}_static)
    message(STATUS "Building static library: lib${PROJECT_NAME}.a")
endif()

# Shared library
if(BUILD_SHARED_LIBS)
    add_library(${PROJECT_NAME}_shared SHARED ${SOURCES})
    target_include_directories(${PROJECT_NAME}_shared PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(${PROJECT_NAME}_shared Threads::Threads)
    set_target_properties(${PROJECT_NAME}_shared PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
    set_target_properties(${PROJECT_NAME}_shared PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
    
    # Create alias for shared library
    add_library(${PROJECT_NAME}::shared ALIAS ${PROJECT_NAME}_shared)
    message(STATUS "Building shared library: lib${PROJECT_NAME}.so")
endif()

# Create a default alias that points to static library (for examples and tests)
if(BUILD_STATIC_LIBS)
    add_library(${PROJECT_NAME} ALIAS ${PROJECT_NAME}_static)
    set(DEFAULT_LIBRARY_TYPE "static")
else()
    add_library(${PROJECT_NAME} ALIAS ${PROJECT_NAME}_shared)
    set(DEFAULT_LIBRARY_TYPE "shared")
endif()

message(STATUS "Examples and tests will use ${DEFAULT_LIBRARY_TYPE} library")

# Add examples subdirectory
option(BUILD_EXAMPLES "Build examples" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Add tests subdirectory
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
