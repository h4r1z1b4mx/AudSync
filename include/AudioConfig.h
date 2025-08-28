#pragma once

#include <vector>
#include <string>
#include <portaudio.h>

/**
 * Audio Configuration Management
 * Handles device capability detection and optimal parameter selection
 */
class AudioConfig {
public:
    struct DeviceInfo {
        int deviceId;
        std::string name;
        std::vector<int> supportedSampleRates;
        std::vector<int> supportedChannels;
        double defaultLowLatency;
        double defaultHighLatency;
        bool isInput;
        bool isOutput;
    };

    struct OptimalBufferInfo {
        int framesPerBuffer;
        double latencyMs;
        int packetSizeBytes;
        std::string recommendation;
    };

    struct AudioParameters {
        int sampleRate;
        int channels;
        int framesPerBuffer;
        int inputDeviceId;
        int outputDeviceId;
        double expectedLatencyMs;
        int packetSizeBytes;
    };

    // Device enumeration
    static std::vector<DeviceInfo> getInputDevices();
    static std::vector<DeviceInfo> getOutputDevices();
    static DeviceInfo getDeviceInfo(int deviceId);
    
    // Parameter optimization
    static std::vector<int> getCommonSampleRates();
    static OptimalBufferInfo getOptimalBufferSize(int sampleRate, int channels, bool lowLatency = true);
    static bool isParameterCombinationValid(int deviceId, int sampleRate, int channels, int framesPerBuffer);
    
    // MTU-aware packet sizing
    static int calculateOptimalPacketSize(int sampleRate, int channels, int framesPerBuffer);
    static std::vector<int> getRecommendedBufferSizes(int sampleRate);
    
    // Interactive configuration
    static AudioParameters configureInteractively();
    static void displayDeviceCapabilities(const DeviceInfo& device);
    
private:
    static constexpr int ETHERNET_MTU = 1500;
    static constexpr int IP_HEADER_SIZE = 20;
    static constexpr int UDP_HEADER_SIZE = 8;
    static constexpr int PROTOCOL_HEADER_SIZE = 32; // Our protocol overhead
    static constexpr int MAX_AUDIO_PAYLOAD = ETHERNET_MTU - IP_HEADER_SIZE - UDP_HEADER_SIZE - PROTOCOL_HEADER_SIZE;
    
    static std::vector<int> COMMON_SAMPLE_RATES;
    static std::vector<int> STANDARD_BUFFER_SIZES;
};
