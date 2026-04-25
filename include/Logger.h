#pragma once

#include <iostream>
#include <utility>

namespace Logger {

enum class Level {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3
};

// Override at compile time: -DLOG_LEVEL=Logger::Level::DEBUG
#ifndef LOG_LEVEL
#define LOG_LEVEL Logger::Level::INFO
#endif

inline const char* levelToString(Level lvl) {
    switch (lvl) {
        case Level::DEBUG: return "DEBUG";
        case Level::INFO:  return "INFO ";
        case Level::WARN:  return "WARN ";
        case Level::ERROR: return "ERROR";
    }
    return "?????";
}

template <typename... Args>
inline void log(Level lvl, Args&&... args) {
    if (lvl < (LOG_LEVEL)) return;
    std::ostream& out = (lvl >= Level::WARN) ? std::cerr : std::cout;
    out << "[" << levelToString(lvl) << "] ";
    (out << ... << std::forward<Args>(args));
    out << std::endl;
}

template <typename... Args> inline void debug(Args&&... args) { log(Level::DEBUG, std::forward<Args>(args)...); }
template <typename... Args> inline void info (Args&&... args) { log(Level::INFO,  std::forward<Args>(args)...); }
template <typename... Args> inline void warn (Args&&... args) { log(Level::WARN,  std::forward<Args>(args)...); }
template <typename... Args> inline void error(Args&&... args) { log(Level::ERROR, std::forward<Args>(args)...); }

// Unfiltered, prefix-less output for user-facing program output (e.g. tree visualization).
// Always prints regardless of LOG_LEVEL — use this when the user explicitly asked to see something.
template <typename... Args>
inline void print(Args&&... args) {
    (std::cout << ... << std::forward<Args>(args)) << std::endl;
}

}
