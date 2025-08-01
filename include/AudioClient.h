#pragma once

#include "NetworkManager.h"
#include "AudioProcessor.h"
#include "SessionLogger.h"
#include "AudioRecorder.h"
#include "JitterBuffer.h"
#include <string>
#include <atomic>
#include <thread>
#include <vector>

class AudioClient {
  public:
    AudioClient(int inputDeviceId,
                int outputDeviceId,
                int sampleRate,
                int channels,
                int framesPerBuffer,
                SessionLogger* logger,
                AudioRecorder* recorder,
                JitterBuffer* jitterBuffer);
    ~AudioClient();

    bool connect(const std::string& server_host, int server_port);
    void disconnect();

    bool startAudio();
    void stopAudio();

    bool isConnected() const;
    bool isAudioActive() const;
    void run();

    // Static utilities for device listing and unique filenames
    static std::vector<std::string> getInputDeviceNames();
    static std::vector<std::string> getOutputDeviceNames();
    static std::string generateUniqueFilename(const std::string& prefix, const std::string& ext);

  private:
    NetworkManager network_manager_;
    AudioProcessor audio_processor_;

    SessionLogger* logger_;
    AudioRecorder* recorder_;
    JitterBuffer* jitterBuffer_;

    int inputDeviceId_;
    int outputDeviceId_;
    int sampleRate_;
    int channels_;
    int framesPerBuffer_;

    std::atomic<bool> connected_;
    std::atomic<bool> audio_active_;
    std::atomic<bool> running_;
    
    std::thread network_thread_;
    
    void handleNetworkMessage(const Message& message, int socket_fd);
    void onAudioCaptured(const float* data, size_t samples);
    void networkLoop();
};