// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "ForsettiCore/ForsettiLogger.h"

#ifndef NDEBUG
#include <iostream>
#endif

namespace Forsetti {

void ConsoleLogger::log(LogLevel level,
                        const std::string& message,
                        const std::string& sourceModuleID,
                        const std::map<std::string, std::string>& metadata) {
#ifndef NDEBUG
    std::string output = "[" + logLevelToString(level) + "]";

    if (!sourceModuleID.empty()) {
        output += " [" + sourceModuleID + "]";
    }

    output += " " + message;

    if (!metadata.empty()) {
        output += " {";
        bool first = true;
        for (const auto& [key, value] : metadata) {
            if (!first) {
                output += ", ";
            }
            output += key + ": " + value;
            first = false;
        }
        output += "}";
    }

    std::cerr << output << std::endl;
#else
    // Silence unused parameter warnings in release builds
    (void)level;
    (void)message;
    (void)sourceModuleID;
    (void)metadata;
#endif
}

} // namespace Forsetti
