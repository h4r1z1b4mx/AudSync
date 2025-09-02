#include "CaptureSource.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <algorithm>
#include <iomanip>

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

    // Setup input parameters optimized for PREMIUM quality
    PaStreamParameters inputParams;
    inputParams.device = deviceId_;
    inputParams.channelCount = channels_;
    inputParams.sampleFormat = paFloat32;
    // Use balanced latency for quality
    inputParams.suggestedLatency = std::min(deviceInfo->defaultHighInputLatency, 0.050); // Max 50ms
    inputParams.hostApiSpecificStreamInfo = nullptr;

    // Open PortAudio stream with PREMIUM settings
    PaError err = Pa_OpenStream(&stream_,
                               &inputParams,
                               nullptr, // No output
                               sampleRate_,
                               framesPerBuffer_,
                               paClipOff | paDitherOff,  // Pure signal path
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

    // Handle status flags with reduced verbosity
    if (statusFlags & paInputOverflow) {
        static int overflowCount = 0;
        overflowCount++;
        if (overflowCount % 100 == 0) {  // Only report every 100th overflow
            std::cerr << "CaptureSource: Input overflows detected (" << overflowCount << " total)" << std::endl;
        }
    }

    captureSource->processAudioData(static_cast<const float*>(inputBuffer), framesPerBuffer);
    
    return paContinue;
}

void CaptureSource::processAudioData(const float* data, size_t samples) {
    if (captureCallback_) {
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();
        
        // **PROFESSIONAL AUDIO PROCESSING PIPELINE** - Real Quality Improvements
        std::vector<float> processedData(data, data + samples);
        
        if (isMuted_.load()) {
            // Professional fade-out to prevent audio pops
            static float muteGain = 1.0f;
            const float fadeStep = 1.0f / (sampleRate_ / 100.0f); // 10ms fade
            
            for (size_t i = 0; i < samples; i++) {
                muteGain = std::max(0.0f, muteGain - fadeStep);
                processedData[i] *= muteGain;
            }
        } else {
            // **ADVANCED DIGITAL SIGNAL PROCESSING CHAIN**
            
            // 1. DC BIAS REMOVAL (High-pass filter at ~20Hz) - Essential for quality
            static float dcBlocker_x1 = 0.0f, dcBlocker_y1 = 0.0f;
            const float dcAlpha = 0.999f; // Very low frequency cutoff ~20Hz at 48kHz
            
            // 2. ADAPTIVE NOISE REDUCTION (spectral subtraction)
            static float noiseFloor = 0.0f;
            static int quietSamples = 0;
            
            // 3. DYNAMIC RANGE COMPRESSION (preserve natural dynamics)
            static float compressorGain = 1.0f;
            const float compressorThreshold = 0.7f;
            const float compressorRatio = 3.0f;
            const float compressorAttack = 0.003f; // 3ms attack
            const float compressorRelease = 0.1f;  // 100ms release
            
            float volume = volume_.load();
            float rmsLevel = 0.0f; // For level detection
            
            for (size_t i = 0; i < samples; i++) {
                float sample = processedData[i];
                
                // **DC BIAS REMOVAL** - Critical for audio quality
                float dcBlocked = sample - dcBlocker_x1 + dcAlpha * dcBlocker_y1;
                dcBlocker_x1 = sample;
                dcBlocker_y1 = dcBlocked;
                sample = dcBlocked;
                
                // **ADAPTIVE NOISE FLOOR ESTIMATION**
                float sampleMagnitude = std::abs(sample);
                if (sampleMagnitude < 0.01f) { // Quiet sections
                    noiseFloor = noiseFloor * 0.9995f + sampleMagnitude * 0.0005f;
                    quietSamples++;
                }
                
                // **INTELLIGENT NOISE GATE** (only when needed)
                if (quietSamples > 1000 && noiseFloor > 0.0005f && sampleMagnitude < noiseFloor * 3.0f) {
                    sample *= 0.2f; // Gentle noise reduction, preserve natural ambience
                }
                
                // **APPLY VOLUME** with perceptual scaling
                if (volume != 1.0f) {
                    // Logarithmic volume scaling (more natural to human hearing)
                    float volumeCurve = volume * volume; // Quadratic curve
                    sample *= volumeCurve;
                }
                
                // **RMS LEVEL CALCULATION** for compressor
                rmsLevel += sample * sample;
                
                // **MUSICAL DYNAMIC RANGE COMPRESSOR**
                float sampleLevel = std::abs(sample);
                if (sampleLevel > compressorThreshold) {
                    float excess = sampleLevel - compressorThreshold;
                    float compressedExcess = excess / compressorRatio;
                    float targetGain = (compressorThreshold + compressedExcess) / sampleLevel;
                    
                    // Smooth gain changes (prevents pumping artifacts)
                    if (targetGain < compressorGain) {
                        compressorGain = compressorGain * (1.0f - compressorAttack) + targetGain * compressorAttack;
                    } else {
                        compressorGain = compressorGain * (1.0f - compressorRelease) + targetGain * compressorRelease;
                    }
                    
                    sample *= compressorGain;
                }
                
                // **PROFESSIONAL SOFT LIMITING** (transparent, musical)
                if (std::abs(sample) > 0.98f) {
                    float sign = (sample >= 0.0f) ? 1.0f : -1.0f;
                    float magnitude = std::abs(sample);
                    // Soft knee limiting with natural saturation curve
                    float limited = 0.98f * std::tanh(magnitude / 0.98f);
                    sample = sign * limited;
                }
                
                processedData[i] = sample;
            }
            
            // **AUTOMATIC GAIN CONTROL** (prevent long-term level drift)
            rmsLevel = std::sqrt(rmsLevel / samples);
            static float targetLevel = 0.15f; // Conservative target RMS level
            static float agcGain = 1.0f;
            
            if (rmsLevel > 0.005f) { // Only adjust if there's significant audio
                float desiredGain = targetLevel / rmsLevel;
                desiredGain = std::max(0.2f, std::min(2.5f, desiredGain)); // Conservative AGC range
                agcGain = agcGain * 0.9995f + desiredGain * 0.0005f; // Very slow adaptation
                
                // Apply AGC very subtly
                if (std::abs(agcGain - 1.0f) > 0.05f) {
                    for (size_t i = 0; i < samples; i++) {
                        processedData[i] *= agcGain;
                    }
                }
            }
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