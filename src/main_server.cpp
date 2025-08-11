#include "AudioServer.h"
#include "SessionLogger.h"
#include "AudioRecorder.h"
#include "JitterBuffer.h"
#include <iostream>
#include <signal.h>
#include <string>
#include <iomanip>

AudioServer* g_server = nullptr;

void signalHandler(int signal) {
    (void) signal;
    if (g_server) {
        std::cout << "\nShutting down server... " << std::endl;
        g_server->stop();
    }
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
    std::cout << "\nThe server will automatically adapt to each client's audio format." << std::endl;
    std::cout << "No server-side audio configuration needed." << std::endl;
    
    std::cout << "\nStart server? (y/n): ";
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

    // ✅ FIXED: Use correct AudioServer constructor with required parameters
    int defaultSampleRate = 48000;  // Default, will be overridden by clients
    int defaultChannels = 2;        // Default, will be overridden by clients
    
    AudioServer server(defaultSampleRate, defaultChannels, &logger, &recorder, &jitterBuffer);
    g_server = &server;

    // Set up signal handler for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    if (!server.start(port)) {
        std::cerr << "Failed to start server on port " << port << std::endl;
        return 1;
    }

    std::cout << "\n✅ Server is running successfully!" << std::endl;
    std::cout << "Clients can connect using: ./audsync_client 127.0.0.1 " << port << std::endl;
    std::cout << "\nType commands during session:" << std::endl;
    std::cout << "  'status'   - Show connected clients" << std::endl;
    std::cout << "  'logon'    - Start logging" << std::endl;
    std::cout << "  'logoff'   - Stop logging" << std::endl;
    std::cout << "  'recstart' - Start recording" << std::endl;
    std::cout << "  'recstop'  - Stop recording" << std::endl;
    std::cout << "  'quit'     - Stop server and exit" << std::endl;

    std::string command;
    while (server.isRunning() && std::cin >> command) {
        if (command == "quit" || command == "stop") {
            break;
        } else if (command == "status") {
            std::cout << "=== SERVER STATUS ===" << std::endl;
            std::cout << "Connected clients: " << server.getConnectedClients() << std::endl;
            std::cout << "Port: " << port << std::endl;
        } else if (command == "logon") {
            std::string logFilename = "server_session_" + std::to_string(std::time(nullptr)) + ".log";
            logger.startLogging(logFilename);
            std::cout << "Logging started: " << logFilename << std::endl;
        } else if (command == "logoff") {
            logger.stopLogging();
            std::cout << "Logging stopped." << std::endl;
        } else if (command == "recstart") {
            std::string filename = "server_audio_" + std::to_string(std::time(nullptr)) + ".wav";
            // Use default configuration for recording
            recorder.startRecording(filename, defaultSampleRate, defaultChannels);
            std::cout << "Recording started: " << filename 
                      << " (" << defaultSampleRate << "Hz, " 
                      << defaultChannels << " channels)" << std::endl;
        } else if (command == "recstop") {
            recorder.stopRecording();
            std::cout << "Recording stopped." << std::endl;
        } else {
            std::cout << "Unknown command: '" << command << "'. Type 'quit' to exit." << std::endl;
        }
    }
    
    std::cout << "Server shutting down... " << std::endl;
    return 0;
}