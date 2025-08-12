#include "NetworkManager.h"
#include <iostream>
#include <cstring>
#include <algorithm>

NetworkManager::NetworkManager() 
    : serverSocket_(INVALID_SOCKET),
      clientSocket_(INVALID_SOCKET),
      isServer_(false),
      isConnected_(false),
      running_(false) {
    
#ifdef _WIN32
    initializeWinsock();
#endif
}

NetworkManager::~NetworkManager() {
    stopServer();
    disconnect();
    
#ifdef _WIN32
    cleanupWinsock();
#endif
}

bool NetworkManager::initializeWinsock() {
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return false;
    }
#endif
    return true;
}

void NetworkManager::cleanupWinsock() {
#ifdef _WIN32
    WSACleanup();
#endif
}

bool NetworkManager::startServer(int port) {
    if (running_) return false;
    
    serverSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket_ == INVALID_SOCKET) {
        std::cerr << "Failed to create server socket" << std::endl;
        return false;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, 
                   reinterpret_cast<const char*>(&opt), sizeof(opt)) < 0) {
        std::cerr << "Failed to set socket options" << std::endl;
        closeSocket(serverSocket_);
        return false;
    }
    
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(static_cast<uint16_t>(port));
    
    if (bind(serverSocket_, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Failed to bind server socket to port " << port << std::endl;
        closeSocket(serverSocket_);
        return false;
    }
    
    if (listen(serverSocket_, 5) == SOCKET_ERROR) {
        std::cerr << "Failed to listen on server socket" << std::endl;
        closeSocket(serverSocket_);
        return false;
    }
    
    isServer_ = true;
    running_ = true;
    serverThread_ = std::thread(&NetworkManager::serverLoop, this);
    
    std::cout << "Server started on port " << port << std::endl;
    return true;
}

void NetworkManager::stopServer() {
    if (!running_ || !isServer_) return;
    
    running_ = false;
    
    // Close all client connections
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (SOCKET client : connectedClients_) {
            closeSocket(client);
        }
        connectedClients_.clear();
    }
    
    closeSocket(serverSocket_);
    
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
    
    if (clientHandlerThread_.joinable()) {
        clientHandlerThread_.join();
    }
    
    isServer_ = false;
    std::cout << "Server stopped" << std::endl;
}

bool NetworkManager::connectToServer(const std::string& host, int port) {
    if (isConnected_) return true;
    
    clientSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket_ == INVALID_SOCKET) {
        std::cerr << "Failed to create client socket" << std::endl;
        return false;
    }
    
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<uint16_t>(port));
    
    if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid server address: " << host << std::endl;
        closeSocket(clientSocket_);
        return false;
    }
    
    if (connect(clientSocket_, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Failed to connect to server " << host << ":" << port << std::endl;
        closeSocket(clientSocket_);
        return false;
    }
    
    isConnected_ = true;
    running_ = true;
    
    // Send connect message
    if (messageHandler_) {
        Message connectMsg;
        connectMsg.type = MessageType::CONNECT;
        connectMsg.size = 0;
        connectMsg.timestamp = 0;
        messageHandler_(connectMsg, clientSocket_);
    }
    
    std::cout << "Connected to server " << host << ":" << port << std::endl;
    return true;
}

void NetworkManager::disconnect() {
    if (!isConnected_) return;
    
    running_ = false;
    
    // Send disconnect message
    if (messageHandler_) {
        Message disconnectMsg;
        disconnectMsg.type = MessageType::DISCONNECT;
        disconnectMsg.size = 0;
        disconnectMsg.timestamp = 0;
        messageHandler_(disconnectMsg, clientSocket_);
    }
    
    closeSocket(clientSocket_);
    isConnected_ = false;
    
    std::cout << "Disconnected from server" << std::endl;
}

void NetworkManager::serverLoop() {
    while (running_) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        
        SOCKET clientSocket = accept(serverSocket_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (clientSocket == INVALID_SOCKET) {
            if (running_) {
                std::cerr << "Failed to accept client connection" << std::endl;
            }
            continue;
        }
        
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            connectedClients_.push_back(clientSocket);
        }
        
        // Handle client in separate thread
        std::thread clientThread(&NetworkManager::handleClient, this, clientSocket);
        clientThread.detach();
        
        // Notify about new connection
        if (messageHandler_) {
            Message connectMsg;
            connectMsg.type = MessageType::CONNECT;
            connectMsg.size = 0;
            connectMsg.timestamp = 0;
            messageHandler_(connectMsg, clientSocket);
        }
    }
}

void NetworkManager::handleClient(SOCKET clientSocket) {
    while (running_) {
        Message message;
        if (receiveMessage(message, clientSocket)) {
            if (messageHandler_) {
                messageHandler_(message, clientSocket);
            }
        } else {
            break; // Client disconnected
        }
    }
    
    // Remove client from list
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        connectedClients_.erase(
            std::remove(connectedClients_.begin(), connectedClients_.end(), clientSocket),
            connectedClients_.end()
        );
    }
    
    // Notify about disconnection
    if (messageHandler_) {
        Message disconnectMsg;
        disconnectMsg.type = MessageType::DISCONNECT;
        disconnectMsg.size = 0;
        disconnectMsg.timestamp = 0;
        messageHandler_(disconnectMsg, clientSocket);
    }
    
    closeSocket(clientSocket);
}

bool NetworkManager::sendMessage(const Message& message, SOCKET clientSocket) {
    std::vector<uint8_t> buffer;
    if (!serializeMessage(message, buffer)) {
        return false;
    }
    
    return sendRawData(clientSocket, buffer.data(), buffer.size());
}

bool NetworkManager::sendMessage(const Message& message) {
    if (!isConnected_) return false;
    return sendMessage(message, clientSocket_);
}

bool NetworkManager::receiveMessage(Message& message) {
    if (!isConnected_) return false;
    return receiveMessage(message, clientSocket_);
}

bool NetworkManager::receiveMessage(Message& message, SOCKET socket) {
    // Receive header (type, size, timestamp)
    struct MessageHeader {
        uint32_t type;
        uint32_t size;
        uint64_t timestamp;
    } header;
    
    if (!receiveRawData(socket, &header, sizeof(header))) {
        return false;
    }
    
    message.type = static_cast<MessageType>(header.type);
    message.size = header.size;
    message.timestamp = header.timestamp;
    
    // Receive data if present
    if (message.size > 0) {
        message.data.resize(message.size);
        if (!receiveRawData(socket, message.data.data(), message.size)) {
            return false;
        }
    } else {
        message.data.clear();
    }
    
    return true;
}

void NetworkManager::setMessageHandler(MessageHandler handler) {
    messageHandler_ = handler;
}

bool NetworkManager::isConnected() const {
    return isConnected_;
}

bool NetworkManager::sendRawData(SOCKET socket, const void* data, size_t size) {
    const char* bytes = static_cast<const char*>(data);
    size_t totalSent = 0;
    
    while (totalSent < size) {
        int sent = send(socket, bytes + totalSent, static_cast<int>(size - totalSent), 0);
        if (sent == SOCKET_ERROR) {
            std::cerr << "Failed to send data" << std::endl;
            return false;
        }
        totalSent += sent;
    }
    
    return true;
}

bool NetworkManager::receiveRawData(SOCKET socket, void* data, size_t size) {
    char* bytes = static_cast<char*>(data);
    size_t totalReceived = 0;
    
    while (totalReceived < size) {
        int received = recv(socket, bytes + totalReceived, static_cast<int>(size - totalReceived), 0);
        if (received <= 0) {
            if (received == 0) {
                std::cout << "Client disconnected" << std::endl;
            } else {
                std::cerr << "Failed to receive data" << std::endl;
            }
            return false;
        }
        totalReceived += received;
    }
    
    return true;
}

void NetworkManager::closeSocket(SOCKET& socket) {
    if (socket != INVALID_SOCKET) {
        closesocket(socket);
        socket = INVALID_SOCKET;
    }
}

bool NetworkManager::serializeMessage(const Message& message, std::vector<uint8_t>& buffer) {
    // Calculate total size: header + data
    size_t totalSize = sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint64_t) + message.size;
    buffer.resize(totalSize);
    
    size_t offset = 0;
    
    // Serialize type
    uint32_t type = static_cast<uint32_t>(message.type);
    std::memcpy(buffer.data() + offset, &type, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    // Serialize size
    std::memcpy(buffer.data() + offset, &message.size, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    // Serialize timestamp
    std::memcpy(buffer.data() + offset, &message.timestamp, sizeof(uint64_t));
    offset += sizeof(uint64_t);
    
    // Serialize data
    if (message.size > 0) {
        std::memcpy(buffer.data() + offset, message.data.data(), message.size);
    }
    
    return true;
}

bool NetworkManager::deserializeMessage(const std::vector<uint8_t>& buffer, Message& message) {
    if (buffer.size() < sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint64_t)) {
        return false;
    }
    
    size_t offset = 0;
    
    // Deserialize type
    uint32_t type;
    std::memcpy(&type, buffer.data() + offset, sizeof(uint32_t));
    message.type = static_cast<MessageType>(type);
    offset += sizeof(uint32_t);
    
    // Deserialize size
    std::memcpy(&message.size, buffer.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    
    // Deserialize timestamp
    std::memcpy(&message.timestamp, buffer.data() + offset, sizeof(uint64_t));
    offset += sizeof(uint64_t);
    
    // Deserialize data
    if (message.size > 0) {
        if (buffer.size() < offset + message.size) {
            return false;
        }
        message.data.resize(message.size);
        std::memcpy(message.data.data(), buffer.data() + offset, message.size);
    } else {
        message.data.clear();
    }
    
    return true;
}