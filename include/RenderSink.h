#pragma once

#include <functional>
#include <vector>
#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
#include <memory>
#include <cstdint>
#include <condition_variable>
#include <portaudio.h>

// Forward declarations
struct RenderSinkConfig;
struct RenderSinkStats;

// Audio data structure for playback queue
struct PlaybackAudioData {
    std::vector<float> audioSamples;
    uint64_t timestamp;
    uint32_t sampleRate;
    uint16_t channels;
    bool isValid;
};

// Callback for playback events
using PlaybackEventCallback = std::function<void(const std::string& event, const RenderSinkStats& stats)>;

// Callback for audio data request (pull mode)
using AudioRequestCallback = std::function<size_t(float* buffer, size_t maxSamples, uint64_t& timestamp)>;

/**
 * @brief RenderSink Module - Handles audio playback through speakers
 * 
 * This module manages audio output devices, handles playback buffering,
 * provides volume control, and ensures smooth audio playback.
 */
class RenderSink {
public:
    RenderSink();
    ~RenderSink();

    // ===== STANDARD MODULE API =====
    
    /**
     * @brief Initialize the render sink
     * @param config Configuration parameters for audio playback
     * @return true if initialization successful, false otherwise
     */
    bool RenderSinkInit(const RenderSinkConfig& config);
    
    /**
     * @brief Deinitialize and cleanup the render sink
     * @return true if cleanup successful, false otherwise
     */
    bool RenderSinkDeinit();
    
    /**
     * @brief Process audio playback (called per frame)
     * @return true if processing successful, false if error occurred
     */
    bool RenderSinkProcess();

    // ===== AUDIO PLAYBACK CONTROL =====
    
    /**
     * @brief Start audio playback
     * @return true if playback started successfully
     */
    bool startPlayback();
    
    /**
     * @brief Stop audio playback
     * @return true if playback stopped successfully
     */
    bool stopPlayback();
    
    /**
     * @brief Pause audio playback
     * @return true if playback paused successfully
     */
    bool pausePlayback();
    
    /**
     * @brief Resume audio playback
     * @return true if playback resumed successfully
     */
    bool resumePlayback();
    
    /**
     * @brief Check if currently playing audio
     * @return true if actively playing
     */
    bool isPlaying() const;

    // ===== AUDIO DATA HANDLING =====
    
    /**
     * @brief Queue audio data for playback (push mode)
     * @param audioData Pointer to audio samples
     * @param samples Number of audio samples
     * @param timestamp Timestamp of the audio data
     * @return true if data queued successfully
     */
    bool queueAudioData(const float* audioData, size_t samples, uint64_t timestamp);
    
    /**
     * @brief Set callback for audio data requests (pull mode)
     * @param callback Function to call when more audio data is needed
     */
    void setAudioRequestCallback(AudioRequestCallback callback);
    
    /**
     * @brief Clear all queued audio data
     * @return true if queue cleared successfully
     */
    bool clearAudioQueue();

    // ===== DEVICE MANAGEMENT =====
    
    /**
     * @brief Get list of available output devices
     * @return Vector of device names with IDs
     */
    static std::vector<std::string> getAvailableOutputDevices();
    
    /**
     * @brief Set output device
     * @param deviceId Device ID (-1 for default)
     * @return true if device set successfully
     */
    bool setOutputDevice(int deviceId);
    
    /**
     * @brief Get current output device info
     * @return Device information string
     */
    std::string getCurrentDeviceInfo() const;

    // ===== VOLUME AND EFFECTS =====
    
    /**
     * @brief Set master volume
     * @param volume Volume level (0.0 to 1.0)
     * @return true if volume set successfully
     */
    bool setVolume(float volume);
    
    /**
     * @brief Get current volume
     * @return Current volume level (0.0 to 1.0)
     */
    float getVolume() const;
    
    /**
     * @brief Enable/disable audio mute
     * @param muted true to mute, false to unmute
     * @return true if mute state changed successfully
     */
    bool setMuted(bool muted);
    
    /**
     * @brief Check if audio is muted
     * @return true if currently muted
     */
    bool isMuted() const;

    // ===== CONFIGURATION & MONITORING =====
    
    /**
     * @brief Set callback for playback events
     * @param callback Function to call on playback events
     */
    void setPlaybackEventCallback(PlaybackEventCallback callback);
    
    /**
     * @brief Get current playback statistics
     * @return Statistics about playback performance
     */
    RenderSinkStats getStats() const;
    
    /**
     * @brief Configure playback buffer size
     * @param bufferSizeMs Buffer size in milliseconds
     * @return true if buffer size set successfully
     */
    bool configurePlaybackBuffer(int bufferSizeMs);

private:
    // PortAudio callback - static function
    static int portAudioCallback(const void* inputBuffer, void* outputBuffer,
                                unsigned long framesPerBuffer,
                                const PaStreamCallbackTimeInfo* timeInfo,
                                PaStreamCallbackFlags statusFlags,
                                void* userData);

    // Audio processing and buffering
    void processPlaybackQueue();
    int fillOutputBuffer(float* outputBuffer, unsigned long framesPerBuffer);
    void applyVolumeAndEffects(float* audioData, size_t samples);
    void handlePlaybackUnderrun();
    void generateSilence(float* buffer, size_t samples);
    
    // Device management
    bool openAudioDevice();
    void closeAudioDevice();
    bool validateDeviceCompatibility(int deviceId);
    
    // Statistics and monitoring
    void updatePlaybackStats();
    void calculateLatencyAndJitter();
    
    // Member variables
    PaStream* stream_;
    RenderSinkConfig* config_;
    
    // Threading and synchronization
    std::atomic<bool> isInitialized_;
    std::atomic<bool> isPlaying_;
    std::atomic<bool> isPaused_;
    std::atomic<bool> processingActive_;
    
    // Audio queue management
    std::queue<PlaybackAudioData> audioQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    std::atomic<size_t> queueSize_;
    std::atomic<size_t> maxQueueSize_;
    
    // Current audio state
    std::vector<float> currentBuffer_;
    std::atomic<size_t> currentBufferPosition_;
    std::atomic<bool> bufferUnderrun_;
    
    // Volume and effects
    std::atomic<float> masterVolume_;
    std::atomic<bool> isMuted_;
    
    // Device configuration
    std::atomic<int> outputDeviceId_;
    std::atomic<int> sampleRate_;
    std::atomic<int> channels_;
    std::atomic<int> framesPerBuffer_;
    std::string currentDeviceName_;
    
    // Statistics
    mutable std::atomic<uint64_t> totalSamplesPlayed_;
    mutable std::atomic<uint64_t> totalUnderruns_;
    mutable std::atomic<uint64_t> totalDroppedSamples_;
    mutable std::atomic<double> averageLatency_;
    mutable std::atomic<double> cpuLoad_;
    mutable std::atomic<uint64_t> lastPlaybackTime_;
    mutable std::atomic<uint64_t> playbackStartTime_;
    
    // Callbacks
    PlaybackEventCallback playbackEventCallback_;
    AudioRequestCallback audioRequestCallback_;
    
    // Timing and synchronization
    std::atomic<uint64_t> lastCallbackTime_;
    std::atomic<double> playbackRate_;
    std::atomic<int> targetBufferSizeMs_;
    std::atomic<int> currentBufferSizeMs_;
};

/**
 * @brief Configuration structure for RenderSink
 */
struct RenderSinkConfig {
    int outputDeviceId = -1;            // -1 for default device
    int sampleRate = 44100;             // Sample rate in Hz
    int channels = 1;                   // Number of channels (1=mono, 2=stereo)
    int framesPerBuffer = 256;          // Frames per buffer
    int playbackBufferSizeMs = 50;      // Playback buffer size in milliseconds
    int maxQueueSizeMs = 200;           // Maximum queue size in milliseconds
    float initialVolume = 1.0f;         // Initial volume (0.0 to 1.0)
    bool enableLowLatency = true;       // Use low latency mode
    float suggestedLatency = 0.01f;     // Suggested latency in seconds
    bool enableVolumeControl = true;    // Enable volume control
    bool enableUnderrunRecovery = true; // Enable underrun recovery
    int underrunThresholdMs = 10;       // Underrun detection threshold
    bool enableLatencyMonitoring = true; // Enable latency monitoring
};

/**
 * @brief Statistics structure for RenderSink
 */
struct RenderSinkStats {
    uint64_t totalSamplesPlayed = 0;
    uint64_t totalUnderruns = 0;
    uint64_t totalDroppedSamples = 0;
    uint64_t queuedSamples = 0;
    uint64_t queuedSamplesMs = 0;
    double averageLatency = 0.0;        // Average latency in ms
    double currentLatency = 0.0;        // Current device latency in ms
    double cpuLoad = 0.0;               // CPU load percentage
    double playbackRate = 0.0;          // Samples per second
    float currentVolume = 1.0f;
    bool isPlaying = false;
    bool isPaused = false;
    bool isMuted = false;
    bool hasUnderrun = false;
    uint64_t playbackDurationMs = 0;
    std::string deviceName;
    int deviceSampleRate = 0;
    int deviceChannels = 0;
};