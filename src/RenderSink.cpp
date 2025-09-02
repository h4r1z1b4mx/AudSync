#include "RenderSink.h"
#include <iostream>
#include <chrono>
#include <cstring>
#include <algorithm>

bool RenderSink::RenderSinkInit(int deviceId, int sampleRate, int channels, int framesPerBuffer) {
    if (isInitialized_) {
        std::cerr << "RenderSink: Already initialized" << std::endl;
        return false;
    }

    // Initialize PortAudio if not already done
    static bool paInitialized = false;
    if (!paInitialized) {
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            std::cerr << "RenderSink: Failed to initialize PortAudio: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }
        paInitialized = true;
    }

    deviceId_ = (deviceId == -1) ? getDefaultDevice() : deviceId;
    sampleRate_ = sampleRate;
    channels_ = channels;
    framesPerBuffer_ = framesPerBuffer;

    // Get device info
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(deviceId_);
    if (!deviceInfo) {
        std::cerr << "RenderSink: Invalid device ID: " << deviceId_ << std::endl;
        return false;
    }

    // Setup output parameters with optimized latency
    PaStreamParameters outputParams;
    outputParams.device = deviceId_;
    outputParams.channelCount = channels_;
    outputParams.sampleFormat = paFloat32;
    outputParams.suggestedLatency = deviceInfo->defaultLowOutputLatency; // Use low latency for better quality
    outputParams.hostApiSpecificStreamInfo = nullptr;

    // Open PortAudio stream
    PaError err = Pa_OpenStream(&stream_,
                               nullptr, // No input
                               &outputParams,
                               sampleRate_,
                               framesPerBuffer_,
                               paClipOff,
                               audioCallback,
                               this);

    if (err != paNoError) {
        std::cerr << "RenderSink: Failed to open audio stream: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    isInitialized_ = true;
    std::cout << "RenderSink: Initialized successfully" << std::endl;
    std::cout << "  Device: " << deviceInfo->name << std::endl;
    std::cout << "  Sample Rate: " << sampleRate_ << "Hz" << std::endl;
    std::cout << "  Channels: " << channels_ << std::endl;
    std::cout << "  Buffer Size: " << framesPerBuffer_ << " frames" << std::endl;
    std::cout << "  Playback Buffer: " << maxBufferMs_ << "ms" << std::endl;
    std::cout << "  Initial Volume: " << volume_.load() << std::endl;

    return true;
}

void RenderSink::RenderSinkDeinit() {
    if (!isInitialized_) return;

    stopPlayback();

    if (stream_) {
        Pa_CloseStream(stream_);
        stream_ = nullptr;
    }

    // Clear audio queue
    {
        std::lock_guard<std::mutex> lock(audioQueueMutex_);
        while (!audioQueue_.empty()) {
            audioQueue_.pop();
        }
        currentBuffer_.clear();
        currentBufferPos_ = 0;
    }

    isInitialized_ = false;
    std::cout << "RenderSink: Deinitialized" << std::endl;
}

bool RenderSink::RenderSinkProcess() {
    // For PortAudio, processing is handled in the callback
    // This function can be used for additional per-frame processing
    if (!isInitialized_ || !isPlaying_) {
        return false;
    }
    
    // Could add additional processing here if needed
    return true;
}

bool RenderSink::startPlayback() {
    if (!isInitialized_ || isPlaying_) {
        return false;
    }

    PaError err = Pa_StartStream(stream_);
    if (err != paNoError) {
        std::cerr << "RenderSink: Failed to start stream: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    isPlaying_ = true;
    std::cout << "RenderSink: Playback started" << std::endl;
    return true;
}

void RenderSink::stopPlayback() {
    if (!isPlaying_) return;

    if (stream_) {
        Pa_StopStream(stream_);
    }

    isPlaying_ = false;
    std::cout << "RenderSink: Playback stopped" << std::endl;
}

bool RenderSink::queueAudioData(const float* audioData, size_t samples, uint64_t timestamp) {
    (void)timestamp; // Timestamp not used in this simple implementation
    
    if (!isInitialized_ || !audioData || samples == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(audioQueueMutex_);
    
    // More efficient buffer size calculation
    size_t totalQueuedSamples = 0;
    std::queue<std::vector<float>> tempQueue = audioQueue_; // Copy for counting
    while (!tempQueue.empty()) {
        totalQueuedSamples += tempQueue.front().size();
        tempQueue.pop();
    }
    
    // Calculate buffer time in milliseconds
    double bufferTimeMs = (double)totalQueuedSamples / (sampleRate_ * channels_) * 1000.0;
    
    // Improved buffer management: drop multiple old packets if needed
    while (bufferTimeMs > maxBufferMs_ && !audioQueue_.empty()) {
        audioQueue_.pop();
        totalQueuedSamples -= audioQueue_.empty() ? 0 : audioQueue_.front().size();
        bufferTimeMs = (double)totalQueuedSamples / (sampleRate_ * channels_) * 1000.0;
    }
    
    // Add new audio data
    std::vector<float> audioBuffer(audioData, audioData + samples);
    audioQueue_.push(audioBuffer);
    
    // Debug: Show when audio is queued for playback (temporarily enabled for testing)
    static int queuedPacketCount = 0;
    queuedPacketCount++;
    if (queuedPacketCount % 500 == 0) {  // Every 500 packets for testing
        std::cout << "Queued " << queuedPacketCount << " packets for playback, " << samples << " samples" << std::endl;
    }
    
    return true;
}

void RenderSink::clearBuffer() {
    std::lock_guard<std::mutex> lock(audioQueueMutex_);
    while (!audioQueue_.empty()) {
        audioQueue_.pop();
    }
    currentBuffer_.clear();
    currentBufferPos_ = 0;
}

void RenderSink::setVolume(float volume) {
    volume_.store(std::max(0.0f, std::min(1.0f, volume)));
}

void RenderSink::setMuted(bool muted) {
    isMuted_.store(muted);
}

std::vector<std::string> RenderSink::getAvailableDevices() {
    std::vector<std::string> devices;
    
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "Failed to initialize PortAudio for device enumeration" << std::endl;
        return devices;
    }

    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxOutputChannels > 0) {
            devices.push_back(std::string(info->name));
        }
    }

    return devices;
}

int RenderSink::getDefaultDevice() {
    return Pa_GetDefaultOutputDevice();
}

size_t RenderSink::getBufferSize() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(audioQueueMutex_));
    return audioQueue_.size();
}

int RenderSink::audioCallback(const void* inputBuffer, void* outputBuffer,
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

    // Debug: Show callback activity occasionally (reduced for production)
    static int callbackCount = 0;
    callbackCount++;
    if (callbackCount % 5000 == 0) {  // Every ~10 seconds for monitoring
        std::cout << "Audio playback active (" << callbackCount << " callbacks)" << std::endl;
    }

    // Handle status flags (reduced verbosity)
    if (statusFlags & paOutputUnderflow) {
        static int underflowCount = 0;
        underflowCount++;
        if (underflowCount % 100 == 0) {  // Only report every 100th underflow
            std::cerr << "RenderSink: Audio underflows detected (" << underflowCount << " total)" << std::endl;
        }
    }

    float* output = static_cast<float*>(outputBuffer);
    
    // **PROFESSIONAL AUDIO CALLBACK PIPELINE**
    size_t totalSamples = framesPerBuffer * renderSink->channels_;
    
    if (renderSink->fillOutputBuffer(output, totalSamples)) {
        // **FINAL STAGE AUDIO ENHANCEMENT** after buffer fill
        for (size_t i = 0; i < totalSamples; i++) {
            float sample = output[i];
            
            // **1. OUTPUT STEREO ENHANCEMENT** (for 2-channel audio)
            if (renderSink->channels_ == 2 && i % 2 == 0) {
                // Subtle stereo width enhancement
                float left = sample;
                float right = (i + 1 < totalSamples) ? output[i + 1] : sample;
                
                // Calculate mid/side for natural stereo enhancement
                float mid = (left + right) * 0.5f;
                float side = (left - right) * 0.5f;
                
                // Gentle stereo enhancement (3% wider for natural sound)
                side *= 1.03f;
                
                // Convert back to L/R
                output[i] = mid + side;      // Left
                if (i + 1 < totalSamples) {
                    output[i + 1] = mid - side;  // Right
                }
            }
            
            // **2. FINAL SUBSONIC FILTERING** (eliminate DC completely)
            static float dc_x1 = 0.0f, dc_y1 = 0.0f;
            const float dc_alpha = 0.999f; // 3Hz high-pass for final cleanup
            
            float filtered = dc_alpha * (dc_y1 + output[i] - dc_x1);
            dc_x1 = output[i];
            dc_y1 = filtered;
            output[i] = filtered;
            
            // **3. ABSOLUTE OUTPUT PROTECTION** (brick-wall limiting)
            if (std::abs(output[i]) > 0.98f) {
                float sign = (output[i] >= 0.0f) ? 1.0f : -1.0f;
                float magnitude = std::abs(output[i]);
                // Emergency limiting with musical response
                magnitude = 0.98f + (magnitude - 0.98f) * 0.08f;
                output[i] = sign * std::min(magnitude, 0.995f);
            }
        }
        
        // Apply volume and muting with enhanced quality
        renderSink->applyVolumeAndMuting(output, totalSamples);
    } else {
        // **INTELLIGENT SILENCE MANAGEMENT** instead of harsh zeros
        static float comfortLevel = 0.0f;
        static bool wasPlaying = false;
        
        if (wasPlaying) {
            // Gentle fade-out when audio stops
            comfortLevel = 0.001f;
            wasPlaying = false;
        }
        
        for (size_t i = 0; i < totalSamples; i++) {
            // Generate subtle comfort noise (pink noise characteristics)
            static float pink_b0 = 0, pink_b1 = 0, pink_b2 = 0;
            float white = ((float)rand() / RAND_MAX - 0.5f) * 2.0f;
            
            // Pink noise filter for natural background
            pink_b0 = 0.99886f * pink_b0 + white * 0.0555179f;
            pink_b1 = 0.99332f * pink_b1 + white * 0.0750759f;
            pink_b2 = 0.96900f * pink_b2 + white * 0.1538520f;
            
            float pink = pink_b0 + pink_b1 + pink_b2 + white * 0.3104856f;
            
            // Extremely quiet comfort noise (-85dB)
            comfortLevel = std::max(0.0f, comfortLevel - 0.00001f);
            output[i] = pink * comfortLevel * 0.00003f;
        }
    }
    
    return paContinue;
}

bool RenderSink::fillOutputBuffer(float* outputBuffer, size_t samples) {
    std::lock_guard<std::mutex> lock(audioQueueMutex_);
    
    size_t samplesWritten = 0;
    
    // **PROFESSIONAL AUDIO OUTPUT PIPELINE**
    while (samplesWritten < samples) {
        // If current buffer is empty or finished, get next one
        if (currentBuffer_.empty() || currentBufferPos_ >= currentBuffer_.size()) {
            if (audioQueue_.empty()) {
                // No more audio data
                break;
            }
            
            currentBuffer_ = audioQueue_.front();
            audioQueue_.pop();
            currentBufferPos_ = 0;
        }
        
        // **ENHANCED AUDIO PROCESSING** during buffer copy
        size_t samplesAvailable = currentBuffer_.size() - currentBufferPos_;
        size_t samplesNeeded = samples - samplesWritten;
        size_t samplesToCopy = std::min(samplesAvailable, samplesNeeded);
        
        // **PROFESSIONAL AUDIO ENHANCEMENT DURING COPY**
        for (size_t i = 0; i < samplesToCopy; i++) {
            float sample = currentBuffer_[currentBufferPos_ + i];
            
            // **1. OUTPUT STAGE ANTI-ALIASING** (final smoothing)
            static float antiAlias_x1 = 0.0f, antiAlias_y1 = 0.0f;
            const float aliasAlpha = 0.9f; // Gentle high-frequency roll-off
            
            float smoothed = aliasAlpha * antiAlias_y1 + (1.0f - aliasAlpha) * sample;
            antiAlias_y1 = smoothed;
            
            // **2. OUTPUT DYNAMICS OPTIMIZATION** (final stage enhancement)
            // Gentle expansion for very quiet signals (restore detail)
            if (std::abs(smoothed) < 0.02f && std::abs(smoothed) > 0.001f) {
                float sign = (smoothed >= 0.0f) ? 1.0f : -1.0f;
                float magnitude = std::abs(smoothed);
                // Gentle expansion curve
                magnitude = std::pow(magnitude / 0.02f, 0.8f) * 0.02f;
                smoothed = sign * magnitude;
            }
            
            // **3. FINAL STAGE SOFT LIMITING** (prevent any output clipping)
            if (std::abs(smoothed) > 0.95f) {
                float sign = (smoothed >= 0.0f) ? 1.0f : -1.0f;
                float magnitude = std::abs(smoothed);
                // Musical soft limiting with natural saturation
                magnitude = 0.95f + (magnitude - 0.95f) * 0.15f;
                smoothed = sign * std::min(magnitude, 0.99f);
            }
            
            outputBuffer[samplesWritten + i] = smoothed;
        }
        
        samplesWritten += samplesToCopy;
        currentBufferPos_ += samplesToCopy;
    }
    
    // **AUDIO CONTINUITY ENHANCEMENT** - fill remaining with intelligent fade
    if (samplesWritten < samples) {
        // Instead of abrupt silence, create gentle fade-out
        static float lastSample = 0.0f;
        size_t remainingSamples = samples - samplesWritten;
        
        for (size_t i = 0; i < remainingSamples; i++) {
            // Exponential decay for natural sound
            lastSample *= 0.95f;
            outputBuffer[samplesWritten + i] = lastSample;
        }
    } else if (samplesWritten > 0) {
        // Store last sample for potential fade-out
        static float lastOutputSample = 0.0f;
        lastOutputSample = outputBuffer[samplesWritten - 1];
    }
    
    return samplesWritten > 0;
}

void RenderSink::applyVolumeAndMuting(float* buffer, size_t samples) {
    if (isMuted_.load()) {
        // **PROFESSIONAL MUTING** with gentle fade instead of harsh silence
        static float muteLevel = 1.0f;
        
        for (size_t i = 0; i < samples; i++) {
            // Exponential fade to mute (natural sounding)
            muteLevel *= 0.92f;  // Quick but musical fade
            buffer[i] *= muteLevel;
            
            // Complete silence when fade is deep enough
            if (muteLevel < 0.001f) {
                buffer[i] = 0.0f;
                muteLevel = 0.0f;
            }
        }
        return;
    } else {
        // **SMOOTH UNMUTE** - restore audio gradually
        static float unmuteLevel = 0.0f;
        static bool wasMuted = false;
        
        if (wasMuted || unmuteLevel < 0.98f) {
            for (size_t i = 0; i < samples; i++) {
                // Gentle fade-in from mute
                unmuteLevel = std::min(1.0f, unmuteLevel + 0.005f);
                buffer[i] *= unmuteLevel;
            }
            wasMuted = false;
        }
    }
    
    float volume = volume_.load();
    if (volume != 1.0f) {
        // **MUSICAL VOLUME SCALING** with smooth transitions
        static float smoothVolume = 1.0f;
        const float volumeSmoothing = 0.02f; // Gentle volume changes
        
        for (size_t i = 0; i < samples; i++) {
            // Smooth volume transitions to prevent clicks
            smoothVolume += (volume - smoothVolume) * volumeSmoothing;
            
            // Apply logarithmic volume scaling (more natural)
            float logVolume = smoothVolume * smoothVolume; // Square law for natural feel
            buffer[i] *= logVolume;
            
            // **PROFESSIONAL OUTPUT PROTECTION** with musical limiting
            if (std::abs(buffer[i]) > 0.99f) {
                float sign = (buffer[i] >= 0.0f) ? 1.0f : -1.0f;
                float magnitude = std::abs(buffer[i]);
                
                // Musical soft clipping using tanh-like response
                if (magnitude > 0.99f) {
                    float excess = magnitude - 0.99f;
                    magnitude = 0.99f + excess * 0.05f; // Very gentle saturation
                }
                
                buffer[i] = sign * std::min(magnitude, 0.995f);
            }
        }
    }
}

bool RenderSink::isRealSpeaker(const std::string& deviceName) {
    std::string lowerName = deviceName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    
    return (lowerName.find("speakers") != std::string::npos ||
            lowerName.find("headphones") != std::string::npos ||
            lowerName.find("headset") != std::string::npos ||
            lowerName.find("built-in") != std::string::npos ||
            lowerName.find("realtek") != std::string::npos) &&
           lowerName.find("cable") == std::string::npos; // Exclude virtual cables
}