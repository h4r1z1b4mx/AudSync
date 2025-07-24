#include "NetworkManager.h"
#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>

NetworkManager::NetworkManager() 
    : server_socket_(-1), client_socket_(-1), is_server_(false), running_(false) {
}

NetworkManager::~NetworkManager() {
    disconnect();
    stopServer();
}

bool NetworkManager::connectToServer(const std::string& host, int port) {
    client_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket_ < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address" << std::endl;
        close(client_socket_);
        client_socket_ = -1;
        return false;
    }

    if (connect(client_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Connection failed" << std::endl;
        close(client_socket_);
        client_socket_ = -1;
        return false;
    }

    // Send connect message
    Message connect_msg;
    connect_msg.type = MessageType::CONNECT;
    connect_msg.size = 0;
    
    return sendMessage(connect_msg, client_socket_);
}

void NetworkManager::disconnect() {
    if (client_socket_ >= 0) {
        // Send disconnect message
        Message disconnect_msg;
        disconnect_msg.type = MessageType::DISCONNECT;
        disconnect_msg.size = 0;
        sendMessage(disconnect_msg, client_socket_);
        
        close(client_socket_);
        client_socket_ = -1;
    }
}

bool NetworkManager::startServer(int port) {
    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_ < 0) {
        std::cerr << "Failed to create server socket" << std::endl;
        return false;
    }

    int opt = 1;
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(server_socket_);
        server_socket_ = -1;
        return false;
    }

    if (listen(server_socket_, 10) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(server_socket_);
        server_socket_ = -1;
        return false;
    }

    is_server_ = true;
    running_ = true;
    accept_thread_ = std::thread(&NetworkManager::acceptClients, this);

    std::cout << "Server started on port " << port << std::endl;
    return true;
}

void NetworkManager::stopServer() {
    running_ = false;
    
    if (server_socket_ >= 0) {
        close(server_socket_);
        server_socket_ = -1;
    }
    
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    
    is_server_ = false;
}

bool NetworkManager::sendMessage(const Message& message, int socket_fd) {
    int target_socket = (socket_fd >= 0) ? socket_fd : client_socket_;
    if (target_socket < 0) return false;

    // Send header
    uint8_t type = static_cast<uint8_t>(message.type);
    if (!sendRaw(&type, sizeof(type), target_socket)) return false;
    if (!sendRaw(&message.size, sizeof(message.size), target_socket)) return false;

    // Send data if any
    if (message.size > 0) {
        return sendRaw(message.data.data(), message.size, target_socket);
    }

    return true;
}

bool NetworkManager::receiveMessage(Message& message, int socket_fd) {
    int target_socket = (socket_fd >= 0) ? socket_fd : client_socket_;
    if (target_socket < 0) return false;

    // Receive header
    uint8_t type;
    if (!receiveRaw(&type, sizeof(type), target_socket)) return false;
    if (!receiveRaw(&message.size, sizeof(message.size), target_socket)) return false;

    message.type = static_cast<MessageType>(type);
    message.data.clear();

    // Receive data if any
    if (message.size > 0) {
        message.data.resize(message.size);
        return receiveRaw(message.data.data(), message.size, target_socket);
    }

    return true;
}

void NetworkManager::setMessageHandler(std::function<void(const Message&, int)> handler) {
    message_handler_ = handler;
}

bool NetworkManager::isConnected() const {
    return client_socket_ >= 0 || (is_server_ && running_);
}

void NetworkManager::acceptClients() {
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_socket_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (running_) {
                std::cerr << "Accept failed" << std::endl;
            }
            continue;
        }

        std::cout << "Client connected: " << client_fd << std::endl;
        std::thread(&NetworkManager::handleClient, this, client_fd).detach();
    }
}

void NetworkManager::handleClient(int client_fd) {
    while (running_) {
        Message message;
        if (receiveMessage(message, client_fd)) {
            if (message_handler_) {
                message_handler_(message, client_fd);
            }
            
            if (message.type == MessageType::DISCONNECT) {
                break;
            }
        } else {
            break;
        }
    }
    
    close(client_fd);
    std::cout << "Client disconnected: " << client_fd << std::endl;
}

bool NetworkManager::sendRaw(const void* data, size_t size, int socket_fd) {
    size_t sent = 0;
    const char* ptr = static_cast<const char*>(data);
    
    while (sent < size) {
        ssize_t result = send(socket_fd, ptr + sent, size - sent, 0);
        if (result <= 0) return false;
        sent += result;
    }
    
    return true;
}

bool NetworkManager::receiveRaw(void* data, size_t size, int socket_fd) {
    size_t received = 0;
    char* ptr = static_cast<char*>(data);
    
    while (received < size) {
        ssize_t result = recv(socket_fd, ptr + received, size - received, 0);
        if (result <= 0) return false;
        received += result;
    }
    
    return true;
}
