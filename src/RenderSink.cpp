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
    
    if (renderSink->fillOutputBuffer(output, framesPerBuffer * renderSink->channels_)) {
        renderSink->applyVolumeAndMuting(output, framesPerBuffer * renderSink->channels_);
    } else {
        // No audio data available, fill with silence
        memset(output, 0, framesPerBuffer * renderSink->channels_ * sizeof(float));
    }
    
    return paContinue;
}

bool RenderSink::fillOutputBuffer(float* outputBuffer, size_t samples) {
    std::lock_guard<std::mutex> lock(audioQueueMutex_);
    
    size_t samplesWritten = 0;
    
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
        
        // Copy data from current buffer
        size_t samplesAvailable = currentBuffer_.size() - currentBufferPos_;
        size_t samplesNeeded = samples - samplesWritten;
        size_t samplesToCopy = std::min(samplesAvailable, samplesNeeded);
        
        memcpy(outputBuffer + samplesWritten, 
               currentBuffer_.data() + currentBufferPos_, 
               samplesToCopy * sizeof(float));
        
        samplesWritten += samplesToCopy;
        currentBufferPos_ += samplesToCopy;
    }
    
    return samplesWritten > 0;
}

void RenderSink::applyVolumeAndMuting(float* buffer, size_t samples) {
    if (isMuted_.load()) {
        memset(buffer, 0, samples * sizeof(float));
        return;
    }
    
    float volume = volume_.load();
    if (volume != 1.0f) {
        for (size_t i = 0; i < samples; i++) {
            buffer[i] *= volume;
            // Prevent clipping on output
            if (buffer[i] > 1.0f) buffer[i] = 1.0f;
            else if (buffer[i] < -1.0f) buffer[i] = -1.0f;
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