#include "Message.h"
#include <chrono>
#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

Message::Message(MessageType type) {
    header_.type = type;
    header_.timestamp = getCurrentTimestamp();
    updateLength();
}

Message::Message(MessageType type, const void* data, size_t dataSize) {
    header_.type = type;
    header_.timestamp = getCurrentTimestamp();
    setData(data, dataSize);
}

void Message::setData(const void* data, size_t size) {
    data_.clear();
    if (data && size > 0) {
        data_.resize(size);
        std::memcpy(data_.data(), data, size);
    }
    updateLength();
}

std::vector<uint8_t> Message::serialize() const {
    std::vector<uint8_t> buffer;
    buffer.resize(header_.length);
    
    // Copy header
    std::memcpy(buffer.data(), &header_, sizeof(MessageHeader));
    
    // Copy data if any
    if (!data_.empty()) {
        std::memcpy(buffer.data() + sizeof(MessageHeader), data_.data(), data_.size());
    }
    
    return buffer;
}

bool Message::deserialize(const std::vector<uint8_t>& buffer) {
    return deserialize(buffer.data(), buffer.size());
}

bool Message::deserialize(const void* buffer, size_t bufferSize) {
    if (!buffer || bufferSize < sizeof(MessageHeader)) {
        return false;
    }
    
    // Copy header
    std::memcpy(&header_, buffer, sizeof(MessageHeader));
    
    // Validate magic number
    if (header_.magic != 0x41554453) { // "AUDS"
        return false;
    }
    
    // Validate length
    if (header_.length < sizeof(MessageHeader) || header_.length > bufferSize) {
        return false;
    }
    
    // Copy data if any
    size_t dataSize = header_.length - sizeof(MessageHeader);
    if (dataSize > 0) {
        data_.resize(dataSize);
        std::memcpy(data_.data(), static_cast<const uint8_t*>(buffer) + sizeof(MessageHeader), dataSize);
    } else {
        data_.clear();
    }
    
    return true;
}

bool Message::isValid() const {
    return header_.magic == 0x41554453 && 
           header_.length >= sizeof(MessageHeader) &&
           header_.length == sizeof(MessageHeader) + data_.size();
}

void Message::setAudioData(const float* samples, size_t sampleCount) {
    if (!samples || sampleCount == 0) {
        data_.clear();
        updateLength();
        return;
    }
    
    size_t dataSize = sampleCount * sizeof(float);
    data_.resize(dataSize);
    std::memcpy(data_.data(), samples, dataSize);
    updateLength();
}

bool Message::getAudioData(std::vector<float>& samples) const {
    if (data_.empty() || data_.size() % sizeof(float) != 0) {
        return false;
    }
    
    size_t sampleCount = data_.size() / sizeof(float);
    samples.resize(sampleCount);
    std::memcpy(samples.data(), data_.data(), data_.size());
    
    return true;
}

void Message::updateLength() {
    header_.length = sizeof(MessageHeader) + data_.size();
}

uint64_t getCurrentTimestamp() {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}
