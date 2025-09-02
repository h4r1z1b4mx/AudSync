#pragma once

#include <string>
#include <atomic>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
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
 * Module 3: RenderSource
 * Responsible for receiving audio packets from network and managing jitter buffer
 * Follows standard API: RenderSourceInit(), RenderSourceDeinit(), RenderSourceProcess()
 */
class RenderSource {
public:
    struct ReceivedAudioPacket {
        uint32_t sequenceNumber;
        uint64_t timestamp;
        uint64_t arrivalTime;
        std::vector<float> audioData;
    };

    // Standard module API
    bool RenderSourceInit(const std::string& serverHost = "localhost", int serverPort = 8080);
    
    // Shared network API (preferred for modular client)
    bool RenderSourceInitWithSharedNetwork(NetworkManager* sharedNetworkManager);
    
    void RenderSourceDeinit();
    bool RenderSourceProcess();

    // Network operations
    bool startReceiving(const std::string& host, int port);
    void stopReceiving();
    
    // Audio output callback
    void setRenderCallback(std::function<void(const float*, size_t, uint64_t)> callback);
    
    // Audio configuration
    void setAudioParameters(int sampleRate, int channels, int framesPerBuffer);
    
    // Jitter buffer control
    void setBufferSize(double minBufferMs, double maxBufferMs, double targetBufferMs);
    
    // Status and statistics
    bool isInitialized() const { return isInitialized_; }
    bool isReceiving() const { return isReceiving_; }
    
    struct Stats {
        uint64_t totalPacketsReceived = 0;
        uint64_t totalPacketsDropped = 0;
        uint64_t totalBytesReceived = 0;
        double currentBufferMs = 0.0;
        double averageJitter = 0.0;
        uint32_t expectedSequence = 0;
    };
    
    Stats getStats() const;

private:
    bool isInitialized_ = false;
    std::atomic<bool> isReceiving_{false};
    std::atomic<bool> isRunning_{false};
    
    // Network configuration
    std::string serverHost_;
    int serverPort_ = 8080;
    socket_t clientSocket_ = INVALID_SOCKET_VALUE;
    
    // Network manager for server communication
    NetworkManager networkManager_;
    NetworkManager* sharedNetworkManager_ = nullptr; // For shared network mode
    bool usingSharedNetwork_ = false;
    
    // Jitter buffer
    std::map<uint32_t, ReceivedAudioPacket> jitterBuffer_;
    std::mutex jitterBufferMutex_;
    std::condition_variable jitterBufferCondition_;
    std::atomic<bool> jitterBufferReady_{false};
    
    // Buffer configuration - optimized for audio quality
    double minBufferMs_ = 40.0;      // Higher minimum for quality stability
    double maxBufferMs_ = 200.0;     // Allow larger buffer for quality
    double targetBufferMs_ = 80.0;   // Higher target for better quality
    double currentBufferSizeMs_ = 80.0;
    double adaptiveMaxBufferMs_ = 150.0;
    double packetIntervalMs_ = 10.7; // Updated for 512 samples at 48kHz
    
    // Audio parameters (configurable)
    int audioSampleRate_ = 48000;    // Professional quality, matches client default
    int audioChannels_ = 2;          // Stereo
    int audioFramesPerBuffer_ = 512; // Higher buffer size for better quality
    
    // Sequence tracking
    std::atomic<uint32_t> expectedSequenceNumber_{0};
    std::atomic<uint32_t> lastReceivedSequence_{0};
    
    // Statistics
    std::atomic<uint64_t> totalPacketsReceived_{0};
    std::atomic<uint64_t> totalPacketsDropped_{0};
    std::atomic<uint64_t> totalBytesReceived_{0};
    
    // Threading
    std::thread receptionThread_;
    std::thread jitterBufferThread_;
    
    // Callbacks
    std::function<void(const float*, size_t, uint64_t)> renderCallback_;
    std::function<void(const std::string&, const Stats&)> renderEventCallback_;
    
    // Network operations
    bool initializeNetworking();
    void cleanupNetworking();
    bool receiveData(void* buffer, size_t size);
    void closeSocket();
    
    // Reception worker
    void receptionWorker();
    void jitterBufferWorker();
    
    // Packet processing
    bool addPacketToBuffer(const ReceivedAudioPacket& audioPacket);
    void processReceivedPacket(const ReceivedAudioPacket& packet);
    void adaptBufferSize();
    ReceivedAudioPacket generateSilencePacket(uint32_t sequenceNumber, uint64_t timestamp);
    
    // Utility functions
    void handleConnectionError();
};
