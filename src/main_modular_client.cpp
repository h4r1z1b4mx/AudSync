#include "CaptureSource.h"
#include "CaptureSink.h"
#include "RenderSource.h"
#include "RenderSink.h"
#include "NetworkManager.h"
#include "AudioNetworkPacket.h"
#include "AudioConfig.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <signal.h>
#include <iomanip>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#endif

// Global flag for graceful shutdown
std::atomic<bool> g_running(true);

void signalHandler(int signal) {
    (void)signal;
    g_running = false;
}

class ModularAudioClient {
public:
    ModularAudioClient(const std::string& serverHost, int serverPort)
        : serverHost_(serverHost), serverPort_(serverPort) {}

    bool initialize() {
        std::cout << "=== Modular Audio Client ===" << std::endl;
        std::cout << "Initializing 4-module audio architecture..." << std::endl;

        // Configure audio parameters
        std::cout << "\nAudio Configuration:" << std::endl;
        std::cout << "1. Use optimized defaults (44.1kHz, 2ch, 256 frames)" << std::endl;
        std::cout << "2. Configure audio parameters interactively" << std::endl;
        std::cout << "Choose option [1-2]: ";
        
        int configChoice;
        std::cin >> configChoice;
        
        if (configChoice == 2) {
            audioParams_ = AudioConfig::configureInteractively();
        } else {
            // Use optimized default parameters for better audio quality
            audioParams_.sampleRate = 44100;     // CD quality, widely supported
            audioParams_.channels = 2;
            audioParams_.framesPerBuffer = 256;   // Better balance: 5.8ms latency (more stable than 128)
            audioParams_.inputDeviceId = -1;     // Default device
            audioParams_.outputDeviceId = -1;    // Default device
            
            // Calculate derived parameters
            auto bufferInfo = AudioConfig::getOptimalBufferSize(audioParams_.sampleRate, audioParams_.channels, true);
            audioParams_.expectedLatencyMs = (double)audioParams_.framesPerBuffer / audioParams_.sampleRate * 1000.0;
            audioParams_.packetSizeBytes = AudioConfig::calculateOptimalPacketSize(audioParams_.sampleRate, audioParams_.channels, audioParams_.framesPerBuffer);
            
            std::cout << "\nUsing Optimized Default Configuration:" << std::endl;
            std::cout << "   Sample Rate: " << audioParams_.sampleRate << "Hz (CD quality)" << std::endl;
            std::cout << "   Channels: " << audioParams_.channels << std::endl;
            std::cout << "   Buffer Size: " << audioParams_.framesPerBuffer << " frames (low latency)" << std::endl;
            std::cout << "   Expected Latency: " << std::fixed << std::setprecision(1) << audioParams_.expectedLatencyMs << "ms" << std::endl;
            std::cout << "   Packet Size: " << audioParams_.packetSizeBytes << " bytes" << std::endl;
        }

        // First, create shared network connection
        std::cout << "\n[Network] Connecting to server..." << std::endl;
        if (!sharedNetworkManager_.connectToServer(serverHost_, serverPort_)) {
            std::cerr << "Failed to connect to server" << std::endl;
            return false;
        }
        
        // Send client configuration (like AudioClient does)
        if (!sendClientConfiguration()) {
            std::cerr << "Failed to send client configuration" << std::endl;
            return false;
        }
        
        std::cout << "Connected to " << serverHost_ << ":" << serverPort_ << std::endl;

        // Module 1: CaptureSource (Microphone capture)
        std::cout << "\n[1/4] Initializing CaptureSource..." << std::endl;
        if (!captureSource_.CaptureSourceInit(audioParams_.inputDeviceId, audioParams_.sampleRate, audioParams_.channels, audioParams_.framesPerBuffer)) {
            std::cerr << "Failed to initialize CaptureSource" << std::endl;
            return false;
        }

        // Module 2: CaptureSink (Network transmission) - use shared connection
        std::cout << "\n[2/4] Initializing CaptureSink..." << std::endl;
        if (!captureSink_.CaptureSinkInitWithSharedNetwork(&sharedNetworkManager_)) {
            std::cerr << "Failed to initialize CaptureSink" << std::endl;
            return false;
        }
        
        // Module 3: RenderSource (Network reception) - use shared connection
        std::cout << "\n[3/4] Initializing RenderSource..." << std::endl;
        if (!renderSource_.RenderSourceInitWithSharedNetwork(&sharedNetworkManager_)) {
            std::cerr << "Failed to initialize RenderSource" << std::endl;
            return false;
        }
        
        // Configure RenderSource with actual audio parameters
        renderSource_.setAudioParameters(audioParams_.sampleRate, audioParams_.channels, audioParams_.framesPerBuffer);
        
        // Module 4: RenderSink (Speaker playback)
        std::cout << "\n[4/4] Initializing RenderSink..." << std::endl;
        if (!renderSink_.RenderSinkInit(audioParams_.outputDeviceId, audioParams_.sampleRate, audioParams_.channels, audioParams_.framesPerBuffer)) {
            std::cerr << "Failed to initialize RenderSink" << std::endl;
            return false;
        }
        
        // Set up audio flow callbacks
        setupAudioFlow();
        
        std::cout << "\nAll modules initialized successfully!" << std::endl;
        return true;
    }
    
    void setupAudioFlow() {
        // CaptureSource → CaptureSink (mic to network)
        captureSource_.setCaptureCallback([this](const float* audioData, size_t samples, uint64_t timestamp) {
            return captureSink_.sendAudioData(audioData, samples, timestamp);
        });
        
        // RenderSource → RenderSink (network to speakers)
        renderSource_.setRenderCallback([this](const float* audioData, size_t samples, uint64_t timestamp) {
            return renderSink_.queueAudioData(audioData, samples, timestamp);
        });
    }
    
    void run() {
        std::cout << "\nStarting audio processing..." << std::endl;
        
        // Start audio capture and playback
        captureSource_.startCapture();
        renderSink_.startPlayback();
        
        std::cout << "✓ Audio transfer started successfully!" << std::endl;
        std::cout << "✓ Microphone capture: ACTIVE" << std::endl;
        std::cout << "✓ Speaker playback: ACTIVE" << std::endl;
        std::cout << "✓ Network streaming: ACTIVE" << std::endl;
        std::cout << "\nAudio call active!" << std::endl;
        
        // User controls
        std::cout << "\nControls:" << std::endl;
        std::cout << "  'm' - Toggle mute" << std::endl;
        std::cout << "  '+' - Increase volume" << std::endl;
        std::cout << "  '-' - Decrease volume" << std::endl;
        std::cout << "  's' - Show status" << std::endl;
        std::cout << "  'q' - Quit" << std::endl;
        std::cout << "\nPress any key for controls..." << std::endl;
        
        float volume = 1.0f;
        bool muted = false;
        
        // Main loop with non-blocking input
        while (g_running) {
            // Process modules
            captureSource_.CaptureSourceProcess();
            captureSink_.CaptureSinkProcess();
            renderSource_.RenderSourceProcess();
            renderSink_.RenderSinkProcess();
            
            // Check for keyboard input (non-blocking)
            if (hasKeyboardInput()) {
                char input = getKeyboardInput();
                handleUserInput(input, volume, muted);
            }
            
            // Small delay to prevent busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    void shutdown() {
        std::cout << "\nShutting down gracefully..." << std::endl;
        std::cout << "\nShutting down modules..." << std::endl;
        
        captureSource_.stopCapture();
        renderSink_.stopPlayback();
        
        captureSource_.CaptureSourceDeinit();
        captureSink_.CaptureSinkDeinit();
        renderSource_.RenderSourceDeinit();
        renderSink_.RenderSinkDeinit();
        
        // Disconnect shared network manager
        sharedNetworkManager_.disconnect();
        
        std::cout << "Shutdown complete" << std::endl;
    }

private:
    bool sendClientConfiguration() {
        // Send client configuration like AudioClient does
        Message configMsg;
        configMsg.type = MessageType::CLIENT_CONFIG;
        configMsg.size = 12; // 3 int32_t values
        configMsg.data.resize(configMsg.size);
        configMsg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();
        
        // Pack audio config (sample rate, channels, buffer size)
        int32_t* configData = reinterpret_cast<int32_t*>(configMsg.data.data());
        configData[0] = audioParams_.sampleRate;
        configData[1] = audioParams_.channels;
        configData[2] = audioParams_.framesPerBuffer;
        
        if (!sharedNetworkManager_.sendMessage(configMsg)) {
            std::cerr << "Failed to send client config" << std::endl;
            return false;
        }

        // Send ready message
        Message readyMsg;
        readyMsg.type = MessageType::CLIENT_READY;
        readyMsg.size = 0;
        readyMsg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();
        
        if (!sharedNetworkManager_.sendMessage(readyMsg)) {
            std::cerr << "Failed to send ready message" << std::endl;
            return false;
        }
        
        return true;
    }

    void handleUserInput(char input, float& volume, bool& muted) {
        switch (input) {
            case 'm':
            case 'M':
                muted = !muted;
                renderSink_.setMuted(muted);
                std::cout << "🔇 Muted: " << (muted ? "ON" : "OFF") << std::endl;
                break;
            case '+':
                volume = std::min(2.0f, volume + 0.1f);
                renderSink_.setVolume(volume);
                std::cout << "🔊 Volume: " << (int)(volume * 100) << "%" << std::endl;
                break;
            case '-':
                volume = std::max(0.0f, volume - 0.1f);
                renderSink_.setVolume(volume);
                std::cout << "🔉 Volume: " << (int)(volume * 100) << "%" << std::endl;
                break;
            case 's':
            case 'S':
                showStatus();
                break;
            case 'q':
            case 'Q':
                g_running = false;
                break;
        }
    }
    
    void showStatus() {
        std::cout << "\n=== Module Status ===" << std::endl;
        std::cout << "CaptureSource: " << (captureSource_.isInitialized() ? "Ready" : "Not Ready") << std::endl;
        std::cout << "CaptureSink: " << (captureSink_.isConnected() ? "Connected" : "Disconnected") << std::endl;
        std::cout << "RenderSource: " << (renderSource_.isReceiving() ? "Receiving" : "Not Receiving") << std::endl;
        std::cout << "RenderSink: " << (renderSink_.isInitialized() ? "Ready" : "Not Ready") << std::endl;
        
        std::cout << "\n=== Statistics ===" << std::endl;
        std::cout << "Packets Sent: " << captureSink_.getTotalPacketsSent() << std::endl;
        
        auto renderStats = renderSource_.getStats();
        std::cout << "Packets Received: " << renderStats.totalPacketsReceived << std::endl;
        std::cout << "========================" << std::endl;
    }

#ifdef _WIN32
    bool hasKeyboardInput() {
        return _kbhit();
    }
    
    char getKeyboardInput() {
        return _getch();
    }
#else
    bool hasKeyboardInput() {
        int ch = getchar();
        if (ch != EOF) {
            ungetc(ch, stdin);
            return true;
        }
        return false;
    }
    
    char getKeyboardInput() {
        return getchar();
    }
#endif

private:
    std::string serverHost_;
    int serverPort_;
    
    AudioConfig::AudioParameters audioParams_;
    NetworkManager sharedNetworkManager_;
    
    CaptureSource captureSource_;
    CaptureSink captureSink_;
    RenderSource renderSource_;
    RenderSink renderSink_;
};

int main(int argc, char* argv[]) {
    // Set up signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Parse command line arguments
    std::string serverHost = "localhost";
    int serverPort = 8080;
    
    if (argc >= 2) {
        serverHost = argv[1];
    }
    if (argc >= 3) {
        serverPort = std::stoi(argv[2]);
    }
    
    // Create and run modular client
    ModularAudioClient client(serverHost, serverPort);
    
    if (!client.initialize()) {
        std::cerr << "Failed to initialize modular audio client" << std::endl;
        return 1;
    }
    
    client.run();
    client.shutdown();
    
    return 0;
}
