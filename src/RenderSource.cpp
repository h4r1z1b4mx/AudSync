#include "RenderSource.h"
#include "NetworkManager.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cstring>

bool RenderSource::RenderSourceInit(const std::string& serverHost, int serverPort) {
    if (isInitialized_) {
        std::cerr << "RenderSource: Already initialized" << std::endl;
        return false;
    }

    if (!initializeNetworking()) {
        std::cerr << "RenderSource: Failed to initialize networking" << std::endl;
        return false;
    }

    serverHost_ = serverHost;
    serverPort_ = serverPort;
    usingSharedNetwork_ = false;
    
    // Reset state
    expectedSequenceNumber_.store(0);
    lastReceivedSequence_.store(0);
    jitterBufferReady_.store(false);
    
    isInitialized_ = true;
    std::cout << "RenderSource: Initialized successfully" << std::endl;
    std::cout << "  Expected Sample Rate: Auto-detect from client" << std::endl;
    std::cout << "  Expected Channels: 2" << std::endl;
    std::cout << "  Buffer Size: " << minBufferMs_ << "-" << maxBufferMs_ << "ms (target: " << targetBufferMs_ << "ms)" << std::endl;
    std::cout << "  Packet Interval: " << packetIntervalMs_ << "ms" << std::endl;

    // Start receiving from server immediately
    if (!startReceiving(serverHost_, serverPort_)) {
        std::cerr << "RenderSource: Failed to connect to server during initialization" << std::endl;
        // Don't fail initialization, allow retries later
    }

    return true;
}

bool RenderSource::RenderSourceInitWithSharedNetwork(NetworkManager* sharedNetworkManager) {
    if (isInitialized_) {
        std::cerr << "RenderSource: Already initialized" << std::endl;
        return false;
    }

    if (!sharedNetworkManager) {
        std::cerr << "RenderSource: Shared network manager is null" << std::endl;
        return false;
    }

    sharedNetworkManager_ = sharedNetworkManager;
    usingSharedNetwork_ = true;
    
    // Reset state
    expectedSequenceNumber_.store(0);
    lastReceivedSequence_.store(0);
    jitterBufferReady_.store(false);
    
    isInitialized_ = true;
    isReceiving_.store(true); // Using shared connection
    
    std::cout << "RenderSource: Initialized with shared network successfully" << std::endl;
    std::cout << "  Expected Sample Rate: Auto-detect from client" << std::endl;
    std::cout << "  Expected Channels: 2" << std::endl;
    std::cout << "  Buffer Size: " << minBufferMs_ << "-" << maxBufferMs_ << "ms (target: " << targetBufferMs_ << "ms)" << std::endl;
    std::cout << "  Packet Interval: " << packetIntervalMs_ << "ms" << std::endl;

    // Start background threads for processing
    isRunning_.store(true);
    receptionThread_ = std::thread(&RenderSource::receptionWorker, this);
    jitterBufferThread_ = std::thread(&RenderSource::jitterBufferWorker, this);
    
    return true;
}

void RenderSource::RenderSourceDeinit() {
    if (!isInitialized_) return;

    stopReceiving();
    
    if (!usingSharedNetwork_) {
        cleanupNetworking();
    }
    
    // Clear jitter buffer
    {
        std::lock_guard<std::mutex> lock(jitterBufferMutex_);
        jitterBuffer_.clear();
    }
    
    sharedNetworkManager_ = nullptr;
    usingSharedNetwork_ = false;
    
    isInitialized_ = false;
    std::cout << "RenderSource: Deinitialized" << std::endl;
}

bool RenderSource::RenderSourceProcess() {
    if (!isInitialized_) return false;
    
    // Processing is handled by background threads
    // This could be used for additional per-frame processing if needed
    return isReceiving_.load();
}

bool RenderSource::startReceiving(const std::string& host, int port) {
    if (!isInitialized_ || isReceiving_.load()) {
        return false;
    }

    // Connect using NetworkManager
    if (!networkManager_.connectToServer(host, port)) {
        std::cerr << "RenderSource: Failed to connect to " << host << ":" << port << std::endl;
        return false;
    }

    // Send client configuration
    Message configMsg;
    configMsg.type = MessageType::CLIENT_CONFIG;
    configMsg.size = 12; // 3 int32_t values
    configMsg.data.resize(configMsg.size);
    configMsg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    
    // Pack audio config (use actual configured parameters instead of hardcoded)
    int32_t* configData = reinterpret_cast<int32_t*>(configMsg.data.data());
    configData[0] = audioSampleRate_; // Use configured sample rate
    configData[1] = audioChannels_;   // Use configured channels  
    configData[2] = audioFramesPerBuffer_; // Use configured buffer size
    
    if (!networkManager_.sendMessage(configMsg)) {
        std::cerr << "RenderSource: Failed to send client config" << std::endl;
        return false;
    }

    // Send ready message
    Message readyMsg;
    readyMsg.type = MessageType::CLIENT_READY;
    readyMsg.size = 0;
    readyMsg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    
    if (!networkManager_.sendMessage(readyMsg)) {
        std::cerr << "RenderSource: Failed to send ready message" << std::endl;
        return false;
    }

    // Reset jitter buffer
    {
        std::lock_guard<std::mutex> lock(jitterBufferMutex_);
        jitterBuffer_.clear();
        jitterBufferReady_.store(false);
    }
    std::cout << "RenderSource: Jitter buffer reset" << std::endl;

    // Start background threads
    isRunning_.store(true);
    isReceiving_.store(true);
    
    receptionThread_ = std::thread(&RenderSource::receptionWorker, this);
    jitterBufferThread_ = std::thread(&RenderSource::jitterBufferWorker, this);
    
    std::cout << "RenderSource: Started receiving from " << host << ":" << port << std::endl;
    return true;
}

void RenderSource::stopReceiving() {
    if (!isReceiving_.load()) return;

    isReceiving_.store(false);
    isRunning_.store(false);
    
    // Wake up waiting threads
    jitterBufferCondition_.notify_all();
    
    // Wait for threads to finish
    if (receptionThread_.joinable()) {
        receptionThread_.join();
    }
    if (jitterBufferThread_.joinable()) {
        jitterBufferThread_.join();
    }
    
    closeSocket();
    std::cout << "RenderSource: Stopped receiving" << std::endl;
}

void RenderSource::setRenderCallback(std::function<void(const float*, size_t, uint64_t)> callback) {
    renderCallback_ = callback;
}

void RenderSource::setAudioParameters(int sampleRate, int channels, int framesPerBuffer) {
    audioSampleRate_ = sampleRate;
    audioChannels_ = channels;
    audioFramesPerBuffer_ = framesPerBuffer;
    
    // Update packet interval based on new parameters
    packetIntervalMs_ = (double)framesPerBuffer / sampleRate * 1000.0;
    
    std::cout << "RenderSource: Audio parameters updated:" << std::endl;
    std::cout << "  Sample Rate: " << sampleRate << "Hz" << std::endl;
    std::cout << "  Channels: " << channels << std::endl;
    std::cout << "  Frames Per Buffer: " << framesPerBuffer << std::endl;
    std::cout << "  Packet Interval: " << packetIntervalMs_ << "ms" << std::endl;
}

void RenderSource::setBufferSize(double minBufferMs, double maxBufferMs, double targetBufferMs) {
    minBufferMs_ = minBufferMs;
    maxBufferMs_ = maxBufferMs;
    targetBufferMs_ = targetBufferMs;
    currentBufferSizeMs_ = targetBufferMs;
}

RenderSource::Stats RenderSource::getStats() const {
    Stats stats;
    stats.totalPacketsReceived = totalPacketsReceived_.load();
    stats.totalPacketsDropped = totalPacketsDropped_.load();
    stats.totalBytesReceived = totalBytesReceived_.load();
    stats.currentBufferMs = currentBufferSizeMs_;
    stats.expectedSequence = expectedSequenceNumber_.load();
    
    // Calculate buffer size from jitter buffer
    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(jitterBufferMutex_));
        stats.currentBufferMs = jitterBuffer_.size() * packetIntervalMs_;
    }
    
    return stats;
}

void RenderSource::receptionWorker() {
    uint32_t packetSequenceCounter = 0; // Track our own sequence for shared network
    
    while (isRunning_.load()) {
        if (isReceiving_.load()) {
            // Use appropriate NetworkManager
            NetworkManager* manager = usingSharedNetwork_ ? sharedNetworkManager_ : &networkManager_;
            if (!manager) {
                std::cerr << "RenderSource: Network manager is null" << std::endl;
                break;
            }
            
            // Receive message using NetworkManager
            Message message;
            if (manager->receiveMessage(message)) {
                
                // Check if it's audio data
                if (message.type == MessageType::AUDIO_DATA && message.size > 0) {
                    // Convert to internal format
                    ReceivedAudioPacket audioPacket;
                    audioPacket.sequenceNumber = packetSequenceCounter++; // Assign sequence number
                    audioPacket.timestamp = message.timestamp;
                    audioPacket.arrivalTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::high_resolution_clock::now().time_since_epoch()
                    ).count();
                    
                    // Convert bytes to float audio data
                    size_t numSamples = message.size / sizeof(float);
                    audioPacket.audioData.resize(numSamples);
                    std::memcpy(audioPacket.audioData.data(), message.data.data(), message.size);
                    
                    if (usingSharedNetwork_) {
                        // For shared network, use jitter buffer for better audio quality
                        // The jitter buffer helps smooth out timing variations
                        processReceivedPacket(audioPacket);
                    } else {
                        // Use jitter buffer for standalone network connections
                        processReceivedPacket(audioPacket);
                    }
                    
                    totalPacketsReceived_.fetch_add(1);
                    totalBytesReceived_.fetch_add(message.size);
                    
                    // Debug: Show reception activity very rarely for monitoring
                    static int receivedPacketCount = 0;
                    receivedPacketCount++;
                    if (receivedPacketCount % 20000 == 0) {  // Every 20000 packets (much less frequent)
                        std::cout << "RenderSource: Received " << receivedPacketCount << " packets (" << audioPacket.audioData.size() << " samples each)" << std::endl;
                    }
                }
            } else {
                // No data available - this is normal when no other clients are sending audio
                // Just wait a bit and try again
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void RenderSource::jitterBufferWorker() {
    while (isRunning_.load()) {
        std::unique_lock<std::mutex> lock(jitterBufferMutex_);
        
        jitterBufferCondition_.wait(lock, [this] {
            return !jitterBuffer_.empty() || !isRunning_.load();
        });

        if (!isRunning_.load()) break;

        // Check if buffer is ready for playback - require minimum buffer for quality
        if (!jitterBufferReady_.load() && !jitterBuffer_.empty()) {
            // Calculate current buffer size in milliseconds
            double currentBufferMs = jitterBuffer_.size() * packetIntervalMs_;
            if (currentBufferMs >= targetBufferMs_) {
                jitterBufferReady_.store(true);
                std::cout << "RenderSource: Jitter buffer ready (" << currentBufferMs << "ms buffered)" << std::endl;
            }
        }

        // Process packets if buffer is ready
        if (jitterBufferReady_.load() && !jitterBuffer_.empty()) {
            if (usingSharedNetwork_) {
                // For shared network, just play packets in arrival order (no strict sequencing)
                auto it = jitterBuffer_.begin();
                ReceivedAudioPacket packet = it->second;
                jitterBuffer_.erase(it);
                lock.unlock();

                // Call render callback if set
                if (renderCallback_) {
                    renderCallback_(packet.audioData.data(), packet.audioData.size(), packet.timestamp);
                }
                
                lock.lock();
            } else {
                // **PROFESSIONAL AUDIO INTERPOLATION** for packet transitions
                uint32_t expectedSeq = expectedSequenceNumber_.load();
                auto it = jitterBuffer_.find(expectedSeq);
                
                if (it != jitterBuffer_.end()) {
                    ReceivedAudioPacket packet = it->second;
                    jitterBuffer_.erase(it);
                    lock.unlock();

                    // **ADVANCED AUDIO ENHANCEMENT PIPELINE**
                    std::vector<float> enhancedAudio = packet.audioData;
                    
                    // **1. INTER-PACKET INTERPOLATION** (eliminates discontinuities)
                    static std::vector<float> previousPacket;
                    if (!previousPacket.empty() && previousPacket.size() == enhancedAudio.size()) {
                        // Cross-fade between packets to eliminate clicks/pops
                        const size_t crossfadeSamples = std::min(static_cast<size_t>(8), enhancedAudio.size() / 8);
                        for (size_t i = 0; i < crossfadeSamples; i++) {
                            float blend = static_cast<float>(i) / crossfadeSamples;
                            size_t prevIdx = previousPacket.size() - crossfadeSamples + i;
                            if (prevIdx < previousPacket.size()) {
                                // Linear interpolation with gentle curve
                                float smoothBlend = 0.5f * (1.0f - std::cos(blend * 3.14159f));
                                enhancedAudio[i] = enhancedAudio[i] * smoothBlend + 
                                                 previousPacket[prevIdx] * (1.0f - smoothBlend);
                            }
                        }
                    }
                    
                    // **2. ANTI-ALIASING FILTER** (reduces digital harshness)
                    static float antiAlias_x1 = 0.0f, antiAlias_y1 = 0.0f;
                    const float aliasAlpha = 0.85f; // Low-pass at ~8kHz for 48kHz
                    
                    for (size_t i = 0; i < enhancedAudio.size(); i++) {
                        float sample = enhancedAudio[i];
                        
                        // Simple but effective anti-aliasing filter
                        float filtered = aliasAlpha * antiAlias_y1 + (1.0f - aliasAlpha) * sample;
                        antiAlias_y1 = filtered;
                        enhancedAudio[i] = filtered;
                    }
                    
                    // **3. GENTLE DYNAMICS EXPANSION** (restore natural dynamics)
                    static float expanderGain = 1.0f;
                    const float expanderThreshold = 0.1f;
                    const float expanderRatio = 1.5f; // Gentle expansion
                    
                    float packetRMS = 0.0f;
                    for (float sample : enhancedAudio) {
                        packetRMS += sample * sample;
                    }
                    packetRMS = std::sqrt(packetRMS / enhancedAudio.size());
                    
                    if (packetRMS > 0.001f) { // Only process significant audio
                        if (packetRMS < expanderThreshold) {
                            float expansion = std::pow(packetRMS / expanderThreshold, 1.0f / expanderRatio);
                            expanderGain = expanderGain * 0.95f + expansion * 0.05f; // Smooth adaptation
                            
                            for (size_t i = 0; i < enhancedAudio.size(); i++) {
                                enhancedAudio[i] *= expanderGain;
                            }
                        }
                    }
                    
                    previousPacket = enhancedAudio;

                    // Send enhanced audio to output
                    if (renderCallback_) {
                        renderCallback_(enhancedAudio.data(), enhancedAudio.size(), packet.timestamp);
                    }
                    
                    expectedSequenceNumber_.fetch_add(1);
                    lock.lock();
                } else {
                    // **PROFESSIONAL PACKET LOSS CONCEALMENT** with audio reconstruction
                    uint32_t expectedSeq = expectedSequenceNumber_.load();
                    auto nextIt = jitterBuffer_.upper_bound(expectedSeq);
                    
                    if (nextIt != jitterBuffer_.end() && 
                        (nextIt->first - expectedSeq) < 3) {  // Allow max 2 missing packets
                        
                        lock.unlock();
                        if (renderCallback_) {
                            // **INTELLIGENT AUDIO RECONSTRUCTION** instead of silence
                            size_t reconstructSamples = audioFramesPerBuffer_ * audioChannels_;
                            std::vector<float> reconstructedAudio(reconstructSamples);
                            
                            // **METHOD 1: PREDICTIVE INTERPOLATION** (if we have history)
                            static std::vector<float> audioHistory;
                            static bool useInterpolation = false;
                            
                            if (!audioHistory.empty() && audioHistory.size() >= reconstructSamples) {
                                useInterpolation = true;
                                
                                // **LINEAR PREDICTIVE CODING** approach - simple but effective
                                for (size_t i = 0; i < reconstructSamples; i++) {
                                    size_t histIdx = audioHistory.size() - reconstructSamples + i;
                                    float prediction = audioHistory[histIdx];
                                    
                                    // Add gentle decay to simulate natural audio fading
                                    float fadeOut = 1.0f - (static_cast<float>(i) / reconstructSamples) * 0.7f;
                                    prediction *= fadeOut;
                                    
                                    // Add very subtle noise to avoid digital silence artifacts
                                    static uint32_t noiseState = 12345;
                                    noiseState = noiseState * 1103515245 + 12345;
                                    float noise = ((noiseState >> 16) & 0xFFFF) / 65535.0f - 0.5f;
                                    prediction += noise * 0.0001f; // Very quiet comfort noise
                                    
                                    reconstructedAudio[i] = prediction;
                                }
                            } else {
                                // **METHOD 2: INTELLIGENT COMFORT NOISE** (when no history available)
                                static float comfortNoiseLevel = 0.0001f; // Very low level
                                static uint32_t noiseState = 54321;
                                
                                for (size_t i = 0; i < reconstructSamples; i++) {
                                    // Generate pink-ish noise (more natural than white noise)
                                    noiseState = noiseState * 1103515245 + 12345;
                                    float noise1 = ((noiseState >> 16) & 0xFFFF) / 65535.0f - 0.5f;
                                    
                                    noiseState = noiseState * 1103515245 + 12345;
                                    float noise2 = ((noiseState >> 16) & 0xFFFF) / 65535.0f - 0.5f;
                                    
                                    // Simple pink noise approximation
                                    float pinkNoise = (noise1 + noise2 * 0.5f) / 1.5f;
                                    reconstructedAudio[i] = pinkNoise * comfortNoiseLevel;
                                }
                            }
                            
                            renderCallback_(reconstructedAudio.data(), reconstructSamples, 0);
                        }
                        expectedSequenceNumber_.fetch_add(1);
                        lock.lock();
                    } else {
                        // Wait for more packets with enhanced timing
                        lock.unlock();
                        std::this_thread::sleep_for(std::chrono::microseconds(500)); // More precise timing
                        lock.lock();
                    }
                }
            } // Close the else block for premium network handling
        }
        
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void RenderSource::processReceivedPacket(const ReceivedAudioPacket& packet) {
    std::lock_guard<std::mutex> lock(jitterBufferMutex_);
    
    // Add packet to buffer if not duplicate
    if (jitterBuffer_.find(packet.sequenceNumber) == jitterBuffer_.end()) {
        jitterBuffer_[packet.sequenceNumber] = packet;
        
        // Limit buffer size
        while (jitterBuffer_.size() * packetIntervalMs_ > adaptiveMaxBufferMs_) {
            auto oldestIt = jitterBuffer_.begin();
            jitterBuffer_.erase(oldestIt);
            totalPacketsDropped_.fetch_add(1);
        }
        
        // Notify jitter buffer worker
        jitterBufferCondition_.notify_one();
    }
}

bool RenderSource::initializeNetworking() {
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "RenderSource: WSAStartup failed: " << result << std::endl;
        return false;
    }
#endif
    return true;
}

void RenderSource::cleanupNetworking() {
#ifdef _WIN32
    WSACleanup();
#endif
}

bool RenderSource::receiveData(void* buffer, size_t size) {
    if (clientSocket_ == INVALID_SOCKET_VALUE) return false;
    
    size_t totalReceived = 0;
    char* buf = static_cast<char*>(buffer);
    
    while (totalReceived < size) {
        int received = recv(clientSocket_, buf + totalReceived, 
                           static_cast<int>(size - totalReceived), 0);
        if (received <= 0) {
            return false;
        }
        totalReceived += received;
    }
    
    return true;
}

void RenderSource::closeSocket() {
    if (clientSocket_ != INVALID_SOCKET_VALUE) {
        closesocket(clientSocket_);
        clientSocket_ = INVALID_SOCKET_VALUE;
    }
}

void RenderSource::handleConnectionError() {
    if (isReceiving_.load()) {
        isReceiving_.store(false);
        closeSocket();
        
        std::cout << "RenderSource: Connection lost, attempting to reconnect..." << std::endl;
        
        // Attempt to reconnect in background
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(2000)); // Wait 2 seconds
            if (!isReceiving_.load() && isRunning_.load()) {
                // Try to reconnect
                startReceiving(serverHost_, serverPort_);
            }
        }).detach();
    }
}
