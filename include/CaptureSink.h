#pragma once

#include <functional>
#include <vector>
#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
#include <memory>
#include <cstdint>
#include <string>
#include <condition_variable>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

#include "Message.h"
#include "AudioNetworkPacket.h"

// Forward declarations
struct CaptureSinkConfig;
struct CaptureSinkStats;
struct AudioPacket;

// Callback for network events
using NetworkEventCallback = std::function<void(const std::string& event, bool success)>;

/**
 * @brief CaptureSink Module - Handles audio data transmission over network
 * 
 * This module takes captured audio data, packetizes it, and transmits it
 * over the network using reliable protocols with proper error handling.
 */
class CaptureSink {
public:
    CaptureSink();
    ~CaptureSink();

    // ===== STANDARD MODULE API =====
    
    /**
     * @brief Initialize the capture sink
     * @param config Configuration parameters for network transmission
     * @return true if initialization successful, false otherwise
     */
    bool CaptureSinkInit(const CaptureSinkConfig& config);
    
    /**
     * @brief Deinitialize and cleanup the capture sink
     * @return true if cleanup successful, false otherwise
     */
    bool CaptureSinkDeinit();
    
    /**
     * @brief Process network transmission (called per frame/buffer)
     * @return true if processing successful, false if error occurred
     */
    bool CaptureSinkProcess();

    // ===== AUDIO DATA HANDLING =====
    
    /**
     * @brief Send audio data over network
     * @param audioData Pointer to audio samples
     * @param samples Number of audio samples
     * @param timestamp Timestamp when audio was captured
     * @return true if data queued successfully for transmission
     */
    bool sendAudioData(const float* audioData, size_t samples, uint64_t timestamp);
    
    /**
     * @brief Send audio packet over network
     * @param packet Pre-formatted audio packet
     * @return true if packet queued successfully for transmission
     */
    bool sendAudioPacket(const AudioNetworkPacket& packet);

    // ===== NETWORK MANAGEMENT =====
    
    /**
     * @brief Connect to server
     * @param serverHost Server hostname or IP address
     * @param serverPort Server port number
     * @return true if connection established successfully
     */
    bool connectToServer(const std::string& serverHost, int serverPort);
    
    /**
     * @brief Disconnect from server
     * @return true if disconnected successfully
     */
    bool disconnectFromServer();
    
    /**
     * @brief Check if connected to server
     * @return true if currently connected
     */
    bool isConnected() const;

    // ===== CONFIGURATION & CONTROL =====
    
    /**
     * @brief Set callback for network events
     * @param callback Function to call on network events
     */
    void setNetworkEventCallback(NetworkEventCallback callback);
    
    /**
     * @brief Get current transmission statistics
     * @return Statistics about network transmission
     */
    CaptureSinkStats getStats() const;
    
    /**
     * @brief Flush pending packets immediately
     * @return Number of packets flushed
     */
    size_t flushPendingPackets();

private:
    // Network transmission thread
    void transmissionWorker();
    
    // Packet creation and management
    AudioNetworkPacket createAudioPacket(const float* audioData, size_t samples, uint64_t timestamp);
    uint32_t calculateChecksum(const uint8_t* data, size_t size);
    bool transmitPacket(const AudioNetworkPacket& packet);
    
    // Connection management
    bool establishConnection();
    void handleConnectionError();
    bool sendHeartbeat();
    
    // Direct socket operations
    bool initializeSocket();
    void closeSocket();
    bool sendData(const void* data, size_t size);
    bool receiveData(void* buffer, size_t size);
    bool sendMessage(const Message& message);  // Send complete message
    
    // Member variables
    socket_t clientSocket_;
    std::string serverHost_;
    int serverPort_;
    CaptureSinkConfig* config_;
    
    // Threading
    std::thread transmissionThread_;
    std::atomic<bool> isRunning_;
    std::atomic<bool> isInitialized_;
    std::atomic<bool> isConnected_;
    
    // Packet queue and synchronization
    std::queue<AudioNetworkPacket> packetQueue_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    
    // Sequence and timing
    std::atomic<uint32_t> sequenceNumber_;
    std::atomic<uint64_t> lastHeartbeatTime_;
    
    // Statistics and monitoring
    mutable std::atomic<uint64_t> totalPacketsSent_;
    mutable std::atomic<uint64_t> totalPacketsDropped_;
    mutable std::atomic<uint64_t> totalBytesTransmitted_;
    mutable std::atomic<uint64_t> lastTransmissionTime_;
    mutable std::atomic<double> averageLatency_;
    
    // Callbacks
    NetworkEventCallback networkEventCallback_;
    
    // Connection retry mechanism
    std::atomic<int> reconnectAttempts_;
    std::atomic<uint64_t> lastReconnectTime_;
};

/**
 * @brief Configuration structure for CaptureSink
 */
struct CaptureSinkConfig {
    std::string serverHost = "localhost";
    int serverPort = 12345;
    int maxQueueSize = 100;                 // Maximum packets in queue
    int heartbeatIntervalMs = 5000;         // Heartbeat interval
    int connectionTimeoutMs = 10000;        // Connection timeout
    int maxReconnectAttempts = 5;           // Maximum reconnection attempts
    int reconnectDelayMs = 2000;            // Delay between reconnects
    bool enableCompression = false;         // Audio compression
    bool enableEncryption = false;          // Network encryption
    int transmissionThreadPriority = 0;    // Thread priority (0=normal)
    size_t maxPacketSize = 4096;            // Maximum packet size in bytes
};

/**
 * @brief Statistics structure for CaptureSink
 */
struct CaptureSinkStats {
    uint64_t totalPacketsSent = 0;
    uint64_t totalPacketsDropped = 0;
    uint64_t totalBytesTransmitted = 0;
    uint64_t lastTransmissionTime = 0;
    uint64_t queuedPackets = 0;
    double averageLatency = 0.0;
    bool isConnected = false;
    bool isTransmitting = false;
    int reconnectAttempts = 0;
    double transmissionRate = 0.0;          // Packets per second
    double bandwidthUtilization = 0.0;      // Bytes per second
};