#pragma once

#include "NetworkManager.h"
#include "SessionLogger.h"
#include "AudioRecorder.h"
#include "JitterBuffer.h"
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <string>

struct AudioConfig {
    int32_t sampleRate;
    int32_t channels;
    int32_t bufferSize;
};

struct ClientInfo {
    SOCKET socket_fd;
    std::string id;
    bool ready;
    int sampleRate = 48000;  // Store client's actual config
    int channels = 2;        // Store client's actual config
    int bufferSize = 256;    // Store client's actual config
};

class AudioServer {
public:
    AudioServer(int sampleRate,
                int channels,
                SessionLogger* logger,
                AudioRecorder* recorder,
                JitterBuffer* jitterBuffer);
    ~AudioServer();

    bool start(int port);
    void stop();

    bool isRunning() const;
    size_t getConnectedClients() const;

    // ADDED: New methods for client configuration management
    std::vector<AudioConfig> getClientConfigurations() const;
    void printClientDetails() const;

    // Utility for unique filenames
    static std::string generateUniqueFilename(const std::string& prefix, const std::string& ext);

private:
    NetworkManager network_manager_;
    std::vector<ClientInfo> clients_;
    std::atomic<bool> running_;

    SessionLogger* logger_;
    AudioRecorder* recorder_;
    JitterBuffer* jitterBuffer_;
    int sampleRate_;
    int channels_;

    mutable std::mutex clients_mutex_;  // FIXED: Use underscore for consistency
    std::thread server_thread_;

    void handleClientMessage(const Message& message, SOCKET client_socket);
    void broadcastAudioToOthers(const Message& message, SOCKET sender_socket);
    void addClient(SOCKET socket_fd);
    void removeClient(SOCKET socket_fd);
    void serverLoop();
};