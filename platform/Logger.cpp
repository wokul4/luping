#include "Logger.h"
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <ctime>

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

ScrError Logger::Init(std::string_view path) {
    if (m_initialized) return ScrError::AlreadyInitialized;

    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    m_file.open(std::filesystem::path(path), std::ios::app);
    if (!m_file.is_open()) {
        return ScrError::Unknown;
    }

    m_initialized = true;
    return ScrError::Success;
}

void Logger::Shutdown() {
    if (m_initialized) {
        m_file.close();
        m_initialized = false;
    }
}

Logger::~Logger() {
    Shutdown();
}

void Logger::Log(Level level, std::string_view msg, std::source_location loc) {
    if (!m_initialized) return;

    auto now   = std::chrono::system_clock::now();
    auto tt    = std::chrono::system_clock::to_time_t(now);
    auto ms    = std::chrono::duration_cast<std::chrono::milliseconds>(
                     now.time_since_epoch()) % 1000;

    std::tm tm;
    localtime_s(&tm, &tt);

    std::lock_guard lock(m_mutex);

    m_file << std::format("[{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:03d}] [{}] {}:{} | {}\n",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec, ms.count(),
        LevelToString(level),
        std::filesystem::path(loc.file_name()).filename().string(),
        loc.line(),
        msg);
    m_file.flush();
}

void Logger::Debug  (std::string_view msg, std::source_location loc) { Log(Dbg, msg, loc); }
void Logger::Info   (std::string_view msg, std::source_location loc) { Log(Inf, msg, loc); }
void Logger::Warning(std::string_view msg, std::source_location loc) { Log(Wrn, msg, loc); }
void Logger::Error  (std::string_view msg, std::source_location loc) { Log(Err, msg, loc); }

std::string_view Logger::LevelToString(Level level) {
    switch (level) {
    case Dbg: return "DEBUG";
    case Inf: return "INFO";
    case Wrn: return "WARN";
    case Err: return "ERROR";
    default:  return "?????";
    }
}
