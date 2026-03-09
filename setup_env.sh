#!/bin/bash

# Storm C++23 Development Environment Setup
# ---------------------------------------
# This script configures the environment for building the Storm ORM library
# with LLVM Clang 20.1.8 and SQLite support on macOS ARM64

# Clear any existing conflicting environment variables
unset LLVM_CONFIG
unset LIBRARY_PATH

# Constant for legacy LLVM version string to detect ABI conflicts
readonly LEGACY_LLVM_VERSION="llvm@11"

# Explicitly unset any LLVM@11 related variables that might cause ABI conflicts
if [[ "$LDFLAGS" == *"${LEGACY_LLVM_VERSION}"* ]]; then
    echo "⚠️ Detected LLVM@11 in LDFLAGS, clearing it"
    unset LDFLAGS
fi

if [[ "$CPPFLAGS" == *"${LEGACY_LLVM_VERSION}"* ]]; then
    echo "⚠️ Detected LLVM@11 in CPPFLAGS, clearing it"
    unset CPPFLAGS
fi

if [[ "$PATH" == *"${LEGACY_LLVM_VERSION}"* ]]; then
    echo "⚠️ Detected LLVM@11 in PATH, removing those entries"
    export PATH=$(echo $PATH | tr ":" "\n" | grep -v "${LEGACY_LLVM_VERSION}" | tr "\n" ":")
fi

# Check if we're on macOS
if [[ "$(uname)" != "Darwin" ]]; then
    echo "⚠️ Warning: This script is designed for macOS. Some settings may not work on your platform."
fi

# Check if Homebrew LLVM is installed
if [[ ! -d "/opt/homebrew/opt/llvm" ]]; then
    echo "❌ Error: Homebrew LLVM not found at /opt/homebrew/opt/llvm" >&2
    echo "Please install it with: brew install llvm" >&2
    return 1
fi

# Check if SQLite is installed
if [[ ! -d "/opt/homebrew/opt/sqlite" ]]; then
    echo "❌ Error: Homebrew SQLite not found at /opt/homebrew/opt/sqlite" >&2
    echo "Please install it with: brew install sqlite" >&2
    return 1
fi

# LLVM and Clang settings for C++23
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"

# Set compiler explicitly
export CC="/opt/homebrew/opt/llvm/bin/clang"
export CXX="/opt/homebrew/opt/llvm/bin/clang++"

# Find macOS SDK path
SDK_PATH="$(xcrun --show-sdk-path)"
if [[ -z "$SDK_PATH" ]]; then
    echo "❌ Error: Could not find macOS SDK path" >&2
    echo "Make sure Xcode Command Line Tools are installed" >&2
    return 1
fi
echo "Found macOS SDK at: $SDK_PATH"

# Library paths for linking
export LDFLAGS="-L/opt/homebrew/opt/llvm/lib -L/opt/homebrew/opt/sqlite/lib"

# Include paths for headers - include macOS SDK headers
export CPPFLAGS="-I/opt/homebrew/opt/llvm/include -I/opt/homebrew/opt/sqlite/include -isysroot $SDK_PATH"
export CFLAGS="-I/opt/homebrew/opt/sqlite/include -isysroot $SDK_PATH"
export CXXFLAGS="-I/opt/homebrew/opt/sqlite/include -isysroot $SDK_PATH"

# Set C++ standard to C++23 explicitly
export CMAKE_CXX_STANDARD=23

# For better C++23 support with modern features
export CXXFLAGS="$CXXFLAGS -std=c++2b"

# Ensure we use the correct libc++ from LLVM 20
export LDFLAGS="$LDFLAGS -L/opt/homebrew/opt/llvm/lib/c++"

# Set CMake environment variables
export CMAKE_PREFIX_PATH="/opt/homebrew/opt/llvm:/opt/homebrew/opt/sqlite:$CMAKE_PREFIX_PATH"

# Print configuration for verification
echo "✅ Environment configured for Storm C++23 development"
echo "---------------------------------------------"
echo "Compiler: $(which $CXX) ($(${CXX} --version | head -n 1))"
echo "C++ Standard: $CMAKE_CXX_STANDARD"
echo "Include paths: $CPPFLAGS"
echo "Library paths: $LDFLAGS"
echo "SQLite: $(sqlite3 --version)"
echo "CMake prefix path: $CMAKE_PREFIX_PATH"
echo "---------------------------------------------"
echo "To build the project:"
echo "1. Clean the build directory: rm -rf build"
echo "2. Configure: cmake --preset=ninja-debug"
echo "3. Build: cmake --build build/debug"
echo "---------------------------------------------"
