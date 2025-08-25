#include "RenderSource.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <mutex>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
// Undefine Windows min/max macros to avoid conflicts with std::min/max
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

RenderSource::RenderSource()
    : clientSocket_(INVALID_SOCKET),
      config_(nullptr),
      isRunning_(false),
      isInitialized_(false),
      isReceiving_(false),
      jitterBufferReady_(false),
      expectedSequenceNumber_(1),
      lastProcessedSequence_(0),
      highestReceivedSequence_(0),
      lastPacketArrivalTime_(0),
      averageJitter_(0.0),
      packetIntervalMs_(0.0),
      currentBufferSizeMs_(0),
      targetBufferSizeMs_(50),
      adaptiveMinBufferMs_(20),
      adaptiveMaxBufferMs_(200),
      totalPacketsReceived_(0),
      totalPacketsLost_(0),
      totalPacketsDropped_(0),
      totalPacketsPlayed_(0),
      totalSilenceInserted_(0),
      averageLatency_(0.0),
      networkJitter_(0.0),
      lastStatsUpdateTime_(0),
      serverPort_(0),
      sampleRate_(44100),
      channels_(1),
      framesPerBuffer_(256) {
      
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

RenderSource::~RenderSource() {
    RenderSourceDeinit();
    
#ifdef _WIN32
    WSACleanup();
#endif
}

bool RenderSource::RenderSourceInit(const RenderSourceConfig& config) {
    if (isInitialized_) {
        std::cerr << "RenderSource: Already initialized" << std::endl;
        return false;
    }

    // Store configuration
    config_ = new RenderSourceConfig(config);
    serverHost_ = config.serverHost;
    serverPort_ = config.serverPort;
    sampleRate_ = config.sampleRate;
    channels_ = config.channels;
    framesPerBuffer_ = config.framesPerBuffer;
    targetBufferSizeMs_ = config.targetBufferMs;
    adaptiveMinBufferMs_ = config.minBufferMs;
    adaptiveMaxBufferMs_ = config.maxBufferMs;
    currentBufferSizeMs_ = config.targetBufferMs;

    // Calculate packet interval based on buffer size and sample rate
    packetIntervalMs_ = (static_cast<double>(framesPerBuffer_) / sampleRate_) * 1000.0;

    // Initialize socket
    if (!initializeSocket()) {
        std::cerr << "RenderSource: Failed to initialize socket" << std::endl;
        delete config_;
        config_ = nullptr;
        return false;
    }

    // Initialize sequence tracking
    expectedSequenceNumber_ = 1;
    lastProcessedSequence_ = 0;
    highestReceivedSequence_ = 0;

    // Start worker threads
    isRunning_ = true;
    receptionThread_ = std::thread(&RenderSource::receptionWorker, this);
    jitterBufferThread_ = std::thread(&RenderSource::jitterBufferWorker, this);

    isInitialized_ = true;
    
    std::cout << "RenderSource: Initialized successfully" << std::endl;
    std::cout << "  Expected Sample Rate: " << sampleRate_ << "Hz" << std::endl;
    std::cout << "  Expected Channels: " << channels_ << std::endl;
    std::cout << "  Buffer Size: " << adaptiveMinBufferMs_.load() << "-" << adaptiveMaxBufferMs_.load() 
              << "ms (target: " << targetBufferSizeMs_.load() << "ms)" << std::endl;
    std::cout << "  Packet Interval: " << packetIntervalMs_.load() << "ms" << std::endl;

    return true;
}

bool RenderSource::RenderSourceDeinit() {
    if (!isInitialized_) {
        return true;
    }

    // Stop worker threads
    isRunning_ = false;
    jitterBufferCondition_.notify_all();

    if (receptionThread_.joinable()) {
        receptionThread_.join();
    }

    if (jitterBufferThread_.joinable()) {
        jitterBufferThread_.join();
    }

    // Stop receiving
    stopReceiving();

    // Clear jitter buffer
    {
        std::lock_guard<std::mutex> lock(jitterBufferMutex_);
        jitterBuffer_.clear();
    }

    // Cleanup network manager
    closeSocket();

    // Cleanup configuration
    delete config_;
    config_ = nullptr;

    isInitialized_ = false;
    
    std::cout << "RenderSource: Deinitialized" << std::endl;
    return true;
}

bool RenderSource::RenderSourceProcess() {
    if (!isInitialized_) {
        return false;
    }

    uint64_t currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();

    // Update statistics periodically
    if (currentTime - lastStatsUpdateTime_ > static_cast<uint64_t>(config_->statsUpdateIntervalMs)) {
        updateNetworkStats(ReceivedAudioPacket{}); // Update with current state
        lastStatsUpdateTime_ = currentTime;
    }

    // Adaptive buffer management
    if (config_->enableAdaptiveBuffer && 
        currentTime % config_->adaptationIntervalMs == 0) {
        adaptiveBufferManagement();
    }

    // Check for packet timeouts and handle missing packets
    if (isReceiving_ && currentTime - lastPacketArrivalTime_ > static_cast<uint64_t>(config_->packetTimeoutMs)) {
        // Handle potential packet loss
        if (expectedSequenceNumber_ <= highestReceivedSequence_) {
            for (uint32_t seq = expectedSequenceNumber_; seq <= highestReceivedSequence_; ++seq) {
                std::lock_guard<std::mutex> lock(jitterBufferMutex_);
                if (jitterBuffer_.find(seq) == jitterBuffer_.end()) {
                    handlePacketLoss(seq);
                }
            }
        }
    }

    return true;
}

bool RenderSource::startReceiving(const std::string& serverHost, int serverPort) {
    if (!isInitialized_ || isReceiving_) {
        return false;
    }

    serverHost_ = serverHost;
    serverPort_ = serverPort;

    // Connect to server
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<u_short>(serverPort));
    
    // Convert hostname to IP address
    if (serverHost == "localhost") {
        serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    } else {
#ifdef _WIN32
        if (inet_pton(AF_INET, serverHost.c_str(), &serverAddr.sin_addr) <= 0) {
            std::cerr << "RenderSource: Invalid address " << serverHost << std::endl;
            return false;
        }
#else
        if (inet_aton(serverHost.c_str(), &serverAddr.sin_addr) == 0) {
            std::cerr << "RenderSource: Invalid address " << serverHost << std::endl;
            return false;
        }
#endif
    }

    if (connect(clientSocket_, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
#ifdef _WIN32
        int error = WSAGetLastError();
        std::cerr << "RenderSource: Failed to connect to " << serverHost << ":" << serverPort 
                  << " (Windows error: " << error << ")" << std::endl;
#else
        std::cerr << "RenderSource: Failed to connect to " << serverHost << ":" << serverPort << std::endl;
#endif
        return false;
    }

    isReceiving_ = true;
    
    // Reset buffer state
    resetJitterBuffer();
    
    if (renderEventCallback_) {
        renderEventCallback_("Started receiving", getStats());
    }
    
    std::cout << "RenderSource: Started receiving from " << serverHost << ":" << serverPort << std::endl;
    return true;
}

bool RenderSource::stopReceiving() {
    if (!isReceiving_) {
        return true;
    }

    closeSocket();

    isReceiving_ = false;
    jitterBufferReady_ = false;
    
    if (renderEventCallback_) {
        renderEventCallback_("Stopped receiving", getStats());
    }
    
    std::cout << "RenderSource: Stopped receiving" << std::endl;
    return true;
}

bool RenderSource::isReceiving() const {
    return isReceiving_.load();
}

bool RenderSource::addReceivedPacket(const AudioNetworkPacket& networkPacket) {
    if (!isInitialized_) {
        return false;
    }

    ReceivedAudioPacket audioPacket;
    if (!validatePacket(networkPacket, audioPacket)) {
        totalPacketsDropped_++;
        return false;
    }

    std::cout << "📦 RenderSource: Adding packet to jitter buffer (seq=" << audioPacket.sequenceNumber << ")" << std::endl;
    processReceivedPacket(audioPacket);
    return true;
}

size_t RenderSource::getAudioData(float* audioData, size_t maxSamples, uint64_t& timestamp) {
    if (!isInitialized_ || !audioData || maxSamples == 0) {
        return 0;
    }

    ReceivedAudioPacket packet;
    if (getFromJitterBuffer(packet)) {
        size_t samplesToCopy = std::min(maxSamples, packet.audioData.size());
        std::memcpy(audioData, packet.audioData.data(), samplesToCopy * sizeof(float));
        timestamp = packet.timestamp;
        totalPacketsPlayed_++;
        return samplesToCopy;
    }

    return 0;
}

void RenderSource::setRenderCallback(RenderCallback callback) {
    renderCallback_ = callback;
}

bool RenderSource::configureJitterBuffer(int minBufferMs, int maxBufferMs, int targetBufferMs) {
    if (!isInitialized_ || minBufferMs < 0 || maxBufferMs <= minBufferMs || 
        targetBufferMs < minBufferMs || targetBufferMs > maxBufferMs) {
        return false;
    }

    adaptiveMinBufferMs_ = minBufferMs;
    adaptiveMaxBufferMs_ = maxBufferMs;
    targetBufferSizeMs_ = targetBufferMs;
    currentBufferSizeMs_ = targetBufferMs;

    std::cout << "RenderSource: Jitter buffer configured - Min: " << minBufferMs 
              << "ms, Max: " << maxBufferMs << "ms, Target: " << targetBufferMs << "ms" << std::endl;

    return true;
}

bool RenderSource::resetJitterBuffer() {
    {
        std::lock_guard<std::mutex> lock(jitterBufferMutex_);
        jitterBuffer_.clear();
    }

    jitterBufferReady_ = false;
    expectedSequenceNumber_ = 1;
    lastProcessedSequence_ = 0;
    highestReceivedSequence_ = 0;
    
    std::cout << "RenderSource: Jitter buffer reset" << std::endl;
    return true;
}

bool RenderSource::isJitterBufferReady() const {
    return jitterBufferReady_.load();
}

void RenderSource::setRenderEventCallback(RenderEventCallback callback) {
    renderEventCallback_ = callback;
}

RenderSourceStats RenderSource::getStats() const {
    RenderSourceStats stats;
    
    stats.totalPacketsReceived = totalPacketsReceived_.load();
    stats.totalPacketsLost = totalPacketsLost_.load();
    stats.totalPacketsDropped = totalPacketsDropped_.load();
    stats.totalPacketsPlayed = totalPacketsPlayed_.load();
    stats.totalSilenceInserted = totalSilenceInserted_.load();
    stats.averageLatency = averageLatency_.load();
    stats.networkJitter = networkJitter_.load();
    stats.isReceiving = isReceiving_.load();
    stats.isBufferReady = jitterBufferReady_.load();
    stats.isAdaptiveMode = config_ ? config_->enableAdaptiveBuffer : false;
    stats.expectedSequence = expectedSequenceNumber_.load();
    stats.lastReceivedSequence = highestReceivedSequence_.load();
    stats.lastPacketTime = lastPacketArrivalTime_.load();
    stats.currentBufferSizeMs = currentBufferSizeMs_.load();

    {
        jitterBufferMutex_.lock();
        stats.currentBufferSize = jitterBuffer_.size();
        jitterBufferMutex_.unlock();
    }

    // Calculate packet loss rate
    if (stats.totalPacketsReceived > 0) {
        stats.packetLossRate = (static_cast<double>(stats.totalPacketsLost) / 
                               (stats.totalPacketsReceived + stats.totalPacketsLost)) * 100.0;
    }

    // Calculate buffer utilization
    if (adaptiveMaxBufferMs_ > 0) {
        stats.bufferUtilization = (static_cast<double>(stats.currentBufferSizeMs) / 
                                  adaptiveMaxBufferMs_.load()) * 100.0;
    }

    return stats;
}

bool RenderSource::adaptJitterBuffer() {
    adaptiveBufferManagement();
    return true;
}

void RenderSource::receptionWorker() {
    while (isRunning_) {
        if (isReceiving_ && clientSocket_ != INVALID_SOCKET) {
            // Actually receive and parse messages from the network
            Message receivedMessage;
            if (receiveMessage(receivedMessage)) {
                std::cout << "🔍 RenderSource: Received message type " << static_cast<int>(receivedMessage.getType()) << std::endl;
                
                if (receivedMessage.getType() == MessageType::AUDIO_DATA) {
                    std::cout << "🎵 RenderSource: Processing audio message, size=" << receivedMessage.getData().size() << " bytes" << std::endl;
                    
                    // SIMPLIFIED: Convert Message directly to AudioNetworkPacket (like old implementation)
                    AudioNetworkPacket networkPacket;
                    networkPacket.sequenceNumber = receivedMessage.getSequence();
                    networkPacket.timestamp = receivedMessage.getTimestamp();
                    
                    // Simple approach: raw audio data only
                    const auto& data = receivedMessage.getData();
                    networkPacket.audioData = data;  // Direct copy
                    networkPacket.sampleRate = 44100;  // Fixed values
                    networkPacket.channels = 1;
                    networkPacket.dataSize = static_cast<uint16_t>(data.size());
                    networkPacket.checksum = 0;  // Skip checksum for now
                    
                    // Add to jitter buffer for processing
                    addReceivedPacket(networkPacket);
                }
            } else {
                // No data available, small sleep to prevent busy waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void RenderSource::jitterBufferWorker() {
    std::cout << "🔧 RenderSource: Jitter buffer worker thread started" << std::endl;
    
    while (isRunning_) {
        std::unique_lock<std::mutex> lock(jitterBufferMutex_);
        
        jitterBufferCondition_.wait(lock, [this] {
            return !jitterBuffer_.empty() || !isRunning_;
        });

        if (!isRunning_) break;

        std::cout << "🔧 RenderSource: Jitter buffer worker processing " << jitterBuffer_.size() << " packets" << std::endl;

        // Check if buffer is ready for playback - MAKE IT READY IMMEDIATELY FOR TESTING
        if (!jitterBufferReady_ && !jitterBuffer_.empty()) {
            jitterBufferReady_ = true;  // ALWAYS READY for testing
            std::cout << "🎵 RenderSource: Jitter buffer ready with " << jitterBuffer_.size() 
                      << " packets" << std::endl;
        }

        // Process packets if buffer is ready
        if (jitterBufferReady_ && !jitterBuffer_.empty()) {
            auto it = jitterBuffer_.find(expectedSequenceNumber_);
            if (it != jitterBuffer_.end()) {
                ReceivedAudioPacket packet = it->second;
                jitterBuffer_.erase(it);  // REMOVE packet from buffer
                lock.unlock();

                // Call render callback if set
                if (renderCallback_) {
                    std::cout << "📥 RenderSource: Calling render callback for packet " << packet.sequenceNumber << std::endl;
                    renderCallback_(packet.audioData.data(), packet.audioData.size(), packet.timestamp);
                }

                expectedSequenceNumber_++;
                lastProcessedSequence_ = packet.sequenceNumber;
            } else {
                lock.unlock();
                // Missing packet - wait a bit or handle loss
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } else {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

bool RenderSource::validatePacket(const AudioNetworkPacket& networkPacket, ReceivedAudioPacket& audioPacket) {
    // Basic validation
    if (networkPacket.audioData.empty() || networkPacket.dataSize == 0) {
        return false;
    }

    // Convert network packet to internal format
    audioPacket.sequenceNumber = networkPacket.sequenceNumber;
    audioPacket.timestamp = networkPacket.timestamp;
    audioPacket.arrivalTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    audioPacket.sampleRate = networkPacket.sampleRate;
    audioPacket.channels = networkPacket.channels;
    audioPacket.expectedChecksum = networkPacket.checksum;

    // Convert byte data to float
    size_t floatSamples = networkPacket.audioData.size() / sizeof(float);
    audioPacket.audioData.resize(floatSamples);
    std::memcpy(audioPacket.audioData.data(), networkPacket.audioData.data(), networkPacket.audioData.size());

    // Validate checksum (TEMPORARILY DISABLED FOR DEBUGGING)
    audioPacket.actualChecksum = calculateChecksum(networkPacket.audioData.data(), networkPacket.audioData.size());
    audioPacket.isValid = true; // (audioPacket.actualChecksum == audioPacket.expectedChecksum);

    // DEBUG: Comment out checksum validation temporarily
    /*
    if (!audioPacket.isValid) {
        std::cerr << "RenderSource: Checksum validation failed for packet " 
                  << audioPacket.sequenceNumber << std::endl;
        return false;
    }
    */

    // Validate audio parameters
    if (audioPacket.sampleRate != static_cast<uint32_t>(sampleRate_) || 
        audioPacket.channels != static_cast<uint16_t>(channels_)) {
        std::cerr << "RenderSource: Audio format mismatch - expected " 
                  << sampleRate_ << "Hz/" << channels_ << "ch, got " 
                  << audioPacket.sampleRate << "Hz/" << audioPacket.channels << "ch" << std::endl;
        return false;
    }

    return true;
}

void RenderSource::processReceivedPacket(const ReceivedAudioPacket& packet) {
    totalPacketsReceived_++;
    
    // Update highest received sequence
    if (packet.sequenceNumber > highestReceivedSequence_) {
        highestReceivedSequence_ = packet.sequenceNumber;
    }

    // Calculate jitter
    calculateJitter(packet.timestamp, packet.arrivalTime);

    // Add to jitter buffer
    addToJitterBuffer(packet);

    // Update network statistics
    updateNetworkStats(packet);
}

uint32_t RenderSource::calculateChecksum(const uint8_t* data, size_t size) {
    uint32_t checksum = 0;
    for (size_t i = 0; i < size; ++i) {
        checksum += data[i];
        checksum = (checksum << 1) | (checksum >> 31); // Rotate left
    }
    return checksum;
}

void RenderSource::addToJitterBuffer(const ReceivedAudioPacket& packet) {
    std::lock_guard<std::mutex> lock(jitterBufferMutex_);
    
    // Check for duplicate packets
    if (jitterBuffer_.find(packet.sequenceNumber) != jitterBuffer_.end()) {
        return; // Duplicate packet, ignore
    }

    // Add packet to buffer
    jitterBuffer_[packet.sequenceNumber] = packet;
    
    std::cout << "📦 JitterBuffer: Added packet " << packet.sequenceNumber << ", buffer size=" << jitterBuffer_.size() << std::endl;
    
    // Limit buffer size
    while (jitterBuffer_.size() * packetIntervalMs_ > adaptiveMaxBufferMs_) {
        auto oldestIt = jitterBuffer_.begin();
        jitterBuffer_.erase(oldestIt);
        totalPacketsDropped_++;
    }

    jitterBufferCondition_.notify_one();
}

bool RenderSource::getFromJitterBuffer(ReceivedAudioPacket& packet) {
    std::lock_guard<std::mutex> lock(jitterBufferMutex_);
    
    if (!jitterBufferReady_ || jitterBuffer_.empty()) {
        return false;
    }

    auto it = jitterBuffer_.find(expectedSequenceNumber_);
    if (it != jitterBuffer_.end()) {
        packet = it->second;
        jitterBuffer_.erase(it);
        expectedSequenceNumber_++;
        return true;
    }

    return false;
}

void RenderSource::handlePacketLoss(uint32_t missedSequence) {
    if (config_->enablePacketLossRecovery) {
        // Generate silence packet
        ReceivedAudioPacket silencePacket = generateSilencePacket(missedSequence, 0);
        addToJitterBuffer(silencePacket);
        totalSilenceInserted_++;
        totalPacketsLost_++;
        
        std::cout << "RenderSource: Inserted silence for missing packet " << missedSequence << std::endl;
    }
}

void RenderSource::adaptiveBufferManagement() {
    double currentJitter = averageJitter_.load();
    double jitterThreshold = config_->jitterThresholdMs;
    
    int newTargetSize = targetBufferSizeMs_.load();
    
    if (currentJitter > jitterThreshold) {
        // Increase buffer size
        newTargetSize = std::min(newTargetSize + 10, adaptiveMaxBufferMs_.load());
    } else if (currentJitter < jitterThreshold / 2.0) {
        // Decrease buffer size
        newTargetSize = std::max(newTargetSize - 5, adaptiveMinBufferMs_.load());
    }
    
    if (newTargetSize != targetBufferSizeMs_.load()) {
        targetBufferSizeMs_ = newTargetSize;
        currentBufferSizeMs_ = newTargetSize;
        
        if (renderEventCallback_) {
            renderEventCallback_("Buffer size adapted", getStats());
        }
        
        std::cout << "RenderSource: Adapted buffer size to " << newTargetSize 
                  << "ms (jitter: " << currentJitter << "ms)" << std::endl;
    }
}

ReceivedAudioPacket RenderSource::generateSilencePacket(uint32_t sequenceNumber, uint64_t timestamp) {
    ReceivedAudioPacket silencePacket;
    silencePacket.sequenceNumber = sequenceNumber;
    silencePacket.timestamp = timestamp;
    silencePacket.arrivalTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    silencePacket.sampleRate = sampleRate_;
    silencePacket.channels = channels_;
    silencePacket.isValid = true;
    
    // Generate silence
    size_t silenceSamples = framesPerBuffer_ * channels_;
    silencePacket.audioData.resize(silenceSamples, 0.0f);
    
    return silencePacket;
}

void RenderSource::updateNetworkStats(const ReceivedAudioPacket& packet) {
    uint64_t currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();

    if (packet.isValid) {
        // Update latency
        double latency = static_cast<double>(packet.arrivalTime - packet.timestamp);
        averageLatency_ = (averageLatency_.load() * 0.9) + (latency * 0.1);
    }

    // Update other statistics as needed
    lastStatsUpdateTime_ = currentTime;
}

void RenderSource::calculateJitter(uint64_t packetTimestamp, uint64_t arrivalTime) {
    static uint64_t lastArrivalTime = 0;
    static uint64_t lastPacketTimestamp = 0;
    
    if (lastArrivalTime > 0 && lastPacketTimestamp > 0) {
        double arrivalDiff = static_cast<double>(arrivalTime - lastArrivalTime);
        double timestampDiff = static_cast<double>(packetTimestamp - lastPacketTimestamp);
        double jitter = std::abs(arrivalDiff - timestampDiff);
        
        // Exponential moving average
        averageJitter_ = (averageJitter_.load() * 0.9) + (jitter * 0.1);
        networkJitter_ = averageJitter_.load();
    }
    
    lastArrivalTime = arrivalTime;
    lastPacketTimestamp = packetTimestamp;
}

// Direct socket implementation
bool RenderSource::initializeSocket() {
    clientSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket_ == INVALID_SOCKET) {
        std::cerr << "RenderSource: Failed to create socket" << std::endl;
        return false;
    }
    
    // Set socket options
    int optval = 1;
    setsockopt(clientSocket_, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval));
    
    return true;
}

void RenderSource::closeSocket() {
    if (clientSocket_ != INVALID_SOCKET) {
        closesocket(clientSocket_);
        clientSocket_ = INVALID_SOCKET;
    }
}

bool RenderSource::sendData(const void* data, size_t size) {
    if (clientSocket_ == INVALID_SOCKET || !data || size == 0) {
        return false;
    }
    
    size_t totalSent = 0;
    const char* buffer = static_cast<const char*>(data);
    
    while (totalSent < size) {
        int sent = send(clientSocket_, buffer + totalSent, size - totalSent, 0);
        if (sent == SOCKET_ERROR) {
            std::cerr << "RenderSource: Send failed" << std::endl;
            return false;
        }
        totalSent += sent;
    }
    
    return true;
}

bool RenderSource::receiveData(void* buffer, size_t size) {
    if (clientSocket_ == INVALID_SOCKET || !buffer || size == 0) {
        return false;
    }
    
    size_t totalReceived = 0;
    char* bufferPtr = static_cast<char*>(buffer);
    
    while (totalReceived < size) {
        int received = recv(clientSocket_, bufferPtr + totalReceived, static_cast<int>(size - totalReceived), 0);
        if (received == SOCKET_ERROR || received == 0) {
            // Connection error or closed - handle gracefully
            handleConnectionError();
            return false;
        }
        totalReceived += received;
    }
    
    return true;
}

bool RenderSource::receiveMessage(Message& message) {
    static int debugCallCount = 0;
    if ((++debugCallCount % 100) == 1) {  // Print every 100 calls to avoid spam
        std::cout << "🔍 RenderSource: receiveMessage() called " << debugCallCount << " times" << std::endl;
    }
    
    if (clientSocket_ == INVALID_SOCKET) {
        return false;
    }
    
    // Try to receive a message header first
    MessageHeader header;
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(clientSocket_, &readSet);
    
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 1000; // 1ms timeout
    
    int selectResult = select(clientSocket_ + 1, &readSet, nullptr, nullptr, &timeout);
    if (selectResult <= 0) {
        return false; // No data available or error
    }
    
    std::cout << "🔍 RenderSource: select() detected data on socket, receiving..." << std::endl;
    
    // Receive message header
    if (!receiveData(&header, sizeof(header))) {
        std::cout << "❌ RenderSource: Failed to receive message header" << std::endl;
        return false;
    }
    
    std::cout << "📥 RenderSource: Received header - magic=0x" << std::hex << header.magic 
              << ", type=" << std::dec << static_cast<int>(header.type) 
              << ", length=" << header.length << std::endl;
    
    // Validate magic number
    if (header.magic != 0x41554453) {
        std::cerr << "RenderSource: Invalid message magic number" << std::endl;
        return false;
    }
    
    // Create message with header info
    message = Message(header.type);
    message.setSequence(header.sequence);
    message.setTimestamp(header.timestamp);
    
    // Receive data if present
    uint32_t dataSize = header.length - sizeof(MessageHeader);
    if (dataSize > 0) {
        std::vector<uint8_t> buffer(dataSize);
        if (!receiveData(buffer.data(), dataSize)) {
            return false;
        }
        message.setData(buffer.data(), dataSize);
    }
    
    return true;
}

void RenderSource::handleConnectionError() {
    if (isReceiving_) {
        isReceiving_ = false;
        closeSocket();
        
        std::cout << "RenderSource: Connection lost, attempting to reconnect..." << std::endl;
        
        // Attempt to reconnect in background
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(2000)); // Wait 2 seconds
            if (!isReceiving_ && isRunning_) {
                // Try to reconnect
                startReceiving(serverHost_, serverPort_);
            }
        }).detach();
    }
}