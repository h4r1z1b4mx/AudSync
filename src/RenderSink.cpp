#include "RenderSink.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <sstream>

RenderSink::RenderSink()
    : stream_(nullptr),
      config_(nullptr),
      isInitialized_(false),
      isPlaying_(false),
      isPaused_(false),
      processingActive_(false),
      queueSize_(0),
      maxQueueSize_(0),
      currentBufferPosition_(0),
      bufferUnderrun_(false),
      masterVolume_(1.0f),
      isMuted_(false),
      outputDeviceId_(-1),
      sampleRate_(44100),
      channels_(1),
      framesPerBuffer_(256),
      totalSamplesPlayed_(0),
      totalUnderruns_(0),
      totalDroppedSamples_(0),
      averageLatency_(0.0),
      cpuLoad_(0.0),
      lastPlaybackTime_(0),
      playbackStartTime_(0),
      lastCallbackTime_(0),
      playbackRate_(0.0),
      targetBufferSizeMs_(50),
      currentBufferSizeMs_(0) {
}

RenderSink::~RenderSink() {
    RenderSinkDeinit();
}

bool RenderSink::RenderSinkInit(const RenderSinkConfig& config) {
    if (isInitialized_) {
        std::cerr << "RenderSink: Already initialized" << std::endl;
        return false;
    }

    // Store configuration
    config_ = new RenderSinkConfig(config);
    outputDeviceId_ = config.outputDeviceId;
    sampleRate_ = config.sampleRate;
    channels_ = config.channels;
    framesPerBuffer_ = config.framesPerBuffer;
    masterVolume_ = config.initialVolume;
    targetBufferSizeMs_ = config.playbackBufferSizeMs;

    // Calculate max queue size in samples
    maxQueueSize_ = (config.maxQueueSizeMs * sampleRate_ * channels_) / 1000;

    // Initialize PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "RenderSink: Failed to initialize PortAudio: " 
                  << Pa_GetErrorText(err) << std::endl;
        delete config_;
        config_ = nullptr;
        return false;
    }

    // Open audio device
    if (!openAudioDevice()) {
        Pa_Terminate();
        delete config_;
        config_ = nullptr;
        return false;
    }

    // Initialize buffer
    currentBuffer_.resize(framesPerBuffer_ * channels_, 0.0f);
    currentBufferPosition_ = 0;

    isInitialized_ = true;
    
    std::cout << "RenderSink: Initialized successfully" << std::endl;
    std::cout << "  Device: " << currentDeviceName_ << std::endl;
    std::cout << "  Sample Rate: " << sampleRate_ << "Hz" << std::endl;
    std::cout << "  Channels: " << channels_ << std::endl;
    std::cout << "  Buffer Size: " << framesPerBuffer_ << " frames" << std::endl;
    std::cout << "  Playback Buffer: " << targetBufferSizeMs_ << "ms" << std::endl;
    std::cout << "  Initial Volume: " << masterVolume_.load() << std::endl;

    return true;
}

bool RenderSink::RenderSinkDeinit() {
    if (!isInitialized_) {
        return true;
    }

    // Stop playback
    stopPlayback();

    // Close audio device
    closeAudioDevice();

    // Clear audio queue
    clearAudioQueue();

    // Terminate PortAudio
    Pa_Terminate();

    // Cleanup configuration
    delete config_;
    config_ = nullptr;

    isInitialized_ = false;
    
    std::cout << "RenderSink: Deinitialized" << std::endl;
    return true;
}

bool RenderSink::RenderSinkProcess() {
    if (!isInitialized_) {
        return false;
    }

    uint64_t currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();

    // Update statistics
    updatePlaybackStats();

    // Check for underruns
    if (config_->enableUnderrunRecovery && bufferUnderrun_) {
        handlePlaybackUnderrun();
        bufferUnderrun_ = false;
    }

    // Update latency measurements
    if (config_->enableLatencyMonitoring) {
        calculateLatencyAndJitter();
    }

    // Update queue size in milliseconds
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (sampleRate_ > 0 && channels_ > 0) {
            size_t totalSamples = 0;
            std::queue<PlaybackAudioData> tempQueue = audioQueue_;
            while (!tempQueue.empty()) {
                totalSamples += tempQueue.front().audioSamples.size();
                tempQueue.pop();
            }
            currentBufferSizeMs_ = (totalSamples * 1000) / (sampleRate_ * channels_);
        }
    }

    // Process queued audio if not using callback mode
    if (!audioRequestCallback_) {
        processPlaybackQueue();
    }

    lastPlaybackTime_ = currentTime;
    return true;
}

bool RenderSink::startPlayback() {
    if (!isInitialized_ || isPlaying_) {
        return false;
    }

    if (!stream_) {
        std::cerr << "RenderSink: No audio stream available" << std::endl;
        return false;
    }

    PaError err = Pa_StartStream(stream_);
    if (err != paNoError) {
        std::cerr << "RenderSink: Failed to start stream: " 
                  << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    isPlaying_ = true;
    isPaused_ = false;
    processingActive_ = true;
    
    playbackStartTime_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();

    if (playbackEventCallback_) {
        playbackEventCallback_("Playback started", getStats());
    }

    std::cout << "RenderSink: Playback started" << std::endl;
    return true;
}

bool RenderSink::stopPlayback() {
    if (!isInitialized_ || !isPlaying_) {
        return true;
    }

    if (stream_) {
        PaError err = Pa_StopStream(stream_);
        if (err != paNoError) {
            std::cerr << "RenderSink: Error stopping stream: " 
                      << Pa_GetErrorText(err) << std::endl;
            return false;
        }
    }

    isPlaying_ = false;
    isPaused_ = false;
    processingActive_ = false;
    
    // Clear current buffer
    currentBufferPosition_ = 0;
    std::fill(currentBuffer_.begin(), currentBuffer_.end(), 0.0f);

    if (playbackEventCallback_) {
        playbackEventCallback_("Playback stopped", getStats());
    }

    std::cout << "RenderSink: Playback stopped" << std::endl;
    return true;
}

bool RenderSink::pausePlayback() {
    if (!isInitialized_ || !isPlaying_ || isPaused_) {
        return false;
    }

    isPaused_ = true;
    processingActive_ = false;

    if (playbackEventCallback_) {
        playbackEventCallback_("Playback paused", getStats());
    }

    std::cout << "RenderSink: Playback paused" << std::endl;
    return true;
}

bool RenderSink::resumePlayback() {
    if (!isInitialized_ || !isPlaying_ || !isPaused_) {
        return false;
    }

    isPaused_ = false;
    processingActive_ = true;

    if (playbackEventCallback_) {
        playbackEventCallback_("Playback resumed", getStats());
    }

    std::cout << "RenderSink: Playback resumed" << std::endl;
    return true;
}

bool RenderSink::isPlaying() const {
    return isPlaying_.load() && !isPaused_.load();
}

bool RenderSink::queueAudioData(const float* audioData, size_t samples, uint64_t timestamp) {
    if (!isInitialized_ || !audioData || samples == 0) {
        return false;
    }

    PlaybackAudioData playbackData;
    playbackData.audioSamples.assign(audioData, audioData + samples);
    playbackData.timestamp = timestamp;
    playbackData.sampleRate = sampleRate_;
    playbackData.channels = channels_;
    playbackData.isValid = true;

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        
        // Check queue size limit
        if (queueSize_ + samples > maxQueueSize_) {
            // Drop oldest audio data to make room
            while (!audioQueue_.empty() && queueSize_ + samples > maxQueueSize_) {
                queueSize_ -= audioQueue_.front().audioSamples.size();
                audioQueue_.pop();
                totalDroppedSamples_ += audioQueue_.front().audioSamples.size();
            }
        }

        audioQueue_.push(playbackData);
        queueSize_ += samples;
    }

    queueCondition_.notify_one();
    return true;
}

void RenderSink::setAudioRequestCallback(AudioRequestCallback callback) {
    audioRequestCallback_ = callback;
}

bool RenderSink::clearAudioQueue() {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!audioQueue_.empty()) {
            audioQueue_.pop();
        }
        queueSize_ = 0;
    }

    // Clear current buffer
    currentBufferPosition_ = 0;
    std::fill(currentBuffer_.begin(), currentBuffer_.end(), 0.0f);

    queueCondition_.notify_all();
    
    std::cout << "RenderSink: Audio queue cleared" << std::endl;
    return true;
}

std::vector<std::string> RenderSink::getAvailableOutputDevices() {
    std::vector<std::string> devices;
    
    // Initialize PortAudio temporarily for device enumeration
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        return devices;
    }

    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxOutputChannels > 0) {
            // Test if device is actually usable
            PaStreamParameters outputParams;
            outputParams.device = i;
            outputParams.channelCount = std::min(2, info->maxOutputChannels);
            outputParams.sampleFormat = paFloat32;
            outputParams.suggestedLatency = info->defaultLowOutputLatency;
            outputParams.hostApiSpecificStreamInfo = nullptr;

            PaStream* testStream;
            err = Pa_OpenStream(&testStream, nullptr, &outputParams, 
                              info->defaultSampleRate, 256, paClipOff, nullptr, nullptr);

            if (err == paNoError) {
                Pa_CloseStream(testStream);
                std::ostringstream oss;
                oss << "[" << i << "] " << info->name 
                    << " (Max: " << info->maxOutputChannels << " ch, "
                    << "Default: " << static_cast<int>(info->defaultSampleRate) << "Hz)";
                devices.push_back(oss.str());
            }
        }
    }

    Pa_Terminate();
    return devices;
}

bool RenderSink::setOutputDevice(int deviceId) {
    if (!isInitialized_) {
        outputDeviceId_ = deviceId;
        return true;
    }

    // Stop current playback
    bool wasPlaying = isPlaying();
    if (wasPlaying) {
        stopPlayback();
    }

    // Close current device
    closeAudioDevice();

    // Set new device
    outputDeviceId_ = deviceId;

    // Reopen with new device
    bool success = openAudioDevice();
    
    // Restart playback if it was running
    if (success && wasPlaying) {
        startPlayback();
    }

    if (success && playbackEventCallback_) {
        playbackEventCallback_("Output device changed", getStats());
    }

    return success;
}

std::string RenderSink::getCurrentDeviceInfo() const {
    return currentDeviceName_;
}

bool RenderSink::setVolume(float volume) {
    if (volume < 0.0f || volume > 1.0f) {
        return false;
    }

    masterVolume_ = volume;
    
    if (playbackEventCallback_) {
        playbackEventCallback_("Volume changed", getStats());
    }

    return true;
}

float RenderSink::getVolume() const {
    return masterVolume_.load();
}

bool RenderSink::setMuted(bool muted) {
    bool wasMuted = isMuted_.load();
    isMuted_ = muted;
    
    if (wasMuted != muted && playbackEventCallback_) {
        playbackEventCallback_(muted ? "Audio muted" : "Audio unmuted", getStats());
    }

    return true;
}

bool RenderSink::isMuted() const {
    return isMuted_.load();
}

void RenderSink::setPlaybackEventCallback(PlaybackEventCallback callback) {
    playbackEventCallback_ = callback;
}

RenderSinkStats RenderSink::getStats() const {
    RenderSinkStats stats;
    
    stats.totalSamplesPlayed = totalSamplesPlayed_.load();
    stats.totalUnderruns = totalUnderruns_.load();
    stats.totalDroppedSamples = totalDroppedSamples_.load();
    stats.queuedSamples = queueSize_.load();
    stats.queuedSamplesMs = currentBufferSizeMs_.load();
    stats.averageLatency = averageLatency_.load();
    stats.cpuLoad = cpuLoad_.load();
    stats.playbackRate = playbackRate_.load();
    stats.currentVolume = masterVolume_.load();
    stats.isPlaying = isPlaying_.load();
    stats.isPaused = isPaused_.load();
    stats.isMuted = isMuted_.load();
    stats.hasUnderrun = bufferUnderrun_.load();
    stats.deviceName = currentDeviceName_;
    stats.deviceSampleRate = sampleRate_.load();
    stats.deviceChannels = channels_.load();

    // Calculate current device latency
    if (stream_) {
        const PaStreamInfo* streamInfo = Pa_GetStreamInfo(stream_);
        if (streamInfo) {
            stats.currentLatency = streamInfo->outputLatency * 1000.0; // Convert to ms
        }
    }

    // Calculate playback duration
    if (playbackStartTime_ > 0) {
        uint64_t currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();
        stats.playbackDurationMs = currentTime - playbackStartTime_;
    }

    return stats;
}

bool RenderSink::configurePlaybackBuffer(int bufferSizeMs) {
    if (bufferSizeMs <= 0) {
        return false;
    }

    targetBufferSizeMs_ = bufferSizeMs;
    
    // Recalculate max queue size
    maxQueueSize_ = (bufferSizeMs * sampleRate_ * channels_) / 1000;

    std::cout << "RenderSink: Playback buffer configured to " << bufferSizeMs << "ms" << std::endl;
    return true;
}

// Static PortAudio callback
int RenderSink::portAudioCallback(const void* inputBuffer, void* outputBuffer,
                                 unsigned long framesPerBuffer,
                                 const PaStreamCallbackTimeInfo* timeInfo,
                                 PaStreamCallbackFlags statusFlags,
                                 void* userData) {
    (void)inputBuffer; // Unused
    (void)timeInfo;    // Could be used for timing

    RenderSink* renderSink = static_cast<RenderSink*>(userData);
    
    if (!renderSink || !outputBuffer) {
        return paAbort;
    }

    // Handle status flags
    if (statusFlags & paOutputUnderflow) {
        renderSink->totalUnderruns_++;
        renderSink->bufferUnderrun_ = true;
    }

    float* output = static_cast<float*>(outputBuffer);
    
    // Fill output buffer
    int result = renderSink->fillOutputBuffer(output, framesPerBuffer);
    
    // Update statistics
    renderSink->totalSamplesPlayed_ += framesPerBuffer * renderSink->channels_;
    renderSink->lastCallbackTime_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();

    // Update CPU load
    if (renderSink->stream_) {
        renderSink->cpuLoad_ = Pa_GetStreamCpuLoad(renderSink->stream_) * 100.0;
    }

    return (result == 0) ? paContinue : paComplete;
}

void RenderSink::processPlaybackQueue() {
    if (!processingActive_ || isPaused_) {
        return;
    }

    // This method is called when not using callback mode
    // It processes the queue and manages buffer filling
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    // Process available audio data
    while (!audioQueue_.empty() && currentBufferPosition_ < currentBuffer_.size()) {
        PlaybackAudioData& audioData = audioQueue_.front();
        
        if (audioData.isValid && !audioData.audioSamples.empty()) {
            size_t samplesToCopy = std::min(
                audioData.audioSamples.size(),
                currentBuffer_.size() - currentBufferPosition_
            );
            
            std::copy(audioData.audioSamples.begin(),
                     audioData.audioSamples.begin() + samplesToCopy,
                     currentBuffer_.begin() + currentBufferPosition_);
            
            currentBufferPosition_ += samplesToCopy;
            
            // Remove processed samples
            if (samplesToCopy >= audioData.audioSamples.size()) {
                queueSize_ -= audioData.audioSamples.size();
                audioQueue_.pop();
            } else {
                audioData.audioSamples.erase(
                    audioData.audioSamples.begin(),
                    audioData.audioSamples.begin() + samplesToCopy
                );
                queueSize_ -= samplesToCopy;
            }
        } else {
            // Remove invalid audio data
            queueSize_ -= audioData.audioSamples.size();
            audioQueue_.pop();
        }
    }
}

int RenderSink::fillOutputBuffer(float* outputBuffer, unsigned long framesPerBuffer) {
    if (!processingActive_ || isPaused_ || isMuted_) {
        generateSilence(outputBuffer, framesPerBuffer * channels_);
        return 0;
    }

    size_t totalSamples = framesPerBuffer * channels_;
    size_t samplesWritten = 0;

    // Try to get audio from callback first (pull mode)
    if (audioRequestCallback_) {
        uint64_t timestamp;
        samplesWritten = audioRequestCallback_(outputBuffer, totalSamples, timestamp);
        
        if (samplesWritten > 0) {
            applyVolumeAndEffects(outputBuffer, samplesWritten);
            
            // Fill remainder with silence if needed
            if (samplesWritten < totalSamples) {
                generateSilence(outputBuffer + samplesWritten, totalSamples - samplesWritten);
            }
            return 0;
        }
    }

    // Use queued audio data (push mode)
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    float* outputPtr = outputBuffer;
    size_t remainingSamples = totalSamples;
    
    while (remainingSamples > 0 && !audioQueue_.empty()) {
        PlaybackAudioData& audioData = audioQueue_.front();
        
        if (!audioData.isValid || audioData.audioSamples.empty()) {
            queueSize_ -= audioData.audioSamples.size();
            audioQueue_.pop();
            continue;
        }
        
        size_t samplesToCopy = std::min(remainingSamples, audioData.audioSamples.size());
        
        std::copy(audioData.audioSamples.begin(),
                 audioData.audioSamples.begin() + samplesToCopy,
                 outputPtr);
        
        outputPtr += samplesToCopy;
        remainingSamples -= samplesToCopy;
        samplesWritten += samplesToCopy;
        
        // Remove processed samples
        if (samplesToCopy >= audioData.audioSamples.size()) {
            queueSize_ -= audioData.audioSamples.size();
            audioQueue_.pop();
        } else {
            audioData.audioSamples.erase(
                audioData.audioSamples.begin(),
                audioData.audioSamples.begin() + samplesToCopy
            );
            queueSize_ -= samplesToCopy;
        }
    }
    
    // Fill remainder with silence if we don't have enough data
    if (remainingSamples > 0) {
        generateSilence(outputPtr, remainingSamples);
        bufferUnderrun_ = true;
    }
    
    // Apply volume and effects
    applyVolumeAndEffects(outputBuffer, samplesWritten);
    
    return 0;
}

void RenderSink::applyVolumeAndEffects(float* audioData, size_t samples) {
    if (!config_->enableVolumeControl) {
        return;
    }

    float volume = isMuted_ ? 0.0f : masterVolume_.load();
    
    if (volume != 1.0f) {
        for (size_t i = 0; i < samples; ++i) {
            audioData[i] *= volume;
            
            // Soft clipping to prevent distortion
            if (audioData[i] > 0.95f) {
                audioData[i] = 0.95f + 0.05f * std::tanh((audioData[i] - 0.95f) / 0.05f);
            } else if (audioData[i] < -0.95f) {
                audioData[i] = -0.95f + 0.05f * std::tanh((audioData[i] + 0.95f) / 0.05f);
            }
        }
    }
}

void RenderSink::handlePlaybackUnderrun() {
    std::cout << "RenderSink: Playback underrun detected - recovering" << std::endl;
    
    if (playbackEventCallback_) {
        playbackEventCallback_("Playback underrun", getStats());
    }
    
    // Clear current buffer to start fresh
    currentBufferPosition_ = 0;
    std::fill(currentBuffer_.begin(), currentBuffer_.end(), 0.0f);
}

void RenderSink::generateSilence(float* buffer, size_t samples) {
    std::fill(buffer, buffer + samples, 0.0f);
}

bool RenderSink::openAudioDevice() {
    // Setup output parameters
    PaStreamParameters outputParameters;
    outputParameters.device = (outputDeviceId_ == -1) ? Pa_GetDefaultOutputDevice() : outputDeviceId_.load();
    
    if (outputParameters.device == paNoDevice) {
        std::cerr << "RenderSink: No output device available" << std::endl;
        return false;
    }

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(outputParameters.device);
    if (!deviceInfo) {
        std::cerr << "RenderSink: Invalid output device" << std::endl;
        return false;
    }

    // Validate device compatibility
    if (!validateDeviceCompatibility(outputParameters.device)) {
        std::cerr << "RenderSink: Device not compatible with requested format" << std::endl;
        return false;
    }

    outputParameters.channelCount = channels_;
    outputParameters.sampleFormat = paFloat32;
    outputParameters.suggestedLatency = config_->enableLowLatency ? 
        deviceInfo->defaultLowOutputLatency : config_->suggestedLatency;
    outputParameters.hostApiSpecificStreamInfo = nullptr;

    // Open PortAudio stream with device sharing support
    PaError err = Pa_OpenStream(&stream_,
                                nullptr,  // No input
                                &outputParameters,
                                sampleRate_,
                                framesPerBuffer_,
                                paNoFlag,  // Use paNoFlag for better device sharing
                                &RenderSink::portAudioCallback,
                                this);

    if (err != paNoError) {
        std::cerr << "RenderSink: Failed to open stream: " 
                  << Pa_GetErrorText(err) << std::endl;
        std::cerr << "RenderSink: Error code: " << err << std::endl;
        std::cerr << "RenderSink: This might be due to device sharing conflict" << std::endl;
        return false;
    }

    // Store device information
    currentDeviceName_ = deviceInfo->name;
    
    std::cout << "RenderSink: Opened audio device successfully" << std::endl;
    std::cout << "  Device: " << deviceInfo->name << std::endl;
    std::cout << "  Sample Rate: " << sampleRate_ << "Hz" << std::endl;
    std::cout << "  Channels: " << channels_ << std::endl;
    std::cout << "  Buffer Size: " << framesPerBuffer_ << " frames" << std::endl;
    std::cout << "  Latency: " << outputParameters.suggestedLatency * 1000.0 << "ms" << std::endl;

    return true;
}

void RenderSink::closeAudioDevice() {
    if (stream_) {
        PaError err = Pa_CloseStream(stream_);
        if (err != paNoError) {
            std::cerr << "RenderSink: Error closing stream: " 
                      << Pa_GetErrorText(err) << std::endl;
        }
        stream_ = nullptr;
    }
    
    currentDeviceName_.clear();
}

bool RenderSink::validateDeviceCompatibility(int deviceId) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(deviceId);
    if (!info) {
        return false;
    }

    // Check if device supports required channels
    if (info->maxOutputChannels < channels_) {
        std::cerr << "RenderSink: Device supports only " << info->maxOutputChannels 
                  << " channels, but " << channels_ << " required" << std::endl;
        return false;
    }

    // Test if device supports the sample rate
    PaStreamParameters outputParams;
    outputParams.device = deviceId;
    outputParams.channelCount = channels_;
    outputParams.sampleFormat = paFloat32;
    outputParams.suggestedLatency = info->defaultLowOutputLatency;
    outputParams.hostApiSpecificStreamInfo = nullptr;

    PaError err = Pa_IsFormatSupported(nullptr, &outputParams, sampleRate_);
    if (err != paFormatIsSupported) {
        std::cerr << "RenderSink: Device does not support " << sampleRate_ 
                  << "Hz sample rate: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    return true;
}

void RenderSink::updatePlaybackStats() {
    uint64_t currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();

    // Calculate playback rate
    if (lastPlaybackTime_ > 0) {
        double timeDiff = static_cast<double>(currentTime - lastPlaybackTime_) / 1000.0;
        if (timeDiff > 0) {
            double samplesDiff = static_cast<double>(totalSamplesPlayed_.load());
            playbackRate_ = samplesDiff / timeDiff;
        }
    }
}

void RenderSink::calculateLatencyAndJitter() {
    if (stream_) {
        const PaStreamInfo* streamInfo = Pa_GetStreamInfo(stream_);
        if (streamInfo) {
            double currentLatency = streamInfo->outputLatency * 1000.0; // Convert to ms
            
            // Exponential moving average for latency
            averageLatency_ = (averageLatency_.load() * 0.9) + (currentLatency * 0.1);
        }
    }
}