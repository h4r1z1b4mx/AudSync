#pragma once

#include<vector>
#include<mutex>
#include<condition_variable>
#include<cstdint>

class AudioBuffer {
  public:
    AudioBuffer(size_t capacity);
    ~AudioBuffer(); 
    bool write(const float* data, size_t samples);
    bool read(float* data, size_t samples);
    size_t available() const;
    size_t space() const;
    void clear();

    bool isEmpty() const;
    bool isFull() const;
  private:
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::vector<float> buffer_;
    size_t capacity_;
    size_t write_pos;
    size_t read_pos;
    size_t size_;
};
