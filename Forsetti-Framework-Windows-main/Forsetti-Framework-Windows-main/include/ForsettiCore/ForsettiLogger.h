// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#pragma once

#include <string>
#include <map>
#include <memory>

namespace Forsetti {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

inline std::string logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Error:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

class IForsettiLogger {
public:
    virtual void log(LogLevel level,
                     const std::string& message,
                     const std::string& sourceModuleID = "",
                     const std::map<std::string, std::string>& metadata = {}) = 0;

    virtual ~IForsettiLogger() = default;
};

class ConsoleLogger final : public IForsettiLogger {
public:
    void log(LogLevel level,
             const std::string& message,
             const std::string& sourceModuleID = "",
             const std::map<std::string, std::string>& metadata = {}) override;
};

} // namespace Forsetti
