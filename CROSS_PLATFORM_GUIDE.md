# Cross-Platform Compatibility Guide

This document outlines the changes made to make AudSync cross-platform compatible and the differences between platform implementations.

## Platform-Specific Changes Made

### 1. Networking Layer (NetworkManager)

#### Before (Linux-only):
```cpp
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int socket_fd;  // Linux socket descriptor
```

#### After (Cross-platform):
```cpp
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef int socklen_t;
    #define close_socket closesocket
    SOCKET socket_fd;  // Windows socket handle
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    typedef int SOCKET;
    #define close_socket close
    SOCKET socket_fd;  // Unix socket descriptor
#endif
```

### 2. Socket Initialization

#### Windows-specific:
```cpp
// Windows requires WSA initialization
WSADATA wsaData;
WSAStartup(MAKEWORD(2, 2), &wsaData);
```

#### Unix/Linux:
```cpp
// No initialization required
```

### 3. Build System (CMake)

#### Enhanced PortAudio Detection:
```cmake
# Cross-platform PortAudio search paths
find_path(PORTAUDIO_INCLUDE_DIR portaudio.h
    PATHS 
        /usr/include                          # Linux
        /usr/local/include                    # macOS/Linux
        ${CMAKE_PREFIX_PATH}/include          # Cross-platform
        $ENV{PORTAUDIO_ROOT}/include          # Environment variable
        "C:/Program Files/PortAudio/include"  # Windows
)
```

#### Platform-specific Linking:
```cmake
# Windows networking libraries
if(WIN32)
    set(NETWORK_LIBRARIES ws2_32 wsock32)
endif()

# macOS audio frameworks
if(APPLE)
    find_library(CORE_AUDIO_FRAMEWORK CoreAudio)
    # ... link frameworks
endif()
```

## Platform Differences

### Networking

| Feature | Linux/macOS | Windows |
|---------|-------------|---------|
| Socket Type | `int` | `SOCKET` (unsigned int) |
| Error Value | `-1` | `SOCKET_ERROR` |
| Invalid Socket | `-1` | `INVALID_SOCKET` |
| Close Function | `close()` | `closesocket()` |
| Initialization | None | `WSAStartup()` |
| Cleanup | None | `WSACleanup()` |
| Send/Recv Size | `ssize_t` | `int` |

### Audio (PortAudio)

| Platform | Installation Method | Library Paths |
|----------|-------------------|---------------|
| Linux | Package manager (`apt`, `yum`, `pacman`) | `/usr/lib`, `/usr/local/lib` |
| Windows | vcpkg or manual | `C:/Program Files/PortAudio/` |
| macOS | Homebrew | `/usr/local/lib`, `/opt/homebrew/lib` |

### Build Tools

| Platform | Compiler | Build System |
|----------|----------|--------------|
| Linux | GCC/Clang | Make, Ninja |
| Windows | MSVC/MinGW | MSBuild, Make, Ninja |
| macOS | Clang | Make, Xcode |

## Installation Scripts

### Linux (`install_deps.sh`)
- Detects distribution (Ubuntu, Fedora, Arch, etc.)
- Uses appropriate package manager
- Installs build tools and development libraries

### Windows (`install_deps.bat`, `install_deps.ps1`)
- Checks for vcpkg availability
- Installs PortAudio via vcpkg
- Provides setup instructions

### macOS (included in `install_deps.sh`)
- Uses Homebrew for package management
- Installs Xcode command line tools dependencies

## Build Scripts

### Cross-platform Detection
All build scripts detect the operating system using `$OSTYPE`:
- `linux-gnu*`: Linux systems
- `darwin*`: macOS systems  
- `msys`/`cygwin`: Windows with Unix-like environment

### Platform-specific Build Commands

#### Linux:
```bash
cmake ..
make -j$(nproc)
```

#### Windows:
```cmd
cmake .. -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake --build . --config Release
```

#### macOS:
```bash
cmake ..
make -j$(sysctl -n hw.ncpu)
```

## Testing Cross-Platform Compatibility

### Compile-time Compatibility
- Platform-specific preprocessor directives
- Type definitions for cross-platform compatibility
- Conditional includes based on platform

### Runtime Compatibility
- Dynamic library loading paths
- Network initialization/cleanup
- Audio device enumeration

## Future Enhancements

### Potential Improvements
1. **Better Error Handling**: Platform-specific error codes and messages
2. **Unicode Support**: Wide character support for Windows file paths
3. **Service Integration**: Windows Service / Linux systemd support
4. **Package Management**: Native package formats (RPM, DEB, MSI)
5. **GUI Support**: Cross-platform GUI using Qt or similar

### Known Limitations
1. **Audio Device Selection**: Currently uses default devices only
2. **Network Discovery**: No automatic server discovery
3. **Configuration**: No configuration file support
4. **Logging**: Basic console logging only

## Development Environment Setup

### Windows Development
1. Install Visual Studio 2019+ or MinGW-w64
2. Install vcpkg package manager
3. Install PortAudio: `vcpkg install portaudio`
4. Set VCPKG_ROOT environment variable

### Linux Development
1. Install development tools: `sudo apt install build-essential cmake`
2. Install PortAudio: `sudo apt install portaudio19-dev`
3. Install ALSA: `sudo apt install libasound2-dev`

### macOS Development
1. Install Xcode Command Line Tools: `xcode-select --install`
2. Install Homebrew: `/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"`
3. Install dependencies: `brew install cmake portaudio`

This cross-platform implementation ensures AudSync can be built and run on all major desktop operating systems while maintaining the same functionality and performance characteristics.
