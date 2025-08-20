#pragma once
#include <vector>
#include <queue>
#include <mutex>
#include <cstdint>
#include <set>

struct AudioPacket {
    std::vector<uint8_t> data;
    uint64_t timestamp;
    uint32_t sequenceNumber;
    
    bool operator<(const AudioPacket& other) const {
        return sequenceNumber > other.sequenceNumber; // Min-heap (earliest first)
    }
};

class JitterBuffer {
public:
    JitterBuffer(size_t maxBufferSize = 256);

    void addPacket(const AudioPacket& packet);
    bool getPacket(AudioPacket& packet);
    void clear();
    
    // Buffer management methods
    size_t getBufferSize() const;
    bool isReady() const;
    void setMinBufferSize(size_t minSize);
    void setMaxBufferSize(size_t size);

private:
    std::priority_queue<AudioPacket> buffer_;
    size_t maxBufferSize_;
    size_t minBufferSize_;
    mutable std::mutex mutex_;
    std::set<uint32_t> seenSequences_;

    uint32_t lastSequenceNumber_ = 0;
    
    // ADDED: Audio filtering methods
    void applyAudioFilters(AudioPacket& packet);
    void applyNoiseGate(float* data, size_t samples);
    void applyBandpassFilter(float* data, size_t samples);
    void applyVolumeNormalization(float* data, size_t samples);
    void applyAntiClipping(float* data, size_t samples);
};