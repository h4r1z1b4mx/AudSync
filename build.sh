#!/bin/bash

# Cross-platform build script for AudSync
set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

echo "Building AudSync..."

# Create build directory if it doesn't exist
if [ ! -d "$BUILD_DIR" ]; then
    mkdir -p "$BUILD_DIR"
fi

cd "$BUILD_DIR"

# Detect OS and configure appropriately
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
    echo "Building for Windows..."
    if [ -n "$VCPKG_ROOT" ]; then
        echo "Using vcpkg toolchain..."
        cmake .. -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    else
        echo "Warning: VCPKG_ROOT not set. Using default cmake configuration."
        cmake ..
    fi
    cmake --build . --config Release
elif [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Building for macOS..."
    cmake ..
    make -j$(sysctl -n hw.ncpu)
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "Building for Linux..."
    cmake ..
    make -j$(nproc)
else
    echo "Unknown OS: $OSTYPE"
    echo "Attempting generic build..."
    cmake ..
    cmake --build .
fi

if [ $? -eq 0 ]; then
    echo ""
    echo "Build completed successfully!"
    echo ""
    echo "Executables created:"
    if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
        echo "  $BUILD_DIR/audsync_server.exe - Audio streaming server"
        echo "  $BUILD_DIR/audsync_client.exe - Audio streaming client"
    else
        echo "  $BUILD_DIR/audsync_server - Audio streaming server"
        echo "  $BUILD_DIR/audsync_client - Audio streaming client"
    fi
else
    echo "Build failed!"
    exit 1
fi
echo ""
echo "Usage:"
echo "  1. Start the server: ./audsync_server [port]"
echo "  2. Connect clients: ./audsync_client [server_ip] [port]"
echo "  3. Type 'start' in client to begin audio streaming"
