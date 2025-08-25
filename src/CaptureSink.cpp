#include "CaptureSink.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <algorithm>

// Undefine Windows min/max macros to avoid conflicts with std::min/max
#ifdef _WIN32
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#endif

CaptureSink::CaptureSink() 
    : clientSocket_(INVALID_SOCKET), serverPort_(0), config_(nullptr),
      isRunning_(false), isInitialized_(false), isConnected_(false),
      sequenceNumber_(0), lastHeartbeatTime_(0), totalPacketsSent_(0),
      totalPacketsDropped_(0), totalBytesTransmitted_(0), lastTransmissionTime_(0),
      averageLatency_(0.0), reconnectAttempts_(0), lastReconnectTime_(0) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

CaptureSink::~CaptureSink() {
    CaptureSinkDeinit();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool CaptureSink::CaptureSinkInit(const CaptureSinkConfig& config) {
    if (isInitialized_.load()) {
        std::cerr << "CaptureSink: Already initialized" << std::endl;
        return true;
    }

    config_ = new CaptureSinkConfig(config);
    serverHost_ = config.serverHost;
    serverPort_ = config.serverPort;
    isInitialized_.store(true);

    std::cout << "CaptureSink: Initialized successfully" << std::endl;
    std::cout << "  Server: " << config.serverHost << ":" << config.serverPort << std::endl;
    std::cout << "  Max Queue Size: " << config.maxQueueSize << " packets" << std::endl;
    std::cout << "  Heartbeat Interval: " << config.heartbeatIntervalMs << "ms" << std::endl;
    std::cout << "  Max Packet Size: " << config.maxPacketSize << " bytes" << std::endl;

    return true;
}

bool CaptureSink::CaptureSinkDeinit() {
    isRunning_.store(false);
    
    if (isConnected_.load()) {
        disconnectFromServer();
    }
    
    if (config_) {
        delete config_;
        config_ = nullptr;
    }
    
    isInitialized_.store(false);
    std::cout << "CaptureSink: Deinitialized" << std::endl;
    return true;
}

bool CaptureSink::connectToServer(const std::string& host, int port) {
    if (!isInitialized_.load()) return false;
    
    // Create socket
    clientSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket_ == INVALID_SOCKET) {
        std::cerr << "CaptureSink: Failed to create socket" << std::endl;
        return false;
    }

    // Setup server address
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<uint16_t>(port));
    
    // Resolve hostname to IP address
#ifdef _WIN32
    // Try direct IP address first
    serverAddr.sin_addr.s_addr = inet_addr(host.c_str());
    if (serverAddr.sin_addr.s_addr == INADDR_NONE) {
        // If not a direct IP, resolve hostname
        struct hostent* he = gethostbyname(host.c_str());
        if (he == nullptr) {
            std::cerr << "CaptureSink: Failed to resolve hostname: " << host << std::endl;
            closesocket(clientSocket_);
            clientSocket_ = INVALID_SOCKET;
            return false;
        }
        memcpy(&serverAddr.sin_addr, he->h_addr_list[0], he->h_length);
    }
#else
    if (inet_aton(host.c_str(), &serverAddr.sin_addr) == 0) {
        // If not a direct IP, resolve hostname  
        struct hostent* he = gethostbyname(host.c_str());
        if (he == nullptr) {
            std::cerr << "CaptureSink: Failed to resolve hostname: " << host << std::endl;
            close(clientSocket_);
            clientSocket_ = INVALID_SOCKET;
            return false;
        }
        memcpy(&serverAddr.sin_addr, he->h_addr_list[0], he->h_length);
    }
#endif

    // Connect with retries
    int maxRetries = config_ ? config_->maxReconnectAttempts : 3;
    for (int i = 0; i < maxRetries; i++) {
        if (connect(clientSocket_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == 0) {
            isConnected_.store(true);
            isRunning_.store(true);
            std::cout << "CaptureSink: Connected to " << host << ":" << port << std::endl;
            return true;
        }
        
        if (i < maxRetries - 1) {
            std::cout << "CaptureSink: Connection attempt failed, retrying..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }
    }

#ifdef _WIN32
    int error = WSAGetLastError();
    std::cerr << "CaptureSink: Failed to connect to " << host << ":" << port 
              << " (Windows error: " << error << ")" << std::endl;
#else
    std::cerr << "CaptureSink: Failed to connect to " << host << ":" << port << std::endl;
#endif

    closesocket(clientSocket_);
    clientSocket_ = INVALID_SOCKET;
    return false;
}

bool CaptureSink::disconnectFromServer() {
    if (isConnected_.load()) {
        isConnected_.store(false);
        isRunning_.store(false);
        
        if (clientSocket_ != INVALID_SOCKET) {
            closesocket(clientSocket_);
            clientSocket_ = INVALID_SOCKET;
        }
        
        std::cout << "CaptureSink: Disconnected from server" << std::endl;
    }
    return true;
}

bool CaptureSink::sendAudioData(const float* audioData, size_t samples, uint64_t timestamp) {
    if (!isConnected_.load() || !audioData || samples == 0) return false;

    // Create simple message for audio data
    Message message(MessageType::AUDIO_DATA);
    message.setTimestamp(timestamp);
    message.setAudioData(audioData, samples);

    // Send message
    if (sendMessage(message)) {
        totalPacketsSent_.fetch_add(1);
        totalBytesTransmitted_.fetch_add(samples * sizeof(float));
        return true;
    }

    return false;
}

bool CaptureSink::sendMessage(const Message& message) {
    if (!isConnected_.load() || clientSocket_ == INVALID_SOCKET) return false;

    // Serialize the message
    auto serializedData = message.serialize();
    
    // Send the complete serialized message
    size_t totalSent = 0;
    const char* data = reinterpret_cast<const char*>(serializedData.data());
    size_t totalSize = serializedData.size();
    
    while (totalSent < totalSize) {
        int sent = send(clientSocket_, data + totalSent, 
                       static_cast<int>(totalSize - totalSent), 0);
        if (sent <= 0) {
            return false;
        }
        totalSent += sent;
    }

    return true;
}

bool CaptureSink::CaptureSinkProcess() {
    // Keep-alive processing (can be used for heartbeat, etc.)
    if (!isConnected_.load()) return false;
    
    // Optional: Send heartbeat periodically
    static auto lastHeartbeat = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    
    if (config_ && std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHeartbeat).count() 
        >= config_->heartbeatIntervalMs) {
        
        Message heartbeat(MessageType::HEARTBEAT);
        sendMessage(heartbeat);
        lastHeartbeat = now;
    }
    
    return true;
}

bool CaptureSink::isConnected() const {
    return isConnected_.load();
}

CaptureSinkStats CaptureSink::getStats() const {
    CaptureSinkStats stats;
    stats.totalPacketsSent = totalPacketsSent_.load();
    stats.totalPacketsDropped = totalPacketsDropped_.load();
    stats.totalBytesTransmitted = totalBytesTransmitted_.load();
    stats.isConnected = isConnected_.load();
    stats.isTransmitting = isRunning_.load();
    stats.averageLatency = averageLatency_.load();
    return stats;
}
