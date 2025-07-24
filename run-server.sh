#!/bin/bash

# Start AudSync Server
echo "🖥️ Starting AudSync Server..."

# Create network if it doesn't exist
docker network create audsync-net 2>/dev/null || true

# Start server
docker run --rm --name audsync-server \
  --network audsync-net \
  -p 8080:8080 \
  audsync-server:latest
