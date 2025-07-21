#!/bin/bash

# Demo script for AudSync - Real-time Audio Streaming
# This script demonstrates how to run the server and connect clients

echo "AudSync Demo - Real-time Audio Communication"
echo "============================================="
echo ""

PROJECT_DIR="/home/harizibam/nbase2/audSync"
BUILD_DIR="$PROJECT_DIR/build"

# Check if executables exist
if [ ! -f "$BUILD_DIR/audsync_server" ]; then
    echo "Error: audsync_server not found. Please build the project first."
    echo "Run: ./build.sh"
    exit 1
fi

if [ ! -f "$BUILD_DIR/audsync_client" ]; then
    echo "Error: audsync_client not found. Please build the project first."
    echo "Run: ./build.sh"
    exit 1
fi

echo "How to use AudSync:"
echo ""
echo "1. START THE SERVER (in one terminal):"
echo "   cd $BUILD_DIR"
echo "   ./audsync_server 8080"
echo ""
echo "2. CONNECT CLIENT 1 (in another terminal):"
echo "   cd $BUILD_DIR"
echo "   ./audsync_client 127.0.0.1 8080"
echo ""
echo "3. CONNECT CLIENT 2 (in a third terminal):"
echo "   cd $BUILD_DIR"
echo "   ./audsync_client 127.0.0.1 8080"
echo ""
echo "4. IN EACH CLIENT:"
echo "   - Type 'start' to begin audio streaming"
echo "   - Speak into your microphone"
echo "   - You should hear the other client's audio"
echo "   - Type 'stop' to pause audio"
echo "   - Type 'quit' to disconnect"
echo ""
echo "Features:"
echo "- Real-time audio streaming between clients"
echo "- Low-latency communication (≈5.8ms buffer)"
echo "- Automatic audio device detection"
echo "- Multi-client support"
echo ""
echo "Troubleshooting:"
echo "- Make sure your microphone and speakers are working"
echo "- Check audio permissions"
echo "- Ensure firewall allows port 8080"
echo "- For network issues, try: sudo ufw allow 8080"
echo ""

# Option to start server immediately
read -p "Would you like to start the server now? (y/n): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "Starting AudSync server on port 8080..."
    echo "Press Ctrl+C to stop the server"
    echo "In other terminals, run: $BUILD_DIR/audsync_client 127.0.0.1 8080"
    echo ""
    cd "$BUILD_DIR"
    ./audsync_server 8080
fi
