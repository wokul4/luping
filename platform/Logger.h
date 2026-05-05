#pragma once
#include <string>
#include <string_view>
#include <mutex>
#include <fstream>
#include <chrono>
#include <format>
#include <source_location>
#include "ErrorCode.h"

class Logger {
public:
    enum Level : uint8_t { Dbg, Inf, Wrn, Err };

    static Logger& Instance();

    ScrError Init(std::string_view path);
    void     Shutdown();

    void Log(Level level, std::string_view msg,
             std::source_location loc = std::source_location::current());

    void Debug   (std::string_view msg, std::source_location loc = std::source_location::current());
    void Info    (std::string_view msg, std::source_location loc = std::source_location::current());
    void Warning (std::string_view msg, std::source_location loc = std::source_location::current());
    void Error   (std::string_view msg, std::source_location loc = std::source_location::current());

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

    static std::string_view LevelToString(Level level);

    std::mutex   m_mutex;
    std::ofstream m_file;
    bool         m_initialized = false;
};
