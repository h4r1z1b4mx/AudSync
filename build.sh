#!/bin/bash

# Build script for AudSync
set -e

PROJECT_DIR="/home/harizibam/cmpy/AudSync"
BUILD_DIR="$PROJECT_DIR/build"

echo "Building AudSync..."

# Create build directory if it doesn't exist
if [ ! -d "$BUILD_DIR" ]; then
    mkdir -p "$BUILD_DIR"
fi

cd "$BUILD_DIR"

# Run CMake
echo "Configuring with CMake..."
cmake ..

# Build the project
echo "Building project..."
make -j$(nproc)

echo "Build completed!"
echo ""
echo "Executables created:"
echo "  $BUILD_DIR/audsync_server - Audio streaming server"
echo "  $BUILD_DIR/audsync_client - Audio streaming client"
echo ""
echo "Usage:"
echo "  1. Start the server: ./audsync_server [port]"
echo "  2. Connect clients: ./audsync_client [server_ip] [port]"
echo "  3. Type 'start' in client to begin audio streaming"
