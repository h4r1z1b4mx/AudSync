#pragma once
#include <vector>
#include <cstdint>
#include <string>

// Audio network packet for transmitting audio data
class AudioNetworkPacket {
public:
    // Packet header structure
    struct Header {
        uint32_t magic;         // Magic number for packet validation
        uint32_t version;       // Protocol version
        uint64_t timestamp;     // Timestamp of audio data
        uint32_t sequenceNumber; // Sequence number for packet ordering
        uint32_t sampleRate;    // Sample rate of audio data
        uint16_t channels;      // Number of audio channels
        uint16_t bitsPerSample; // Bits per sample (usually 32 for float)
        uint32_t dataSize;      // Size of audio data in bytes
        uint32_t checksum;      // Simple checksum for data integrity
    };

    // Constants
    static const uint32_t PACKET_MAGIC = 0x41534E44; // "ASND" - Audio Sound
    static const uint32_t PROTOCOL_VERSION = 1;

    AudioNetworkPacket();
    ~AudioNetworkPacket();

    // Packet creation and serialization
    bool createPacket(const float* audioData, size_t samples, 
                     uint64_t timestamp, uint32_t sequenceNumber,
                     int sampleRate, int channels);
    
    std::vector<uint8_t> serialize() const;
    bool deserialize(const uint8_t* data, size_t dataSize);

    // Data access
    const Header& getHeader() const { return header_; }
    const std::vector<float>& getAudioData() const { return audioData_; }
    
    // Validation
    bool isValid() const;
    uint32_t calculateChecksum() const;

    // Utility
    size_t getPacketSize() const;
    void clear();

private:
    Header header_;
    std::vector<float> audioData_;
    
    void initializeHeader();
    bool validateHeader() const;
};
