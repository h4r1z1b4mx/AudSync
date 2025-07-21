#include "AudioServer.h"
#include <iostream>
#include <algorithm>

AudioServer::AudioServer() : running_(false) {
}

AudioServer::~AudioServer() {
    stop();
}

bool AudioServer::start(int port) {
    if (running_) return true;

    network_manager_.setMessageHandler(
        [this](const Message& msg, int socket) {
            handleClientMessage(msg, socket);
        }
    );

    if (!network_manager_.startServer(port)) {
        std::cerr << "Failed to start server on port " << port << std::endl;
        return false;
    }

    running_ = true;
    server_thread_ = std::thread(&AudioServer::serverLoop, this);
    
    std::cout << "AudSync Server started on port " << port << std::endl;
    return true;
}

void AudioServer::stop() {
    if (!running_) return;
    
    running_ = false;
    
    // Stop network manager first
    network_manager_.stopServer();
    
    // Join server thread
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    
    // Clear clients
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.clear();
    
    std::cout << "Server stopped" << std::endl;
}

bool AudioServer::isRunning() const {
    return running_;
}

size_t AudioServer::getConnectedClients() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return clients_.size();
}

void AudioServer::handleClientMessage(const Message& message, int client_socket) {
    switch (message.type) {
        case MessageType::CONNECT:
            addClient(client_socket);
            std::cout << "Client " << client_socket << " connected. Total clients: " 
                      << getConnectedClients() << std::endl;
            break;
            
        case MessageType::DISCONNECT:
            removeClient(client_socket);
            std::cout << "Client " << client_socket << " disconnected. Total clients: " 
                      << getConnectedClients() << std::endl;
            break;
            
        case MessageType::CLIENT_READY:
            {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                auto it = std::find_if(clients_.begin(), clients_.end(),
                    [client_socket](const ClientInfo& client) {
                        return client.socket_fd == client_socket;
                    });
                if (it != clients_.end()) {
                    it->ready = true;
                    std::cout << "Client " << client_socket << " is ready for audio" << std::endl;
                }
            }
            break;
            
        case MessageType::AUDIO_DATA:
            broadcastAudioToOthers(message, client_socket);
            break;
            
        case MessageType::HEARTBEAT:
            // Echo heartbeat back
            network_manager_.sendMessage(message, client_socket);
            break;
            
        default:
            break;
    }
}

void AudioServer::broadcastAudioToOthers(const Message& message, int sender_socket) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    for (const auto& client : clients_) {
        if (client.socket_fd != sender_socket && client.ready) {
            network_manager_.sendMessage(message, client.socket_fd);
        }
    }
}

void AudioServer::addClient(int socket_fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    ClientInfo client;
    client.socket_fd = socket_fd;
    client.ready = false;
    client.id = "client_" + std::to_string(socket_fd);
    
    clients_.push_back(client);
}

void AudioServer::removeClient(int socket_fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    clients_.erase(
        std::remove_if(clients_.begin(), clients_.end(),
            [socket_fd](const ClientInfo& client) {
                return client.socket_fd == socket_fd;
            }),
        clients_.end()
    );
}

void AudioServer::serverLoop() {
    std::cout << "Server loop started. Waiting for clients..." << std::endl;
    
    while (running_) {
        // Server management tasks could go here
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Log status every 30 seconds
        static int counter = 0;
        if (++counter >= 30) {
            counter = 0;
            std::cout << "Server status: " << getConnectedClients() << " clients connected" << std::endl;
        }
    }
}
