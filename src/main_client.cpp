#include "AudioClient.h"
#include "SessionLogger.h"
#include "AudioRecorder.h"
#include "JitterBuffer.h"
#include <iostream>
#include <string>
#include <vector>
#include <portaudio.h>  // ✅ Add this include

void printDeviceList(const std::vector<std::string>& devices) {
    std::cout << "Available Audio Devices:\n";
    for (size_t i = 0; i < devices.size(); ++i) {
        std::cout << "  [" << i << "] " << devices[i] << "\n";
    }
}

// ✅ ADD THIS FUNCTION
bool validateChannelConfiguration(int inputDeviceId, int outputDeviceId, int channels) {
    Pa_Initialize();  // Initialize PortAudio for device info access
    
    const PaDeviceInfo* inputInfo = Pa_GetDeviceInfo(inputDeviceId);
    const PaDeviceInfo* outputInfo = Pa_GetDeviceInfo(outputDeviceId);
    
    bool valid = true;
    
    if (!inputInfo) {
        std::cerr << "Invalid input device ID: " << inputDeviceId << std::endl;
        valid = false;
    } else if (inputInfo->maxInputChannels < channels) {
        std::cerr << "Input device supports max " << inputInfo->maxInputChannels 
                  << " channels, requested " << channels << std::endl;
        valid = false;
    }
    
    if (!outputInfo) {
        std::cerr << "Invalid output device ID: " << outputDeviceId << std::endl;
        valid = false;
    } else if (outputInfo->maxOutputChannels < channels) {
        std::cerr << "Output device supports max " << outputInfo->maxOutputChannels 
                  << " channels, requested " << channels << std::endl;
        valid = false;
    }
    
    Pa_Terminate();
    return valid;
}

int main(int argc, char* argv[]) {
    std::string server_host = "127.0.0.1";
    int server_port = 8080;

    // Parse command line arguments
    if (argc >= 2) server_host = argv[1];
    if (argc >= 3) server_port = std::stoi(argv[2]);

    std::cout << "AudSync Client - Real-time Audio Streaming\n";
    std::cout << "Connecting to Server: " << server_host << ":" << server_port << "\n";

    // Input device selection
    std::vector<std::string> inputDevices = AudioClient::getInputDeviceNames();
    printDeviceList(inputDevices);
    int inputDeviceId = 0;
    std::cout << "Select input device ID: ";
    std::cin >> inputDeviceId;

    // ✅ ADD VALIDATION HERE
    if (inputDeviceId < 0 || inputDeviceId >= static_cast<int>(inputDevices.size())) {
        std::cerr << "Invalid input device ID selected" << std::endl;
        return 1;
    }

    // Output device selection
    std::vector<std::string> outputDevices = AudioClient::getOutputDeviceNames();
    printDeviceList(outputDevices);
    int outputDeviceId = 0;
    std::cout << "Select output device ID: ";
    std::cin >> outputDeviceId;

    // ✅ ADD VALIDATION HERE
    if (outputDeviceId < 0 || outputDeviceId >= static_cast<int>(outputDevices.size())) {
        std::cerr << "Invalid output device ID selected" << std::endl;
        return 1;
    }

    // Sample rate selection
    int sampleRate = 44100;
    std::cout << "Enter desired sample rate (e.g., 44100): ";
    std::cin >> sampleRate;

    // Channel selection
    int channels = 1;
    std::cout << "Enter number of channels (1=Mono, 2=Stereo, etc.): ";
    std::cin >> channels;

    // ✅ ADD CHANNEL VALIDATION HERE
    if (!validateChannelConfiguration(inputDeviceId, outputDeviceId, channels)) {
        std::cerr << "Channel configuration not supported by selected devices" << std::endl;
        std::cout << "Please restart and select compatible devices/channels" << std::endl;
        return 1;
    }

    // Buffer size selection
    int framesPerBuffer = 256;
    std::cout << "Enter frames per buffer (e.g., 256, 512): ";
    std::cin >> framesPerBuffer;

    // Initialize logger, recorder, jitter buffer
    SessionLogger logger;
    AudioRecorder recorder;
    JitterBuffer jitterBuffer;

    // Pass all 8 arguments as required by the new constructor
    AudioClient client(inputDeviceId, outputDeviceId, sampleRate, channels, framesPerBuffer, &logger, &recorder, &jitterBuffer);

    if (!client.connect(server_host, server_port)) {
        std::cerr << "Failed to connect to server. Make sure the server is running.\n";
        return 1;
    }

    std::cout << "Successfully connected to Server!\n";
    std::cout << "Configuration: " << channels << " channels at " << sampleRate << "Hz" << std::endl;
    std::cout << "Type commands during session:\n";
    std::cout << "  'start'  - Begin audio streaming\n";
    std::cout << "  'stop'   - Pause audio streaming\n";
    std::cout << "  'logon'  - Start logging\n";
    std::cout << "  'logoff' - Stop logging\n";
    std::cout << "  'recstart' - Start recording session\n";
    std::cout << "  'recstop'  - Stop recording session\n";
    std::cout << "  'quit'   - Exit client\n";

    // Run the client main loop (should handle commands and pass to logger/recorder)
    client.run();

    std::cout << "Client shutting down...\n";
    return 0;
}