#pragma once
#include <vector>
#include <queue>
#include <mutex>
#include <cstdint>
#include <set>

struct AudioPacket {
    std::vector<uint8_t> data;
    uint64_t timestamp;
    bool operator<(const AudioPacket& other) const {
        return timestamp > other.timestamp; // For min-heap (earliest first)
    }
};

class JitterBuffer {
public:
    JitterBuffer(size_t maxBufferSize = 50);

    void addPacket(const AudioPacket& packet);
    bool getPacket(AudioPacket& packet);
    void clear();

    void setMaxBufferSize(size_t size);

private:
    std::priority_queue<AudioPacket> buffer_; // Min-heap by timestamp
    size_t maxBufferSize_;
    std::mutex mutex_;
    std::set<uint64_t> seenTimestamps_; // Prevent duplicates
};