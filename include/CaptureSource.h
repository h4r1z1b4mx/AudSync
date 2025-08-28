#pragma once

#include <portaudio.h>
#include <vector>
#include <functional>
#include <atomic>
#include <string>

/**
 * Module 1: CaptureSource
 * Responsible for capturing audio from microphone using PortAudio
 * Follows standard API: CaptureSourceInit(), CaptureSourceDeinit(), CaptureSourceProcess()
 */
class CaptureSource {
public:
    // Standard module API
    bool CaptureSourceInit(int deviceId = -1, int sampleRate = 48000, int channels = 2, int framesPerBuffer = 256);
    void CaptureSourceDeinit();
    bool CaptureSourceProcess();

    // Configuration and callbacks
    void setCaptureCallback(std::function<void(const float*, size_t, uint64_t)> callback);
    
    // Utility functions
    static std::vector<std::string> getAvailableDevices();
    static int getDefaultDevice();
    
    // Status
    bool isInitialized() const { return isInitialized_; }
    bool isCapturing() const { return isCapturing_; }
    
    // Start/Stop capture
    bool startCapture();
    void stopCapture();
    
    // Volume and mute controls
    void setVolume(float volume); // 0.0 to 1.0
    void setMuted(bool muted);
    float getVolume() const { return volume_.load(); }
    bool isMuted() const { return isMuted_.load(); }

private:
    // PortAudio stream
    PaStream* stream_ = nullptr;
    bool isInitialized_ = false;
    bool isCapturing_ = false;
    
    // Audio configuration
    int deviceId_ = -1;
    int sampleRate_ = 48000;
    int channels_ = 2;
    int framesPerBuffer_ = 256;
    
    // Callback for captured audio
    std::function<void(const float*, size_t, uint64_t)> captureCallback_;
    
    // Volume and mute controls
    std::atomic<float> volume_{0.7f};
    std::atomic<bool> isMuted_{false};
    
    // Internal PortAudio callback
    static int audioCallback(const void* inputBuffer, void* outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void* userData);
    
    // Helper functions
    void processAudioData(const float* data, size_t samples);
    bool isRealMicrophone(const std::string& deviceName);
};
