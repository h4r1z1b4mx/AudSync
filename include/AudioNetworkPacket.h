#pragma once

#include <vector>
#include <cstdint>

// Network packet structure for audio transmission
struct AudioNetworkPacket {
    uint32_t sequenceNumber;
    uint64_t timestamp;
    uint32_t sampleRate;
    uint16_t channels;
    uint16_t dataSize;
    std::vector<uint8_t> audioData;
    uint32_t checksum;
};
