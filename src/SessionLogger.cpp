#include "SessionLogger.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <vector>  // ADDED: Missing include for std::vector

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
#else
    #include <sys/stat.h>
    #include <unistd.h>
#endif

SessionLogger::SessionLogger() : logging_(false) {}

SessionLogger::~SessionLogger() {
    stopLogging();
}

void SessionLogger::startLogging(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // ADDED: Ensure directories exist before opening log file
    createLogDirectories();
    
    logFile_.open(filename, std::ios::out | std::ios::app);
    logging_ = logFile_.is_open();
    startTime_ = std::chrono::steady_clock::now();
    
    if (logging_) {
        // ENHANCED: Better session start logging with timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        logFile_ << "========================================\n";
        logFile_ << "=== Session Logging Started ===\n";
        logFile_ << "Start Time: ";
        
        #ifdef _WIN32
            struct tm timeinfo;
            localtime_s(&timeinfo, &time_t);
            logFile_ << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S");
        #else
            logFile_ << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        #endif
        
        logFile_ << "\n========================================\n";
        logFile_.flush();
    }
}

void SessionLogger::stopLogging() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logging_) {
        // ENHANCED: Better session end logging with timestamp and duration
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto sessionDuration = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - startTime_).count();
        
        logFile_ << "========================================\n";
        logFile_ << "=== Session Logging Stopped ===\n";
        logFile_ << "Stop Time: ";
        
        #ifdef _WIN32
            struct tm timeinfo;
            localtime_s(&timeinfo, &time_t);
            logFile_ << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S");
        #else
            logFile_ << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        #endif
        
        logFile_ << "\nSession Duration: " << sessionDuration << " seconds\n";
        logFile_ << "========================================\n";
        
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
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_).count();
        
        logFile_ << "[" << std::setfill('0') << std::setw(8) << ms << "ms] "
                 << "[AudioStats] Bytes: " << bytes
                 << ", SampleRate: " << sampleRate << "Hz"
                 << ", Channels: " << channels
                 << ", Endpoint: " << endpoint 
                 << ", Bitrate: " << std::fixed << std::setprecision(1) 
                 << (bytes * 8.0 * sampleRate / (bytes / (channels * sizeof(float)))) / 1000.0 << "kbps"
                 << "\n";
        logFile_.flush();
    }
}

void SessionLogger::logPacketMetadata(uint64_t timestamp, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logging_) {
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_).count();
        
        logFile_ << "[" << std::setfill('0') << std::setw(8) << ms << "ms] "
                 << "[Packet] Timestamp: " << timestamp
                 << ", Size: " << size << "B"
                 << ", Rate: " << std::fixed << std::setprecision(2) 
                 << (size / 1024.0) << "KB"
                 << "\n";
        logFile_.flush();
    }
}

// ADDED: Generate session log file path with proper directory structure
std::string SessionLogger::generateLogPath(const std::string& prefix, bool isClient) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    #ifdef _WIN32
        struct tm timeinfo;
        localtime_s(&timeinfo, &time_t);
        oss << std::put_time(&timeinfo, "%Y%m%d_%H%M%S");
    #else
        oss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    #endif
    
    // FIXED: Use root directory structure for session logs
    std::string baseDir = "sessionlogs";
    std::string subDir = isClient ? "client" : "server";
    
    return baseDir + "/" + subDir + "/" + prefix + "_" + oss.str() + ".log";
}

// ADDED: Create all necessary session log directories
bool SessionLogger::createLogDirectories() {
    std::vector<std::string> dirsToCreate = {
        "sessionlogs",
        "sessionlogs/client", 
        "sessionlogs/server"
    };
    
    for (const auto& dir : dirsToCreate) {
        #ifdef _WIN32
            if (_mkdir(dir.c_str()) != 0 && errno != EEXIST) {
                // Directory creation failed and it doesn't already exist
                continue;
            }
        #else
            if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
                // Directory creation failed and it doesn't already exist
                continue;
            }
        #endif
    }
    
    return true; // Return true even if some directories already exist
}