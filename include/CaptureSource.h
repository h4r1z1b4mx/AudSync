#pragma once

#include <functional>
#include <vector>
#include <atomic>
#include <string>
#include <portaudio.h>

// Forward declarations
struct CaptureSourceConfig;
struct CaptureSourceStats;

// Callback type for captured audio data
using CaptureCallback = std::function<void(const float* audioData, size_t samples, uint64_t timestamp)>;

/**
 * @brief CaptureSource Module - Handles microphone audio capture
 * 
 * This module encapsulates all microphone capture functionality using PortAudio.
 * It provides a clean API for initializing, processing, and managing audio capture.
 */
class CaptureSource {
public:
    CaptureSource();
    ~CaptureSource();

    // ===== STANDARD MODULE API =====
    
    /**
     * @brief Initialize the capture source
     * @param config Configuration parameters for audio capture
     * @return true if initialization successful, false otherwise
     */
    bool CaptureSourceInit(const CaptureSourceConfig& config);
    
    /**
     * @brief Deinitialize and cleanup the capture source
     * @return true if cleanup successful, false otherwise
     */
    bool CaptureSourceDeinit();
    
    /**
     * @brief Process captured audio (called per frame/buffer)
     * @return true if processing successful, false if error occurred
     */
    bool CaptureSourceProcess();

    // ===== CONFIGURATION & CONTROL =====
    
    /**
     * @brief Set callback for captured audio data
     * @param callback Function to call when audio is captured
     */
    void setCaptureCallback(CaptureCallback callback);
    
    /**
     * @brief Start audio capture
     * @return true if started successfully
     */
    bool startCapture();
    
    /**
     * @brief Stop audio capture
     * @return true if stopped successfully
     */
    bool stopCapture();
    
    /**
     * @brief Check if capture is currently active
     * @return true if capturing audio
     */
    bool isCapturing() const;

    // ===== DEVICE MANAGEMENT =====
    
    /**
     * @brief Get list of available input devices
     * @return Vector of device names with IDs
     */
    static std::vector<std::string> getAvailableDevices();
    
    /**
     * @brief Get current capture statistics
     * @return Statistics about capture performance
     */
    CaptureSourceStats getStats() const;

private:
    // PortAudio callback - static function
    static int portAudioCallback(const void* inputBuffer, void* outputBuffer,
                                unsigned long framesPerBuffer,
                                const PaStreamCallbackTimeInfo* timeInfo,
                                PaStreamCallbackFlags statusFlags,
                                void* userData);

    // Internal processing
    void processAudioData(const float* data, size_t samples);
    
    // Member variables
    PaStream* stream_;
    CaptureSourceConfig* config_;
    CaptureCallback captureCallback_;
    std::atomic<bool> isCapturing_;
    std::atomic<bool> isInitialized_;
    
    // Statistics
    mutable std::atomic<uint64_t> totalFramesProcessed_;
    mutable std::atomic<uint64_t> totalDroppedFrames_;
    mutable std::atomic<uint64_t> lastProcessTime_;
};

/**
 * @brief Configuration structure for CaptureSource
 */
struct CaptureSourceConfig {
    int deviceId = -1;              // -1 for default device
    int sampleRate = 44100;         // Sample rate in Hz
    int channels = 1;               // Number of channels (1=mono, 2=stereo)
    int framesPerBuffer = 256;      // Frames per buffer
    bool enableLowLatency = true;   // Use low latency mode
    float suggestedLatency = 0.01f; // Suggested latency in seconds
};

/**
 * @brief Statistics structure for CaptureSource
 */
struct CaptureSourceStats {
    uint64_t totalFramesProcessed = 0;
    uint64_t totalDroppedFrames = 0;
    uint64_t lastProcessTime = 0;
    bool isActive = false;
    double currentLatency = 0.0;
    double cpuLoad = 0.0;
};