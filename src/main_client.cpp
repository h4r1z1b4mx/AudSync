#include "AudioClient.h"
#include "SessionLogger.h"
#include "AudioRecorder.h"
#include "JitterBuffer.h"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <portaudio.h>
#include <sstream>
#include <iomanip>

// FIXED: Better virtual device detection based on actual behavior
bool isSystemAudioCapture(const std::string& deviceName) {
    std::string lowerName = deviceName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    
    // These capture system OUTPUT and present it as an INPUT (loopback)
    return lowerName.find("cable output") != std::string::npos ||
           lowerName.find("stereo mix") != std::string::npos ||
           lowerName.find("what u hear") != std::string::npos ||
           lowerName.find("wave out mix") != std::string::npos ||
           lowerName.find("speakers") != std::string::npos && lowerName.find("realtek") == std::string::npos;
}

bool isVirtualInput(const std::string& deviceName) {
    std::string lowerName = deviceName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    
    // These are virtual inputs that should appear as outputs
    return lowerName.find("cable in") != std::string::npos ||
           lowerName.find("vb-audio") != std::string::npos && lowerName.find("cable in") != std::string::npos;
}

bool isRealMicrophone(const std::string& deviceName) {
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

bool isRealSpeaker(const std::string& deviceName) {
    std::string lowerName = deviceName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    
    return (lowerName.find("speakers") != std::string::npos ||
            lowerName.find("headphones") != std::string::npos ||
            lowerName.find("headset") != std::string::npos ||
            lowerName.find("realtek") != std::string::npos) &&
           lowerName.find("cable") == std::string::npos && // Exclude virtual cables
           lowerName.find("vb-audio") == std::string::npos;
}

void printDeviceList(const std::vector<std::pair<int, std::string>>& devices) {
    std::cout << "Available Audio Devices:\n";
    for (size_t i = 0; i < devices.size(); ++i) {
        std::cout << "  [" << i << "] " << devices[i].second << "\n";
    }
}

// FIXED: Only show REAL input devices (microphones, line-in)
std::vector<std::pair<int, std::string>> getAvailableInputDevices() {
    std::vector<std::pair<int, std::string>> devices;
    Pa_Initialize();
    
    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        
        if (info && info->maxInputChannels > 0) {
            std::string deviceName = info->name;
            
            // FIXED: Skip system audio capture devices (they're not real microphones)
            if (isSystemAudioCapture(deviceName)) {
                continue; // Skip virtual loopback devices
            }
            
            // Test actual input capability
            PaStreamParameters inputParams;
            inputParams.device = i;
            inputParams.channelCount = std::min(2, info->maxInputChannels);
            inputParams.sampleFormat = paFloat32;
            inputParams.suggestedLatency = info->defaultLowInputLatency;
            inputParams.hostApiSpecificStreamInfo = nullptr;
            
            PaStream* testStream;
            PaError err = Pa_OpenStream(&testStream, &inputParams, nullptr, 
                                      info->defaultSampleRate, 256, paClipOff, nullptr, nullptr);
            
            if (err == paNoError) {
                Pa_CloseStream(testStream);
                
                std::ostringstream oss;
                // Add device type indicator
                if (isRealMicrophone(deviceName)) {
                    oss << "[MIC] ";
                } else if (deviceName.find("Sound Mapper") != std::string::npos) {
                    oss << "[DEFAULT] ";
                } else {
                    oss << "[INPUT] ";
                }
                
                oss << deviceName << " (Max: " << info->maxInputChannels 
                    << " ch, Default: " << static_cast<int>(info->defaultSampleRate) << "Hz)";
                devices.push_back(std::make_pair(i, oss.str()));
            }
        }
    }
    
    Pa_Terminate();
    return devices;
}

// FIXED: Only show REAL output devices (speakers, headphones)
std::vector<std::pair<int, std::string>> getAvailableOutputDevices() {
    std::vector<std::pair<int, std::string>> devices;
    Pa_Initialize();
    
    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        
        if (info && info->maxOutputChannels > 0) {
            std::string deviceName = info->name;
            
            // FIXED: Skip virtual input endpoints that appear as outputs
            if (isVirtualInput(deviceName)) {
                continue; // Skip virtual inputs that appear as outputs
            }
            
            // Test actual output capability
            PaStreamParameters outputParams;
            outputParams.device = i;
            outputParams.channelCount = std::min(2, info->maxOutputChannels);
            outputParams.sampleFormat = paFloat32;
            outputParams.suggestedLatency = info->defaultLowOutputLatency;
            outputParams.hostApiSpecificStreamInfo = nullptr;
            
            PaStream* testStream;
            PaError err = Pa_OpenStream(&testStream, nullptr, &outputParams, 
                                      info->defaultSampleRate, 256, paClipOff, nullptr, nullptr);
            
            if (err == paNoError) {
                Pa_CloseStream(testStream);
                
                std::ostringstream oss;
                // Add device type indicator
                if (isRealSpeaker(deviceName)) {
                    oss << "[SPEAKERS] ";
                } else if (deviceName.find("Sound Mapper") != std::string::npos) {
                    oss << "[DEFAULT] ";
                } else {
                    oss << "[OUTPUT] ";
                }
                
                oss << deviceName << " (Max: " << info->maxOutputChannels 
                    << " ch, Default: " << static_cast<int>(info->defaultSampleRate) << "Hz)";
                devices.push_back(std::make_pair(i, oss.str()));
            }
        }
    }
    
    Pa_Terminate();
    return devices;
}

// Get ACTUAL supported sample rates for specific device and direction
std::vector<int> getSupportedSampleRates(int deviceId, bool isInput) {
    std::vector<int> supportedRates;
    std::vector<int> testRates = {8000, 11025, 16000, 22050, 44100, 48000, 88200, 96000, 176400, 192000};
    
    Pa_Initialize();
    const PaDeviceInfo* info = Pa_GetDeviceInfo(deviceId);
    if (!info) {
        Pa_Terminate();
        return supportedRates;
    }
    
    int maxChannels = isInput ? info->maxInputChannels : info->maxOutputChannels;
    if (maxChannels <= 0) {
        Pa_Terminate();
        return supportedRates;
    }
    
    for (int rate : testRates) {
        PaStreamParameters params;
        params.device = deviceId;
        params.channelCount = std::min(2, maxChannels);
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

// Get ACTUAL supported channels for specific device and direction
std::vector<int> getSupportedChannels(int deviceId, bool isInput) {
    std::vector<int> supportedChannels;
    
    Pa_Initialize();
    const PaDeviceInfo* info = Pa_GetDeviceInfo(deviceId);
    if (!info) {
        Pa_Terminate();
        return supportedChannels;
    }
    
    int maxChannels = isInput ? info->maxInputChannels : info->maxOutputChannels;
    if (maxChannels <= 0) {
        Pa_Terminate();
        return supportedChannels;
    }
    
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

int calculateOptimalBufferSize(int channels) {
    const size_t MAX_PACKET_SIZE = 1400;
    const size_t HEADER_OVERHEAD = 50;
    const size_t MAX_AUDIO_BYTES = MAX_PACKET_SIZE - HEADER_OVERHEAD;
    
    int maxFrames = static_cast<int>(MAX_AUDIO_BYTES / (channels * sizeof(float)));
    
    int optimalFrames = 32;
    while (optimalFrames * 2 <= maxFrames) {
        optimalFrames *= 2;
    }
    
    return optimalFrames;
}

void displayBufferOptions(int sampleRate, int channels) {
    std::vector<int> bufferOptions = {64, 128, 256, 512};
    std::cout << "Available buffer sizes:" << std::endl;
    
    for (size_t i = 0; i < bufferOptions.size(); ++i) {
        size_t packetSize = bufferOptions[i] * channels * sizeof(float) + 50;
        bool mtuSafe = packetSize <= 1400;
        
        float latency = static_cast<float>(bufferOptions[i]) / sampleRate * 1000;
        
        std::cout << "  [" << i << "] " << bufferOptions[i] << " frames (" 
                  << std::fixed << std::setprecision(1) << latency << "ms latency, "
                  << packetSize << "B packet) " << (mtuSafe ? "[OK]" : "[WARN]") << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::string server_host = "127.0.0.1";
    int server_port = 8080;

    if (argc >= 2) server_host = argv[1];
    if (argc >= 3) server_port = std::stoi(argv[2]);

    std::cout << "AudSync Client - Real-time Audio Streaming\n";
    std::cout << "Connecting to Server: " << server_host << ":" << server_port << "\n";

    // Get REAL input devices (microphones only)
    auto inputDevices = getAvailableInputDevices();
    if (inputDevices.empty()) {
        std::cerr << "No real microphone devices found!" << std::endl;
        std::cerr << "Make sure you have a working microphone connected." << std::endl;
        return 1;
    }
    
    std::cout << "\n=== AVAILABLE INPUT DEVICES (Microphones) ===" << std::endl;
    printDeviceList(inputDevices);
    int inputChoice = 0;
    std::cout << "Select input device: ";
    std::cin >> inputChoice;

    if (inputChoice < 0 || inputChoice >= static_cast<int>(inputDevices.size())) {
        std::cerr << "Invalid input device selection" << std::endl;
        return 1;
    }
    int inputDeviceId = inputDevices[inputChoice].first;

    // Get REAL output devices (speakers/headphones only)
    auto outputDevices = getAvailableOutputDevices();
    if (outputDevices.empty()) {
        std::cerr << "No real speaker devices found!" << std::endl;
        return 1;
    }
    
    std::cout << "\n=== AVAILABLE OUTPUT DEVICES (Speakers) ===" << std::endl;
    printDeviceList(outputDevices);
    int outputChoice = 0;
    std::cout << "Select output device: ";
    std::cin >> outputChoice;

    if (outputChoice < 0 || outputChoice >= static_cast<int>(outputDevices.size())) {
        std::cerr << "Invalid output device selection" << std::endl;
        return 1;
    }
    int outputDeviceId = outputDevices[outputChoice].first;

    // Get compatible sample rates
    auto inputSampleRates = getSupportedSampleRates(inputDeviceId, true);
    auto outputSampleRates = getSupportedSampleRates(outputDeviceId, false);
    
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
    
    int sampleRateChoice = 4; // Default to 44.1kHz if available
    std::cout << "Select sample rate (default 44.1kHz): ";
    std::cin >> sampleRateChoice;
    
    if (sampleRateChoice < 0 || sampleRateChoice >= static_cast<int>(commonSampleRates.size())) {
        std::cout << "Invalid selection, using first available: " << commonSampleRates[0] << "Hz" << std::endl;
        sampleRateChoice = 0;
    }
    int sampleRate = commonSampleRates[sampleRateChoice];

    // Get compatible channels
    auto inputChannels = getSupportedChannels(inputDeviceId, true);
    auto outputChannels = getSupportedChannels(outputDeviceId, false);
    
    std::vector<int> commonChannels;
    for (int ch : inputChannels) {
        if (std::find(outputChannels.begin(), outputChannels.end(), ch) != outputChannels.end()) {
            commonChannels.push_back(ch);
        }
    }
    
    if (commonChannels.empty()) {
        std::cerr << "No common channel configurations!" << std::endl;
        return 1;
    }
    
    std::cout << "\n=== SUPPORTED CHANNEL CONFIGURATIONS ===" << std::endl;
    for (size_t i = 0; i < commonChannels.size(); ++i) {
        std::string description = (commonChannels[i] == 1) ? "Mono" : 
                                 (commonChannels[i] == 2) ? "Stereo" :
                                 std::to_string(commonChannels[i]) + " channels";
        std::cout << "  [" << i << "] " << commonChannels[i] << " (" << description << ")" << std::endl;
    }
    
    int channelChoice = 1; // Default to stereo
    std::cout << "Select channel configuration (default Stereo): ";
    std::cin >> channelChoice;
    
    if (channelChoice < 0 || channelChoice >= static_cast<int>(commonChannels.size())) {
        std::cout << "Invalid selection, using Mono" << std::endl;
        channelChoice = 0;
    }
    int channels = commonChannels[channelChoice];

    // Buffer size configuration
    int recommendedBuffer = calculateOptimalBufferSize(channels);
    std::cout << "\n=== BUFFER SIZE CONFIGURATION ===" << std::endl;
    std::cout << "Recommended buffer size for " << channels << " channels: " << recommendedBuffer << " frames" << std::endl;
    std::cout << "This ensures packets stay under network MTU limit (1400 bytes)" << std::endl;
    
    displayBufferOptions(sampleRate, channels);
    
    int bufferChoice = 1; // Default to 128
    std::cout << "Select buffer size (default 128): ";
    std::cin >> bufferChoice;
    
    std::vector<int> bufferOptions = {64, 128, 256, 512};
    if (bufferChoice < 0 || bufferChoice >= static_cast<int>(bufferOptions.size())) {
        std::cout << "Using default: 128 frames" << std::endl;
        bufferChoice = 1;
    }
    int framesPerBuffer = bufferOptions[bufferChoice];

    // Initialize components
    SessionLogger logger;
    AudioRecorder recorder;
    JitterBuffer jitterBuffer;

    // Create client
    AudioClient client(inputDeviceId, outputDeviceId, sampleRate, channels, framesPerBuffer, &logger, &recorder, &jitterBuffer);

    if (!client.connect(server_host, server_port)) {
        std::cerr << "Failed to connect to server. Make sure the server is running.\n";
        return 1;
    }

    std::cout << "\nSuccessfully connected to Server!" << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Input Device: " << inputDevices[inputChoice].second << std::endl;
    std::cout << "  Output Device: " << outputDevices[outputChoice].second << std::endl;
    std::cout << "  Sample Rate: " << sampleRate << "Hz" << std::endl;
    std::cout << "  Channels: " << channels << std::endl;
    std::cout << "  Buffer Size: " << framesPerBuffer << " frames" << std::endl;
    std::cout << "  Estimated Latency: " << std::fixed << std::setprecision(1) 
              << static_cast<float>(framesPerBuffer) / sampleRate * 1000 << "ms" << std::endl;

    client.run();

    std::cout << "Client shutting down...\n";
    return 0;
}