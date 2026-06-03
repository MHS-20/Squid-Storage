#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <ctime>

enum class LogLevel
{
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger
{
public:
    explicit Logger(const std::string &name) : name_(name) {}

    void debug(const std::string &msg) const { log(LogLevel::DEBUG, msg); }
    void info (const std::string &msg) const { log(LogLevel::INFO,  msg); }
    void warn (const std::string &msg) const { log(LogLevel::WARN,  msg); }
    void error(const std::string &msg) const { log(LogLevel::ERROR, msg); }

    static void setMinLevel(LogLevel level) { minLevel_ = level; }

private:
    std::string name_;
    static LogLevel minLevel_;
    static std::mutex mutex_;

    void log(LogLevel level, const std::string &msg) const
    {
        if (level < minLevel_)
            return;

        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_r(&t, &tm);

        std::lock_guard<std::mutex> lock(mutex_);
        std::cout
            << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
            << " [" << levelToString(level) << "]"
            << " [" << name_ << "] "
            << msg
            << "\n";
    }

    static const char *levelToString(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default:              return "UNKNOWN";
        }
    }
};

inline LogLevel Logger::minLevel_ = LogLevel::DEBUG;
inline std::mutex Logger::mutex_;
