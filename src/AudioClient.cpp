#include "AudioClient.h"
#include <iostream>
#include <cstring>

AudioClient::AudioClient() 
    : connected_(false), audio_active_(false), running_(false) {
}

AudioClient::~AudioClient() {
    disconnect();
}

bool AudioClient::connect(const std::string& server_host, int server_port) {
    if (connected_) return true;

    network_manager_.setMessageHandler(
        [this](const Message& msg, int socket) {
            handleNetworkMessage(msg, socket);
        }
    );

    if (!network_manager_.connectToServer(server_host, server_port)) {
        std::cerr << "Failed to connect to server" << std::endl;
        return false;
    }

    connected_ = true;
    running_ = true;
    
    network_thread_ = std::thread(&AudioClient::networkLoop, this);
    
    std::cout << "Connected to server at " << server_host << ":" << server_port << std::endl;
    return true;
}

void AudioClient::disconnect() {
    running_ = false;
    
    if (network_thread_.joinable()) {
        network_thread_.join();
    }
    
    stopAudio();
    network_manager_.disconnect();
    connected_ = false;
    
    std::cout << "Disconnected from server" << std::endl;
}

bool AudioClient::startAudio() {
    if (!connected_ || audio_active_) return false;

    if (!audio_processor_.initialize()) {
        std::cerr << "Failed to initialize audio processor" << std::endl;
        return false;
    }

    // Set up audio capture callback
    audio_processor_.setAudioCaptureCallback(
        [this](const float* data, size_t samples) {
            onAudioCaptured(data, samples);
        }
    );

    if (!audio_processor_.startRecording()) {
        std::cerr << "Failed to start recording" << std::endl;
        return false;
    }

    if (!audio_processor_.startPlayback()) {
        std::cerr << "Failed to start playback" << std::endl;
        audio_processor_.stop();
        return false;
    }

    // Send ready message to server
    Message ready_msg;
    ready_msg.type = MessageType::CLIENT_READY;
    ready_msg.size = 0;
    network_manager_.sendMessage(ready_msg);

    audio_active_ = true;
    std::cout << "Audio system started - you can now speak!" << std::endl;
    return true;
}

void AudioClient::stopAudio() {
    if (!audio_active_) return;
    
    audio_processor_.stop();
    audio_processor_.cleanup();
    audio_active_ = false;
    
    std::cout << "Audio system stopped" << std::endl;
}

bool AudioClient::isConnected() const {
    return connected_;
}

bool AudioClient::isAudioActive() const {
    return audio_active_;
}

void AudioClient::run() {
    std::cout << "AudSync Client" << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  start - Start audio streaming" << std::endl;
    std::cout << "  stop  - Stop audio streaming" << std::endl;
    std::cout << "  quit  - Disconnect and exit" << std::endl;

    std::string command;
    while (running_ && std::cin >> command) {
        if (command == "start") {
            if (!audio_active_) {
                startAudio();
            } else {
                std::cout << "Audio already active" << std::endl;
            }
        } else if (command == "stop") {
            if (audio_active_) {
                stopAudio();
            } else {
                std::cout << "Audio not active" << std::endl;
            }
        } else if (command == "quit") {
            break;
        } else {
            std::cout << "Unknown command: " << command << std::endl;
        }
    }
}

void AudioClient::handleNetworkMessage(const Message& message, int socket_fd) {
    (void)socket_fd; // Unused in client mode

    switch (message.type) {
        case MessageType::AUDIO_DATA:
            if (audio_active_ && message.size > 0) {
                // Convert received data to float and add to playback buffer
                const float* audio_data = reinterpret_cast<const float*>(message.data.data());
                size_t samples = message.size / sizeof(float);
                audio_processor_.addPlaybackData(audio_data, samples);
            }
            break;
            
        case MessageType::HEARTBEAT:
            // Respond to heartbeat
            {
                Message response;
                response.type = MessageType::HEARTBEAT;
                response.size = 0;
                network_manager_.sendMessage(response);
            }
            break;
            
        default:
            break;
    }
}

void AudioClient::onAudioCaptured(const float* data, size_t samples) {
    if (!connected_ || !audio_active_) return;

    // Send audio data to server
    Message audio_msg;
    audio_msg.type = MessageType::AUDIO_DATA;
    audio_msg.size = samples * sizeof(float);
    audio_msg.data.resize(audio_msg.size);
    
    memcpy(audio_msg.data.data(), data, audio_msg.size);
    network_manager_.sendMessage(audio_msg);
}

void AudioClient::networkLoop() {
    while (running_) {
        Message message;
        if (network_manager_.receiveMessage(message)) {
            handleNetworkMessage(message, -1);
        } else {
            // Connection lost
            std::cout << "Connection to server lost" << std::endl;
            running_ = false;
            break;
        }
    }
}

