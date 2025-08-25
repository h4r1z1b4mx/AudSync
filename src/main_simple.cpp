#include "AudioClient.h"
#include <iostream>
#include <string>
#include <sstream>

void printHelp() {
    std::cout << "\n📋 Available Commands:" << std::endl;
    std::cout << "  connect <host> <port>  - Connect to audio server" << std::endl;
    std::cout << "  start                  - Start audio (after connecting)" << std::endl;
    std::cout << "  stop                   - Stop audio" << std::endl;
    std::cout << "  stats                  - Show audio statistics" << std::endl;
    std::cout << "  volume <0.0-1.0>       - Set volume level" << std::endl;
    std::cout << "  mute <on/off>          - Mute/unmute audio" << std::endl;
    std::cout << "  disconnect             - Disconnect from server" << std::endl;
    std::cout << "  help                   - Show this help" << std::endl;
    std::cout << "  quit                   - Exit client" << std::endl;
    std::cout << std::endl;
}

int main() {
    std::cout << "🎵 AudSync Cross-Platform Audio Client" << std::endl;
    std::cout << "=======================================" << std::endl;
    std::cout << "This version uses the proper 4-module architecture:" << std::endl;
    std::cout << "  🎤 Module 1 - CaptureSource: Microphone capture (PortAudio)" << std::endl;
    std::cout << "  📡 Module 2 - CaptureSink: Network transmission (IP packets)" << std::endl;
    std::cout << "  📥 Module 3 - RenderSource: Network reception & jitter buffer" << std::endl;
    std::cout << "  🔊 Module 4 - RenderSink: Speaker playback (PortAudio)" << std::endl;
    std::cout << std::endl;
    std::cout << "Each module follows standard API: <Module>Init(), <Module>Deinit(), <Module>Process()" << std::endl;
    
    printHelp();
    
    AudioClient client;
    std::string line;
    
    while (true) {
        std::cout << "\n> ";
        std::cout.flush(); // Ensure prompt is displayed
        
        if (!std::getline(std::cin, line)) {
            std::cout << "\n❌ Input stream closed or error occurred" << std::endl;
            break;
        }
        
        // Debug: Show what we received
        if (!line.empty()) {
            std::cout << "🔍 Processing command: '" << line << "'" << std::endl;
        }
        
        std::istringstream iss(line);
        std::string command;
        iss >> command;
        
        if (command == "help") {
            printHelp();
        }
        else if (command == "quit" || command == "exit") {
            std::cout << "👋 Goodbye!" << std::endl;
            break;
        }
        else if (command == "connect") {
            std::string host;
            int port;
            if (iss >> host >> port) {
                std::cout << "🔗 Connecting to " << host << ":" << port << "..." << std::endl;
                if (client.connect(host, port)) {
                    std::cout << "✅ Connected successfully!" << std::endl;
                } else {
                    std::cout << "❌ Connection failed!" << std::endl;
                }
            } else {
                std::cout << "❌ Usage: connect <host> <port>" << std::endl;
            }
        }
        else if (command == "start") {
            std::cout << "🎵 Starting audio..." << std::endl;
            if (client.startAudio()) {
                std::cout << "✅ Audio started successfully!" << std::endl;
            } else {
                std::cout << "❌ Failed to start audio!" << std::endl;
            }
        }
        else if (command == "stop") {
            std::cout << "🛑 Stopping audio..." << std::endl;
            client.stopAudio();
        }
        else if (command == "stats") {
            client.showComprehensiveStats();
        }
        else if (command == "volume") {
            float volume;
            if (iss >> volume) {
                if (volume >= 0.0f && volume <= 1.0f) {
                    client.setVolume(volume);
                    std::cout << "🔊 Volume set to " << (volume * 100) << "%" << std::endl;
                } else {
                    std::cout << "❌ Volume must be between 0.0 and 1.0" << std::endl;
                }
            } else {
                std::cout << "❌ Usage: volume <0.0-1.0>" << std::endl;
            }
        }
        else if (command == "mute") {
            std::string state;
            if (iss >> state) {
                bool muted = (state == "on" || state == "true" || state == "1");
                client.setMuted(muted);
                std::cout << (muted ? "🔇 Audio muted" : "🔊 Audio unmuted") << std::endl;
            } else {
                std::cout << "❌ Usage: mute <on/off>" << std::endl;
            }
        }
        else if (command == "disconnect") {
            std::cout << "🔌 Disconnecting..." << std::endl;
            client.disconnect();
        }
        else if (!command.empty()) {
            std::cout << "❌ Unknown command: " << command << std::endl;
            std::cout << "Type 'help' for available commands." << std::endl;
        }
    }
    
    return 0;
}
