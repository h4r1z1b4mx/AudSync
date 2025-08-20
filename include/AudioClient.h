#pragma once

#include "NetworkManager.h"
#include "AudioProcessor.h"
#include "SessionLogger.h"
#include "AudioRecorder.h"
#include "JitterBuffer.h"
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <chrono>

class AudioClient {
public:
    AudioClient(int inputDeviceId = -1,
                int outputDeviceId = -1,
                int sampleRate = 44100,
                int channels = 2,
                int framesPerBuffer = 256,
                SessionLogger* logger = nullptr,
                AudioRecorder* recorder = nullptr,
                JitterBuffer* jitterBuffer = nullptr);
    
    ~AudioClient();
    
    bool connect(const std::string& server_host, int server_port);
    void disconnect();
    bool startAudio();
    void stopAudio();
    bool isConnected() const;
    bool isAudioActive() const;
    void run();
    
    static std::vector<std::string> getInputDeviceNames();
    static std::vector<std::string> getOutputDeviceNames();

private:
    // Audio settings
    int inputDeviceId_;
    int outputDeviceId_;
    int sampleRate_;
    int channels_;
    int framesPerBuffer_;
    
    // Components
    NetworkManager network_manager_;
    AudioProcessor audio_processor_;
    SessionLogger* logger_;
    AudioRecorder* recorder_;
    JitterBuffer* jitterBuffer_;
    
    // Sequence tracking
    uint32_t outgoingSequenceNumber_;
    uint32_t incomingSequenceNumber_;
    bool jitterBufferReady_;
    
    // State management
    std::atomic<bool> connected_;
    std::atomic<bool> audio_active_;
    std::atomic<bool> running_;
    std::atomic<bool> jitterBufferRunning_;
    
    // Threading
    std::thread network_thread_;
    std::thread jitterBufferThread_;
    
    std::chrono::steady_clock::time_point lastPacketTime_;

    // Audio filter state (thread-safe per instance)
    struct FilterState {  // ✅ ADDED: Proper filter state management
        float hp_last = 0.0f;
        float lp_last = 0.0f;
        float de_esser_last = 0.0f;
    } filterState_;


    // Private methods
    void handleNetworkMessage(const Message& message, int socket_fd);
    void onAudioCaptured(const float* data, size_t samples);
    void networkLoop();
    void jitterBufferLoop();
    void processJitterBuffer();
    
    // Audio filter method declarations
    void applyVoiceFilters(float* data, size_t samples);
    void applyNoiseGate(float* data, size_t samples);
    void applyVoiceEQ(float* data, size_t samples);
    void applyCompressor(float* data, size_t samples);
    void applyDeEsser(float* data, size_t samples);
};