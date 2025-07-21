#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <cstdint>

class AudioBuffer {
public:
    AudioBuffer(size_t capacity);
    ~AudioBuffer();

    // Thread-safe audio buffer operations
    bool write(const float* data, size_t samples);
    bool read(float* data, size_t samples);
    
    size_t available() const;
    size_t space() const;
    void clear();
    
    bool isEmpty() const;
    bool isFull() const;

private:
    std::vector<float> buffer_;
    size_t capacity_;
    size_t read_pos_;
    size_t write_pos_;
    size_t size_;
    
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
};
