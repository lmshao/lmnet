cmake_minimum_required(VERSION 3.10)
project(lmnet VERSION 1.5.1 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Export compile commands for tools like clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build options
option(BUILD_STATIC_LIBS "Build static libraries" ON)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_TESTS "Build tests" ON)
option(INSTALL_TO_USER_LOCAL "Install to ~/.local instead of system-wide" OFF)

set(LINUX_BACKEND_DEFINE "")
set(LINUX_BACKEND_LIBS "")

if(UNIX)
    set(LMNET_LINUX_BACKEND "AUTO" CACHE STRING "Linux networking backend (AUTO, EPOLL, IOURING)")
    set_property(CACHE LMNET_LINUX_BACKEND PROPERTY STRINGS AUTO EPOLL IOURING)

    if(DEFINED CACHE{LMNET_LINUX_BACKEND_SELECTED})
        get_property(_legacy_backend CACHE LMNET_LINUX_BACKEND_SELECTED PROPERTY VALUE)
        if(_legacy_backend)
            message(WARNING "LMNET_LINUX_BACKEND_SELECTED is deprecated and ignored. Use -DLMNET_LINUX_BACKEND=${_legacy_backend}. Propagating value for this configure run.")
            set(LMNET_LINUX_BACKEND "${_legacy_backend}" CACHE STRING "Linux networking backend (AUTO, EPOLL, IOURING)" FORCE)
        else()
            message(WARNING "LMNET_LINUX_BACKEND_SELECTED is deprecated and ignored. Use -DLMNET_LINUX_BACKEND=<AUTO|EPOLL|IOURING> instead.")
        endif()
        unset(LMNET_LINUX_BACKEND_SELECTED CACHE)
        unset(_legacy_backend)
    endif()
endif()

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Display configuration options for developers
message(STATUS "")
message(STATUS "========== Lmnet Library Configuration Options ==========")
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
message(STATUS "  Custom prefix:   cmake -DCMAKE_INSTALL_PREFIX=/opt/lmnet ..")
message(STATUS "  Minimal build:   cmake -DBUILD_EXAMPLES=OFF -DBUILD_TESTS=OFF ..")
message(STATUS "  Force epoll:     cmake -DLMNET_LINUX_BACKEND=EPOLL ..")
message(STATUS "  Force io_uring:  cmake -DLMNET_LINUX_BACKEND=IOURING ..  (requires liburing-dev)")
message(STATUS "  Auto backend:    cmake -DLMNET_LINUX_BACKEND=AUTO ..    (default: tries io_uring then falls back to epoll)")
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
    set(PLATFORM_NAME "Darwin/macOS")
    set(PLATFORM_DIR "platforms/darwin")
    message(STATUS "Using kqueue backend on macOS")
elseif(UNIX)
    set(PLATFORM_NAME "Linux")

    set(LINUX_BACKEND "EPOLL")
    set(LINUX_BACKEND_DEFINE "LMNET_LINUX_BACKEND_EPOLL")
    set(LINUX_BACKEND_LIBS "")

    set(LIBURING_FOUND FALSE)
    set(LIBURING_INCLUDE_DIR "")
    set(LIBURING_LIBRARY "")

    if(LMNET_LINUX_BACKEND STREQUAL "IOURING" OR LMNET_LINUX_BACKEND STREQUAL "AUTO")
        find_path(LIBURING_INCLUDE_DIR liburing.h
                  HINTS /usr/include /usr/local/include
                  PATH_SUFFIXES liburing)
        find_library(LIBURING_LIBRARY NAMES uring)

        if(NOT LIBURING_INCLUDE_DIR OR NOT LIBURING_LIBRARY)
            find_package(PkgConfig QUIET)
            if(PKG_CONFIG_FOUND)
                pkg_check_modules(PC_LIBURING QUIET liburing)
                if(PC_LIBURING_FOUND)
                    set(LIBURING_INCLUDE_DIR ${PC_LIBURING_INCLUDE_DIRS})
                    set(LIBURING_LIBRARY ${PC_LIBURING_LIBRARIES})
                endif()
            endif()
        endif()

        if(LIBURING_INCLUDE_DIR AND LIBURING_LIBRARY)
            set(LIBURING_FOUND TRUE)
        endif()
    endif()

    if(LMNET_LINUX_BACKEND STREQUAL "IOURING")
        if(NOT LIBURING_FOUND)
            message(FATAL_ERROR "lmnet configured with LMNET_LINUX_BACKEND=IOURING but liburing was not found. Install liburing-dev or choose EPOLL backend.")
        endif()
        set(LINUX_BACKEND "IOURING")
    elseif(LMNET_LINUX_BACKEND STREQUAL "AUTO")
        if(LIBURING_FOUND)
            set(LINUX_BACKEND "IOURING")
        else()
            message(STATUS "liburing not found; falling back to epoll backend")
        endif()
    else()
        set(LINUX_BACKEND "EPOLL")
    endif()

    if(LINUX_BACKEND STREQUAL "IOURING")
        set(PLATFORM_DIR "platforms/linux/io_uring")
        set(LINUX_BACKEND_DEFINE "LMNET_LINUX_BACKEND_IOURING")
        if(LIBURING_INCLUDE_DIR)
            include_directories(${LIBURING_INCLUDE_DIR})
        endif()
        set(LINUX_BACKEND_LIBS ${LIBURING_LIBRARY})
        message(STATUS "Using io_uring backend (liburing include: ${LIBURING_INCLUDE_DIR}, lib: ${LIBURING_LIBRARY})")
    else()
        set(PLATFORM_DIR "platforms/linux/epoll")
        set(LINUX_BACKEND_DEFINE "LMNET_LINUX_BACKEND_EPOLL")
        message(STATUS "Using epoll backend")
    endif()
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# Source files and include directories
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/src/${PLATFORM_DIR})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR APPLE)
    include_directories(${PROJECT_SOURCE_DIR}/src/platforms/posix)
endif()


# Try to find installed lmcore first, fallback to local source if not found
find_package(lmcore QUIET)
if(lmcore_FOUND)
    message(STATUS "Using system-installed lmcore library")
    set(LMCORE_SOURCE "system")
else()
    # Check if local lmcore directory exists
    set(LMCORE_LOCAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lmcore")
    if(EXISTS "${LMCORE_LOCAL_DIR}/CMakeLists.txt")
        if(NOT TARGET lmcore)
            add_subdirectory(../lmcore lmcore_build)
        endif()
        message(STATUS "Using local lmcore from sibling directory")
        set(LMCORE_SOURCE "local")
    else()
        # Neither system nor local lmcore found - show helpful error
        message(STATUS "")
        message(STATUS "========== DEPENDENCY ERROR ==========")
        message(FATAL_ERROR 
            "Cannot find lmcore dependency!\n"
            "\n"
            "Searched locations:\n"
            "  1. System installation (using find_package lmcore)\n"
            "  2. Local directory: ${LMCORE_LOCAL_DIR}\n"
            "\n"
            "Solution - Download lmcore to sibling directory:\n"
            "\n"
            "    cd ${CMAKE_CURRENT_SOURCE_DIR}/..\n"
            "    git clone ssh://git@github.com/lmshao/lmcore.git\n"
            "\n"
            "    # OR if SSH is not available:\n"
            "    git clone https://github.com/lmshao/lmcore.git\n"
            "\n"
            "Repository Information:\n"
            "  📍 GitHub: https://github.com/lmshao/lmcore\n"
            "  🔑 SSH:    ssh://git@github.com/lmshao/lmcore.git\n"
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

# Collect POSIX-common sources (shared by Linux and macOS)
set(COMMON_PLATFORM_SOURCES "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR APPLE)
    file(GLOB COMMON_PLATFORM_SOURCES "src/platforms/posix/*.cpp")
endif()

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

set(SOURCES ${COMMON_SOURCES} ${COMMON_PLATFORM_SOURCES} ${PLATFORM_SOURCES})

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
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error -Wno-unused-result")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-format-truncation")
    endif()
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

# Find and link pthread library (not needed on Windows)
if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

# Platform-specific libraries and compiler definitions
if(WIN32)
    # Windows socket libraries
    set(PLATFORM_LIBS ws2_32 wsock32)
    # Define WIN32_LEAN_AND_MEAN to prevent windows.h from including winsock.h
    add_compile_definitions(WIN32_LEAN_AND_MEAN)
else()
    # Unix/Linux libraries
    find_package(Threads REQUIRED)
    set(PLATFORM_LIBS Threads::Threads)
endif()

# Static library
if(BUILD_STATIC_LIBS)
    add_library(lmnet_static STATIC ${SOURCES})
    target_include_directories(lmnet_static PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/lmcore/include>
    )
    # Link to lmcore static library and platform libraries
    target_link_libraries(lmnet_static PUBLIC 
        $<IF:$<TARGET_EXISTS:lmcore::static>,lmcore::static,lmcore> 
        ${PLATFORM_LIBS}
        ${LINUX_BACKEND_LIBS}
    )
    set_target_properties(lmnet_static PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
    set_target_properties(lmnet_static PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )

    if(LINUX_BACKEND_DEFINE)
        target_compile_definitions(lmnet_static PUBLIC ${LINUX_BACKEND_DEFINE})
    endif()
    
    # Create alias for static library (for internal use)
    add_library(${PROJECT_NAME}::static ALIAS lmnet_static)
    message(STATUS "Building static library: lib${PROJECT_NAME}.a")
endif()

# Shared library
if(BUILD_SHARED_LIBS)
    add_library(lmnet_shared SHARED ${SOURCES})
    target_include_directories(lmnet_shared PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/lmcore/include>
    )
    # Link to lmcore shared library and platform libraries  
    if(TARGET lmcore::shared)
        target_link_libraries(lmnet_shared PUBLIC lmcore::shared ${PLATFORM_LIBS} ${LINUX_BACKEND_LIBS})
    elseif(TARGET lmcore-shared)
        target_link_libraries(lmnet_shared PUBLIC lmcore-shared ${PLATFORM_LIBS} ${LINUX_BACKEND_LIBS})
    else()
        # Find lmcore shared library in standard locations
        find_library(LMCORE_SHARED_LIB 
            NAMES lmcore
            HINTS ${CMAKE_INSTALL_PREFIX}/lib /usr/local/lib /usr/lib
            PATH_SUFFIXES lib
        )
        if(LMCORE_SHARED_LIB)
            target_link_libraries(lmnet_shared PUBLIC ${LMCORE_SHARED_LIB} ${PLATFORM_LIBS} ${LINUX_BACKEND_LIBS})
            message(STATUS "Found lmcore shared library: ${LMCORE_SHARED_LIB}")
        else()
            # Fallback to generic library name (let linker find it)
            target_link_libraries(lmnet_shared PUBLIC lmcore ${PLATFORM_LIBS} ${LINUX_BACKEND_LIBS})
            message(STATUS "Using generic lmcore library name (linker will resolve)")
        endif()
    endif()
    set_target_properties(lmnet_shared PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
    set_target_properties(lmnet_shared PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
    
    # Create alias for shared library
    add_library(${PROJECT_NAME}::shared ALIAS lmnet_shared)
    message(STATUS "Building shared library: lib${PROJECT_NAME}.so")

    if(LINUX_BACKEND_DEFINE)
        target_compile_definitions(lmnet_shared PUBLIC ${LINUX_BACKEND_DEFINE})
    endif()
endif()

# Create a default alias that points to static library (for examples and tests)
if(BUILD_STATIC_LIBS)
    add_library(${PROJECT_NAME} ALIAS lmnet_static)
    set(DEFAULT_LIBRARY_TYPE "static")
else()
    add_library(${PROJECT_NAME} ALIAS lmnet_shared)
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
        list(APPEND INSTALL_TARGETS lmnet_static)
    endif()
    if(BUILD_SHARED_LIBS)
        list(APPEND INSTALL_TARGETS lmnet_shared)
    endif()

    if(INSTALL_TARGETS)
        install(TARGETS ${INSTALL_TARGETS}
            EXPORT lmnetTargets
            ARCHIVE DESTINATION lib
            LIBRARY DESTINATION lib
            RUNTIME DESTINATION bin
            INCLUDES DESTINATION include
        )
    endif()

    # Export targets for find_package
    install(EXPORT lmnetTargets
        FILE lmnetTargets.cmake
        NAMESPACE lmnet::
        DESTINATION lib/cmake/lmnet
    )

    # Create and install config files
    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/lmnetConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/lmnetConfig.cmake"
        INSTALL_DESTINATION lib/cmake/lmnet
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/lmnetConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY AnyNewerVersion
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/lmnetConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/lmnetConfigVersion.cmake"
        DESTINATION lib/cmake/lmnet
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
message(STATUS "========== Lmnet Library Build Configuration ==========")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Platform: ${PLATFORM_NAME}")
message(STATUS "Lmcore source: ${LMCORE_SOURCE}")
if(UNIX)
    message(STATUS "Linux backend: ${LINUX_BACKEND}")
endif()
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
