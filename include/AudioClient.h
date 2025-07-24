#pragma once

#include "NetworkManager.h"
#include "AudioProcessor.h"
#include <string>
#include <atomic>
#include <thread>

class AudioClient {
  public:
    AudioClient();
    ~AudioClient();

    bool connect(const std::string& server_host, int server_port);
    void disconnect();

    bool startAudio();
    void stopAudio();

    bool isConnected() const;
    bool isAudioActive() const;
    void run(); // Main client loop
  private:
    NetworkManager network_manager_;
    AudioProcessor audio_processor_;

    std::atomic<bool> connected_;
    std::atomic<bool> audio_active_;
    std::atomic<bool> running_;
    
    std::thread network_thread_;
    
    void handleNetworkMessage(const Message& message, int socket_fd);
    void onAudioCaptured(const float* data, size_t samples);
    void networkLoop();
};

