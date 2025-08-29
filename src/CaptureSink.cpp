#include "CaptureSink.h"
#include "NetworkManager.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>

bool CaptureSink::CaptureSinkInit(const std::string &serverHost, int serverPort)
{
    if (isInitialized_)
    {
        std::cerr << "CaptureSink: Already initialized" << std::endl;
        return false;
    }

    serverHost_ = serverHost;
    serverPort_ = serverPort;
    sequenceNumber_ = 0;
    usingSharedNetwork_ = false;

    // Initialize configuration
    config_ = new CaptureSinkConfig();

    isInitialized_ = true;
    std::cout << "CaptureSink: Initialized successfully" << std::endl;
    std::cout << "  Server: " << serverHost_ << ":" << serverPort_ << std::endl;
    std::cout << "  Max Queue Size: " << config_->maxQueueSize << " packets" << std::endl;
    std::cout << "  Heartbeat Interval: " << config_->heartbeatIntervalMs << "ms" << std::endl;
    std::cout << "  Max Packet Size: " << config_->maxPacketSize << " bytes" << std::endl;

    // Connect using NetworkManager
    if (networkManager_.connectToServer(serverHost_, serverPort_))
    {
        isConnected_.store(true);
        std::cout << "CaptureSink: Connected to " << serverHost_ << ":" << serverPort_ << std::endl;

        // Send client configuration to server (like AudioClient does)
        Message configMsg;
        configMsg.type = MessageType::CLIENT_CONFIG;
        configMsg.size = 0; // No additional config data for now
        configMsg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::high_resolution_clock::now().time_since_epoch())
                                  .count();

        networkManager_.sendMessage(configMsg);

        // Send ready message
        Message readyMsg;
        readyMsg.type = MessageType::CLIENT_READY;
        readyMsg.size = 0;
        readyMsg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::high_resolution_clock::now().time_since_epoch())
                                 .count();

        networkManager_.sendMessage(readyMsg);
    }
    else
    {
        std::cerr << "CaptureSink: Failed to connect to server during initialization" << std::endl;
        // Don't fail initialization, allow retries later
    }

    return true;
}

bool CaptureSink::CaptureSinkInitWithSharedNetwork(NetworkManager *sharedNetworkManager)
{
    if (isInitialized_)
    {
        std::cerr << "CaptureSink: Already initialized" << std::endl;
        return false;
    }

    if (!sharedNetworkManager)
    {
        std::cerr << "CaptureSink: Shared network manager is null" << std::endl;
        return false;
    }

    sharedNetworkManager_ = sharedNetworkManager;
    usingSharedNetwork_ = true;
    sequenceNumber_ = 0;

    // Initialize configuration
    config_ = new CaptureSinkConfig();

    isInitialized_ = true;
    isConnected_.store(true); // Using shared connection

    std::cout << "CaptureSink: Initialized with shared network successfully" << std::endl;
    std::cout << "  Max Queue Size: " << config_->maxQueueSize << " packets" << std::endl;
    std::cout << "  Heartbeat Interval: " << config_->heartbeatIntervalMs << "ms" << std::endl;
    std::cout << "  Max Packet Size: " << config_->maxPacketSize << " bytes" << std::endl;

    return true;
}

void CaptureSink::CaptureSinkDeinit()
{
    if (!isInitialized_)
        return;

    if (!usingSharedNetwork_)
    {
        networkManager_.disconnect();
    }
    isConnected_.store(false);

    delete config_;
    config_ = nullptr;

    sharedNetworkManager_ = nullptr;
    usingSharedNetwork_ = false;

    isInitialized_ = false;
    std::cout << "CaptureSink: Deinitialized" << std::endl;
}

bool CaptureSink::CaptureSinkProcess()
{
    if (!isInitialized_)
        return false;

    // Handle periodic tasks like heartbeat
    if (isConnected_.load())
    {
        uint64_t currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::high_resolution_clock::now().time_since_epoch())
                                   .count();

        // Send heartbeat if needed
        if (currentTime - lastHeartbeatTime_.load() > config_->heartbeatIntervalMs)
        {
            // Could implement heartbeat here if needed
            lastHeartbeatTime_.store(currentTime);
        }
    }

    return true;
}

bool CaptureSink::connectToServer(const std::string &host, int port)
{
    if (!isInitialized_ || isConnected_.load())
    {
        return false;
    }

    // Create socket
    clientSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket_ == INVALID_SOCKET_VALUE)
    {
        std::cerr << "CaptureSink: Failed to create socket" << std::endl;
        return false;
    }

    // Setup server address
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    // Resolve hostname
#ifdef _WIN32
    serverAddr.sin_addr.s_addr = inet_addr(host.c_str());
    if (serverAddr.sin_addr.s_addr == INADDR_NONE)
    {
        struct hostent *he = gethostbyname(host.c_str());
        if (he == nullptr)
        {
            std::cerr << "CaptureSink: Failed to resolve hostname: " << host << std::endl;
            closesocket(clientSocket_);
            clientSocket_ = INVALID_SOCKET_VALUE;
            return false;
        }
        memcpy(&serverAddr.sin_addr, he->h_addr_list[0], he->h_length);
    }
#else
    if (inet_aton(host.c_str(), &serverAddr.sin_addr) == 0)
    {
        struct hostent *he = gethostbyname(host.c_str());
        if (he == nullptr)
        {
            std::cerr << "CaptureSink: Failed to resolve hostname: " << host << std::endl;
            close(clientSocket_);
            clientSocket_ = INVALID_SOCKET_VALUE;
            return false;
        }
        memcpy(&serverAddr.sin_addr, he->h_addr_list[0], he->h_length);
    }
#endif

    // Connect with retries
    int maxRetries = 3;
    for (int i = 0; i < maxRetries; i++)
    {
        if (connect(clientSocket_, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == 0)
        {
            isConnected_.store(true);
            isRunning_.store(true);
            std::cout << "CaptureSink: Connected to " << host << ":" << port << std::endl;
            return true;
        }

        if (i < maxRetries - 1)
        {
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
    clientSocket_ = INVALID_SOCKET_VALUE;
    return false;
}

bool CaptureSink::disconnectFromServer()
{
    if (isConnected_.load())
    {
        isConnected_.store(false);
        isRunning_.store(false);

        if (clientSocket_ != INVALID_SOCKET_VALUE)
        {
            closesocket(clientSocket_);
            clientSocket_ = INVALID_SOCKET_VALUE;
        }

        std::cout << "CaptureSink: Disconnected from server" << std::endl;
    }
    return true;
}

bool CaptureSink::sendAudioData(const float *audioData, size_t samples, uint64_t timestamp)
{
    if (!isConnected_.load() || !audioData || samples == 0)
        return false;

    // Create Message using NetworkManager protocol
    Message audioMessage;
    audioMessage.type = MessageType::AUDIO_DATA;
    audioMessage.size = static_cast<uint32_t>(samples * sizeof(float));
    audioMessage.timestamp = timestamp;
    audioMessage.data.resize(audioMessage.size);

    // Copy audio data to message
    std::memcpy(audioMessage.data.data(), audioData, audioMessage.size);

    // Send using appropriate NetworkManager
    NetworkManager *manager = usingSharedNetwork_ ? sharedNetworkManager_ : &networkManager_;
    if (manager && manager->sendMessage(audioMessage))
    {
        totalPacketsSent_.fetch_add(1);
        totalBytesTransmitted_.fetch_add(audioMessage.size);

        // Debug: Show transmission activity occasionally (silenced for cleaner output)
        static int packetCount = 0;
        packetCount++;
        if (packetCount % 1000 == 0)
        { // Every 1000 packets (reduced frequency)
            // std::cout << "Sent " << packetCount << " packets, " << samples << " samples" << std::endl;
        }

        return true;
    }

    return false;
}

bool CaptureSink::initializeNetworking()
{
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
    {
        std::cerr << "CaptureSink: WSAStartup failed: " << result << std::endl;
        return false;
    }
#endif
    return true;
}

void CaptureSink::cleanupNetworking()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

bool CaptureSink::sendData(const void *data, size_t size)
{
    if (!isConnected_.load() || clientSocket_ == INVALID_SOCKET_VALUE)
        return false;

    size_t totalSent = 0;
    const char *buffer = static_cast<const char *>(data);

    while (totalSent < size)
    {
        int sent = send(clientSocket_, buffer + totalSent,
                        static_cast<int>(size - totalSent), 0);
        if (sent <= 0)
        {
            std::cerr << "CaptureSink: Failed to send data" << std::endl;
            return false;
        }
        totalSent += sent;
    }

    return true;
}
