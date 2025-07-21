# AudSync

Real-time audio streaming application built with C++ and PortAudio. Allows two or more clients to connect through a cloud server and exchange audio in real-time.

## Features

- **Real-time Audio Streaming**: Low-latency audio communication using PortAudio
- **Console-based Interface**: Simple command-line interface for easy use
- **Cloud Server Architecture**: Centralized server manages multiple client connections
- **Cross-platform**: Works on Linux, macOS, and Windows
- **Thread-safe Audio Buffers**: Efficient audio buffer management for smooth streaming
- **Automatic Audio Device Detection**: Uses system default audio input/output devices

## Architecture

```
Client 1 (Microphone) ←→ Cloud Server ←→ Client 2 (Speakers)
                          ↑
                      Manages connections
                      and audio forwarding
```

## Prerequisites

- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.10 or later
- PortAudio development libraries
- ALSA development libraries (Linux only)

## Installation

### Automatic Installation (Linux)

```bash
./install_deps.sh
```

### Manual Installation

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install build-essential cmake pkg-config libasound2-dev portaudio19-dev
```

#### CentOS/RHEL
```bash
sudo yum groupinstall "Development Tools"
sudo yum install cmake pkg-config alsa-lib-devel portaudio-devel
```

#### Arch Linux
```bash
sudo pacman -S base-devel cmake pkg-config alsa-lib portaudio
```

#### macOS (with Homebrew)
```bash
brew install cmake portaudio
```

## Building

### Quick Build
```bash
./build.sh
```

### Manual Build
```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

## Usage

### 1. Start the Server
```bash
# Start server on default port (8080)
./build/audsync_server

# Start server on custom port
./build/audsync_server 9999
```

### 2. Connect Clients
```bash
# Connect to local server
./build/audsync_client

# Connect to remote server
./build/audsync_client 192.168.1.100 8080
```

### 3. Begin Audio Streaming

In each client console, use these commands:
- `start` - Begin audio streaming (microphone → speaker)
- `stop` - Stop audio streaming
- `quit` - Disconnect and exit

## How It Works

1. **Server Setup**: The server listens for client connections on a specified port
2. **Client Connection**: Clients connect to the server using TCP sockets
3. **Audio Initialization**: When a client types "start", it initializes PortAudio for recording and playback
4. **Real-time Streaming**: 
   - Client captures audio from microphone
   - Audio data is sent to server
   - Server forwards audio to all other connected clients
   - Clients play received audio through speakers

## Technical Details

### Audio Specifications
- **Sample Rate**: 44.1 kHz
- **Channels**: Mono (1 channel)
- **Sample Format**: 32-bit float
- **Buffer Size**: 256 frames (≈5.8ms latency)

### Network Protocol
- **Transport**: TCP for reliability
- **Message Types**: CONNECT, DISCONNECT, AUDIO_DATA, HEARTBEAT, CLIENT_READY
- **Audio Encoding**: Raw 32-bit float samples

### Threading Model
- **Server**: Multi-threaded, one thread per client connection
- **Client**: Separate threads for audio processing and network communication
- **Audio Buffers**: Thread-safe circular buffers for smooth audio flow

## Project Structure

```
audSync/
├── CMakeLists.txt          # Build configuration
├── README.md               # This file
├── build.sh               # Build script
├── install_deps.sh        # Dependency installation script
├── include/               # Header files
│   ├── AudioBuffer.h      # Thread-safe audio buffer
│   ├── AudioClient.h      # Client implementation
│   ├── AudioProcessor.h   # PortAudio wrapper
│   ├── AudioServer.h      # Server implementation
│   └── NetworkManager.h   # Network communication
└── src/                   # Source files
    ├── AudioBuffer.cpp
    ├── AudioClient.cpp
    ├── AudioProcessor.cpp
    ├── AudioServer.cpp
    ├── NetworkManager.cpp
    ├── main_client.cpp    # Client entry point
    └── main_server.cpp    # Server entry point
```

## Troubleshooting

### Audio Issues
- **No audio input/output**: Check microphone and speaker connections
- **Permission denied**: Ensure user has access to audio devices
- **High latency**: Try reducing buffer size in AudioProcessor constructor

### Network Issues
- **Connection refused**: Ensure server is running and firewall allows connections
- **Connection drops**: Check network stability and firewall settings

### Build Issues
- **PortAudio not found**: Install PortAudio development libraries
- **Compiler errors**: Ensure C++17 support and proper include paths

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

## License

This project is open source. Feel free to use, modify, and distribute as needed.

## Future Enhancements

- [ ] Audio compression/decompression
- [ ] Multiple audio formats support
- [ ] GUI interface
- [ ] Voice activity detection
- [ ] Echo cancellation
- [ ] Encryption for secure communication
- [ ] Conference call support (more than 2 clients)
- [ ] Audio recording/playback from files
