#include "JitterBuffer.h"
#include <algorithm>
#include <cmath>
#include <iostream> 

JitterBuffer::JitterBuffer(size_t maxBufferSize)
    : maxBufferSize_(maxBufferSize), minBufferSize_(2) {} // REDUCED: 3 packets minimum

void JitterBuffer::addPacket(const AudioPacket& packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // ADDED: Detect sequence gaps
    if (!buffer_.empty()) {
        uint32_t expectedNext = lastSequenceNumber_ + 1;
        if (packet.sequenceNumber > expectedNext) {
            uint32_t gapSize = packet.sequenceNumber - expectedNext;
            std::cout << "Packet gap detected: missing " << gapSize << " packets" << std::endl;
            
            // ADDED: Fill gap with silence packets if gap is small
            if (gapSize <= 3) {
                for (uint32_t seq = expectedNext; seq < packet.sequenceNumber; ++seq) {
                    AudioPacket silencePacket;
                    silencePacket.sequenceNumber = seq;
                    silencePacket.data.resize(packet.data.size(), 0); // Silent audio
                    silencePacket.timestamp = packet.timestamp - (packet.sequenceNumber - seq) * 5; // Estimate timestamp
                    
                    buffer_.push(silencePacket);
                }
            }
        }
    }
    
    // Drop oldest packets if buffer is full
    while (buffer_.size() >= maxBufferSize_) {
        AudioPacket oldest = buffer_.top();
        buffer_.pop();
        seenSequences_.erase(oldest.sequenceNumber);
    }
    
    buffer_.push(packet);
    seenSequences_.insert(packet.sequenceNumber);
    lastSequenceNumber_ = packet.sequenceNumber;
}

bool JitterBuffer::getPacket(AudioPacket& packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (buffer_.empty()) {
        return false;
    }
    
    packet = buffer_.top();
    buffer_.pop();
    seenSequences_.erase(packet.sequenceNumber);
    
    // ADDED: Apply audio filters before returning packet
    //applyAudioFilters(packet);
    
    return true;
}

// ADDED: Audio filtering methods
void JitterBuffer::applyAudioFilters(AudioPacket& packet) {
    if (packet.data.size() < sizeof(float)) return;
    
    float* audioData = reinterpret_cast<float*>(packet.data.data());
    size_t samples = packet.data.size() / sizeof(float);
    
    // Apply multiple filters for better voice quality
    applyNoiseGate(audioData, samples);
    applyBandpassFilter(audioData, samples);
    applyVolumeNormalization(audioData, samples);
    applyAntiClipping(audioData, samples);
}

void JitterBuffer::applyNoiseGate(float* data, size_t samples) {
    const float threshold = 0.01f; // Noise gate threshold
    const float ratio = 0.1f;      // Reduction ratio
    
    for (size_t i = 0; i < samples; ++i) {
        float absValue = std::abs(data[i]);
        if (absValue < threshold) {
            data[i] *= ratio; // Reduce low-level noise
        }
    }
}

void JitterBuffer::applyBandpassFilter(float* data, size_t samples) {
    // Simple highpass filter to remove low-frequency noise
    const float alpha = 0.95f; // Highpass cutoff (~100Hz at 44.1kHz)
    static float lastInput = 0.0f;
    static float lastOutput = 0.0f;
    
    for (size_t i = 0; i < samples; ++i) {
        float output = alpha * (lastOutput + data[i] - lastInput);
        lastInput = data[i];
        lastOutput = output;
        data[i] = output;
    }
    
    // Simple lowpass filter to remove high-frequency noise
    const float beta = 0.7f; // Lowpass cutoff (~3.5kHz at 44.1kHz)
    static float lpLastOutput = 0.0f;
    
    for (size_t i = 0; i < samples; ++i) {
        lpLastOutput = beta * lpLastOutput + (1.0f - beta) * data[i];
        data[i] = lpLastOutput;
    }
}

void JitterBuffer::applyVolumeNormalization(float* data, size_t samples) {
    // Calculate RMS level
    float rms = 0.0f;
    for (size_t i = 0; i < samples; ++i) {
        rms += data[i] * data[i];
    }
    rms = std::sqrt(rms / samples);
    
    // Target RMS level for voice
    const float targetRMS = 0.2f;
    
    if (rms > 0.001f) { // Avoid division by zero
        float gain = targetRMS / rms;
        
        // Limit gain to prevent over-amplification
        gain = std::min(gain, 3.0f);
        gain = std::max(gain, 0.3f);
        
        // Apply gentle gain adjustment
        for (size_t i = 0; i < samples; ++i) {
            data[i] *= gain;
        }
    }
}

void JitterBuffer::applyAntiClipping(float* data, size_t samples) {
    // Soft limiting to prevent clipping
    for (size_t i = 0; i < samples; ++i) {
        if (data[i] > 0.95f) {
            data[i] = 0.95f + 0.05f * std::tanh((data[i] - 0.95f) / 0.05f);
        } else if (data[i] < -0.95f) {
            data[i] = -0.95f + 0.05f * std::tanh((data[i] + 0.95f) / 0.05f);
        }
    }
}

void JitterBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::priority_queue<AudioPacket> empty;
    std::swap(buffer_, empty);
    seenSequences_.clear();
}

size_t JitterBuffer::getBufferSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.size();
}

bool JitterBuffer::isReady() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.size() >= minBufferSize_;
}

void JitterBuffer::setMinBufferSize(size_t minSize) {
    std::lock_guard<std::mutex> lock(mutex_);
    minBufferSize_ = minSize;
}

void JitterBuffer::setMaxBufferSize(size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    maxBufferSize_ = size;
    while (buffer_.size() > maxBufferSize_) {
        AudioPacket oldest = buffer_.top();
        buffer_.pop();
        seenSequences_.erase(oldest.sequenceNumber);
    }
}