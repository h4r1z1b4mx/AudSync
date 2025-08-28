#include "JitterBuffer.h"

JitterBuffer::JitterBuffer(size_t maxBufferSize)
    : maxBufferSize_(maxBufferSize) {}

void JitterBuffer::addPacket(const AudioPacket& packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (seenTimestamps_.count(packet.timestamp)) {
        // Duplicate packet, ignore
        return;
    }
    seenTimestamps_.insert(packet.timestamp);
    if (buffer_.size() >= maxBufferSize_) {
        buffer_.pop(); // Drop oldest (earliest) packet
    }
    buffer_.push(packet);
}

bool JitterBuffer::getPacket(AudioPacket& packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (buffer_.empty()) {
        return false;
    }
    packet = buffer_.top();
    buffer_.pop();
    seenTimestamps_.erase(packet.timestamp);
    return true;
}

void JitterBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::priority_queue<AudioPacket> empty;
    std::swap(buffer_, empty);
    seenTimestamps_.clear();
}

void JitterBuffer::setMaxBufferSize(size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    maxBufferSize_ = size;
    while (buffer_.size() > maxBufferSize_) {
        buffer_.pop();
    }
}