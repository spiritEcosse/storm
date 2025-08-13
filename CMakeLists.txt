cmake_minimum_required(VERSION 3.30)
project(storm)

set(CMAKE_CXX_STANDARD 23)
message(STATUS "Using C++23 (standard build)")

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable modules support
set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP 1)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "2182bf5c-ef0d-489a-91da-49dbc3090d2a")

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
        add_compile_options(-fmax-errors=1)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
          # For Clang (including AppleClang) with CMAKE_CXX_STANDARD set to 20 or 23,
          # C++ coroutines are typically enabled by the standard itself.
          # The explicit -fcoroutines flag can cause errors with modern Clang.
          add_compile_options(-ferror-limit=1 -stdlib=libc++) # Clang specific error limit
          add_link_options(-stdlib=libc++)
      endif()
 endif()
 
 # Build the core library (modules-first)
 add_library(${PROJECT_NAME} STATIC)
 
 # Collect sources automatically
 file(GLOB_RECURSE STORM_MODULE_UNITS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cppm"
 )
 file(GLOB_RECURSE STORM_NONMODULE_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
 )
 # Exclude example/demo mains from the library
 list(FILTER STORM_NONMODULE_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")
 
 # Declare C++23 module interface and partition sources (all *.cppm)
 target_sources(${PROJECT_NAME}
     PUBLIC
         FILE_SET cxx_modules TYPE CXX_MODULES
         FILES
             ${STORM_MODULE_UNITS}
 )
 
 # Add implementation sources (non-interface units)
 target_sources(${PROJECT_NAME}
     PRIVATE
         ${STORM_NONMODULE_SOURCES}
 )

 # Enable Clang modules and builtin module maps so import <...> works for libc++ headers
 target_compile_options(${PROJECT_NAME} PRIVATE -fmodules -fbuiltin-module-map)
 
 # Group sources by folder in IDEs
 set_property(GLOBAL PROPERTY USE_FOLDERS ON)
 source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES
     ${STORM_MODULE_UNITS}
     ${STORM_NONMODULE_SOURCES}
 )
 
 # Includes for consumers and internal builds
 target_include_directories(${PROJECT_NAME}
     PUBLIC
         "${CMAKE_CURRENT_SOURCE_DIR}"
         "${CMAKE_CURRENT_SOURCE_DIR}/src"
     )

# Ensure compile_commands.json is generated
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    PostgreSQL::PostgreSQL
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

# Add cmake-scripts for sanitizers and other utilities
CPMAddPackage(
    NAME cmake-scripts
    GITHUB_REPOSITORY StableCoder/cmake-scripts
    GIT_TAG 24.08.1
)

# Add sanitizer option
option(USE_SANITIZER "Compile with sanitizer support. Options: address, leak, undefined, thread, memory, memorywithorigins, cfi" "")

# Configure sanitizer options for memory and memorywithorigins
# Memory sanitizer is only available in Clang

# Apply sanitizers if any are selected
if(USE_SANITIZER)
    message(STATUS "Building with sanitizer: ${USE_SANITIZER}")
    # Include sanitizer modules only when needed and compatible
    include(${cmake-scripts_SOURCE_DIR}/sanitizers.cmake)
    
    # Check if memory sanitizer is requested but not available
    if("${USE_SANITIZER}" STREQUAL "memory" OR "${USE_SANITIZER}" STREQUAL "memorywithorigins")
        message(STATUS "Memory sanitizer options configured")
        set_sanitizer_options(memory DEFAULT -fno-omit-frame-pointer)
        set_sanitizer_options(memorywithorigins DEFAULT
                            SANITIZER memory
                            -fno-omit-frame-pointer 
                            -fsanitize-memory-track-origins)
    else()
        # This will automatically check if the sanitizers are available and apply them
        add_sanitizer_support(${USE_SANITIZER})
        message(STATUS "Sanitizer options configured for ${USE_SANITIZER}")
    endif()
    
    # Make sure we have debug symbols for better error reporting
    add_compile_options(-g)
endif()

# Check libc++ version
set(LIBCXX_VERSION_FILE "${CMAKE_BINARY_DIR}/libcxx_version.cpp")
file(WRITE ${LIBCXX_VERSION_FILE} "#include <iostream>\n#include <cstdio>\nint main() {\n#ifdef _LIBCPP_VERSION\n    printf(\"%d\", _LIBCPP_VERSION);\n#else\n    printf(\"Not using libc++\");\n#endif\n    return 0;\n}\n")

try_run(
    LIBCXX_VERSION_RUN_RESULT
    LIBCXX_VERSION_COMPILE_RESULT
    ${CMAKE_BINARY_DIR}
    ${LIBCXX_VERSION_FILE}
    RUN_OUTPUT_VARIABLE LIBCXX_VERSION_OUTPUT
)

if(LIBCXX_VERSION_COMPILE_RESULT AND LIBCXX_VERSION_RUN_RESULT EQUAL 0)
    set(LIBCXX_VERSION ${LIBCXX_VERSION_OUTPUT})
else()
    set(LIBCXX_VERSION "Unknown")
endif()

# Print build configuration
message(STATUS "=== Storm Build Configuration ===")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "libc++ Version: ${LIBCXX_VERSION}")
message(STATUS "Testing: ${STORM_ENABLE_TESTING}")
message(STATUS "Sanitizer: ${STORM_ENABLE_SANITIZER}")
message(STATUS "===================================")
