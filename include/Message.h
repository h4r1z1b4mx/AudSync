#pragma once

#include <vector>
#include <cstdint>
#include <cstring>

enum class MessageType : uint16_t {
    AUDIO_DATA = 1,
    HEARTBEAT = 2,
    CONFIG = 3,
    DISCONNECT = 4
};

struct MessageHeader {
    uint32_t magic;           // Magic number for validation
    MessageType type;         // Message type
    uint32_t length;          // Total message length including header
    uint32_t sequence;        // Sequence number for ordering
    uint64_t timestamp;       // Timestamp in microseconds
    
    MessageHeader() : magic(0x41554453), type(MessageType::AUDIO_DATA), 
                     length(sizeof(MessageHeader)), sequence(0), timestamp(0) {}
};

class Message {
public:
    Message() = default;
    Message(MessageType type);
    Message(MessageType type, const void* data, size_t dataSize);
    
    // Getters
    MessageType getType() const { return header_.type; }
    uint32_t getSequence() const { return header_.sequence; }
    uint64_t getTimestamp() const { return header_.timestamp; }
    uint32_t getLength() const { return header_.length; }
    const std::vector<uint8_t>& getData() const { return data_; }
    
    // Setters
    void setSequence(uint32_t seq) { header_.sequence = seq; }
    void setTimestamp(uint64_t ts) { header_.timestamp = ts; }
    void setData(const void* data, size_t size);
    
    // Serialization
    std::vector<uint8_t> serialize() const;
    bool deserialize(const std::vector<uint8_t>& buffer);
    bool deserialize(const void* buffer, size_t bufferSize);
    
    // Validation
    bool isValid() const;
    
    // Audio data helpers
    void setAudioData(const float* samples, size_t sampleCount);
    bool getAudioData(std::vector<float>& samples) const;
    
private:
    MessageHeader header_;
    std::vector<uint8_t> data_;
    
    void updateLength();
};

// Utility functions for timestamp
uint64_t getCurrentTimestamp();
