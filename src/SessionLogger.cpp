#include "SessionLogger.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>

SessionLogger::SessionLogger() : logging_(false) {}

SessionLogger::~SessionLogger() {
    stopLogging();
}

void SessionLogger::startLogging(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (logging_) {
        stopLogging();
    }
    
    logFile_.open(filename, std::ios::out | std::ios::app);
    if (logFile_.is_open()) {
        logging_ = true;
        startTime_ = std::chrono::steady_clock::now();
        
        // Write session header
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        logFile_ << "=== Session Started: " << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << " ===" << std::endl;
    } else {
        std::cerr << "Failed to open log file: " << filename << std::endl;
    }
}

void SessionLogger::stopLogging() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (logging_ && logFile_.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        logFile_ << "=== Session Ended: " << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << " ===" << std::endl;
        logFile_.close();
        logging_ = false;
    }
}

bool SessionLogger::isLogging() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    return logging_;
}

void SessionLogger::logAudioStats(size_t bytes, int sampleRate, int channels, const std::string& endpoint) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!logging_ || !logFile_.is_open()) {
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_).count();
    
    logFile_ << "[" << elapsed << "ms] AUDIO: " << bytes << " bytes, " 
             << sampleRate << "Hz, " << channels << "ch, endpoint=" << endpoint << std::endl;
}

void SessionLogger::logPacketMetadata(uint64_t timestamp, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!logging_ || !logFile_.is_open()) {
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_).count();
    
    logFile_ << "[" << elapsed << "ms] PACKET: timestamp=" << timestamp 
             << ", size=" << size << " bytes" << std::endl;
}

std::string SessionLogger::generateLogPath(const std::string& prefix, bool isClient) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    oss << "sessionlogs/" << prefix;
    if (isClient) {
        oss << "_client";
    } else {
        oss << "_server";
    }
    oss << "_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") << ".log";
    
    return oss.str();
}

bool SessionLogger::createLogDirectories() {
    try {
        std::filesystem::create_directories("sessionlogs");
        std::filesystem::create_directories("recordings");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create log directories: " << e.what() << std::endl;
        return false;
    }
}
