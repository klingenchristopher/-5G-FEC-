#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <chrono>
#include <iomanip>

namespace mpquic_fec {

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void set_level(LogLevel level) {
        level_ = level;
    }

    template<typename... Args>
    void log(LogLevel level, Args&&... args) {
        if (level < level_) return;

        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        
        std::ostringstream oss;
        oss << "[" << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << "] ";
        oss << "[" << level_to_string(level) << "] ";
        
        (oss << ... << args);
        
        std::cout << oss.str() << std::endl;
    }

private:
    Logger() = default;
    LogLevel level_ = LogLevel::INFO;

    std::string level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERROR: return "ERROR";
            default: return "UNKNO";
        }
    }
};

// 便捷宏
#define LOG_DEBUG(...) mpquic_fec::Logger::instance().log(mpquic_fec::LogLevel::DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  mpquic_fec::Logger::instance().log(mpquic_fec::LogLevel::INFO, __VA_ARGS__)
#define LOG_WARN(...)  mpquic_fec::Logger::instance().log(mpquic_fec::LogLevel::WARN, __VA_ARGS__)
#define LOG_ERROR(...) mpquic_fec::Logger::instance().log(mpquic_fec::LogLevel::ERROR, __VA_ARGS__)

} // namespace mpquic_fec
