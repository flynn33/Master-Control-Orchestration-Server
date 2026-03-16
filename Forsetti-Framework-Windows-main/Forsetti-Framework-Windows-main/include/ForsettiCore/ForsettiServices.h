// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <future>
#include <cstdint>

namespace Forsetti {

class INetworkingService {
public:
    virtual std::future<std::vector<uint8_t>> data(
        const std::string& url,
        const std::map<std::string, std::string>& headers = {}) = 0;

    virtual ~INetworkingService() = default;
};

class IStorageService {
public:
    virtual void set(const std::string& key, const std::string& value) = 0;
    virtual std::optional<std::string> get(const std::string& key) = 0;
    virtual void remove(const std::string& key) = 0;

    virtual ~IStorageService() = default;
};

class ISecureStorageService {
public:
    virtual void set(const std::string& key, const std::vector<uint8_t>& data) = 0;
    virtual std::optional<std::vector<uint8_t>> get(const std::string& key) = 0;
    virtual void remove(const std::string& key) = 0;

    virtual ~ISecureStorageService() = default;
};

class IFileExportService {
public:
    virtual bool exportData(const std::vector<uint8_t>& data,
                            const std::string& filename) = 0;

    virtual ~IFileExportService() = default;
};

class ITelemetryService {
public:
    virtual void trackEvent(const std::string& name,
                            const std::map<std::string, std::string>& properties = {}) = 0;

    virtual ~ITelemetryService() = default;
};

} // namespace Forsetti
