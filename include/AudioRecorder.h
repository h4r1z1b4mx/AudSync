#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <mutex>

class AudioRecorder {
public:
    AudioRecorder();
    ~AudioRecorder();

    bool startRecording(const std::string& filename, int sampleRate, int channels);
    void stopRecording();
    bool isRecording() const;

    void writeSamples(const std::vector<uint8_t>& samples);

    // ADDED: Static methods for directory management
    static std::string generateRecordingPath(const std::string& prefix, bool isClient = true);
    static bool createRecordingDirectories();

private:
    void writeWavHeader(int sampleRate, int channels);
    void finalizeWav();

    std::ofstream outFile_;
    std::mutex mutex_;
    bool recording_;
    size_t dataSize_;
    int sampleRate_;
    int channels_;
};