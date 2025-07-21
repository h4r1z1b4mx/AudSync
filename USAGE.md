# AudSync Usage Guide

## Quick Start

### 1. Build the Project
```bash
./build.sh
```

### 2. Start the Server
```bash
cd build
./audsync_server 8080
```

### 3. Connect Clients
In separate terminals:
```bash
cd build
./audsync_client 127.0.0.1 8080
```

### 4. Begin Audio Communication
In each client terminal:
- Type `start` - Begin audio streaming
- Type `stop` - Pause audio streaming  
- Type `quit` - Disconnect and exit

## Server Commands
- `status` - Show number of connected clients
- `quit` - Stop server and exit
- `help` - Show available commands

## Network Configuration

### Local Testing
- Server: `./audsync_server 8080`
- Client: `./audsync_client 127.0.0.1 8080`

### Remote Connections
- Server: `./audsync_server 8080`
- Client: `./audsync_client <server_ip> 8080`

### Firewall Setup
```bash
# Allow incoming connections on port 8080
sudo ufw allow 8080

# Or for specific interface
sudo iptables -A INPUT -p tcp --dport 8080 -j ACCEPT
```

## Audio Requirements
- Working microphone for input
- Working speakers/headphones for output
- Audio permissions for the application

## Architecture Flow
```
Client 1: Microphone → Network → Server → Network → Client 2: Speakers
Client 2: Microphone → Network → Server → Network → Client 1: Speakers
```

## Technical Specifications
- **Sample Rate**: 44.1 kHz
- **Channels**: Mono (1 channel)
- **Bit Depth**: 32-bit float
- **Buffer Size**: 256 frames (≈5.8ms latency)
- **Protocol**: TCP for reliability
- **Compression**: None (raw audio)

## Troubleshooting

### Audio Issues
- **No sound**: Check microphone/speaker connections
- **Permission denied**: Run with appropriate audio permissions
- **High latency**: Audio system may be under load

### Network Issues
- **Connection refused**: Ensure server is running first
- **Timeout**: Check firewall settings and network connectivity
- **Audio drops**: Check network stability

### Build Issues
- **PortAudio not found**: Install development libraries
  ```bash
  sudo apt install portaudio19-dev libportaudio2
  ```
- **Compiler errors**: Ensure C++17 support

## Performance Tips
- Use wired connections for best audio quality
- Close unnecessary applications to reduce latency
- Use dedicated audio interfaces for professional use
- Monitor CPU usage during streaming

## Security Notes
- Audio data is transmitted unencrypted
- Only use on trusted networks
- Consider VPN for remote connections
- Server accepts connections from any IP by default
