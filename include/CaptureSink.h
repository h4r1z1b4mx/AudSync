#pragma once

#include <string>
#include <atomic>
#include <vector>
#include <cstdint>
#include "NetworkManager.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define INVALID_SOCKET_VALUE INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
typedef int socket_t;
#define INVALID_SOCKET_VALUE -1
#define closesocket close
#endif

/**
 * Module 2: CaptureSink
 * Responsible for sending audio data over network as IP packets
 * Follows standard API: CaptureSinkInit(), CaptureSinkDeinit(), CaptureSinkProcess()
 */
class CaptureSink {
public:
    // Standard module API
    bool CaptureSinkInit(const std::string& serverHost = "localhost", int serverPort = 8080);
    
    // Shared network API (preferred for modular client)
    bool CaptureSinkInitWithSharedNetwork(NetworkManager* sharedNetworkManager);
    
    void CaptureSinkDeinit();
    bool CaptureSinkProcess();

    // Network operations
    bool connectToServer(const std::string& host, int port);
    bool disconnectFromServer();
    bool sendAudioData(const float* audioData, size_t samples, uint64_t timestamp);
    
    // Status
    bool isInitialized() const { return isInitialized_; }
    bool isConnected() const { return isConnected_.load(); }
    
    // Statistics
    uint64_t getTotalPacketsSent() const { return totalPacketsSent_.load(); }
    uint64_t getTotalBytesTransmitted() const { return totalBytesTransmitted_.load(); }

private:
    bool isInitialized_ = false;
    std::atomic<bool> isConnected_{false};
    std::atomic<bool> isRunning_{false};
    
    // Network configuration
    std::string serverHost_;
    int serverPort_ = 8080;
    socket_t clientSocket_ = INVALID_SOCKET_VALUE;
    
    // Sequence tracking
    uint32_t sequenceNumber_ = 0;
    
    // Statistics
    std::atomic<uint64_t> totalPacketsSent_{0};
    std::atomic<uint64_t> totalBytesTransmitted_{0};
    std::atomic<uint64_t> lastHeartbeatTime_{0};
    
    // Configuration
    struct CaptureSinkConfig {
        size_t maxQueueSize = 50;
        uint32_t heartbeatIntervalMs = 5000;
        size_t maxPacketSize = 4096;
    };
    
    CaptureSinkConfig* config_ = nullptr;
    
    // Network manager for server communication
    NetworkManager networkManager_;
    NetworkManager* sharedNetworkManager_ = nullptr; // For shared network mode
    bool usingSharedNetwork_ = false;
    
    // Network utilities
    bool initializeNetworking();
    void cleanupNetworking();
    bool sendData(const void* data, size_t size);
};