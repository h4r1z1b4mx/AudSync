#pragma once

#include <functional>
#include <vector>
#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
#include <memory>
#include <cstdint>
#include <condition_variable>
#include <map>
#include <string>
#include "Message.h"

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
struct RenderSourceConfig;
struct RenderSourceStats;

// Audio packet structure for internal processing
struct ReceivedAudioPacket {
    uint32_t sequenceNumber;
    uint64_t timestamp;
    uint64_t arrivalTime;
    uint32_t sampleRate;
    uint16_t channels;
    std::vector<float> audioData;
    bool isValid;
    uint32_t expectedChecksum;
    uint32_t actualChecksum;
};

// Callback for processed audio data ready for playback
using RenderCallback = std::function<void(const float* audioData, size_t samples, uint64_t timestamp)>;

// Callback for network events and statistics
using RenderEventCallback = std::function<void(const std::string& event, const RenderSourceStats& stats)>;

/**
 * @brief RenderSource Module - Handles network packet reception and jitter buffering
 * 
 * This module receives audio packets from the network, manages jitter buffering,
 * handles packet loss recovery, and provides smooth audio data for playback.
 */
class RenderSource {
public:
    RenderSource();
    ~RenderSource();

    // ===== STANDARD MODULE API =====
    
    /**
     * @brief Initialize the render source
     * @param config Configuration parameters for network reception
     * @return true if initialization successful, false otherwise
     */
    bool RenderSourceInit(const RenderSourceConfig& config);
    
    /**
     * @brief Deinitialize and cleanup the render source
     * @return true if cleanup successful, false otherwise
     */
    bool RenderSourceDeinit();
    
    /**
     * @brief Process received packets and jitter buffer (called per frame)
     * @return true if processing successful, false if error occurred
     */
    bool RenderSourceProcess();

    // ===== NETWORK MANAGEMENT =====
    
    /**
     * @brief Start listening for incoming audio packets
     * @param serverHost Host to connect to for receiving packets
     * @param serverPort Port to connect to
     * @return true if started successfully
     */
    bool startReceiving(const std::string& serverHost, int serverPort);
    
    /**
     * @brief Stop receiving audio packets
     * @return true if stopped successfully
     */
    bool stopReceiving();
    
    /**
     * @brief Check if currently receiving packets
     * @return true if actively receiving
     */
    bool isReceiving() const;

    // ===== AUDIO DATA HANDLING =====
    
    /**
     * @brief Manually add received packet (for testing or alternative network handling)
     * @param packet Network packet received from remote
     * @return true if packet processed successfully
     */
    bool addReceivedPacket(const AudioNetworkPacket& packet);
    
    /**
     * @brief Get next available audio data for playback
     * @param audioData Output buffer for audio samples
     * @param maxSamples Maximum samples to retrieve
     * @param timestamp Output timestamp of the audio data
     * @return Number of samples retrieved (0 if none available)
     */
    size_t getAudioData(float* audioData, size_t maxSamples, uint64_t& timestamp);
    
    /**
     * @brief Set callback for processed audio ready for playback
     * @param callback Function to call when audio is ready
     */
    void setRenderCallback(RenderCallback callback);

    // ===== JITTER BUFFER MANAGEMENT =====
    
    /**
     * @brief Configure jitter buffer parameters
     * @param minBufferMs Minimum buffer size in milliseconds
     * @param maxBufferMs Maximum buffer size in milliseconds
     * @param targetBufferMs Target buffer size in milliseconds
     * @return true if configuration applied successfully
     */
    bool configureJitterBuffer(int minBufferMs, int maxBufferMs, int targetBufferMs);
    
    /**
     * @brief Reset jitter buffer and clear all packets
     * @return true if reset successful
     */
    bool resetJitterBuffer();
    
    /**
     * @brief Check if jitter buffer is ready for playback
     * @return true if buffer has enough packets for stable playback
     */
    bool isJitterBufferReady() const;

    // ===== CONFIGURATION & MONITORING =====
    
    /**
     * @brief Set callback for render events and statistics
     * @param callback Function to call on events
     */
    void setRenderEventCallback(RenderEventCallback callback);
    
    /**
     * @brief Get current render statistics
     * @return Statistics about reception and jitter buffer performance
     */
    RenderSourceStats getStats() const;
    
    /**
     * @brief Force adaptation of jitter buffer based on current network conditions
     * @return true if adaptation applied
     */
    bool adaptJitterBuffer();

private:
    // Network reception thread
    void receptionWorker();
    
    // Jitter buffer processing thread
    void jitterBufferWorker();
    
    // Packet processing and validation
    bool validatePacket(const AudioNetworkPacket& networkPacket, ReceivedAudioPacket& audioPacket);
    void processReceivedPacket(const ReceivedAudioPacket& packet);
    uint32_t calculateChecksum(const uint8_t* data, size_t size);
    
    // Jitter buffer management
    void addToJitterBuffer(const ReceivedAudioPacket& packet);
    bool getFromJitterBuffer(ReceivedAudioPacket& packet);
    void handlePacketLoss(uint32_t missedSequence);
    void adaptiveBufferManagement();
    ReceivedAudioPacket generateSilencePacket(uint32_t sequenceNumber, uint64_t timestamp);
    
    // Network statistics and monitoring
    void updateNetworkStats(const ReceivedAudioPacket& packet);
    void calculateJitter(uint64_t packetTimestamp, uint64_t arrivalTime);
    
    // Member variables
    socket_t clientSocket_;
    std::string serverHost_;
    int serverPort_;
    RenderSourceConfig* config_;
    
    // Audio configuration
    int sampleRate_;
    int channels_;
    int framesPerBuffer_;
    
    // Direct socket operations
    bool initializeSocket();
    void closeSocket();
    bool sendData(const void* data, size_t size);
    bool receiveData(void* buffer, size_t size);
    bool receiveMessage(Message& message);
    void handleConnectionError();
    
    // Threading
    std::thread receptionThread_;
    std::thread jitterBufferThread_;
    std::atomic<bool> isRunning_;
    std::atomic<bool> isInitialized_;
    std::atomic<bool> isReceiving_;
    
    // Jitter buffer
    std::map<uint32_t, ReceivedAudioPacket> jitterBuffer_;
    mutable std::mutex jitterBufferMutex_;
    std::condition_variable jitterBufferCondition_;
    std::atomic<bool> jitterBufferReady_;
    
    // Packet sequencing
    std::atomic<uint32_t> expectedSequenceNumber_;
    std::atomic<uint32_t> lastProcessedSequence_;
    std::atomic<uint32_t> highestReceivedSequence_;
    
    // Timing and adaptation
    std::atomic<uint64_t> lastPacketArrivalTime_;
    std::atomic<double> averageJitter_;
    std::atomic<double> packetIntervalMs_;
    std::atomic<int> currentBufferSizeMs_;
    std::atomic<int> targetBufferSizeMs_;
    std::atomic<int> adaptiveMinBufferMs_;
    std::atomic<int> adaptiveMaxBufferMs_;
    
    // Statistics
    mutable std::atomic<uint64_t> totalPacketsReceived_;
    mutable std::atomic<uint64_t> totalPacketsLost_;
    mutable std::atomic<uint64_t> totalPacketsDropped_;
    mutable std::atomic<uint64_t> totalPacketsPlayed_;
    mutable std::atomic<uint64_t> totalSilenceInserted_;
    mutable std::atomic<double> averageLatency_;
    mutable std::atomic<double> networkJitter_;
    mutable std::atomic<uint64_t> lastStatsUpdateTime_;
    
    // Callbacks
    RenderCallback renderCallback_;
    RenderEventCallback renderEventCallback_;
};

/**
 * @brief Configuration structure for RenderSource
 */
struct RenderSourceConfig {
    std::string serverHost = "localhost";
    int serverPort = 12345;
    int sampleRate = 44100;             // Expected sample rate
    int channels = 1;                   // Expected number of channels
    int framesPerBuffer = 256;          // Frames per buffer for processing
    int minBufferMs = 20;               // Minimum jitter buffer size in ms
    int maxBufferMs = 200;              // Maximum jitter buffer size in ms
    int targetBufferMs = 50;            // Target jitter buffer size in ms
    int packetTimeoutMs = 100;          // Timeout for missing packets
    int adaptationIntervalMs = 1000;    // How often to adapt buffer size
    bool enableAdaptiveBuffer = true;   // Enable adaptive buffer sizing
    bool enablePacketLossRecovery = true; // Enable silence insertion for lost packets
    double jitterThresholdMs = 10.0;    // Jitter threshold for adaptation
    int maxConsecutiveLoss = 5;         // Max consecutive packets before reset
    int statsUpdateIntervalMs = 1000;   // Statistics update interval
};

/**
 * @brief Statistics structure for RenderSource
 */
struct RenderSourceStats {
    uint64_t totalPacketsReceived = 0;
    uint64_t totalPacketsLost = 0;
    uint64_t totalPacketsDropped = 0;
    uint64_t totalPacketsPlayed = 0;
    uint64_t totalSilenceInserted = 0;
    uint64_t currentBufferSize = 0;         // Current packets in buffer
    uint64_t currentBufferSizeMs = 0;       // Current buffer size in milliseconds
    double averageLatency = 0.0;            // Average latency in ms
    double networkJitter = 0.0;             // Network jitter in ms
    double packetLossRate = 0.0;            // Packet loss rate percentage
    double bufferUtilization = 0.0;         // Buffer utilization percentage
    bool isReceiving = false;
    bool isBufferReady = false;
    bool isAdaptiveMode = false;
    uint32_t expectedSequence = 0;
    uint32_t lastReceivedSequence = 0;
    uint64_t lastPacketTime = 0;
};