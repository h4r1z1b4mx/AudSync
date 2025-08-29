#include "CaptureSource.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <algorithm>

bool CaptureSource::CaptureSourceInit(int deviceId, int sampleRate, int channels, int framesPerBuffer) {
    if (isInitialized_) {
        std::cerr << "CaptureSource: Already initialized" << std::endl;
        return false;
    }

    // Initialize PortAudio if not already done
    static bool paInitialized = false;
    if (!paInitialized) {
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            std::cerr << "CaptureSource: Failed to initialize PortAudio: " << Pa_GetErrorText(err) << std::endl;
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
        std::cerr << "CaptureSource: Invalid device ID: " << deviceId_ << std::endl;
        return false;
    }

    // Setup input parameters
    PaStreamParameters inputParams;
    inputParams.device = deviceId_;
    inputParams.channelCount = channels_;
    inputParams.sampleFormat = paFloat32;
    inputParams.suggestedLatency = deviceInfo->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;

    // Open PortAudio stream
    PaError err = Pa_OpenStream(&stream_,
                               &inputParams,
                               nullptr, // No output
                               sampleRate_,
                               framesPerBuffer_,
                               paClipOff,
                               audioCallback,
                               this);

    if (err != paNoError) {
        std::cerr << "CaptureSource: Failed to open audio stream: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    isInitialized_ = true;
    std::cout << "CaptureSource: Initialized successfully" << std::endl;
    std::cout << "  Device: " << deviceInfo->name << std::endl;
    std::cout << "  Sample Rate: " << sampleRate_ << "Hz" << std::endl;
    std::cout << "  Channels: " << channels_ << std::endl;
    std::cout << "  Buffer Size: " << framesPerBuffer_ << " frames" << std::endl;

    return true;
}

void CaptureSource::CaptureSourceDeinit() {
    if (!isInitialized_) return;

    stopCapture();

    if (stream_) {
        Pa_CloseStream(stream_);
        stream_ = nullptr;
    }

    isInitialized_ = false;
    std::cout << "CaptureSource: Deinitialized" << std::endl;
}

bool CaptureSource::CaptureSourceProcess() {
    // For PortAudio, processing is handled in the callback
    // This function can be used for any additional per-frame processing
    if (!isInitialized_ || !isCapturing_) {
        return false;
    }
    
    // Could add additional processing here if needed
    return true;
}

void CaptureSource::setCaptureCallback(std::function<void(const float*, size_t, uint64_t)> callback) {
    captureCallback_ = callback;
}

bool CaptureSource::startCapture() {
    if (!isInitialized_ || isCapturing_) {
        return false;
    }

    PaError err = Pa_StartStream(stream_);
    if (err != paNoError) {
        std::cerr << "CaptureSource: Failed to start stream: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    isCapturing_ = true;
    std::cout << "CaptureSource: Capture started" << std::endl;
    return true;
}

void CaptureSource::stopCapture() {
    if (!isCapturing_) return;

    if (stream_) {
        Pa_StopStream(stream_);
    }

    isCapturing_ = false;
    std::cout << "CaptureSource: Capture stopped" << std::endl;
}

std::vector<std::string> CaptureSource::getAvailableDevices() {
    std::vector<std::string> devices;
    
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "Failed to initialize PortAudio for device enumeration" << std::endl;
        return devices;
    }

    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxInputChannels > 0) {
            devices.push_back(std::string(info->name));
        }
    }

    return devices;
}

int CaptureSource::getDefaultDevice() {
    return Pa_GetDefaultInputDevice();
}

int CaptureSource::audioCallback(const void* inputBuffer, void* outputBuffer,
                                unsigned long framesPerBuffer,
                                const PaStreamCallbackTimeInfo* timeInfo,
                                PaStreamCallbackFlags statusFlags,
                                void* userData) {
    (void)outputBuffer; // Unused
    (void)timeInfo;     // Could be used for timestamp

    CaptureSource* captureSource = static_cast<CaptureSource*>(userData);
    
    if (!captureSource || !inputBuffer) {
        return paAbort;
    }

    // Handle status flags (underflow, overflow, etc.)
    if (statusFlags & paInputOverflow) {
        std::cerr << "CaptureSource: Input overflow detected" << std::endl;
    }

    captureSource->processAudioData(static_cast<const float*>(inputBuffer), framesPerBuffer);
    
    return paContinue;
}

void CaptureSource::processAudioData(const float* data, size_t samples) {
    if (captureCallback_) {
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();
        
        // Apply volume and mute controls with proper bounds checking
        std::vector<float> processedData(data, data + samples);
        
        if (isMuted_.load()) {
            // Mute: fill with silence
            std::fill(processedData.begin(), processedData.end(), 0.0f);
        } else {
            // Apply volume with clipping protection
            float volume = volume_.load();
            if (volume != 1.0f) {
                for (float& sample : processedData) {
                    sample *= volume;
                    // Prevent clipping
                    if (sample > 1.0f) sample = 1.0f;
                    else if (sample < -1.0f) sample = -1.0f;
                }
            }
        }
        
        // Debug: Show audio activity occasionally (silenced for cleaner output)
        static int audioFrameCount = 0;
        audioFrameCount++;
        if (audioFrameCount % 10000 == 0) {  // Every ~4.5 minutes at 44.1kHz (much less frequent)
            float avgLevel = 0.0f;
            for (size_t i = 0; i < samples; i++) {
                avgLevel += std::abs(processedData[i]);
            }
            avgLevel /= samples;
            // std::cout << "Audio captured: " << samples << " samples, level: " << avgLevel << std::endl;
        }
        
        captureCallback_(processedData.data(), samples, timestamp);
    }
}

bool CaptureSource::isRealMicrophone(const std::string& deviceName) {
    std::string lowerName = deviceName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    
    return (lowerName.find("microphone") != std::string::npos ||
            lowerName.find("mic") != std::string::npos ||
            lowerName.find("webcam") != std::string::npos ||
            lowerName.find("headset") != std::string::npos ||
            lowerName.find("built-in") != std::string::npos ||
            lowerName.find("intel") != std::string::npos ||
            lowerName.find("array") != std::string::npos) &&
           lowerName.find("cable") == std::string::npos; // Exclude virtual cables
}

void CaptureSource::setVolume(float volume) {
    volume_.store(std::max(0.0f, std::min(1.0f, volume)));
}

void CaptureSource::setMuted(bool muted) {
    isMuted_.store(muted);
}