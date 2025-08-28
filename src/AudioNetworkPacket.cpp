#include "AudioNetworkPacket.h"
#include <cstring>
#include <iostream>

AudioNetworkPacket::AudioNetworkPacket() {
    initializeHeader();
}

AudioNetworkPacket::~AudioNetworkPacket() {
    clear();
}

void AudioNetworkPacket::initializeHeader() {
    memset(&header_, 0, sizeof(Header));
    header_.magic = PACKET_MAGIC;
    header_.version = PROTOCOL_VERSION;
}

bool AudioNetworkPacket::createPacket(const float* audioData, size_t samples,
                                     uint64_t timestamp, uint32_t sequenceNumber,
                                     int sampleRate, int channels) {
    if (!audioData || samples == 0 || channels <= 0 || sampleRate <= 0) {
        return false;
    }

    // Initialize header
    initializeHeader();
    header_.timestamp = timestamp;
    header_.sequenceNumber = sequenceNumber;
    header_.sampleRate = static_cast<uint32_t>(sampleRate);
    header_.channels = static_cast<uint16_t>(channels);
    header_.bitsPerSample = 32; // float = 32 bits
    header_.dataSize = static_cast<uint32_t>(samples * sizeof(float));

    // Copy audio data
    audioData_.clear();
    audioData_.reserve(samples);
    audioData_.assign(audioData, audioData + samples);

    // Calculate checksum
    header_.checksum = calculateChecksum();

    return true;
}

std::vector<uint8_t> AudioNetworkPacket::serialize() const {
    std::vector<uint8_t> buffer;
    size_t totalSize = sizeof(Header) + audioData_.size() * sizeof(float);
    buffer.reserve(totalSize);

    // Serialize header
    const uint8_t* headerBytes = reinterpret_cast<const uint8_t*>(&header_);
    buffer.insert(buffer.end(), headerBytes, headerBytes + sizeof(Header));

    // Serialize audio data
    if (!audioData_.empty()) {
        const uint8_t* audioBytes = reinterpret_cast<const uint8_t*>(audioData_.data());
        size_t audioDataSize = audioData_.size() * sizeof(float);
        buffer.insert(buffer.end(), audioBytes, audioBytes + audioDataSize);
    }

    return buffer;
}

bool AudioNetworkPacket::deserialize(const uint8_t* data, size_t dataSize) {
    if (!data || dataSize < sizeof(Header)) {
        return false;
    }

    // Deserialize header
    memcpy(&header_, data, sizeof(Header));

    // Validate header
    if (!validateHeader()) {
        return false;
    }

    // Check if we have enough data for the audio payload
    size_t expectedSize = sizeof(Header) + header_.dataSize;
    if (dataSize < expectedSize) {
        return false;
    }

    // Deserialize audio data
    audioData_.clear();
    if (header_.dataSize > 0) {
        size_t audioSamples = header_.dataSize / sizeof(float);
        audioData_.reserve(audioSamples);

        const float* audioBytes = reinterpret_cast<const float*>(data + sizeof(Header));
        audioData_.assign(audioBytes, audioBytes + audioSamples);
    }

    // Validate checksum
    uint32_t calculatedChecksum = calculateChecksum();
    if (calculatedChecksum != header_.checksum) {
        std::cerr << "AudioNetworkPacket: Checksum mismatch. Expected: " 
                  << header_.checksum << ", Calculated: " << calculatedChecksum << std::endl;
        return false;
    }

    return true;
}

bool AudioNetworkPacket::isValid() const {
    return validateHeader() && 
           (audioData_.size() * sizeof(float) == header_.dataSize) &&
           (calculateChecksum() == header_.checksum);
}

bool AudioNetworkPacket::validateHeader() const {
    return header_.magic == PACKET_MAGIC &&
           header_.version == PROTOCOL_VERSION &&
           header_.channels > 0 &&
           header_.channels <= 8 && // Reasonable limit
           header_.sampleRate > 0 &&
           header_.sampleRate <= 192000 && // Reasonable limit
           header_.bitsPerSample == 32; // We only support float (32-bit)
}

uint32_t AudioNetworkPacket::calculateChecksum() const {
    // Simple checksum: XOR of all audio data bytes
    uint32_t checksum = 0;
    
    // Include header fields in checksum (excluding checksum field itself)
    checksum ^= header_.magic;
    checksum ^= header_.version;
    checksum ^= static_cast<uint32_t>(header_.timestamp);
    checksum ^= static_cast<uint32_t>(header_.timestamp >> 32);
    checksum ^= header_.sequenceNumber;
    checksum ^= header_.sampleRate;
    checksum ^= header_.channels;
    checksum ^= header_.bitsPerSample;
    checksum ^= header_.dataSize;

    // Include audio data in checksum
    for (const float& sample : audioData_) {
        uint32_t sampleBits = *reinterpret_cast<const uint32_t*>(&sample);
        checksum ^= sampleBits;
    }

    return checksum;
}

size_t AudioNetworkPacket::getPacketSize() const {
    return sizeof(Header) + audioData_.size() * sizeof(float);
}

void AudioNetworkPacket::clear() {
    initializeHeader();
    audioData_.clear();
}
