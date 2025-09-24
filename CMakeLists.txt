cmake_minimum_required(VERSION 3.30)
project(storm)

set(CMAKE_CXX_STANDARD 26)
message(STATUS "Using C++26 (standard build)")

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

message(STATUS "CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_COMPILER_VERSION: ${CMAKE_CXX_COMPILER_VERSION}")

# Common compiler options - applied to all targets (Clang)
# Apply template backtrace limit globally for debug builds
add_compile_options($<$<CONFIG:Debug>:-ftemplate-backtrace-limit=0>)

# For Clang (including AppleClang) with CMAKE_CXX_STANDARD set to 26,
# C++ coroutines are typically enabled by the standard itself.
# The explicit -fcoroutines flag can cause errors with modern Clang.
add_compile_options(-ferror-limit=1) # Clang specific error limit

# Add standard libcxx flags for non-custom builds
if(NOT DEFINED LIBCXX_ROOT)
    add_compile_options(-stdlib=libc++)
    add_link_options(-stdlib=libc++)
endif()

# Configure libcxx paths if LIBCXX_ROOT is provided
if(DEFINED LIBCXX_ROOT)
    set(LIBCXX_INCLUDE_DIR "${LIBCXX_ROOT}/build/include/c++/v1")
    set(LIBCXX_BUILD_INCLUDE_DIR "${LIBCXX_ROOT}/build/include/x86_64-unknown-linux-gnu/c++/v1")
    message(STATUS "Using custom libcxx from: ${LIBCXX_ROOT}")

    # Add global custom libc++ link options for all executables
    add_link_options(
        -nostdlib++
        -L${LIBCXX_ROOT}/build/lib/x86_64-unknown-linux-gnu
        -Wl,-rpath,${LIBCXX_ROOT}/build/lib/x86_64-unknown-linux-gnu
        -lc++
        -lc++abi
        -lunwind
    )
    message(STATUS "Global custom libc++ linking configured")
endif()

 # Build the core library (modules-first)
 add_library(${PROJECT_NAME} STATIC)
 
 # Collect sources automatically
# Collect all modules automatically from src subdirectories
file(GLOB_RECURSE STORM_MODULE_UNITS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cppm"
)
 
 # Declare C++26 module interface and partition sources (all *.cppm)
 
 target_sources(${PROJECT_NAME}
     PUBLIC
         FILE_SET cxx_modules TYPE CXX_MODULES
         FILES
             ${STORM_MODULE_UNITS}
 )
 

# Function to apply Clang-specific flags (modules and reflection) to a target
function(apply_clang_flags target_name)
    # Enable Clang modules and builtin module maps
    target_compile_options(${target_name} PRIVATE -fmodules -fbuiltin-module-map)
    message(STATUS "Module flags enabled for ${target_name}")

    # Add custom libcxx and reflection flags when using custom libcxx
    if(DEFINED LIBCXX_ROOT)
        target_compile_options(${target_name} PRIVATE
            -freflection
            -fannotation-attributes
            -nostdinc++
            -I${LIBCXX_INCLUDE_DIR}
            -I${LIBCXX_BUILD_INCLUDE_DIR}
        )
        message(STATUS "Reflection flags enabled for ${target_name}")
    endif()
endfunction()

 # Add Clang-specific flags (modules and reflection) to Storm library
 apply_clang_flags(${PROJECT_NAME})
 
# Group sources by folder in IDEs
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES
    ${STORM_MODULE_UNITS}
)
 
 # Includes for consumers and internal builds
 target_include_directories(${PROJECT_NAME}
     PUBLIC
         "${CMAKE_CURRENT_SOURCE_DIR}"
         "${CMAKE_CURRENT_SOURCE_DIR}/src"
         "${CMAKE_CURRENT_SOURCE_DIR}/include"
     )

# Ensure compile_commands.json is generated
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/envs.cmake)

# Find PostgreSQL package
find_package(PostgreSQL REQUIRED)

# Function to link SQLite to a target
function(link_sqlite target_name)
    if (SQLite3_FOUND)
        target_link_libraries(${target_name} PRIVATE SQLite::SQLite3)
    else()
        target_link_libraries(${target_name} PRIVATE sqlite3)
    endif()
endfunction()

# Find SQLite3 package and link it
find_package(SQLite3)
link_sqlite(${PROJECT_NAME})

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

    # Configure GoogleTest with custom libcxx if needed
    if(DEFINED LIBCXX_ROOT)
        set(CMAKE_CXX_FLAGS_BACKUP "${CMAKE_CXX_FLAGS}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -nostdinc++ -I${LIBCXX_INCLUDE_DIR} -I${LIBCXX_BUILD_INCLUDE_DIR}")
        message(STATUS "Configuring GoogleTest with custom libcxx")
    endif()

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

    # Restore original CMAKE_CXX_FLAGS
    if(DEFINED LIBCXX_ROOT)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_BACKUP}")
    endif()

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

 # Clang-Format integration
 find_program(CLANG_FORMAT_EXE NAMES clang-format)
 if(CLANG_FORMAT_EXE)
   # Collect files to format (modules, sources, headers, and tests)
   file(GLOB_RECURSE CLANG_FORMAT_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cppm"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.ixx"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
     "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
   )

  # Use repo style if present, otherwise fall back to LLVM
  set(CLANG_FORMAT_STYLE "--style=file")
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.clang-format" AND
     NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/_clang-format")
    set(CLANG_FORMAT_STYLE "--style=LLVM")
  endif()

   add_custom_target(format
    COMMAND "${CLANG_FORMAT_EXE}" -i ${CLANG_FORMAT_STYLE} ${CLANG_FORMAT_SOURCES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Running clang-format on source files"
    VERBATIM
  )

   add_custom_target(format-check
    COMMAND "${CLANG_FORMAT_EXE}" ${CLANG_FORMAT_STYLE} --dry-run --Werror ${CLANG_FORMAT_SOURCES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Checking code style with clang-format (fails on diffs)"
    VERBATIM
  )
 else()
   message(STATUS "clang-format not found; 'format' and 'format-check' targets are disabled")
 endif()

 # Print build configuration
 message(STATUS "=== Storm Build Configuration ===")
 message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
 message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
 message(STATUS "Testing: ${STORM_ENABLE_TESTING}")
 message(STATUS "Sanitizer: ${STORM_ENABLE_SANITIZER}")
 message(STATUS "===================================")
