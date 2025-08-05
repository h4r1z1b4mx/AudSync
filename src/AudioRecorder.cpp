#include "AudioRecorder.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <random>

AudioRecorder::AudioRecorder()
    : recording_(false), dataSize_(0), sampleRate_(0), channels_(0) {}

AudioRecorder::~AudioRecorder() {
    stopRecording();
}

bool AudioRecorder::startRecording(const std::string& filename, int sampleRate, int channels) {
    std::lock_guard<std::mutex> lock(mutex_);
    outFile_.open(filename, std::ios::binary);
    if (!outFile_.is_open()) return false;
    recording_ = true;
    dataSize_ = 0;
    sampleRate_ = sampleRate;
    channels_ = channels;
    writeWavHeader(sampleRate, channels);
    return true;
}

void AudioRecorder::stopRecording() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (recording_) {
        finalizeWav();
        outFile_.close();
        recording_ = false;
    }
}

bool AudioRecorder::isRecording() const {
    return recording_;
}

void AudioRecorder::writeSamples(const std::vector<uint8_t>& samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (recording_ && outFile_.is_open()) {
        const float* floatSamples = reinterpret_cast<const float*>(samples.data());
        size_t sampleCount = samples.size() / sizeof(float);
        
        std::vector<int16_t> pcmSamples(sampleCount);
        
        // High-quality conversion with dithering
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_real_distribution<float> dither(-0.5f/32768.0f, 0.5f/32768.0f);
        
        for (size_t i = 0; i < sampleCount; ++i) {
            float sample = floatSamples[i];
            
            // Soft clipping for natural sound
            if (sample > 1.0f) {
                sample = std::tanh(sample);
            } else if (sample < -1.0f) {
                sample = std::tanh(sample);
            }
            
            // Add triangular dither to reduce quantization noise
            sample += dither(gen);
            
            // High-precision conversion
            pcmSamples[i] = static_cast<int16_t>(std::lround(sample * 32767.0f));
        }
        
        outFile_.write(reinterpret_cast<const char*>(pcmSamples.data()), 
                      pcmSamples.size() * sizeof(int16_t));
        dataSize_ += pcmSamples.size() * sizeof(int16_t);
    }
}

void AudioRecorder::writeWavHeader(int sampleRate, int channels) {
    // Enhanced 16-bit PCM WAV header
    char header[44] = {0};
    std::memcpy(header, "RIFF", 4);
    uint32_t fileSize = 0;
    std::memcpy(header + 4, &fileSize, 4);
    std::memcpy(header + 8, "WAVEfmt ", 8);
    uint32_t fmtChunkSize = 16;
    std::memcpy(header + 16, &fmtChunkSize, 4);
    uint16_t audioFormat = 1; // PCM
    std::memcpy(header + 20, &audioFormat, 2);
    std::memcpy(header + 22, &channels, 2);
    std::memcpy(header + 24, &sampleRate, 4);
    uint16_t bitsPerSample = 16;
    uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
    std::memcpy(header + 28, &byteRate, 4);
    uint16_t blockAlign = channels * bitsPerSample / 8;
    std::memcpy(header + 32, &blockAlign, 2);
    std::memcpy(header + 34, &bitsPerSample, 2);
    std::memcpy(header + 36, "data", 4);
    uint32_t dataChunkSize = 0;
    std::memcpy(header + 40, &dataChunkSize, 4);
    outFile_.write(header, 44);
}

void AudioRecorder::finalizeWav() {
    if (!outFile_.is_open()) return;
    uint32_t fileSize = static_cast<uint32_t>(36 + dataSize_);
    uint32_t dataChunkSize = static_cast<uint32_t>(dataSize_);
    outFile_.seekp(4, std::ios::beg);
    outFile_.write(reinterpret_cast<const char*>(&fileSize), 4);
    outFile_.seekp(40, std::ios::beg);
    outFile_.write(reinterpret_cast<const char*>(&dataChunkSize), 4);
    outFile_.seekp(0, std::ios::end);
}