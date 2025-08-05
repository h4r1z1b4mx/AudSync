#include "AudioServer.h"
#include "SessionLogger.h"
#include "AudioRecorder.h"
#include "JitterBuffer.h"
#include <iostream>
#include <signal.h>
#include <string>

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
    }

    std::cout << "AudSync Server - Real-time Audio Streaming Hub" << std::endl;
    std::cout << "Starting server on port: " << port << std::endl;

    // Enhanced quality settings
    int sampleRate = 48000;  // Higher quality default
    int channels = 2;        // Stereo default for better quality
    std::cout << "Enter desired sample rate for recording (48000 recommended): ";
    std::cin >> sampleRate;
    std::cout << "Enter number of channels (2=Stereo recommended): ";
    std::cin >> channels;

    // Initialize logger, recorder, jitter buffer
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

    std::cout << "Server is running. Press Ctrl + C to stop" << std::endl;
    std::cout << "Client can connect using: ./audsync_client <server_ip> " << port << std::endl;
    std::cout << "Type commands during session:" << std::endl;
    std::cout << "  status    - Show server status" << std::endl;
    std::cout << "  logon     - Start logging" << std::endl;
    std::cout << "  logoff    - Stop logging" << std::endl;
    std::cout << "  recstart  - Start recording session" << std::endl;
    std::cout << "  recstop   - Stop recording session" << std::endl;
    std::cout << "  quit      - Stop server and exit" << std::endl;
    std::cout << "  help      - Show this help" << std::endl;

    std::string command;
    while (server.isRunning() && std::cin >> command) {
        if (command == "quit" || command == "stop") {
            break;
        } else if (command == "status") {
            std::cout << "Connected clients: " << server.getConnectedClients() << std::endl;
        } else if (command == "logon") {
            std::string logFilename = AudioServer::generateUniqueFilename("server_session", "log");
            logger.startLogging(logFilename);
            std::cout << "Logging started: " << logFilename << std::endl;
        } else if (command == "logoff") {
            logger.stopLogging();
            std::cout << "Logging stopped." << std::endl;
        } else if (command == "recstart") {
            // ✅ Fixed: Generate unique filename with timestamp
            std::string filename = AudioServer::generateUniqueFilename("server_audio", "wav");
            recorder.startRecording(filename, sampleRate, channels);
            std::cout << "Audio recording started: " << filename << std::endl;
        } else if (command == "recstop") {
            recorder.stopRecording();
            std::cout << "Audio recording stopped." << std::endl;
        } else if (command == "help") {
            std::cout << "Commands:" << std::endl;
            std::cout << "  status    - Show server status" << std::endl;
            std::cout << "  logon     - Start logging" << std::endl;
            std::cout << "  logoff    - Stop logging" << std::endl;
            std::cout << "  recstart  - Start recording session" << std::endl;
            std::cout << "  recstop   - Stop recording session" << std::endl;
            std::cout << "  quit      - Stop server and exit" << std::endl;
            std::cout << "  help      - Show this help" << std::endl;
        } else {
            std::cout << "Unknown command. Type 'help' for available commands." << std::endl;
        }
    }
    std::cout << "Server shutting down... " << std::endl;
    return 0;
}