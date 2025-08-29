#pragma once

#include <portaudio.h>
#include <vector>
#include <functional>
#include <atomic>
#include <string>
#include <mutex>
#include <queue>

/**
 * Module 4: RenderSink
 * Responsible for playing audio through speakers using PortAudio
 * Follows standard API: RenderSinkInit(), RenderSinkDeinit(), RenderSinkProcess()
 */
class RenderSink {
public:
    // Standard module API
    bool RenderSinkInit(int deviceId = -1, int sampleRate = 48000, int channels = 2, int framesPerBuffer = 256);
    void RenderSinkDeinit();
    bool RenderSinkProcess();

    // Audio playback control
    bool startPlayback();
    void stopPlayback();
    
    // Audio data management
    bool queueAudioData(const float* audioData, size_t samples, uint64_t timestamp);
    void clearBuffer();
    
    // Volume and muting
    void setVolume(float volume); // 0.0 to 1.0
    float getVolume() const { return volume_; }
    void setMuted(bool muted);
    bool isMuted() const { return isMuted_; }
    
    // Utility functions
    static std::vector<std::string> getAvailableDevices();
    static int getDefaultDevice();
    
    // Status
    bool isInitialized() const { return isInitialized_; }
    bool isPlaying() const { return isPlaying_; }
    size_t getBufferSize() const;

private:
    // PortAudio stream
    PaStream* stream_ = nullptr;
    bool isInitialized_ = false;
    bool isPlaying_ = false;
    
    // Audio configuration
    int deviceId_ = -1;
    int sampleRate_ = 48000;
    int channels_ = 2;
    int framesPerBuffer_ = 256;
    
    // Audio buffer
    std::queue<std::vector<float>> audioQueue_;
    std::mutex audioQueueMutex_;
    std::vector<float> currentBuffer_;
    size_t currentBufferPos_ = 0;
    
    // Volume control
    std::atomic<float> volume_{1.0f};
    std::atomic<bool> isMuted_{false};
    
    // Buffer configuration - optimized for quality and stability
    size_t maxBufferMs_ = 100; // Increased buffer for better quality (was 75ms)
    
    // Internal PortAudio callback
    static int audioCallback(const void* inputBuffer, void* outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void* userData);
    
    // Helper functions
    bool fillOutputBuffer(float* outputBuffer, size_t frames);
    void applyVolumeAndMuting(float* buffer, size_t samples);
    bool isRealSpeaker(const std::string& deviceName);
};
