cmake_minimum_required(VERSION 3.10)
project(network VERSION 1.2.0 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build options
option(BUILD_STATIC_LIBS "Build static libraries" ON)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_TESTS "Build tests" ON)
option(INSTALL_TO_USER_LOCAL "Install to ~/.local instead of system-wide" OFF)

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Display configuration options for developers
message(STATUS "")
message(STATUS "========== Network Library Configuration Options ==========")
message(STATUS "")
message(STATUS "Build Type Configuration:")
message(STATUS "  Current build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Available types: Debug, Release, RelWithDebInfo, MinSizeRel")
message(STATUS "  Change with: cmake -DCMAKE_BUILD_TYPE=Debug ..")
message(STATUS "")
message(STATUS "Library Build Options:")
message(STATUS "  BUILD_STATIC_LIBS: Build static libraries (current: ${BUILD_STATIC_LIBS})")
message(STATUS "  BUILD_SHARED_LIBS: Build shared libraries (current: ${BUILD_SHARED_LIBS})")
message(STATUS "  BUILD_EXAMPLES: Build example programs (current: ${BUILD_EXAMPLES})")
message(STATUS "  BUILD_TESTS: Build unit tests (current: ${BUILD_TESTS})")
message(STATUS "")
message(STATUS "Installation Options:")
message(STATUS "  CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  INSTALL_TO_USER_LOCAL: Install to ~/.local (current: ${INSTALL_TO_USER_LOCAL})")
message(STATUS "")
message(STATUS "Usage Examples:")
message(STATUS "  Debug build:     cmake -DCMAKE_BUILD_TYPE=Debug ..")
message(STATUS "  User install:    cmake -DINSTALL_TO_USER_LOCAL=ON ..")
message(STATUS "  Custom prefix:   cmake -DCMAKE_INSTALL_PREFIX=/opt/network ..")
message(STATUS "  Minimal build:   cmake -DBUILD_EXAMPLES=OFF -DBUILD_TESTS=OFF ..")
message(STATUS "")
message(STATUS "========================================================")

# Handle user-local installation
if(INSTALL_TO_USER_LOCAL AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH "User local install path" FORCE)
    message(STATUS "Installing to user directory: ${CMAKE_INSTALL_PREFIX}")
endif()

# Provide install path information to users
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    message(STATUS "Using default install prefix: ${CMAKE_INSTALL_PREFIX}")
    message(STATUS "You can customize it with: cmake -DCMAKE_INSTALL_PREFIX=/your/path ..")
endif()

# Determine target platform
if(WIN32)
    set(PLATFORM_DIR "platforms/windows")
    set(PLATFORM_NAME "Windows")
elseif(APPLE)
    message(FATAL_ERROR "macOS platform is not supported yet")
elseif(UNIX)
    set(PLATFORM_DIR "platforms/linux")
    set(PLATFORM_NAME "Linux")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# Source files and include directories
include_directories(${PROJECT_SOURCE_DIR}/include/network)
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/src/${PLATFORM_DIR})

# Try to find installed coreutils first, fallback to local source if not found
find_package(coreutils QUIET)
if(coreutils_FOUND)
    message(STATUS "Using system-installed coreutils library")
    set(COREUTILS_SOURCE "system")
else()
    # Check if local coreutils directory exists
    set(COREUTILS_LOCAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../coreutils")
    if(EXISTS "${COREUTILS_LOCAL_DIR}/CMakeLists.txt")
        if(NOT TARGET coreutils)
            add_subdirectory(../coreutils coreutils_build)
        endif()
        message(STATUS "Using local coreutils from sibling directory")
        set(COREUTILS_SOURCE "local")
    else()
        # Neither system nor local coreutils found - show helpful error
        message(STATUS "")
        message(STATUS "========== DEPENDENCY ERROR ==========")
        message(FATAL_ERROR 
            "Cannot find coreutils dependency!\n"
            "\n"
            "Searched locations:\n"
            "  1. System installation (using find_package coreutils)\n"
            "  2. Local directory: ${COREUTILS_LOCAL_DIR}\n"
            "\n"
            "Solution - Download coreutils to sibling directory:\n"
            "\n"
            "    cd ${CMAKE_CURRENT_SOURCE_DIR}/..\n"
            "    git clone ssh://git@github.com/lmshao/coreutils.git\n"
            "\n"
            "    # OR if SSH is not available:\n"
            "    git clone https://github.com/lmshao/coreutils.git\n"
            "\n"
            "Repository Information:\n"
            "  📍 GitHub: https://github.com/lmshao/coreutils\n"
            "  🔑 SSH:    ssh://git@github.com/lmshao/coreutils.git\n"
            "\n"
            "After downloading, re-run: cmake .."
        )
    endif()
endif()

# Source files and compilation setup
message(STATUS "Building for platform: ${PLATFORM_NAME}")
message(STATUS "Platform source directory: src/${PLATFORM_DIR}")

# Collect source files
file(GLOB COMMON_SOURCES "src/*.cpp")
file(GLOB_RECURSE PLATFORM_SOURCES "src/${PLATFORM_DIR}/*.cpp")

# On Windows, exclude Unix domain socket files since they are not supported
if(WIN32)
    list(FILTER COMMON_SOURCES EXCLUDE REGEX ".*unix_.*\.cpp$")
    list(FILTER PLATFORM_SOURCES EXCLUDE REGEX ".*unix_.*\.cpp$")
    message(STATUS "Unix domain socket files excluded on Windows platform")
endif()

# Remove any duplicates and invalid paths
list(REMOVE_DUPLICATES COMMON_SOURCES)
list(REMOVE_DUPLICATES PLATFORM_SOURCES)

set(SOURCES ${COMMON_SOURCES} ${PLATFORM_SOURCES})

# Print source file summary
list(LENGTH COMMON_SOURCES COMMON_COUNT)
list(LENGTH PLATFORM_SOURCES PLATFORM_COUNT)
message(STATUS "Found ${COMMON_COUNT} common source files")
message(STATUS "Found ${PLATFORM_COUNT} platform-specific source files")

# Compiler flags
if(MSVC)
    # MSVC specific flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3")
    # Debug-specific flags for MSVC
    set(CMAKE_CXX_FLAGS_DEBUG "/MDd /Zi /Od /DDEBUG")
    # Release-specific flags for MSVC
    set(CMAKE_CXX_FLAGS_RELEASE "/MD /O2 /DNDEBUG /DRELEASE")
else()
    # GCC/Clang flags for Linux/Unix
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error -Wno-format-truncation -Wno-unused-result")
    # Debug-specific flags for GCC/Clang
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
    # Release-specific flags for GCC/Clang
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -DRELEASE")
    
    # Enable position independent code for shared library compatibility
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# Print build configuration
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ flags: ${CMAKE_CXX_FLAGS}")
message(STATUS "C++ flags for ${CMAKE_BUILD_TYPE}: ${CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}}")

# Library build options validation
if(NOT BUILD_STATIC_LIBS AND NOT BUILD_SHARED_LIBS)
    message(FATAL_ERROR "At least one of BUILD_STATIC_LIBS or BUILD_SHARED_LIBS must be ON")
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
if(MSVC)
    # MSVC specific flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3")
    # Debug-specific flags for MSVC
    set(CMAKE_CXX_FLAGS_DEBUG "/MDd /Zi /Od /DDEBUG")
    # Release-specific flags for MSVC
    set(CMAKE_CXX_FLAGS_RELEASE "/MD /O2 /DNDEBUG /DRELEASE")
else()
    # GCC/Clang flags for Linux/Unix
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error -Wno-format-truncation -Wno-unused-result")
    # Debug-specific flags for GCC/Clang
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
    # Release-specific flags for GCC/Clang
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -DRELEASE")
endif()

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

# Find and link pthread library (not needed on Windows)
if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

# Platform-specific libraries
if(WIN32)
    # Windows socket libraries
    set(PLATFORM_LIBS ws2_32 wsock32)
else()
    # Unix/Linux libraries
    find_package(Threads REQUIRED)
    set(PLATFORM_LIBS Threads::Threads)
endif()

# Static library
if(BUILD_STATIC_LIBS)
    add_library(${PROJECT_NAME}_static STATIC ${SOURCES})
    target_include_directories(${PROJECT_NAME}_static PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/coreutils/include>
    )
    # Link to coreutils static library and platform libraries
    target_link_libraries(${PROJECT_NAME}_static PUBLIC 
        $<IF:$<TARGET_EXISTS:coreutils::static>,coreutils::static,coreutils> 
        ${PLATFORM_LIBS}
    )
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
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/coreutils/include>
    )
    # Link to coreutils shared library and platform libraries  
    if(TARGET coreutils::shared)
        target_link_libraries(${PROJECT_NAME}_shared PUBLIC coreutils::shared ${PLATFORM_LIBS})
    elseif(TARGET coreutils-shared)
        target_link_libraries(${PROJECT_NAME}_shared PUBLIC coreutils-shared ${PLATFORM_LIBS})
    else()
        # Find coreutils shared library in standard locations
        find_library(COREUTILS_SHARED_LIB 
            NAMES coreutils
            HINTS ${CMAKE_INSTALL_PREFIX}/lib /usr/local/lib /usr/lib
            PATH_SUFFIXES lib
        )
        if(COREUTILS_SHARED_LIB)
            target_link_libraries(${PROJECT_NAME}_shared PUBLIC ${COREUTILS_SHARED_LIB} ${PLATFORM_LIBS})
            message(STATUS "Found coreutils shared library: ${COREUTILS_SHARED_LIB}")
        else()
            # Fallback to generic library name (let linker find it)
            target_link_libraries(${PROJECT_NAME}_shared PUBLIC coreutils ${PLATFORM_LIBS})
            message(STATUS "Using generic coreutils library name (linker will resolve)")
        endif()
    endif()
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

# Add subdirectories (only for development, not installed)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install and export functionality (Unix/Linux only)
if(NOT WIN32)
    # Install headers
    install(DIRECTORY include/ DESTINATION include
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

    # Install libraries and create export targets
    set(INSTALL_TARGETS)
    if(BUILD_STATIC_LIBS)
        list(APPEND INSTALL_TARGETS ${PROJECT_NAME}_static)
    endif()
    if(BUILD_SHARED_LIBS)
        list(APPEND INSTALL_TARGETS ${PROJECT_NAME}_shared)
    endif()

    if(INSTALL_TARGETS)
        install(TARGETS ${INSTALL_TARGETS}
            EXPORT networkTargets
            ARCHIVE DESTINATION lib
            LIBRARY DESTINATION lib
            RUNTIME DESTINATION bin
            INCLUDES DESTINATION include
        )
    endif()

    # Export targets for find_package
    install(EXPORT networkTargets
        FILE networkTargets.cmake
        NAMESPACE network::
        DESTINATION lib/cmake/network
    )

    # Create and install config files
    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/networkConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/networkConfig.cmake"
        INSTALL_DESTINATION lib/cmake/network
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/networkConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY AnyNewerVersion
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/networkConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/networkConfigVersion.cmake"
        DESTINATION lib/cmake/network
    )

    # Add uninstall target
    if(NOT TARGET uninstall)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
            IMMEDIATE @ONLY)

        add_custom_target(uninstall
            COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
    endif()
else()
    message(STATUS "Install/uninstall targets are disabled on Windows platform")
    message(STATUS "Use the library directly from the build directory")
endif()

# Print build summary
message(STATUS "")
message(STATUS "========== Network Library Build Configuration ==========")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Platform: ${PLATFORM_NAME}")
message(STATUS "Coreutils source: ${COREUTILS_SOURCE}")
message(STATUS "Build static library: ${BUILD_STATIC_LIBS}")
message(STATUS "Build shared library: ${BUILD_SHARED_LIBS}")
message(STATUS "Build examples: ${BUILD_EXAMPLES}")
message(STATUS "Build tests: ${BUILD_TESTS}")
if(NOT WIN32)
    message(STATUS "Install support: ON")
    message(STATUS "Uninstall support: ON (make uninstall)")
else()
    message(STATUS "Install support: DISABLED (Windows platform)")
    message(STATUS "Uninstall support: DISABLED (Windows platform)")
endif()
message(STATUS "=======================================================")
message(STATUS "")
