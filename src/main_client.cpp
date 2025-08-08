#include "AudioClient.h"
#include "SessionLogger.h"
#include "AudioRecorder.h"
#include "JitterBuffer.h"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <portaudio.h>
#include <sstream>      // ✅ ADD: For std::ostringstream
#include <iomanip>      // ✅ ADD: For std::setprecision

void printDeviceList(const std::vector<std::pair<int, std::string>>& devices) {
    std::cout << "Available Audio Devices:\n";
    for (size_t i = 0; i < devices.size(); ++i) {
        std::cout << "  [" << i << "] " << devices[i].second << "\n";
    }
}

// ✅ NEW: Get only available and accessible input devices
std::vector<std::pair<int, std::string>> getAvailableInputDevices() {
    std::vector<std::pair<int, std::string>> devices;
    Pa_Initialize();
    
    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxInputChannels > 0) {
            // Test if device is actually accessible
            PaStreamParameters inputParams;
            inputParams.device = i;
            inputParams.channelCount = 1;
            inputParams.sampleFormat = paFloat32;
            inputParams.suggestedLatency = info->defaultLowInputLatency;
            inputParams.hostApiSpecificStreamInfo = nullptr;
            
            PaStream* testStream;
            PaError err = Pa_OpenStream(&testStream, &inputParams, nullptr, 
                                      info->defaultSampleRate, 256, paClipOff, nullptr, nullptr);
            
            if (err == paNoError) {
                Pa_CloseStream(testStream);
                std::ostringstream oss;  // ✅ FIXED: Added proper header
                oss << info->name << " (Max: " << info->maxInputChannels << " channels, Default: " 
                    << static_cast<int>(info->defaultSampleRate) << "Hz)";
                devices.push_back(std::make_pair(i, oss.str()));  // ✅ FIXED: Use make_pair
            }
        }
    }
    
    Pa_Terminate();
    return devices;
}

// ✅ NEW: Get only available and accessible output devices
std::vector<std::pair<int, std::string>> getAvailableOutputDevices() {
    std::vector<std::pair<int, std::string>> devices;
    Pa_Initialize();
    
    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxOutputChannels > 0) {
            // Test if device is actually accessible
            PaStreamParameters outputParams;
            outputParams.device = i;
            outputParams.channelCount = 1;
            outputParams.sampleFormat = paFloat32;
            outputParams.suggestedLatency = info->defaultLowOutputLatency;
            outputParams.hostApiSpecificStreamInfo = nullptr;
            
            PaStream* testStream;
            PaError err = Pa_OpenStream(&testStream, nullptr, &outputParams, 
                                      info->defaultSampleRate, 256, paClipOff, nullptr, nullptr);
            
            if (err == paNoError) {
                Pa_CloseStream(testStream);
                std::ostringstream oss;  // ✅ FIXED: Added proper header
                oss << info->name << " (Max: " << info->maxOutputChannels << " channels, Default: " 
                    << static_cast<int>(info->defaultSampleRate) << "Hz)";
                devices.push_back(std::make_pair(i, oss.str()));  // ✅ FIXED: Use make_pair
            }
        }
    }
    
    Pa_Terminate();
    return devices;
}

// ✅ NEW: Get supported sample rates for a device
std::vector<int> getSupportedSampleRates(int deviceId, bool isInput) {
    std::vector<int> supportedRates;
    std::vector<int> testRates = {8000, 11025, 16000, 22050, 44100, 48000, 88200, 96000, 176400, 192000};
    
    Pa_Initialize();
    const PaDeviceInfo* info = Pa_GetDeviceInfo(deviceId);
    if (!info) {
        Pa_Terminate();
        return supportedRates;
    }
    
    for (int rate : testRates) {
        PaStreamParameters params;
        params.device = deviceId;
        params.channelCount = 1;
        params.sampleFormat = paFloat32;
        params.suggestedLatency = isInput ? info->defaultLowInputLatency : info->defaultLowOutputLatency;
        params.hostApiSpecificStreamInfo = nullptr;
        
        PaStream* testStream;
        PaError err;
        
        if (isInput) {
            err = Pa_OpenStream(&testStream, &params, nullptr, rate, 256, paClipOff, nullptr, nullptr);
        } else {
            err = Pa_OpenStream(&testStream, nullptr, &params, rate, 256, paClipOff, nullptr, nullptr);
        }
        
        if (err == paNoError) {
            Pa_CloseStream(testStream);
            supportedRates.push_back(rate);
        }
    }
    
    Pa_Terminate();
    return supportedRates;
}

// ✅ NEW: Get supported channel counts for a device
std::vector<int> getSupportedChannels(int deviceId, bool isInput) {
    std::vector<int> supportedChannels;
    
    Pa_Initialize();
    const PaDeviceInfo* info = Pa_GetDeviceInfo(deviceId);
    if (!info) {
        Pa_Terminate();
        return supportedChannels;
    }
    
    int maxChannels = isInput ? info->maxInputChannels : info->maxOutputChannels;
    
    // Test common channel configurations
    std::vector<int> testChannels = {1, 2, 4, 6, 8};
    
    for (int channels : testChannels) {
        if (channels <= maxChannels) {
            PaStreamParameters params;
            params.device = deviceId;
            params.channelCount = channels;
            params.sampleFormat = paFloat32;
            params.suggestedLatency = isInput ? info->defaultLowInputLatency : info->defaultLowOutputLatency;
            params.hostApiSpecificStreamInfo = nullptr;
            
            PaStream* testStream;
            PaError err;
            
            if (isInput) {
                err = Pa_OpenStream(&testStream, &params, nullptr, info->defaultSampleRate, 256, paClipOff, nullptr, nullptr);
            } else {
                err = Pa_OpenStream(&testStream, nullptr, &params, info->defaultSampleRate, 256, paClipOff, nullptr, nullptr);
            }
            
            if (err == paNoError) {
                Pa_CloseStream(testStream);
                supportedChannels.push_back(channels);
            }
        }
    }
    
    Pa_Terminate();
    return supportedChannels;
}

// ✅ NEW: Calculate optimal buffer size for MTU safety
int calculateOptimalBufferSize(int channels) {
    const size_t MAX_PACKET_SIZE = 1400;    // Safe MTU
    const size_t HEADER_OVERHEAD = 50;      // Message headers
    const size_t MAX_AUDIO_BYTES = MAX_PACKET_SIZE - HEADER_OVERHEAD;
    
    // Calculate max frames that fit in one packet
    int maxFrames = static_cast<int>(MAX_AUDIO_BYTES / (channels * sizeof(float)));
    
    // Find largest power-of-2 that fits
    int optimalFrames = 32;  // Minimum reasonable size
    while (optimalFrames * 2 <= maxFrames) {
        optimalFrames *= 2;
    }
    
    return optimalFrames;
}

int main(int argc, char* argv[]) {
    std::string server_host = "127.0.0.1";
    int server_port = 8080;

    // Parse command line arguments
    if (argc >= 2) server_host = argv[1];
    if (argc >= 3) server_port = std::stoi(argv[2]);

    std::cout << "AudSync Client - Real-time Audio Streaming\n";
    std::cout << "Connecting to Server: " << server_host << ":" << server_port << "\n";

    // ✅ INPUT DEVICE SELECTION - Only available devices
    auto inputDevices = getAvailableInputDevices();
    if (inputDevices.empty()) {
        std::cerr << "No accessible input devices found!" << std::endl;
        return 1;
    }
    
    std::cout << "\n=== AVAILABLE INPUT DEVICES ===" << std::endl;
    printDeviceList(inputDevices);
    int inputChoice = 0;
    std::cout << "Select input device: ";
    std::cin >> inputChoice;

    if (inputChoice < 0 || inputChoice >= static_cast<int>(inputDevices.size())) {
        std::cerr << "Invalid input device selection" << std::endl;
        return 1;
    }
    int inputDeviceId = inputDevices[inputChoice].first;

    // ✅ OUTPUT DEVICE SELECTION - Only available devices
    auto outputDevices = getAvailableOutputDevices();
    if (outputDevices.empty()) {
        std::cerr << "No accessible output devices found!" << std::endl;
        return 1;
    }
    
    std::cout << "\n=== AVAILABLE OUTPUT DEVICES ===" << std::endl;
    printDeviceList(outputDevices);
    int outputChoice = 0;
    std::cout << "Select output device: ";
    std::cin >> outputChoice;

    if (outputChoice < 0 || outputChoice >= static_cast<int>(outputDevices.size())) {
        std::cerr << "Invalid output device selection" << std::endl;
        return 1;
    }
    int outputDeviceId = outputDevices[outputChoice].first;

    // ✅ SAMPLE RATE SELECTION - Only supported rates
    auto inputSampleRates = getSupportedSampleRates(inputDeviceId, true);
    auto outputSampleRates = getSupportedSampleRates(outputDeviceId, false);
    
    // Find common sample rates
    std::vector<int> commonSampleRates;
    for (int rate : inputSampleRates) {
        if (std::find(outputSampleRates.begin(), outputSampleRates.end(), rate) != outputSampleRates.end()) {
            commonSampleRates.push_back(rate);
        }
    }
    
    if (commonSampleRates.empty()) {
        std::cerr << "No common sample rates supported by both devices!" << std::endl;
        return 1;
    }
    
    std::cout << "\n=== SUPPORTED SAMPLE RATES ===" << std::endl;
    for (size_t i = 0; i < commonSampleRates.size(); ++i) {
        std::cout << "  [" << i << "] " << commonSampleRates[i] << "Hz" << std::endl;
    }
    
    int sampleRateChoice = 0;
    std::cout << "Select sample rate: ";
    std::cin >> sampleRateChoice;
    
    if (sampleRateChoice < 0 || sampleRateChoice >= static_cast<int>(commonSampleRates.size())) {
        std::cerr << "Invalid sample rate selection" << std::endl;
        return 1;
    }
    int sampleRate = commonSampleRates[sampleRateChoice];

    // ✅ CHANNEL SELECTION - Only supported channels
    auto inputChannels = getSupportedChannels(inputDeviceId, true);
    auto outputChannels = getSupportedChannels(outputDeviceId, false);
    
    // Find common channel counts
    std::vector<int> commonChannels;
    for (int ch : inputChannels) {
        if (std::find(outputChannels.begin(), outputChannels.end(), ch) != outputChannels.end()) {
            commonChannels.push_back(ch);
        }
    }
    
    if (commonChannels.empty()) {
        std::cerr << "No common channel configurations supported by both devices!" << std::endl;
        return 1;
    }
    
    std::cout << "\n=== SUPPORTED CHANNEL CONFIGURATIONS ===" << std::endl;
    for (size_t i = 0; i < commonChannels.size(); ++i) {
        std::string description = (commonChannels[i] == 1) ? "Mono" : 
                                 (commonChannels[i] == 2) ? "Stereo" :
                                 std::to_string(commonChannels[i]) + " channels";
        std::cout << "  [" << i << "] " << commonChannels[i] << " (" << description << ")" << std::endl;
    }
    
    int channelChoice = 0;
    std::cout << "Select channel configuration: ";
    std::cin >> channelChoice;
    
    if (channelChoice < 0 || channelChoice >= static_cast<int>(commonChannels.size())) {
        std::cerr << "Invalid channel selection" << std::endl;
        return 1;
    }
    int channels = commonChannels[channelChoice];

    // ✅ BUFFER SIZE SELECTION - MTU-aware
    int recommendedBuffer = calculateOptimalBufferSize(channels);
    std::cout << "\n=== BUFFER SIZE CONFIGURATION ===" << std::endl;
    std::cout << "Recommended buffer size for " << channels << " channels: " << recommendedBuffer << " frames" << std::endl;
    std::cout << "This ensures packets stay under network MTU limit (1400 bytes)" << std::endl;
    
    std::vector<int> bufferOptions = {64, 128, 256, 512};
    std::cout << "Available buffer sizes:" << std::endl;
    
    for (size_t i = 0; i < bufferOptions.size(); ++i) {
        size_t packetSize = bufferOptions[i] * channels * sizeof(float) + 50;
        bool mtuSafe = packetSize <= 1400;
        float latency = static_cast<float>(bufferOptions[i]) / sampleRate * 1000;
        
        std::cout << "  [" << i << "] " << bufferOptions[i] << " frames (" 
                  << std::fixed << std::setprecision(1) << latency << "ms latency, "
                  << packetSize << "B packet) " << (mtuSafe ? "✅" : "⚠️") << std::endl;
    }
    
    int bufferChoice = 2; // Default to 256
    std::cout << "Select buffer size (default 256): ";
    std::cin >> bufferChoice;
    
    if (bufferChoice < 0 || bufferChoice >= static_cast<int>(bufferOptions.size())) {
        std::cout << "Invalid selection, using recommended size: " << recommendedBuffer << std::endl;
        bufferChoice = recommendedBuffer;
    } else {
        bufferChoice = bufferOptions[bufferChoice];
    }
    int framesPerBuffer = bufferChoice;

    // ✅ MTU WARNING CHECK
    size_t packetSize = framesPerBuffer * channels * sizeof(float) + 50;
    if (packetSize > 1400) {
        std::cout << "\n⚠️  WARNING: Large packet size (" << packetSize << "B) may cause network fragmentation!" << std::endl;
        std::cout << "Consider using a smaller buffer size for better network performance." << std::endl;
        std::cout << "Continue anyway? (y/n): ";
        char confirm;
        std::cin >> confirm;
        if (confirm != 'y' && confirm != 'Y') {
            std::cout << "Using recommended safe buffer size: " << recommendedBuffer << std::endl;
            framesPerBuffer = recommendedBuffer;
        }
    }

    // Initialize components
    SessionLogger logger;
    AudioRecorder recorder;
    JitterBuffer jitterBuffer;

    // Create client with validated parameters
    AudioClient client(inputDeviceId, outputDeviceId, sampleRate, channels, framesPerBuffer, &logger, &recorder, &jitterBuffer);

    if (!client.connect(server_host, server_port)) {
        std::cerr << "Failed to connect to server. Make sure the server is running.\n";
        return 1;
    }

    std::cout << "\n✅ Successfully connected to Server!" << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Input Device: " << inputDevices[inputChoice].second << std::endl;
    std::cout << "  Output Device: " << outputDevices[outputChoice].second << std::endl;
    std::cout << "  Sample Rate: " << sampleRate << "Hz" << std::endl;
    std::cout << "  Channels: " << channels << std::endl;
    std::cout << "  Buffer Size: " << framesPerBuffer << " frames" << std::endl;
    std::cout << "  Estimated Latency: " << std::fixed << std::setprecision(1) 
              << static_cast<float>(framesPerBuffer) / sampleRate * 1000 << "ms" << std::endl;
    
    std::cout << "\nType commands during session:" << std::endl;
    std::cout << "  'start'    - Begin audio streaming" << std::endl;
    std::cout << "  'stop'     - Pause audio streaming" << std::endl;
    std::cout << "  'logon'    - Start logging" << std::endl;
    std::cout << "  'logoff'   - Stop logging" << std::endl;
    std::cout << "  'recstart' - Start recording session" << std::endl;
    std::cout << "  'recstop'  - Stop recording session" << std::endl;
    std::cout << "  'quit'     - Exit client" << std::endl;

    // Run the client main loop
    client.run();

    std::cout << "Client shutting down...\n";
    return 0;
}