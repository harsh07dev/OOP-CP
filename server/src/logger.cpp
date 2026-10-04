// logger.cpp - Implementation of the Logger class.
// Handles thread-safe, timestamped log writing to logs/chat.log and console mirroring.

#include "logger.h"

#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>

Logger::Logger(const std::string& filePath, bool mirrorToConsole)
    : m_filePath(filePath),
      m_mirrorToConsole(mirrorToConsole)
{
    // Ensure parent directory exists safely
    std::filesystem::path p(filePath);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    // Open file in append mode so existing records are preserved across server restarts
    m_logFile.open(filePath, std::ios::out | std::ios::app);
    if (!m_logFile.is_open()) {
        std::cerr << "[ERROR] Logger failed to open log file: " << filePath << std::endl;
    }
}

Logger::~Logger() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_logFile.is_open()) {
        m_logFile.close();
    }
}

std::string Logger::getTimestamp() const {
    auto now = std::chrono::system_clock::now();
    std::time_t timeT = std::chrono::system_clock::to_time_t(now);

    std::tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &timeT);
#else
    localtime_r(&timeT, &tmBuf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::INFO: return "INFO";
        case LogLevel::CHAT: return "CHAT";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERR:  return "ERROR";
        default:             return "INFO";
    }
}

void Logger::log(LogLevel level, const std::string& message) {
    std::string timestamp = getTimestamp();
    std::string levelStr = levelToString(level);
    std::string logLine = "[" + timestamp + "] [" + levelStr + "] " + message;

    std::lock_guard<std::mutex> lock(m_mutex);

    // Write complete entry to log file
    if (m_logFile.is_open()) {
        m_logFile << logLine << "\n";
        m_logFile.flush();
    }

    // Mirror to console without interleaving across concurrent threads
    if (m_mirrorToConsole) {
        if (level == LogLevel::WARN || level == LogLevel::ERR) {
            std::cerr << logLine << std::endl;
        } else {
            std::cout << logLine << std::endl;
        }
    }
}

void Logger::info(const std::string& message) {
    log(LogLevel::INFO, message);
}

void Logger::chat(const std::string& message) {
    log(LogLevel::CHAT, message);
}

void Logger::warning(const std::string& message) {
    log(LogLevel::WARN, message);
}

void Logger::warn(const std::string& message) {
    log(LogLevel::WARN, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::ERR, message);
}
