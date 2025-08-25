#include "CaptureSource.h"
#include <iostream>
#include <chrono>
#include <sstream>
#include <cstring>

CaptureSource::CaptureSource()
    : stream_(nullptr),
      config_(nullptr),
      isCapturing_(false),
      isInitialized_(false),
      totalFramesProcessed_(0),
      totalDroppedFrames_(0),
      lastProcessTime_(0) {
}

CaptureSource::~CaptureSource() {
    CaptureSourceDeinit();
}

bool CaptureSource::CaptureSourceInit(const CaptureSourceConfig& config) {
    if (isInitialized_) {
        std::cerr << "CaptureSource: Already initialized" << std::endl;
        return false;
    }

    // Store configuration
    config_ = new CaptureSourceConfig(config);

    // Initialize PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "CaptureSource: Failed to initialize PortAudio: " 
                  << Pa_GetErrorText(err) << std::endl;
        delete config_;
        config_ = nullptr;
        return false;
    }

    // Setup input parameters
    PaStreamParameters inputParameters;
    inputParameters.device = (config.deviceId == -1) ? Pa_GetDefaultInputDevice() : config.deviceId;
    
    if (inputParameters.device == paNoDevice) {
        std::cerr << "CaptureSource: No input device available" << std::endl;
        Pa_Terminate();
        delete config_;
        config_ = nullptr;
        return false;
    }

    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(inputParameters.device);
    if (!deviceInfo) {
        std::cerr << "CaptureSource: Invalid input device" << std::endl;
        Pa_Terminate();
        delete config_;
        config_ = nullptr;
        return false;
    }

    inputParameters.channelCount = config.channels;
    inputParameters.sampleFormat = paFloat32;
    inputParameters.suggestedLatency = config.enableLowLatency ? 
        deviceInfo->defaultLowInputLatency : config.suggestedLatency;
    inputParameters.hostApiSpecificStreamInfo = nullptr;

    // Open PortAudio stream with device sharing support
    err = Pa_OpenStream(&stream_,
                        &inputParameters,
                        nullptr,  // No output
                        config.sampleRate,
                        config.framesPerBuffer,
                        paNoFlag,  // Use paNoFlag for better device sharing
                        &CaptureSource::portAudioCallback,
                        this);

    if (err != paNoError) {
        std::cerr << "CaptureSource: Failed to open stream: " 
                  << Pa_GetErrorText(err) << std::endl;
        std::cerr << "CaptureSource: Error code: " << err << std::endl;
        std::cerr << "CaptureSource: This might be due to device sharing conflict" << std::endl;
        Pa_Terminate();
        delete config_;
        config_ = nullptr;
        return false;
    }

    isInitialized_ = true;
    
    std::cout << "CaptureSource: Initialized successfully" << std::endl;
    std::cout << "  Device: " << deviceInfo->name << std::endl;
    std::cout << "  Sample Rate: " << config.sampleRate << "Hz" << std::endl;
    std::cout << "  Channels: " << config.channels << std::endl;
    std::cout << "  Buffer Size: " << config.framesPerBuffer << " frames" << std::endl;
    std::cout << "  Latency: " << inputParameters.suggestedLatency * 1000.0 << "ms" << std::endl;

    return true;
}

bool CaptureSource::CaptureSourceDeinit() {
    if (!isInitialized_) {
        return true; // Already deinitialized
    }

    // Stop capture if running
    stopCapture();

    // Close PortAudio stream
    if (stream_) {
        PaError err = Pa_CloseStream(stream_);
        if (err != paNoError) {
            std::cerr << "CaptureSource: Error closing stream: " 
                      << Pa_GetErrorText(err) << std::endl;
        }
        stream_ = nullptr;
    }

    // Terminate PortAudio
    Pa_Terminate();

    // Cleanup configuration
    delete config_;
    config_ = nullptr;

    isInitialized_ = false;
    
    std::cout << "CaptureSource: Deinitialized" << std::endl;
    return true;
}

bool CaptureSource::CaptureSourceProcess() {
    if (!isInitialized_ || !isCapturing_) {
        return false;
    }

    // Check stream status
    if (stream_) {
        PaError err = Pa_IsStreamActive(stream_);
        if (err == 1) {
            // Stream is active - processing happens in callback
            lastProcessTime_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()
            ).count();
            return true;
        } else if (err == 0) {
            // Stream stopped unexpectedly
            std::cerr << "CaptureSource: Stream stopped unexpectedly" << std::endl;
            isCapturing_ = false;
            return false;
        } else {
            // Error occurred
            std::cerr << "CaptureSource: Stream error: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }
    }

    return false;
}

void CaptureSource::setCaptureCallback(CaptureCallback callback) {
    captureCallback_ = callback;
    std::cout << "🔧 CaptureSource: Callback set successfully" << std::endl;
}

bool CaptureSource::startCapture() {
    if (!isInitialized_ || isCapturing_) {
        return false;
    }

    if (!stream_) {
        std::cerr << "CaptureSource: No stream available" << std::endl;
        return false;
    }

    PaError err = Pa_StartStream(stream_);
    if (err != paNoError) {
        std::cerr << "CaptureSource: Failed to start stream: " 
                  << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    isCapturing_ = true;
    std::cout << "CaptureSource: Capture started" << std::endl;
    return true;
}

bool CaptureSource::stopCapture() {
    if (!isInitialized_ || !isCapturing_) {
        return true; // Already stopped
    }

    if (stream_) {
        PaError err = Pa_StopStream(stream_);
        if (err != paNoError) {
            std::cerr << "CaptureSource: Error stopping stream: " 
                      << Pa_GetErrorText(err) << std::endl;
            return false;
        }
    }

    isCapturing_ = false;
    std::cout << "CaptureSource: Capture stopped" << std::endl;
    return true;
}

bool CaptureSource::isCapturing() const {
    return isCapturing_.load();
}

std::vector<std::string> CaptureSource::getAvailableDevices() {
    std::vector<std::string> devices;
    
    // Initialize PortAudio temporarily for device enumeration
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        return devices; // Return empty list on error
    }

    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxInputChannels > 0) {
            // Test if device is actually usable
            PaStreamParameters inputParams;
            inputParams.device = i;
            inputParams.channelCount = 1;
            inputParams.sampleFormat = paFloat32;
            inputParams.suggestedLatency = info->defaultLowInputLatency;
            inputParams.hostApiSpecificStreamInfo = nullptr;

            PaStream* testStream;
            err = Pa_OpenStream(&testStream, &inputParams, nullptr, 
                              info->defaultSampleRate, 256, paClipOff, nullptr, nullptr);

            if (err == paNoError) {
                Pa_CloseStream(testStream);
                std::ostringstream oss;
                oss << "[" << i << "] " << info->name 
                    << " (Max: " << info->maxInputChannels << " ch, "
                    << "Default: " << static_cast<int>(info->defaultSampleRate) << "Hz)";
                devices.push_back(oss.str());
            }
        }
    }

    Pa_Terminate();
    return devices;
}

CaptureSourceStats CaptureSource::getStats() const {
    CaptureSourceStats stats;
    stats.totalFramesProcessed = totalFramesProcessed_.load();
    stats.totalDroppedFrames = totalDroppedFrames_.load();
    stats.lastProcessTime = lastProcessTime_.load();
    stats.isActive = isCapturing_.load();
    
    if (stream_ && isInitialized_) {
        const PaStreamInfo* streamInfo = Pa_GetStreamInfo(stream_);
        if (streamInfo) {
            stats.currentLatency = streamInfo->inputLatency;
        }
        stats.cpuLoad = Pa_GetStreamCpuLoad(stream_);
    }
    
    return stats;
}

// Static PortAudio callback
int CaptureSource::portAudioCallback(const void* inputBuffer, void* outputBuffer,
                                   unsigned long framesPerBuffer,
                                   const PaStreamCallbackTimeInfo* timeInfo,
                                   PaStreamCallbackFlags statusFlags,
                                   void* userData) {
    (void)outputBuffer; // Unused
    (void)timeInfo;     // Could be used for timestamp

    // Debug: Show that PortAudio is calling us
    static int callback_count = 0;
    if (++callback_count % 1000 == 0) {
        std::cout << "🎤 PortAudio callback #" << callback_count << " triggered" << std::endl;
    }

    CaptureSource* captureSource = static_cast<CaptureSource*>(userData);
    
    if (!captureSource || !inputBuffer) {
        std::cout << "❌ PortAudio callback: Invalid parameters!" << std::endl;
        return paAbort;
    }

    // Handle status flags (underflow, overflow, etc.)
    if (statusFlags & paInputUnderflow) {
        captureSource->totalDroppedFrames_++;
    }
    if (statusFlags & paInputOverflow) {
        captureSource->totalDroppedFrames_++;
    }

    // Process audio data
    const float* audioData = static_cast<const float*>(inputBuffer);
    captureSource->processAudioData(audioData, framesPerBuffer * captureSource->config_->channels);
    
    captureSource->totalFramesProcessed_ += framesPerBuffer;
    
    return paContinue;
}

void CaptureSource::processAudioData(const float* data, size_t samples) {
    if (captureCallback_) {
        // Debug: Show that we're receiving audio data
        static int debug_count = 0;
        if (++debug_count % 1000 == 0) {  // Print every 1000 callbacks (~23 seconds at 44.1kHz/256 frames)
            std::cout << "🎤 CaptureSource: Processing audio callback #" << debug_count 
                      << " (samples: " << samples << ")" << std::endl;
        }
        
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();
        
        captureCallback_(data, samples, timestamp);
    } else {
        // Debug: Callback not set
        static bool warned = false;
        if (!warned) {
            std::cout << "⚠️ CaptureSource: No callback set for audio data!" << std::endl;
            warned = true;
        }
    }
}