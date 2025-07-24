#!/bin/bash

echo "Installing dependencies for AudSync..."

# Check if running on Ubuntu/Debian
if command -v apt-get >/dev/null 2>&1; then
    echo "Detected Debian/Ubuntu system"
    sudo apt-get update
    sudo apt-get install -y \
        build-essential \
        cmake \
        pkg-config \
        libasound2-dev \
        portaudio19-dev \
        libportaudio2 \
        libportaudiocpp0
elif command -v yum >/dev/null 2>&1; then
    echo "Detected RedHat/CentOS system"
    sudo yum groupinstall -y "Development Tools"
    sudo yum install -y \
        cmake \
        pkg-config \
        alsa-lib-devel \
        portaudio-devel
elif command -v pacman >/dev/null 2>&1; then
    echo "Detected Arch Linux system"
    sudo pacman -S --needed \
        base-devel \
        cmake \
        pkg-config \
        alsa-lib \
        portaudio
else
    echo "Unknown system. Please install the following dependencies manually:"
    echo "- Build tools (gcc, g++, make)"
    echo "- CMake"
    echo "- PortAudio development libraries"
    echo "- ALSA development libraries (Linux)"
fi

echo "Dependencies installation completed!"
echo "You can now build the project with:"
echo "  mkdir build"
echo "  cd build"
echo "  cmake .."
echo "  make"
