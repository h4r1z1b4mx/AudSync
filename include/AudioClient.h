#pragma once

#include "CaptureSource.h"
#include "CaptureSink.h"
#include "RenderSource.h"
#include "RenderSink.h"
#include <iostream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <string>

class AudioClient {
private:
    // ===== PROPER 4-MODULE ARCHITECTURE =====
    CaptureSource captureSource_;    // Module 1: Microphone capture (PortAudio)
    CaptureSink captureSink_;        // Module 2: Network transmission (IP packets)
    RenderSource renderSource_;     // Module 3: Network reception & jitter buffer
    RenderSink renderSink_;         // Module 4: Speaker playback (PortAudio)
    
    // Application state
    std::atomic<bool> connected_;
    std::atomic<bool> audio_active_;
    std::thread processing_thread_;
    std::atomic<bool> running_;

public:
    AudioClient() : connected_(false), audio_active_(false), running_(false) {}
    ~AudioClient() { disconnect(); }

    bool connect(const std::string& server_host, int server_port);
    bool startAudio();
    void stopAudio();
    void disconnect();
    void showComprehensiveStats();
    
    // Volume control
    void setVolume(float volume);
    void setMuted(bool muted);
};
