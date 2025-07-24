#!/bin/bash

# Docker build script for AudSync
set -e

echo "🐳 Building AudSync Docker Images..."

# Get version from git or use default
VERSION=$(git describe --tags --always --dirty 2>/dev/null || echo "latest")
REGISTRY=${REGISTRY:-"your-dockerhub-username"}  # Change this to your Docker Hub username

echo "Building version: $VERSION"

# Build server image
echo "📦 Building AudSync Server..."
docker build -f Dockerfile.server -t audsync-server:$VERSION -t audsync-server:latest .
docker tag audsync-server:$VERSION $REGISTRY/audsync-server:$VERSION
docker tag audsync-server:latest $REGISTRY/audsync-server:latest

# Build client image
echo "📦 Building AudSync Client..."
docker build -f Dockerfile.client -t audsync-client:$VERSION -t audsync-client:latest .
docker tag audsync-client:$VERSION $REGISTRY/audsync-client:$VERSION
docker tag audsync-client:latest $REGISTRY/audsync-client:latest

echo "✅ Build completed!"
echo ""
echo "Built images:"
echo "  🖥️  audsync-server:$VERSION"
echo "  🎤 audsync-client:$VERSION"
echo ""
echo "To push to registry:"
echo "  docker push $REGISTRY/audsync-server:$VERSION"
echo "  docker push $REGISTRY/audsync-client:$VERSION"
