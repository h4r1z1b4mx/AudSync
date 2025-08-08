#include "AudioServer.h"
#include "SessionLogger.h"
#include "AudioRecorder.h"
#include "JitterBuffer.h"
#include <iostream>
#include <signal.h>
#include <string>
#include <vector>
#include <algorithm>
#include <iomanip>      // ✅ ADD: For std::setprecision
#include <portaudio.h>

AudioServer* g_server = nullptr;

void signalHandler(int signal) {
    (void) signal;
    if (g_server) {
        std::cout << "\nShutting down server... " << std::endl;
        g_server->stop();
    }
}

// ✅ NEW: Get server-compatible sample rates (based on common professional rates)
std::vector<int> getServerSampleRates() {
    return {8000, 16000, 22050, 44100, 48000, 88200, 96000};
}

// ✅ NEW: Get server-compatible channel configurations
std::vector<std::pair<int, std::string>> getServerChannelOptions() {
    return {
        {1, "Mono (1 channel)"},
        {2, "Stereo (2 channels)"},
        {4, "Quad (4 channels)"},
        {6, "5.1 Surround (6 channels)"},
        {8, "7.1 Surround (8 channels)"}
    };
}

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc >= 2) {
        port = std::stoi(argv[1]);
        if (port < 1024 || port > 65535) {
            std::cerr << "Invalid port number. Using default 8080." << std::endl;
            port = 8080;
        }
    }

    std::cout << "AudSync Server - Real-time Audio Streaming Hub" << std::endl;
    std::cout << "Starting server on port: " << port << std::endl;

    // ✅ SAMPLE RATE SELECTION - Show supported options
    auto sampleRates = getServerSampleRates();
    std::cout << "\n=== SUPPORTED SAMPLE RATES ===" << std::endl;
    for (size_t i = 0; i < sampleRates.size(); ++i) {
        std::string quality = (sampleRates[i] <= 16000) ? "Voice" :
                             (sampleRates[i] <= 48000) ? "Standard" : "High-Res";
        std::cout << "  [" << i << "] " << sampleRates[i] << "Hz (" << quality << ")" << std::endl;
    }
    
    int sampleRateChoice = 4; // Default to 48000Hz
    std::cout << "Select sample rate (default 48000Hz): ";
    std::cin >> sampleRateChoice;
    
    if (sampleRateChoice < 0 || sampleRateChoice >= static_cast<int>(sampleRates.size())) {
        std::cout << "Invalid selection, using default 48000Hz" << std::endl;
        sampleRateChoice = 4;
    }
    int sampleRate = sampleRates[sampleRateChoice];

    // ✅ CHANNEL SELECTION - Show supported options
    auto channelOptions = getServerChannelOptions();
    std::cout << "\n=== SUPPORTED CHANNEL CONFIGURATIONS ===" << std::endl;
    for (size_t i = 0; i < channelOptions.size(); ++i) {
        std::cout << "  [" << i << "] " << channelOptions[i].second << std::endl;
    }
    
    int channelChoice = 1; // Default to stereo
    std::cout << "Select channel configuration (default Stereo): ";
    std::cin >> channelChoice;
    
    if (channelChoice < 0 || channelChoice >= static_cast<int>(channelOptions.size())) {
        std::cout << "Invalid selection, using default Stereo" << std::endl;
        channelChoice = 1;
    }
    int channels = channelOptions[channelChoice].first;

    // ✅ VALIDATE CONFIGURATION
    std::cout << "\n=== SERVER CONFIGURATION ===" << std::endl;
    std::cout << "Sample Rate: " << sampleRate << "Hz" << std::endl;
    std::cout << "Channels: " << channels << " (" << channelOptions[channelChoice].second << ")" << std::endl;
    std::cout << "Port: " << port << std::endl;
    
    // Calculate bandwidth requirements
    int bytesPerSecond = sampleRate * channels * sizeof(float);
    std::cout << "Bandwidth per client: " << std::fixed << std::setprecision(1) 
              << bytesPerSecond / 1024.0 << " KB/s" << std::endl;
    
    std::cout << "\nConfiguration looks good? (y/n): ";
    char confirm;
    std::cin >> confirm;
    if (confirm != 'y' && confirm != 'Y') {
        std::cout << "Server startup cancelled." << std::endl;
        return 0;
    }

    // Initialize components
    SessionLogger logger;
    AudioRecorder recorder;
    JitterBuffer jitterBuffer;

    AudioServer server(sampleRate, channels, &logger, &recorder, &jitterBuffer);
    g_server = &server;

    // Set up signal handler for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    if (!server.start(port)) {
        std::cerr << "Failed to start server on port " << port << std::endl;
        return 1;
    }

    std::cout << "\n✅ Server is running successfully!" << std::endl;
    std::cout << "Clients can connect using: ./audsync_client 127.0.0.1 " << port << std::endl;  // ✅ FIXED: Hardcoded IP
    std::cout << "\nType commands during session:" << std::endl;
    std::cout << "  'status'   - Show server status" << std::endl;
    std::cout << "  'logon'    - Start logging" << std::endl;
    std::cout << "  'logoff'   - Stop logging" << std::endl;
    std::cout << "  'recstart' - Start recording session" << std::endl;
    std::cout << "  'recstop'  - Stop recording session" << std::endl;
    std::cout << "  'quit'     - Stop server and exit" << std::endl;
    std::cout << "  'help'     - Show this help" << std::endl;

    std::string command;
    while (server.isRunning() && std::cin >> command) {
        if (command == "quit" || command == "stop") {
            break;
        } else if (command == "status") {
            std::cout << "=== SERVER STATUS ===" << std::endl;
            std::cout << "Connected clients: " << server.getConnectedClients() << std::endl;
            std::cout << "Sample rate: " << sampleRate << "Hz" << std::endl;
            std::cout << "Channels: " << channels << std::endl;
            std::cout << "Total bandwidth: " << std::fixed << std::setprecision(1)
                      << (bytesPerSecond * server.getConnectedClients()) / 1024.0 << " KB/s" << std::endl;
        } else if (command == "logon") {
            std::string logFilename = AudioServer::generateUniqueFilename("server_session", "log");
            logger.startLogging(logFilename);
            std::cout << "Logging started: " << logFilename << std::endl;
        } else if (command == "logoff") {
            logger.stopLogging();
            std::cout << "Logging stopped." << std::endl;
        } else if (command == "recstart") {
            std::string filename = AudioServer::generateUniqueFilename("server_audio", "wav");
            recorder.startRecording(filename, sampleRate, channels);
            std::cout << "Audio recording started: " << filename << std::endl;
        } else if (command == "recstop") {
            recorder.stopRecording();
            std::cout << "Audio recording stopped." << std::endl;
        } else if (command == "help") {
            std::cout << "Available commands:" << std::endl;
            std::cout << "  status   - Show server status and statistics" << std::endl;
            std::cout << "  logon    - Start session logging" << std::endl;
            std::cout << "  logoff   - Stop session logging" << std::endl;
            std::cout << "  recstart - Start audio recording" << std::endl;
            std::cout << "  recstop  - Stop audio recording" << std::endl;
            std::cout << "  quit     - Stop server and exit" << std::endl;
            std::cout << "  help     - Show this help message" << std::endl;
        } else {
            std::cout << "Unknown command: '" << command << "'. Type 'help' for available commands." << std::endl;
        }
    }
    
    std::cout << "Server shutting down... " << std::endl;
    return 0;
}