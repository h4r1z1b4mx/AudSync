#include "AudioClient.h"
#include "AudioRecorder.h"
#include "SessionLogger.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <portaudio.h>
#include <algorithm>
#include <cmath>
#include <thread>

// Updated constructor to include output device and buffer size
AudioClient::AudioClient(int inputDeviceId,
                         int outputDeviceId,
                         int sampleRate,
                         int channels,
                         int framesPerBuffer,
                         SessionLogger* logger,
                         AudioRecorder* recorder,
                         JitterBuffer* jitterBuffer)
    : inputDeviceId_(inputDeviceId),
      outputDeviceId_(outputDeviceId),
      sampleRate_(sampleRate),
      channels_(channels),
      framesPerBuffer_(framesPerBuffer),
      logger_(logger),
      recorder_(recorder),
      jitterBuffer_(jitterBuffer),
      outgoingSequenceNumber_(0),
      incomingSequenceNumber_(0),  // FIXED: Add incoming sequence tracking
      jitterBufferReady_(false),
      lastPacketTime_(std::chrono::steady_clock::now()),   // FIXED: Add buffer readiness tracking
      connected_(false),
      audio_active_(false),
      running_(false) {
}

AudioClient::~AudioClient() {
    disconnect();
}

bool AudioClient::connect(const std::string& server_host, int server_port) {
    if (connected_) return true;

    network_manager_.setMessageHandler(
        [this](const Message& msg, int socket) {
            handleNetworkMessage(msg, socket);
        }
    );

    if (!network_manager_.connectToServer(server_host, server_port)) {
        std::cerr << "Failed to connect to server" << std::endl;
        return false;
    }

    // Send client audio configuration to server
    Message config_msg;
    config_msg.type = MessageType::CLIENT_CONFIG;
    
    struct AudioConfig {
        int32_t sampleRate;
        int32_t channels;
        int32_t bufferSize;
    } config;
    
    config.sampleRate = sampleRate_;
    config.channels = channels_;
    config.bufferSize = framesPerBuffer_;
    
    config_msg.size = sizeof(AudioConfig);
    config_msg.data.resize(config_msg.size);
    config_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    
    std::memcpy(config_msg.data.data(), &config, sizeof(AudioConfig));
    network_manager_.sendMessage(config_msg);
    
    connected_ = true;
    running_ = true;
    
    network_thread_ = std::thread(&AudioClient::networkLoop, this);
    
    // FIXED: Start jitter buffer processing thread only for reception
    if (jitterBuffer_) {
        jitterBufferRunning_ = true;
        jitterBufferThread_ = std::thread(&AudioClient::jitterBufferLoop, this);
    }
    
    std::cout << "Connected to server at " << server_host << ":" << server_port << std::endl;
    std::cout << "Sent configuration:" << std::endl;
    std::cout << "  Sample Rate: " << sampleRate_ << "Hz" << std::endl;
    std::cout << "  Channels: " << channels_ << std::endl;
    std::cout << "  Buffer Size: " << framesPerBuffer_ << " frames" << std::endl;
    
    return true;
}

void AudioClient::disconnect() {
    running_ = false;
    jitterBufferRunning_ = false;
    
    if (network_thread_.joinable()) {
        network_thread_.join();
    }
    
    if (jitterBufferThread_.joinable()) {
        jitterBufferThread_.join();
    }
    
    stopAudio();
    network_manager_.disconnect();
    connected_ = false;
    
    std::cout << "Disconnected from server" << std::endl;
}

bool AudioClient::startAudio() {
    if (!connected_ || audio_active_) return false;

    if (!audio_processor_.initialize(inputDeviceId_, outputDeviceId_, sampleRate_, channels_, framesPerBuffer_)) {
        std::cerr << "Failed to initialize audio processor" << std::endl;
        return false;
    }

    // FIXED: Optimal jitter buffer configuration for voice quality
    if (jitterBuffer_) {
        jitterBuffer_->clear();  // Clear any old data
        jitterBuffer_->setMinBufferSize(3);   // REDUCED: Faster start (3 packets ~8.7ms)
        jitterBuffer_->setMaxBufferSize(64);  // REDUCED: Lower latency
        jitterBufferReady_ = false;
        incomingSequenceNumber_ = 0;  // Reset sequence
        
        std::cout << "Jitter buffer configured: min=3, max=64 packets (optimized for voice)" << std::endl;
    }

    audio_processor_.setAudioCaptureCallback(
        [this](const float* data, size_t samples) {
            onAudioCaptured(data, samples);
        }
    );

    if (!audio_processor_.startRecording()) {
        std::cerr << "Failed to start recording" << std::endl;
        return false;
    }

    if (!audio_processor_.startPlayback()) {
        std::cerr << "Failed to start playback" << std::endl;
        audio_processor_.stop();
        return false;
    }

    Message ready_msg;
    ready_msg.type = MessageType::CLIENT_READY;
    ready_msg.size = 0;
    ready_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    network_manager_.sendMessage(ready_msg);

    audio_active_ = true;
    std::cout << "Audio system started - low latency mode active..." << std::endl;
    return true;
}

void AudioClient::stopAudio() {
    if (!audio_active_) return;
    
    audio_processor_.stop();
    audio_processor_.cleanup();
    audio_active_ = false;
    
    std::cout << "Audio system stopped" << std::endl;
}

bool AudioClient::isConnected() const {
    return connected_;
}

bool AudioClient::isAudioActive() const {
    return audio_active_;
}

void AudioClient::run() {
    std::cout << "AudSync Client" << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  start     - Start audio streaming" << std::endl;
    std::cout << "  stop      - Stop audio streaming" << std::endl;
    std::cout << "  logon     - Start logging" << std::endl;
    std::cout << "  logoff    - Stop logging" << std::endl;
    std::cout << "  recstart  - Start recording session" << std::endl;
    std::cout << "  recstop   - Stop recording session" << std::endl;
    std::cout << "  quit      - Disconnect and exit" << std::endl;

    std::string command;
    while (running_ && std::cin >> command) {
        if (command == "start") {
            if (!audio_active_) {
                startAudio();
            } else {
                std::cout << "Audio already active" << std::endl;
            }
        } else if (command == "stop") {
            if (audio_active_) {
                stopAudio();
            } else {
                std::cout << "Audio not active" << std::endl;
            }
        } else if (command == "logon") {
            if (logger_) {
                std::string logPath = SessionLogger::generateLogPath("client_session", true);
                logger_->startLogging(logPath);
                std::cout << "Logging started: " << logPath << std::endl;
            }
        } else if (command == "logoff") {
            if (logger_) {
                logger_->stopLogging();
                std::cout << "Logging stopped." << std::endl;
            }
        } else if (command == "recstart") {
            if (recorder_) {
                std::string recordPath = AudioRecorder::generateRecordingPath("client_audio", true);
                recorder_->startRecording(recordPath, sampleRate_, channels_);
                std::cout << "Audio recording started: " << recordPath << std::endl;
            }
        } else if (command == "recstop") {
            if (recorder_) {
                recorder_->stopRecording();
                std::cout << "Audio recording stopped." << std::endl;
            }
        } else if (command == "quit") {
            break;
        } else {
            std::cout << "Unknown command: " << command << std::endl;
        }
    }
}
void AudioClient::handleNetworkMessage(const Message& message, int socket_fd) {
    (void)socket_fd;

    switch (message.type) {
        case MessageType::AUDIO_DATA:
            if (audio_active_ && message.size > 0) {
                if (jitterBuffer_) {
                    AudioPacket packet;
                    packet.data = message.data;
                    packet.timestamp = message.timestamp;
                    packet.sequenceNumber = incomingSequenceNumber_++;
                    
                    jitterBuffer_->addPacket(packet);
                    
                    // FIXED: Don't set ready immediately - let processJitterBuffer() handle it
                    // The ready state will be set when we have enough packets
                    
                } else {
                    // Direct playback fallback
                    const float* audio_data = reinterpret_cast<const float*>(message.data.data());
                    size_t samples = message.size / sizeof(float);
                    audio_processor_.addPlaybackData(audio_data, samples);
                }
            }
            break;
            
        case MessageType::HEARTBEAT:
            {
                Message response;
                response.type = MessageType::HEARTBEAT;
                response.size = 0;
                response.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now().time_since_epoch()
                ).count();
                network_manager_.sendMessage(response);
            }
            break;
            
        default:
            break;
    }
}

// FIXED: Direct transmission - NO jitter buffer for outgoing audio
void AudioClient::onAudioCaptured(const float* data, size_t samples) {
    if (!connected_ || !audio_active_) return;

    uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();

    // FIXED: DIRECT TRANSMISSION - No jitter buffer for outgoing audio
    Message audio_msg;
    audio_msg.type = MessageType::AUDIO_DATA;
    audio_msg.size = static_cast<uint32_t>(samples * sizeof(float));
    audio_msg.data.resize(audio_msg.size);
    audio_msg.timestamp = timestamp;
    std::memcpy(audio_msg.data.data(), data, audio_msg.size);
    
    // Send immediately to network
    network_manager_.sendMessage(audio_msg);

    // Logging
    if (logger_) {
        logger_->logAudioStats(audio_msg.size, sampleRate_, channels_, std::to_string(inputDeviceId_));
        logger_->logPacketMetadata(timestamp, audio_msg.size);
    }

    // Recording
    if (recorder_ && recorder_->isRecording()) {
        std::vector<uint8_t> raw(reinterpret_cast<const uint8_t*>(data),
                                 reinterpret_cast<const uint8_t*>(data) + audio_msg.size);
        recorder_->writeSamples(raw);
    }
}

void AudioClient::networkLoop() {
    while (running_) {
        Message message;
        if (network_manager_.receiveMessage(message)) {
            handleNetworkMessage(message, -1);
        } else {
            std::cout << "Connection to server lost" << std::endl;
            running_ = false;
            break;
        }
    }
}

// FIXED: Much faster jitter buffer processing
void AudioClient::jitterBufferLoop() {
    while (jitterBufferRunning_) {
        processJitterBuffer();
        // FIXED: Faster processing for real-time audio
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); // 200Hz processing
    }
}

// FIXED: More aggressive packet processing
// Add this method to AudioClient.cpp after processJitterBuffer()

void AudioClient::processJitterBuffer() {
    if (!jitterBuffer_ || !audio_active_) {
        return;
    }
    
    size_t currentBufferSize = jitterBuffer_->getBufferSize();
    
    // ENHANCED: Timeout-based buffer management
    if (!jitterBufferReady_) {
        if (currentBufferSize >= 3) {
            jitterBufferReady_ = true;
            lastPacketTime_ = std::chrono::steady_clock::now();
            std::cout << "Jitter buffer ready - " << currentBufferSize << " packets buffered" << std::endl;
        } else {
            // ADDED: Timeout fallback
            auto now = std::chrono::steady_clock::now();
            auto timeSinceLastPacket = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPacketTime_).count();
            
            if (timeSinceLastPacket > 100 && currentBufferSize > 0) { // 100ms timeout
                jitterBufferReady_ = true; // Start with whatever we have
                std::cout << "Buffer timeout - starting with " << currentBufferSize << " packets" << std::endl;
            }
        }
        return;
    }
    
    // ENHANCED: Adaptive packet processing
    int packetsToProcess = std::min(4, static_cast<int>(currentBufferSize));
    for (int i = 0; i < packetsToProcess; ++i) {
        AudioPacket packet;
        
        if (jitterBuffer_->getPacket(packet)) {
            const float* audio_data = reinterpret_cast<const float*>(packet.data.data());
            size_t samples = packet.data.size() / sizeof(float);
            
            if (samples > 0) {
                std::vector<float> filteredData(audio_data, audio_data + samples);
                applyVoiceFilters(filteredData.data(), samples);
                audio_processor_.addPlaybackData(filteredData.data(), samples);
            }
            
            lastPacketTime_ = std::chrono::steady_clock::now();
        } else {
            break;
        }
    }
    
    // ENHANCED: Intelligent underrun handling
    if (currentBufferSize == 0 && jitterBufferReady_) {
        auto now = std::chrono::steady_clock::now();
        auto underrunDuration = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPacketTime_).count();
        
        if (underrunDuration > 50) { // 50ms silence threshold
            jitterBufferReady_ = false;
            std::cout << "Buffer underrun - rebuffering (silence: " << underrunDuration << "ms)" << std::endl;
        }
    }
}
// ADDED: Voice-specific audio filters
void AudioClient::applyVoiceFilters(float* data, size_t samples) {
    // 1. Noise Gate - Remove background noise
    applyNoiseGate(data, samples);
    
    // 2. Voice EQ - Enhance voice frequencies
    applyVoiceEQ(data, samples);
    
    // 3. Compressor - Even out volume levels
    applyCompressor(data, samples);
    
    // 4. De-esser - Reduce harsh sibilant sounds
    applyDeEsser(data, samples);
}

void AudioClient::applyNoiseGate(float* data, size_t samples) {
    const float threshold = 0.005f; // Very sensitive gate
    const float ratio = 0.05f;      // Heavy reduction
    
    for (size_t i = 0; i < samples; ++i) {
        if (std::abs(data[i]) < threshold) {
            data[i] *= ratio;
        }
    }
}

void AudioClient::applyVoiceEQ(float* data, size_t samples) {
    // Simple voice presence boost (around 1-3kHz)
    static float hp_last = 0.0f, lp_last = 0.0f;
    
    for (size_t i = 0; i < samples; ++i) {
        // Highpass at ~200Hz
        float hp_out = 0.98f * (hp_last + data[i] - data[i]);
        hp_last = data[i];
        
        // Boost mid frequencies slightly
        float boosted = hp_out * 1.2f;
        
        // Gentle lowpass at ~4kHz
        lp_last = 0.8f * lp_last + 0.2f * boosted;
        
        data[i] = lp_last;
    }
}

    /**
     * @brief Simple compressor to even out volume levels
     *
     * This compressor implements a simple 4:1 ratio compression with a threshold of 0.3f.
     * This should be enough to reduce loud peaks without significantly affecting the overall volume.
     *
     * @param data The audio data to be processed
     * @param samples The number of samples in the data array
     */
void AudioClient::applyCompressor(float* data, size_t samples) {
    const float threshold = 0.3f;
    const float ratio = 0.25f; // 4:1 compression
    
    for (size_t i = 0; i < samples; ++i) {
        float abs_val = std::abs(data[i]);
        if (abs_val > threshold) {
            float excess = abs_val - threshold;
            float compressed = threshold + excess * ratio;
            data[i] = (data[i] > 0) ? compressed : -compressed;
        }
    }
}

    /**
     * @brief Simple de-esser to reduce harsh 's' sounds
     *
     * This method reduces the volume of sudden, high-amplitude sounds
     * (such as the 's' sound in speech) to prevent loud, harsh transients.
     *
     * The algorithm works by detecting the derivative (rate of change) of the
     * audio signal. If the derivative is large (i.e. the signal is changing
     * quickly) and the signal is above a certain amplitude, the signal is
     * reduced in volume to prevent harsh sounds.
     */
void AudioClient::applyDeEsser(float* data, size_t samples) {
    // Simple de-esser to reduce harsh 's' sounds
    static float last_sample = 0.0f;
    
    for (size_t i = 0; i < samples; ++i) {
        float derivative = data[i] - last_sample;
        if (std::abs(derivative) > 0.1f && std::abs(data[i]) > 0.2f) {
            data[i] *= 0.7f; // Reduce harsh transients
        }
        last_sample = data[i];
    }
}

std::vector<std::string> AudioClient::getInputDeviceNames() {
    std::vector<std::string> devices;
    Pa_Initialize();
    
    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxInputChannels > 0) {
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

std::vector<std::string> AudioClient::getOutputDeviceNames() {
    std::vector<std::string> devices;
    Pa_Initialize();
    
    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxOutputChannels > 0) {
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
                std::ostringstream oss;
                oss << "[" << i << "] " << info->name 
                    << " (Max: " << info->maxOutputChannels << " ch, "
                    << "Default: " << static_cast<int>(info->defaultSampleRate) << "Hz)";
                devices.push_back(oss.str());
            }
        }
    }
    
    Pa_Terminate();
    return devices;
}