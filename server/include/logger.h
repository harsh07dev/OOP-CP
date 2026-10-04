// logger.h - Declaration of the Logger class.
// Provides thread-safe, timestamped logging to both persistent log files and console output.

#pragma once

#include <string>
#include <fstream>
#include <mutex>

#ifdef ERROR
#undef ERROR
#endif

enum class LogLevel {
    INFO,
    CHAT,
    WARN,
    ERR
};

class Logger {
public:
    explicit Logger(const std::string& filePath = "logs/chat.log", bool mirrorToConsole = true);
    ~Logger();

    // Disable copy semantics to protect the file stream and synchronization mutex
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Primary logging methods
    void info(const std::string& message);
    void chat(const std::string& message);
    void warning(const std::string& message);
    void warn(const std::string& message); // Alias for warning
    void error(const std::string& message);

    // Generic log method
    void log(LogLevel level, const std::string& message);

private:
    std::string getTimestamp() const;
    static std::string levelToString(LogLevel level);

    std::string m_filePath;
    bool m_mirrorToConsole;
    std::ofstream m_logFile;
    mutable std::mutex m_mutex;
};
