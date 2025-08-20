#include "AudioBuffer.h"
#include <algorithm>

AudioBuffer::AudioBuffer(size_t capacity)
    : buffer_(capacity), capacity_(capacity), read_pos(0), write_pos(0), size_(0) {}

AudioBuffer::~AudioBuffer() = default;

// FIXED: Non-blocking write with immediate return
bool AudioBuffer::write(const float* data, size_t samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Calculate available space
    size_t available_space = capacity_ - size_;
    size_t to_write = std::min(samples, available_space);
    
    if (to_write == 0) {
        return false; // Buffer full, drop data (better than blocking)
    }
    
    // Write samples to circular buffer
    for (size_t i = 0; i < to_write; ++i) {
        buffer_[write_pos] = data[i];
        write_pos = (write_pos + 1) % capacity_;
    }
    
    size_ += to_write;
    return to_write == samples; // Return true only if all samples written
}

// FIXED: Non-blocking read with silence padding
bool AudioBuffer::read(float* data, size_t samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t to_read = std::min(samples, size_);
    
    // Read available samples
    for (size_t i = 0; i < to_read; ++i) {
        data[i] = buffer_[read_pos];
        read_pos = (read_pos + 1) % capacity_;
    }
    
    // Fill remainder with silence (smooth dropout)
    for (size_t i = to_read; i < samples; ++i) {
        data[i] = 0.0f;
    }
    
    size_ -= to_read;
    return to_read > 0; // Return true if any data was read
}

// ... rest of methods unchanged ...
size_t AudioBuffer::available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}

size_t AudioBuffer::space() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capacity_ - size_;
}

void AudioBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    read_pos = write_pos = size_ = 0;
}

bool AudioBuffer::isEmpty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_ == 0;
}

bool AudioBuffer::isFull() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_ == capacity_;
}