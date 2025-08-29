#include "AudioConfig.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <iomanip>

// Static member definitions
std::vector<int> AudioConfig::COMMON_SAMPLE_RATES = {8000, 16000, 22050, 44100, 48000, 88200, 96000, 192000};
std::vector<int> AudioConfig::STANDARD_BUFFER_SIZES = {64, 128, 256, 512, 1024, 2048, 4096};

std::vector<AudioConfig::DeviceInfo> AudioConfig::getInputDevices() {
    std::vector<DeviceInfo> devices;
    
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "Failed to initialize PortAudio: " << Pa_GetErrorText(err) << std::endl;
        return devices;
    }
    
    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxInputChannels > 0) {
            std::string deviceName = info->name;
            std::string lowerName = deviceName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            
            // Only include devices that are clearly input devices
            // Exclude devices that are primarily output devices
            bool isOutputDevice = (lowerName.find("speaker") != std::string::npos ||
                                 lowerName.find("headphone") != std::string::npos ||
                                 lowerName.find("output") != std::string::npos);
            
            // Include if it's clearly an input device or has more input than output channels
            bool isInputDevice = (lowerName.find("microphone") != std::string::npos ||
                                lowerName.find("mic ") != std::string::npos ||
                                lowerName.find("input") != std::string::npos ||
                                lowerName.find("capture") != std::string::npos ||
                                lowerName.find("array") != std::string::npos ||
                                info->maxInputChannels > info->maxOutputChannels);
            
            if (isInputDevice && !isOutputDevice) {
                DeviceInfo device = getDeviceInfo(i);
                device.isInput = true;
                device.isOutput = false;
                devices.push_back(device);
            }
        }
    }
    
    Pa_Terminate();
    return devices;
}

std::vector<AudioConfig::DeviceInfo> AudioConfig::getOutputDevices() {
    std::vector<DeviceInfo> devices;
    
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "Failed to initialize PortAudio: " << Pa_GetErrorText(err) << std::endl;
        return devices;
    }
    
    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxOutputChannels > 0) {
            std::string deviceName = info->name;
            std::string lowerName = deviceName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            
            // Only include devices that are clearly output devices
            // Exclude devices that are primarily input devices
            bool isInputDevice = (lowerName.find("microphone") != std::string::npos ||
                                lowerName.find("mic ") != std::string::npos ||
                                lowerName.find("input") != std::string::npos ||
                                lowerName.find("capture") != std::string::npos ||
                                lowerName.find("array") != std::string::npos);
            
            // Include if it's clearly an output device or has more output than input channels
            bool isOutputDevice = (lowerName.find("speaker") != std::string::npos ||
                                 lowerName.find("headphone") != std::string::npos ||
                                 lowerName.find("headset") != std::string::npos ||
                                 lowerName.find("output") != std::string::npos ||
                                 info->maxOutputChannels > info->maxInputChannels);
            
            if (isOutputDevice && !isInputDevice) {
                DeviceInfo device = getDeviceInfo(i);
                device.isInput = false;
                device.isOutput = true;
                devices.push_back(device);
            }
        }
    }
    
    Pa_Terminate();
    return devices;
}

AudioConfig::DeviceInfo AudioConfig::getDeviceInfo(int deviceId) {
    DeviceInfo device;
    device.deviceId = deviceId;
    
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        return device;
    }
    
    const PaDeviceInfo* info = Pa_GetDeviceInfo(deviceId);
    if (!info) {
        Pa_Terminate();
        return device;
    }
    
    device.name = info->name;
    device.defaultLowLatency = info->defaultLowInputLatency;
    device.defaultHighLatency = info->defaultHighInputLatency;
    
    // Test supported sample rates
    for (int sampleRate : COMMON_SAMPLE_RATES) {
        PaStreamParameters params;
        params.device = deviceId;
        params.channelCount = std::min(2, std::max(info->maxInputChannels, info->maxOutputChannels));
        params.sampleFormat = paFloat32;
        params.suggestedLatency = info->defaultLowInputLatency;
        params.hostApiSpecificStreamInfo = nullptr;
        
        PaError testErr = Pa_IsFormatSupported(
            info->maxInputChannels > 0 ? &params : nullptr,
            info->maxOutputChannels > 0 ? &params : nullptr,
            sampleRate
        );
        
        if (testErr == paFormatIsSupported) {
            device.supportedSampleRates.push_back(sampleRate);
        }
    }
    
    // Test supported channel counts
    if (!device.supportedSampleRates.empty()) {
        int testSampleRate = device.supportedSampleRates[0];
        int maxChannels = std::max(info->maxInputChannels, info->maxOutputChannels);
        
        for (int channels = 1; channels <= maxChannels && channels <= 8; channels++) {
            PaStreamParameters params;
            params.device = deviceId;
            params.channelCount = channels;
            params.sampleFormat = paFloat32;
            params.suggestedLatency = info->defaultLowInputLatency;
            params.hostApiSpecificStreamInfo = nullptr;
            
            PaError testErr = Pa_IsFormatSupported(
                info->maxInputChannels > 0 ? &params : nullptr,
                info->maxOutputChannels > 0 ? &params : nullptr,
                testSampleRate
            );
            
            if (testErr == paFormatIsSupported) {
                device.supportedChannels.push_back(channels);
            }
        }
    }
    
    Pa_Terminate();
    return device;
}

std::vector<int> AudioConfig::getCommonSampleRates() {
    return COMMON_SAMPLE_RATES;
}

AudioConfig::OptimalBufferInfo AudioConfig::getOptimalBufferSize(int sampleRate, int channels, bool lowLatency) {
    OptimalBufferInfo info;
    
    // Calculate optimal buffer size based on sample rate and latency requirements
    double targetLatencyMs = lowLatency ? 10.0 : 20.0; // Target latency
    int targetFrames = static_cast<int>(sampleRate * targetLatencyMs / 1000.0);
    
    // Round to nearest power of 2 or standard buffer size
    auto it = std::lower_bound(STANDARD_BUFFER_SIZES.begin(), STANDARD_BUFFER_SIZES.end(), targetFrames);
    if (it != STANDARD_BUFFER_SIZES.end()) {
        info.framesPerBuffer = *it;
    } else {
        info.framesPerBuffer = STANDARD_BUFFER_SIZES.back();
    }
    
    // Calculate actual latency
    info.latencyMs = (double)info.framesPerBuffer / sampleRate * 1000.0;
    
    // Calculate packet size
    info.packetSizeBytes = calculateOptimalPacketSize(sampleRate, channels, info.framesPerBuffer);
    
    // Generate recommendation
    if (info.packetSizeBytes > MAX_AUDIO_PAYLOAD) {
        info.recommendation = "WARNING: Packet size exceeds MTU, may cause fragmentation";
    } else if (info.latencyMs < 5.0) {
        info.recommendation = "Very low latency - may cause audio dropouts on slower systems";
    } else if (info.latencyMs > 50.0) {
        info.recommendation = "High latency - suitable for reliability over low-latency";
    } else {
        info.recommendation = "Optimal balance of latency and reliability";
    }
    
    return info;
}

bool AudioConfig::isParameterCombinationValid(int deviceId, int sampleRate, int channels, int framesPerBuffer) {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        return false;
    }
    
    const PaDeviceInfo* info = Pa_GetDeviceInfo(deviceId);
    if (!info) {
        Pa_Terminate();
        return false;
    }
    
    PaStreamParameters params;
    params.device = deviceId;
    params.channelCount = channels;
    params.sampleFormat = paFloat32;
    params.suggestedLatency = info->defaultLowInputLatency;
    params.hostApiSpecificStreamInfo = nullptr;
    
    PaError testErr = Pa_IsFormatSupported(
        info->maxInputChannels > 0 ? &params : nullptr,
        info->maxOutputChannels > 0 ? &params : nullptr,
        sampleRate
    );
    
    Pa_Terminate();
    return testErr == paFormatIsSupported;
}

int AudioConfig::calculateOptimalPacketSize(int sampleRate, int channels, int framesPerBuffer) {
    int samplesPerPacket = framesPerBuffer * channels;
    int bytesPerSample = sizeof(float); // 32-bit float
    int audioDataSize = samplesPerPacket * bytesPerSample;
    int totalPacketSize = audioDataSize + PROTOCOL_HEADER_SIZE;
    
    return totalPacketSize;
}

std::vector<int> AudioConfig::getRecommendedBufferSizes(int sampleRate) {
    std::vector<int> recommended;
    
    // Standard buffer sizes that are commonly supported
    std::vector<int> candidateBuffers = {64, 128, 256, 512, 1024, 2048};
    
    for (int bufferSize : candidateBuffers) {
        double latencyMs = (double)bufferSize / sampleRate * 1000.0;
        int packetSize = calculateOptimalPacketSize(sampleRate, 2, bufferSize); // Assume stereo
        
        // Include if latency is reasonable and packet fits in MTU
        if (latencyMs >= 1.0 && latencyMs <= 100.0 && packetSize <= MAX_AUDIO_PAYLOAD) {
            recommended.push_back(bufferSize);
        }
    }
    
    // Ensure we have at least some options even if they're not optimal
    if (recommended.empty()) {
        recommended = {128, 256, 512}; // Safe fallback options
    }
    
    return recommended;
}

void AudioConfig::displayDeviceCapabilities(const DeviceInfo& device) {
    std::cout << "\nDevice: " << device.name << " (ID: " << device.deviceId << ")" << std::endl;
    std::cout << "   Type: " << (device.isInput ? "Input" : "Output") << std::endl;
    
    std::cout << "   Supported Sample Rates: ";
    for (size_t i = 0; i < device.supportedSampleRates.size(); i++) {
        std::cout << device.supportedSampleRates[i] << "Hz";
        if (i < device.supportedSampleRates.size() - 1) std::cout << ", ";
    }
    std::cout << std::endl;
    
    std::cout << "   Supported Channels: ";
    for (size_t i = 0; i < device.supportedChannels.size(); i++) {
        std::cout << device.supportedChannels[i];
        if (i < device.supportedChannels.size() - 1) std::cout << ", ";
    }
    std::cout << std::endl;
    
    std::cout << "   Default Latency: " << std::fixed << std::setprecision(1) 
              << device.defaultLowLatency * 1000.0 << "ms (low) / " 
              << device.defaultHighLatency * 1000.0 << "ms (high)" << std::endl;
}

AudioConfig::AudioParameters AudioConfig::configureInteractively() {
    AudioParameters params;
    
    std::cout << "\n=== Audio Configuration Wizard ===" << std::endl;
    
    // Get input devices
    std::cout << "\nAvailable Input Devices:" << std::endl;
    auto inputDevices = getInputDevices();
    for (size_t i = 0; i < inputDevices.size(); i++) {
        std::cout << "  [" << i << "] " << inputDevices[i].name;
        if (inputDevices[i].deviceId == Pa_GetDefaultInputDevice()) {
            std::cout << " (default)";
        }
        std::cout << std::endl;
    }
    
    std::cout << "\nSelect input device [0-" << (inputDevices.size()-1) << "]: ";
    size_t inputChoice;
    std::cin >> inputChoice;
    if (inputChoice >= inputDevices.size()) inputChoice = 0;
    params.inputDeviceId = inputDevices[inputChoice].deviceId;
    
    // Get output devices
    std::cout << "\nAvailable Output Devices:" << std::endl;
    auto outputDevices = getOutputDevices();
    for (size_t i = 0; i < outputDevices.size(); i++) {
        std::cout << "  [" << i << "] " << outputDevices[i].name;
        if (outputDevices[i].deviceId == Pa_GetDefaultOutputDevice()) {
            std::cout << " (default)";
        }
        std::cout << std::endl;
    }
    
    std::cout << "\nSelect output device [0-" << (outputDevices.size()-1) << "]: ";
    size_t outputChoice;
    std::cin >> outputChoice;
    if (outputChoice >= outputDevices.size()) outputChoice = 0;
    params.outputDeviceId = outputDevices[outputChoice].deviceId;
    
    // Display device capabilities
    displayDeviceCapabilities(inputDevices[inputChoice]);
    displayDeviceCapabilities(outputDevices[outputChoice]);
    
    // Sample Rate Selection
    std::cout << "\nSample Rate Selection:" << std::endl;
    auto commonRates = inputDevices[inputChoice].supportedSampleRates;
    
    // Filter to rates supported by both devices
    std::vector<int> mutualRates;
    for (int rate : commonRates) {
        if (std::find(outputDevices[outputChoice].supportedSampleRates.begin(),
                     outputDevices[outputChoice].supportedSampleRates.end(), rate) !=
            outputDevices[outputChoice].supportedSampleRates.end()) {
            mutualRates.push_back(rate);
        }
    }
    
    for (size_t i = 0; i < mutualRates.size(); i++) {
        std::cout << "  [" << i << "] " << mutualRates[i] << "Hz";
        if (mutualRates[i] == 48000) std::cout << " (recommended for VoIP)";
        if (mutualRates[i] == 44100) std::cout << " (CD quality)";
        std::cout << std::endl;
    }
    
    std::cout << "\nSelect sample rate [0-" << (mutualRates.size()-1) << "]: ";
    size_t rateChoice;
    std::cin >> rateChoice;
    if (rateChoice >= mutualRates.size()) rateChoice = 0;
    params.sampleRate = mutualRates[rateChoice];
    
    // Channel Selection
    std::cout << "\nChannel Configuration:" << std::endl;
    int maxChannels = std::min(
        *std::max_element(inputDevices[inputChoice].supportedChannels.begin(),
                         inputDevices[inputChoice].supportedChannels.end()),
        *std::max_element(outputDevices[outputChoice].supportedChannels.begin(),
                         outputDevices[outputChoice].supportedChannels.end())
    );
    
    for (int ch = 1; ch <= maxChannels && ch <= 8; ch++) {
        std::cout << "  [" << ch << "] " << ch << " channel" << (ch > 1 ? "s" : "");
        if (ch == 1) std::cout << " (mono)";
        if (ch == 2) std::cout << " (stereo, recommended)";
        if (ch == 6) std::cout << " (5.1 surround)";
        if (ch == 8) std::cout << " (7.1 surround)";
        std::cout << std::endl;
    }
    
    std::cout << "\nSelect channels [1-" << maxChannels << "]: ";
    std::cin >> params.channels;
    if (params.channels < 1 || params.channels > maxChannels) params.channels = 2;
    
    // Buffer Size Selection
    std::cout << "\nBuffer Size Configuration (frames):" << std::endl;
    auto recommendedBuffers = getRecommendedBufferSizes(params.sampleRate);
    
    for (size_t i = 0; i < recommendedBuffers.size(); i++) {
        int bufferFrames = recommendedBuffers[i];
        double latencyMs = (double)bufferFrames / params.sampleRate * 1000.0;
        int packetSizeBytes = calculateOptimalPacketSize(params.sampleRate, params.channels, bufferFrames);
        
        std::cout << "  [" << i << "] " << bufferFrames << " frames (" 
                  << std::fixed << std::setprecision(1) << latencyMs << "ms, "
                  << packetSizeBytes << " bytes)";
        
        // Show MTU compliance
        if (packetSizeBytes <= MAX_AUDIO_PAYLOAD) {
            std::cout << " - MTU OK";
        } else {
            std::cout << " - MTU EXCEEDED";
        }
        
        // Show quality recommendations
        if (latencyMs <= 5.0) {
            std::cout << " [Low Latency]";
        } else if (latencyMs <= 15.0) {
            std::cout << " [Balanced]";
        } else {
            std::cout << " [High Quality]";
        }
        
        std::cout << std::endl;
    }
    
    std::cout << "\nSelect buffer size [0-" << (recommendedBuffers.size()-1) << "]: ";
    size_t bufferChoice;
    std::cin >> bufferChoice;
    if (bufferChoice >= recommendedBuffers.size()) bufferChoice = 0;
    params.framesPerBuffer = recommendedBuffers[bufferChoice];
    
    // Calculate final parameters
    params.expectedLatencyMs = (double)params.framesPerBuffer / params.sampleRate * 1000.0;
    params.packetSizeBytes = calculateOptimalPacketSize(params.sampleRate, params.channels, params.framesPerBuffer);
    
    // Display final configuration
    std::cout << "\nFinal Audio Configuration:" << std::endl;
    std::cout << "   Input Device: " << inputDevices[inputChoice].name << std::endl;
    std::cout << "   Output Device: " << outputDevices[outputChoice].name << std::endl;
    std::cout << "   Sample Rate: " << params.sampleRate << "Hz" << std::endl;
    std::cout << "   Channels: " << params.channels << std::endl;
    std::cout << "   Buffer Size: " << params.framesPerBuffer << " frames" << std::endl;
    std::cout << "   Expected Latency: " << std::fixed << std::setprecision(1) << params.expectedLatencyMs << "ms" << std::endl;
    std::cout << "   Packet Size: " << params.packetSizeBytes << " bytes" << std::endl;
    std::cout << "   MTU Efficiency: " << std::fixed << std::setprecision(1) 
              << (double)params.packetSizeBytes / MAX_AUDIO_PAYLOAD * 100.0 << "%" << std::endl;
    
    // Show recommendation
    if (params.packetSizeBytes > MAX_AUDIO_PAYLOAD) {
        std::cout << "   Warning: Packet size exceeds MTU, may cause network fragmentation" << std::endl;
    } else if (params.expectedLatencyMs < 3.0) {
        std::cout << "   Note: Very low latency - may cause audio dropouts on slower systems" << std::endl;
    } else if (params.expectedLatencyMs > 30.0) {
        std::cout << "   Note: High latency - good for stability, less responsive" << std::endl;
    } else {
        std::cout << "   Status: Optimal configuration for real-time audio" << std::endl;
    }
    
    return params;
}
