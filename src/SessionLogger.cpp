#include "SessionLogger.h"
#include <iostream>
#include <iomanip>

SessionLogger::SessionLogger() : logging_(false) {}

SessionLogger::~SessionLogger() {
    stopLogging();
}

void SessionLogger::startLogging(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    logFile_.open(filename, std::ios::out | std::ios::app);
    logging_ = logFile_.is_open();
    startTime_ = std::chrono::steady_clock::now();
    if (logging_) {
        logFile_ << "=== Session Logging Started ===\n";
    }
}

void SessionLogger::stopLogging() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logging_) {
        logFile_ << "=== Session Logging Stopped ===\n";
        logFile_.close();
        logging_ = false;
    }
}

bool SessionLogger::isLogging() const {
    return logging_;
}

void SessionLogger::logAudioStats(size_t bytes, int sampleRate, int channels, const std::string& endpoint) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logging_) {
        logFile_ << "[AudioStats] Bytes: " << bytes
                 << ", SampleRate: " << sampleRate
                 << ", Channels: " << channels
                 << ", Endpoint: " << endpoint << "\n";
    }
}

void SessionLogger::logPacketMetadata(uint64_t timestamp, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logging_) {
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_).count();
        logFile_ << "[Packet] Timestamp: " << timestamp
                 << ", Size: " << size
                 << ", Elapsed(ms): " << ms << "\n";
    }
}