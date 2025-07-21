#include "AudioBuffer.h"
#include <algorithm>

AudioBuffer::AudioBuffer(size_t capacity) 
    : buffer_(capacity), capacity_(capacity), read_pos_(0), write_pos_(0), size_(0) {
}

AudioBuffer::~AudioBuffer() = default;

bool AudioBuffer::write(const float* data, size_t samples) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    // Wait if buffer is full
    not_full_.wait(lock, [this] { return size_ < capacity_; });
    
    size_t to_write = std::min(samples, capacity_ - size_);
    
    for (size_t i = 0; i < to_write; ++i) {
        buffer_[write_pos_] = data[i];
        write_pos_ = (write_pos_ + 1) % capacity_;
    }
    
    size_ += to_write;
    not_empty_.notify_all();
    
    return to_write == samples;
}

bool AudioBuffer::read(float* data, size_t samples) {
    std::unique_lock<std::mutex> lock(mutex_);
    
    // Wait if buffer is empty
    not_empty_.wait(lock, [this] { return size_ > 0; });
    
    size_t to_read = std::min(samples, size_);
    
    for (size_t i = 0; i < to_read; ++i) {
        data[i] = buffer_[read_pos_];
        read_pos_ = (read_pos_ + 1) % capacity_;
    }
    
    // Fill remaining with silence if not enough data
    for (size_t i = to_read; i < samples; ++i) {
        data[i] = 0.0f;
    }
    
    size_ -= to_read;
    not_full_.notify_all();
    
    return to_read == samples;
}

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
    read_pos_ = write_pos_ = size_ = 0;
    not_full_.notify_all();
}

bool AudioBuffer::isEmpty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_ == 0;
}

bool AudioBuffer::isFull() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_ == capacity_;
}
