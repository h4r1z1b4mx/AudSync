#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <chrono>

class SessionLogger {
public:
    SessionLogger();
    ~SessionLogger();

    void startLogging(const std::string& filename);
    void stopLogging();
    bool isLogging() const;

    void logAudioStats(size_t bytes, int sampleRate, int channels, const std::string& endpoint);
    void logPacketMetadata(uint64_t timestamp, size_t size);

    // ADDED: Static methods for directory management and path generation
    static std::string generateLogPath(const std::string& prefix, bool isClient = true);
    static bool createLogDirectories();

private:
    std::ofstream logFile_;
    std::mutex mutex_;
    bool logging_;
    std::chrono::steady_clock::time_point startTime_;
};