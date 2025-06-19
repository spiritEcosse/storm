cmake_minimum_required(VERSION 3.30)
project(storm)

set(CMAKE_CXX_STANDARD 23)
message(STATUS "Using C++23 (standard build)")

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message(STATUS "CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_COMPILER_VERSION: ${CMAKE_CXX_COMPILER_VERSION}")

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Common options for both GNU and Clang
    add_compile_options($<$<CONFIG:Debug>:-ftemplate-backtrace-limit=0>)

    # Compiler-specific flags
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        add_compile_options(-fcoroutines) # Apply only for GNU compilers
        add_compile_options(-fmax-errors=5)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # For Clang (including AppleClang) with CMAKE_CXX_STANDARD set to 20 or 23,
        # C++ coroutines are typically enabled by the standard itself.
        # The explicit -fcoroutines flag can cause errors with modern Clang.
        add_compile_options(-ferror-limit=5) # Clang specific error limit
    endif()
endif()

# start src
file(GLOB_RECURSE RESOURCES CONFIGURE_DEPENDS src/*.*)
add_executable(${PROJECT_NAME} ${RESOURCES})

# Set include directories properly
# This makes both "#include <File.h>" and "#include "File.h"" work correctly
target_include_directories(${PROJECT_NAME} 
    PRIVATE 
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# Ensure compile_commands.json is generated
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# end src

include(${CMAKE_CURRENT_SOURCE_DIR}/envs.cmake)

# Find PostgreSQL package
find_package(PostgreSQL REQUIRED)

# Find SQLite3 package and link it
find_package(SQLite3)
if (SQLite3_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE SQLite::SQLite3)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE sqlite3)
endif()

# start CPM.cmake
set(CPM_DOWNLOAD_VERSION 0.40.5)
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")

if(NOT (EXISTS ${CPM_DOWNLOAD_LOCATION}))
    message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
    file(DOWNLOAD
            https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
            ${CPM_DOWNLOAD_LOCATION}
    )
endif()

include(${CPM_DOWNLOAD_LOCATION})

CPMAddPackage(
    NAME refl-cpp
    GITHUB_REPOSITORY veselink1/refl-cpp
    VERSION 0.12.4
)

CPMAddPackage(
    NAME fmt
    GITHUB_REPOSITORY fmtlib/fmt
    GIT_TAG 11.2.0
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    PostgreSQL::PostgreSQL
    fmt
    refl-cpp
)

option(ENABLE_TESTS "Enable testing support" OFF)

if (ENABLE_TESTS)
    message(STATUS "Testing is enabled.")
    # Start adding gtest
    CPMAddPackage(
            NAME googletest
            GITHUB_REPOSITORY google/googletest
            GIT_TAG v1.15.2
            VERSION 1.15.2
            OPTIONS
            "INSTALL_GTEST OFF"
    )
    set(GTEST_PARALLEL TRUE)
    # End adding gtest
    enable_testing()
    add_subdirectory(tests)
else ()
    message(STATUS "Testing is disabled.")
endif ()

# Print build configuration summary
message(STATUS "=== Storm Build Configuration ===")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Testing: ${ENABLE_TESTS}")
message(STATUS "==================================")