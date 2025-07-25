#!/bin/bash

echo "Installing dependencies for AudSync..."

# Detect the operating system
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
    echo "Windows environment detected (MSYS/Cygwin)"
    echo "Please use install_deps.bat or install_deps.ps1 instead"
    exit 1
elif [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Detected macOS system"
    # Check if Homebrew is installed
    if command -v brew >/dev/null 2>&1; then
        echo "Installing dependencies via Homebrew..."
        brew install cmake pkg-config portaudio
    else
        echo "Homebrew not found. Please install Homebrew first:"
        echo "/bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
        exit 1
    fi
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
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
    elif command -v dnf >/dev/null 2>&1; then
        echo "Detected Fedora system"
        sudo dnf groupinstall -y "Development Tools"
        sudo dnf install -y \
            cmake \
            pkg-config \
            alsa-lib-devel \
            portaudio-devel
    else
        echo "Unsupported Linux distribution"
        echo "Please install the following packages manually:"
        echo "- cmake"
        echo "- pkg-config"
        echo "- PortAudio development libraries"
        echo "- ALSA development libraries (for Linux)"
        exit 1
    fi
else
    echo "Unsupported operating system: $OSTYPE"
    echo "Please install the dependencies manually:"
    echo "- cmake"
    echo "- pkg-config"
    echo "- PortAudio development libraries"
    exit 1
fi

echo ""
echo "Dependencies installed successfully!"
echo ""
echo "To build the project:"
echo "1. mkdir build"
echo "2. cd build"
echo "3. cmake .."
echo "4. make -j\$(nproc)"
echo ""
