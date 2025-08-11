#include "AudioServer.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
    #include <windows.h>  // For CreateDirectoryA
#else
    #include <sys/stat.h> // For mkdir
#endif

// REMOVED: AudioConfig struct - it should be in AudioServer.h instead

// Updated constructor to accept buffer size and output device (future use)
AudioServer::AudioServer(int sampleRate,
                         int channels,
                         SessionLogger* logger,
                         AudioRecorder* recorder,
                         JitterBuffer* jitterBuffer)
    : sampleRate_(sampleRate),
      channels_(channels),
      logger_(logger),
      recorder_(recorder),
      jitterBuffer_(jitterBuffer),
      running_(false) {}

AudioServer::~AudioServer() {
    stop();
}

bool AudioServer::start(int port){
    if (running_) return true;
  
    network_manager_.setMessageHandler(
        [this] (const Message& msg, int socket) {
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
  
    network_manager_.stopServer();

    if(server_thread_.joinable()){
        server_thread_.join();
    }
  
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.clear();
    std::cout <<"Server stopped" << std::endl;
}

bool AudioServer::isRunning() const {
    return running_;
}

size_t AudioServer::getConnectedClients() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return clients_.size();
}

void AudioServer::handleClientMessage(const Message& message, SOCKET client_socket) {
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

        case MessageType::CLIENT_CONFIG:
            // Handle client audio configuration
            {
                if (message.size >= sizeof(AudioConfig)) {
                    const AudioConfig* config = reinterpret_cast<const AudioConfig*>(message.data.data());
                    std::lock_guard<std::mutex> lock(clients_mutex_);
                    auto it = std::find_if(clients_.begin(), clients_.end(),
                        [client_socket](const ClientInfo& client) {
                            return client.socket_fd == client_socket;
                        });
                    if (it != clients_.end()) {
                        it->sampleRate = config->sampleRate;
                        it->channels = config->channels;
                        it->bufferSize = config->bufferSize;
                        std::cout << "Client " << client_socket << " configuration received:" << std::endl;
                        std::cout << "  Sample Rate: " << config->sampleRate << "Hz" << std::endl;
                        std::cout << "  Channels: " << config->channels << std::endl;
                        std::cout << "  Buffer Size: " << config->bufferSize << " frames" << std::endl;
                        
                        // Calculate and display packet info
                        size_t packetSize = config->bufferSize * config->channels * sizeof(float);
                        float latency = static_cast<float>(config->bufferSize) / config->sampleRate * 1000;
                        std::cout << "  Packet Size: " << packetSize << " bytes" << std::endl;
                        std::cout << "  Latency: " << std::fixed << std::setprecision(1) << latency << "ms" << std::endl;
                    }
                }
            }
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
                    std::cout << "Client " << client_socket << " is ready for audio streaming" << std::endl;
                }
            }
            break;
            
        case MessageType::AUDIO_DATA:
            {
                // Get client's actual audio parameters for proper logging
                int clientSampleRate = sampleRate_;  // default fallback
                int clientChannels = channels_;      // default fallback
                
                {
                    std::lock_guard<std::mutex> lock(clients_mutex_);
                    auto it = std::find_if(clients_.begin(), clients_.end(),
                        [client_socket](const ClientInfo& client) {
                            return client.socket_fd == client_socket;
                        });
                    if (it != clients_.end()) {
                        clientSampleRate = it->sampleRate;
                        clientChannels = it->channels;
                    }
                }
                
                // Logging with correct client parameters
                if (logger_) {
                    logger_->logAudioStats(message.size, clientSampleRate, clientChannels, std::to_string(client_socket));
                    logger_->logPacketMetadata(message.timestamp, message.size);
                }
                
                // Recording with client's audio format
                if (recorder_ && recorder_->isRecording()) {
                    std::vector<uint8_t> raw(message.data.begin(), message.data.end());
                    recorder_->writeSamples(raw);
                }
                
                // Jitter buffer integration
                if (jitterBuffer_) {
                    AudioPacket pkt;
                    pkt.data = message.data;
                    pkt.timestamp = message.timestamp;
                    jitterBuffer_->addPacket(pkt);
                }
                
                // Broadcast audio to other clients
                broadcastAudioToOthers(message, client_socket);
            }
            break;
            
        case MessageType::HEARTBEAT:
            // Echo heartbeat back to client
            network_manager_.sendMessage(message, client_socket);
            break;
            
        default:
            std::cout << "Unknown message type received from client " << client_socket << std::endl;
            break;
    }
}

void AudioServer::broadcastAudioToOthers(const Message& message, SOCKET sender_socket) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for(const auto& client: clients_) {
        if(client.socket_fd != sender_socket && client.ready) {
            network_manager_.sendMessage(message, client.socket_fd);
        }
    }
}

void AudioServer::addClient(SOCKET socket_fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    ClientInfo client;
    client.socket_fd = socket_fd;
    client.ready = false;
    client.id = "client_" + std::to_string(socket_fd);
    // Initialize with default values - will be updated by CLIENT_CONFIG
    client.sampleRate = sampleRate_;
    client.channels = channels_;
    client.bufferSize = 256;  // default buffer size
    clients_.push_back(client);
}

void AudioServer::removeClient(SOCKET socket_fd) {
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
        std::this_thread::sleep_for(std::chrono::seconds(1));
        static int counter = 0;
        if (++counter >= 30) {
            counter = 0;
            size_t clientCount = getConnectedClients();
            if (clientCount > 0) {
                std::cout << "Server status: " << clientCount << " clients connected" << std::endl;
                
                // Show client details every 30 seconds
                std::lock_guard<std::mutex> lock(clients_mutex_);
                for (const auto& client : clients_) {
                    std::cout << "  Client " << client.socket_fd << ": " 
                              << client.sampleRate << "Hz, " 
                              << client.channels << " channels, "
                              << (client.ready ? "ready" : "not ready") << std::endl;
                }
            }
        }
    }
}

// Helper method to get client configurations (for main_server.cpp)
std::vector<AudioConfig> AudioServer::getClientConfigurations() const {
    std::vector<AudioConfig> configs;
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (const auto& client : clients_) {
        AudioConfig config;
        config.sampleRate = client.sampleRate;
        config.channels = client.channels;
        config.bufferSize = client.bufferSize;
        configs.push_back(config);
    }
    return configs;
}

// Method to print detailed client information
void AudioServer::printClientDetails() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    if (clients_.empty()) {
        std::cout << "No clients connected." << std::endl;
        return;
    }
    
    for (const auto& client : clients_) {
        std::cout << "Client " << client.socket_fd << " (" << client.id << "):" << std::endl;
        std::cout << "  Audio Format: " << client.sampleRate << "Hz, " 
                  << client.channels << " channels" << std::endl;
        std::cout << "  Buffer Size: " << client.bufferSize << " frames" << std::endl;
        std::cout << "  Status: " << (client.ready ? "Ready for streaming" : "Not ready") << std::endl;
        
        // Calculate packet info
        size_t packetSize = client.bufferSize * client.channels * sizeof(float);
        float latency = static_cast<float>(client.bufferSize) / client.sampleRate * 1000;
        std::cout << "  Packet Size: " << packetSize << " bytes" << std::endl;
        std::cout << "  Latency: " << std::fixed << std::setprecision(1) << latency << "ms" << std::endl;
        std::cout << std::endl;
    }
}

// Utility to generate unique filenames with timestamp
std::string AudioServer::generateUniqueFilename(const std::string& prefix, const std::string& ext) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    #ifdef _WIN32
        struct tm timeinfo;
        localtime_s(&timeinfo, &time_t);
        oss << std::put_time(&timeinfo, "%Y%m%d_%H%M%S");
    #else
        oss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    #endif
    
    std::string directory = "recordings/";
    
    #ifdef _WIN32
        CreateDirectoryA(directory.c_str(), NULL);
    #else
        mkdir(directory.c_str(), 0755);
    #endif
    
    return directory + prefix + "_" + oss.str() + "." + ext;
}