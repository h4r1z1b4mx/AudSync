#!/bin/bash

# Start AudSync Client
echo "🎤 Starting AudSync Client..."

# Default server address
SERVER=${1:-audsync-server}
PORT=${2:-8080}

echo "Connecting to: $SERVER:$PORT"

# Create network if it doesn't exist
docker network create audsync-net 2>/dev/null || true

# Start client
docker run -it --rm --name audsync-client \
  --network audsync-net \
  audsync-client:latest $SERVER $PORT
