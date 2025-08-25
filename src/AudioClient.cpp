#include "AudioClient.h"
#include <iostream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <string>

bool AudioClient::connect(const std::string& server_host, int server_port) {
    if (connected_) {
        std::cout << "Already connected to server" << std::endl;
        return true;
    }

    std::cout << "🔗 Initializing 4-module architecture connection to " << server_host << ":" << server_port << "..." << std::endl;

    // ===== MODULE 2: INITIALIZE CAPTURE SINK (Network Transmission) =====
    CaptureSinkConfig sinkConfig;
    sinkConfig.serverHost = server_host;
    sinkConfig.serverPort = server_port;
    sinkConfig.maxQueueSize = 50;
    sinkConfig.heartbeatIntervalMs = 5000;
    sinkConfig.connectionTimeoutMs = 10000;
    sinkConfig.maxReconnectAttempts = 3;
    sinkConfig.reconnectDelayMs = 2000;
    
    if (!captureSink_.CaptureSinkInit(sinkConfig)) {
        std::cerr << "❌ Failed to initialize CaptureSink module" << std::endl;
        return false;
    }

    // ===== MODULE 3: INITIALIZE RENDER SOURCE (Network Reception) =====
    RenderSourceConfig sourceConfig;
    sourceConfig.serverHost = server_host;
    sourceConfig.serverPort = server_port;
    sourceConfig.sampleRate = 44100;
    sourceConfig.channels = 1;
    sourceConfig.minBufferMs = 20;
    sourceConfig.maxBufferMs = 200;
    sourceConfig.targetBufferMs = 50;
    sourceConfig.enableAdaptiveBuffer = true;
    sourceConfig.enablePacketLossRecovery = true;
    
    if (!renderSource_.RenderSourceInit(sourceConfig)) {
        std::cerr << "❌ Failed to initialize RenderSource module" << std::endl;
        captureSink_.CaptureSinkDeinit();
        return false;
    }

    // ===== ESTABLISH NETWORK CONNECTIONS =====
    std::cout << "🔄 Attempting to connect to server..." << std::endl;
    
    // Try connecting with retries
    bool connected = false;
    for (int attempt = 1; attempt <= 3; attempt++) {
        std::cout << "🔗 Connection attempt " << attempt << "/3..." << std::endl;
        
        if (captureSink_.connectToServer(server_host, server_port)) {
            connected = true;
            break;
        }
        
        if (attempt < 3) {
            std::cout << "⏳ Retrying in 2 seconds..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
    
    if (!connected) {
        std::cerr << "❌ CaptureSink failed to connect to server after 3 attempts" << std::endl;
        std::cerr << "💡 Make sure the server is running: .\\audsync_server.exe" << std::endl;
        captureSink_.CaptureSinkDeinit();
        renderSource_.RenderSourceDeinit();
        return false;
    }
    
    if (!renderSource_.startReceiving(server_host, server_port)) {
        std::cerr << "❌ RenderSource failed to start receiving" << std::endl;
        captureSink_.disconnectFromServer();
        captureSink_.CaptureSinkDeinit();
        renderSource_.RenderSourceDeinit();
        return false;
    }

    connected_ = true;
    running_ = true;

    // ===== START PROCESSING THREAD FOR MODULES =====
    processing_thread_ = std::thread([this]() {
        std::cout << "� Processing thread started for 4-module architecture" << std::endl;
        while (running_) {
            // Process each module per frame
            if (connected_) {
                captureSink_.CaptureSinkProcess();
                renderSource_.RenderSourceProcess();
            }
            
            // Small delay to prevent busy waiting (~172 FPS for audio processing)
            std::this_thread::sleep_for(std::chrono::microseconds(5800));
        }
        std::cout << "🔄 Processing thread stopped" << std::endl;
    });

    std::cout << "✅ Connected successfully using proper 4-module architecture" << std::endl;
    std::cout << "  📡 CaptureSink: Ready for transmission" << std::endl;
    std::cout << "  📥 RenderSource: Ready for reception" << std::endl;
    return true;
}

bool AudioClient::startAudio() {
    if (!connected_) {
        std::cerr << "❌ Not connected to server" << std::endl;
        return false;
    }

    if (audio_active_) {
        std::cout << "⚠️ Audio is already active" << std::endl;
        return true;
    }

    std::cout << "🎵 Starting 4-module audio system..." << std::endl;

    // ===== MODULE 1: INITIALIZE CAPTURE SOURCE (Microphone) =====
    CaptureSourceConfig captureConfig;
    captureConfig.deviceId = -1;  // Default input device
    captureConfig.sampleRate = 44100;
    captureConfig.channels = 1;
    captureConfig.framesPerBuffer = 256;
    captureConfig.enableLowLatency = false;  // Shared mode for multiple clients
    captureConfig.suggestedLatency = 0.1f; // 100ms latency for reliable sharing
    
    std::cout << "🎤 Initializing audio capture in shared mode..." << std::endl;
    
    if (!captureSource_.CaptureSourceInit(captureConfig)) {
        std::cerr << "❌ Failed to initialize CaptureSource module" << std::endl;
        std::cerr << "🔧 Audio device access failed - check permissions" << std::endl;
        return false;
    } else {
        std::cout << "✅ Audio capture initialized successfully" << std::endl;
    }

    // ===== MODULE 4: INITIALIZE RENDER SINK (Speakers) =====
    RenderSinkConfig renderConfig;
    renderConfig.outputDeviceId = -1;  // Default output device
    renderConfig.sampleRate = 44100;
    renderConfig.channels = 1;
    renderConfig.framesPerBuffer = 256;
    renderConfig.playbackBufferSizeMs = 100;  // Increased buffer for shared mode
    renderConfig.enableLowLatency = false;  // Shared mode for multiple clients
    renderConfig.initialVolume = 1.0f;
    
    std::cout << "🔊 Initializing audio playback in shared mode..." << std::endl;
    
    if (!renderSink_.RenderSinkInit(renderConfig)) {
        std::cerr << "❌ Failed to initialize RenderSink module" << std::endl;
        std::cerr << "🔧 Audio playback device access failed" << std::endl;
        captureSource_.CaptureSourceDeinit();
        return false;
    }
    
    std::cout << "✅ Audio playback initialized successfully" << std::endl;

    // ===== CONNECT THE 4-MODULE PIPELINE =====
    
    // Module 1 -> Module 2: CaptureSource -> CaptureSink
    captureSource_.setCaptureCallback(
        [this](const float* data, size_t samples, uint64_t timestamp) {
            try {
                // Safety checks
                if (!data || samples == 0) return;
                
                // Send captured audio through network
                static int packet_count = 0;
                packet_count++;
                if (packet_count % 100 == 0) {
                    std::cout << "📤 Sent " << packet_count << " audio packets (samples: " << samples << ")" << std::endl;
                }
                captureSink_.sendAudioData(data, samples, timestamp);
            } catch (const std::exception& e) {
                std::cerr << "❌ CaptureSource callback error: " << e.what() << std::endl;
            }
        }
    );

    // Module 3 -> Module 4: RenderSource -> RenderSink
    renderSource_.setRenderCallback(
        [this](const float* data, size_t samples, uint64_t timestamp) {
            try {
                // Safety checks
                if (!data || samples == 0) return;
                
                // Queue received audio for playback
                static int receive_count = 0;
                receive_count++;
                if (receive_count % 100 == 0) {
                    std::cout << "📥 Received " << receive_count << " audio packets (samples: " << samples << ")" << std::endl;
                }
                renderSink_.queueAudioData(data, samples, timestamp);
            } catch (const std::exception& e) {
                std::cerr << "❌ RenderSource callback error: " << e.what() << std::endl;
            }
        }
    );

    // ===== START AUDIO PROCESSING =====
    
    std::cout << "🔧 Starting CaptureSource..." << std::endl;
    if (!captureSource_.startCapture()) {
        std::cerr << "❌ Failed to start audio capture (Module 1)" << std::endl;
        std::cerr << "� Try closing other audio applications or check microphone permissions" << std::endl;
        captureSource_.CaptureSourceDeinit();
        renderSink_.RenderSinkDeinit();
        return false;
    }
    std::cout << "✅ CaptureSource started successfully" << std::endl;

    std::cout << "🔧 Starting RenderSink..." << std::endl;
    if (!renderSink_.startPlayback()) {
        std::cerr << "❌ Failed to start audio playback (Module 4)" << std::endl;
        captureSource_.stopCapture();
        captureSource_.CaptureSourceDeinit();
        renderSink_.RenderSinkDeinit();
        return false;
    }
    std::cout << "✅ RenderSink started successfully" << std::endl;

    audio_active_ = true;
    
    std::cout << "🎵 4-Module audio system started successfully!" << std::endl;
    std::cout << "  🎤 Module 1 (CaptureSource): Capturing from microphone ✅" << std::endl;
    std::cout << "  📡 Module 2 (CaptureSink): Transmitting to server ✅" << std::endl;
    std::cout << "  📥 Module 3 (RenderSource): Receiving from server ✅" << std::endl;
    std::cout << "  🔊 Module 4 (RenderSink): Playing through speakers ✅" << std::endl;

    return true;
}

void AudioClient::stopAudio() {
    if (!audio_active_) return;
    
    std::cout << "🛑 Stopping 4-module audio system..." << std::endl;
    
    // Stop modules in reverse order
    renderSink_.stopPlayback();
    captureSource_.stopCapture();
    
    // Deinitialize audio modules (modules 1 & 4)
    renderSink_.RenderSinkDeinit();
    captureSource_.CaptureSourceDeinit();
    
    audio_active_ = false;
    std::cout << "✅ 4-module audio system stopped" << std::endl;
}

void AudioClient::disconnect() {
    std::cout << "🔌 Disconnecting 4-module system..." << std::endl;
    
    // Stop audio processing first
    stopAudio();
    
    // Stop processing thread
    running_ = false;
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
    
    // Disconnect and deinitialize network modules (modules 2 & 3)
    if (connected_) {
        renderSource_.stopReceiving();
        captureSink_.disconnectFromServer();
        
        renderSource_.RenderSourceDeinit();
        captureSink_.CaptureSinkDeinit();
    }
    
    connected_ = false;
    std::cout << "✅ Disconnected from server - all 4 modules deinitialized" << std::endl;
}

void AudioClient::showComprehensiveStats() {
    if (!connected_) {
        std::cout << "❌ Not connected - no stats available" << std::endl;
        return;
    }

    auto captureStats = captureSource_.getStats();
    auto sinkStats = captureSink_.getStats();
    auto renderSourceStats = renderSource_.getStats();
    auto renderSinkStats = renderSink_.getStats();
    
    std::cout << "\n╔═══════════════════════════════════════════════╗" << std::endl;
    std::cout <<   "║          4-MODULE ARCHITECTURE STATISTICS     ║" << std::endl;
    std::cout <<   "╠═══════════════════════════════════════════════╣" << std::endl;
    
    std::cout << "║ 🎤 MODULE 1: CAPTURE SOURCE (Microphone)     ║" << std::endl;
    std::cout << "║   Frames Processed: " << std::setw(20) << captureStats.totalFramesProcessed << "      ║" << std::endl;
    std::cout << "║   Dropped Frames:   " << std::setw(20) << captureStats.totalDroppedFrames << "      ║" << std::endl;
    std::cout << "║   Current Latency:  " << std::setw(15) << std::fixed << std::setprecision(2) 
              << captureStats.currentLatency * 1000.0 << "ms     ║" << std::endl;
    std::cout << "║   CPU Load:         " << std::setw(15) << std::fixed << std::setprecision(1) 
              << captureStats.cpuLoad << "%      ║" << std::endl;
    std::cout << "║   Is Active:        " << std::setw(15) << (captureStats.isActive ? "Yes" : "No") << "        ║" << std::endl;
    
    std::cout << "╠═══════════════════════════════════════════════╣" << std::endl;
    std::cout << "║ 📡 MODULE 2: CAPTURE SINK (Network TX)       ║" << std::endl;
    std::cout << "║   Packets Sent:     " << std::setw(20) << sinkStats.totalPacketsSent << "      ║" << std::endl;
    std::cout << "║   Packets Dropped:  " << std::setw(20) << sinkStats.totalPacketsDropped << "      ║" << std::endl;
    std::cout << "║   Bytes Transmitted:" << std::setw(15) << sinkStats.totalBytesTransmitted / 1024 << "KB     ║" << std::endl;
    std::cout << "║   Avg Latency:      " << std::setw(15) << std::fixed << std::setprecision(2) 
              << sinkStats.averageLatency << "ms     ║" << std::endl;
    std::cout << "║   Bandwidth:        " << std::setw(15) << std::fixed << std::setprecision(1) 
              << sinkStats.bandwidthUtilization / 1024.0 << "KB/s   ║" << std::endl;
    std::cout << "║   Connection:       " << std::setw(15) << (sinkStats.isConnected ? "Active" : "Inactive") << "        ║" << std::endl;
    
    std::cout << "╠═══════════════════════════════════════════════╣" << std::endl;
    std::cout << "║ 📥 MODULE 3: RENDER SOURCE (Network RX)      ║" << std::endl;
    std::cout << "║   Packets Received: " << std::setw(20) << renderSourceStats.totalPacketsReceived << "      ║" << std::endl;
    std::cout << "║   Packets Lost:     " << std::setw(20) << renderSourceStats.totalPacketsLost << "      ║" << std::endl;
    std::cout << "║   Loss Rate:        " << std::setw(15) << std::fixed << std::setprecision(2) 
              << renderSourceStats.packetLossRate << "%      ║" << std::endl;
    std::cout << "║   Network Jitter:   " << std::setw(15) << std::fixed << std::setprecision(2) 
              << renderSourceStats.networkJitter << "ms     ║" << std::endl;
    std::cout << "║   Buffer Size:      " << std::setw(15) << renderSourceStats.currentBufferSizeMs << "ms     ║" << std::endl;
    std::cout << "║   Buffer Ready:     " << std::setw(15) << (renderSourceStats.isBufferReady ? "Yes" : "No") << "        ║" << std::endl;
    std::cout << "║   Silence Inserted: " << std::setw(20) << renderSourceStats.totalSilenceInserted << "      ║" << std::endl;
    
    std::cout << "╠═══════════════════════════════════════════════╣" << std::endl;
    std::cout << "║ 🔊 MODULE 4: RENDER SINK (Speakers)          ║" << std::endl;
    std::cout << "║   Samples Played:   " << std::setw(20) << renderSinkStats.totalSamplesPlayed << "      ║" << std::endl;
    std::cout << "║   Underruns:        " << std::setw(20) << renderSinkStats.totalUnderruns << "      ║" << std::endl;
    std::cout << "║   Current Latency:  " << std::setw(15) << std::fixed << std::setprecision(2) 
              << renderSinkStats.currentLatency << "ms     ║" << std::endl;
    std::cout << "║   CPU Load:         " << std::setw(15) << std::fixed << std::setprecision(1) 
              << renderSinkStats.cpuLoad << "%      ║" << std::endl;
    std::cout << "║   Volume:           " << std::setw(15) << std::fixed << std::setprecision(1) 
              << renderSinkStats.currentVolume * 100.0 << "%      ║" << std::endl;
    std::cout << "║   Is Playing:       " << std::setw(15) << (renderSinkStats.isPlaying ? "Yes" : "No") << "        ║" << std::endl;
    
    std::cout << "╚═══════════════════════════════════════════════╝" << std::endl;
    
    // Overall health assessment
    std::cout << "\n🏥 SYSTEM HEALTH:" << std::endl;
    
    bool isHealthy = true;
    if (renderSourceStats.packetLossRate > 5.0) {
        std::cout << "⚠️  High packet loss rate (" << renderSourceStats.packetLossRate << "%)" << std::endl;
        isHealthy = false;
    }
    if (renderSinkStats.totalUnderruns > 10) {
        std::cout << "⚠️  Frequent audio underruns (" << renderSinkStats.totalUnderruns << ")" << std::endl;
        isHealthy = false;
    }
    if (renderSourceStats.networkJitter > 20.0) {
        std::cout << "⚠️  High network jitter (" << renderSourceStats.networkJitter << "ms)" << std::endl;
        isHealthy = false;
    }
    if (!sinkStats.isConnected) {
        std::cout << "⚠️  Network transmission connection lost" << std::endl;
        isHealthy = false;
    }
    
    if (isHealthy) {
        std::cout << "✅ All 4 modules operating normally" << std::endl;
    }
}

void AudioClient::setVolume(float volume) {
    if (audio_active_) {
        renderSink_.setVolume(volume);
        std::cout << "🔊 Volume set to " << (volume * 100.0f) << "%" << std::endl;
    }
}

void AudioClient::setMuted(bool muted) {
    if (audio_active_) {
        renderSink_.setMuted(muted);
        std::cout << (muted ? "🔇 Audio muted" : "🔊 Audio unmuted") << std::endl;
    }
}