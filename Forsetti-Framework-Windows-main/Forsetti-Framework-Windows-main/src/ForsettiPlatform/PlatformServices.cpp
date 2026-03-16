// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "ForsettiPlatform/PlatformServices.h"
#include <future>

namespace Forsetti {

// ---------------------------------------------------------------------------
// WinHttpNetworkingService
// ---------------------------------------------------------------------------

std::future<std::vector<uint8_t>> WinHttpNetworkingService::data(
    const std::string& /*url*/,
    const std::map<std::string, std::string>& /*headers*/)
{
    // Stub: immediately resolve with an empty byte vector.
    // Phase 2a: Replace with actual WinHTTP request.
    std::promise<std::vector<uint8_t>> promise;
    promise.set_value({});
    return promise.get_future();
}

// ---------------------------------------------------------------------------
// RegistryStorageService
// ---------------------------------------------------------------------------

void RegistryStorageService::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    store_[key] = value;
}

std::optional<std::string> RegistryStorageService::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = store_.find(key);
    if (it != store_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void RegistryStorageService::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    store_.erase(key);
}

// ---------------------------------------------------------------------------
// DpapiSecureStorageService
// ---------------------------------------------------------------------------

void DpapiSecureStorageService::set(const std::string& key, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    store_[key] = data;
}

std::optional<std::vector<uint8_t>> DpapiSecureStorageService::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = store_.find(key);
    if (it != store_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void DpapiSecureStorageService::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    store_.erase(key);
}

// ---------------------------------------------------------------------------
// LocalFileExportService
// ---------------------------------------------------------------------------

bool LocalFileExportService::exportData(const std::vector<uint8_t>& /*data*/,
                                         const std::string& /*filename*/)
{
    // Stub: no-op, returns false.
    // Phase 2a: Replace with actual file export using Win32 API.
    return false;
}

// ---------------------------------------------------------------------------
// NoopTelemetryService
// ---------------------------------------------------------------------------

void NoopTelemetryService::trackEvent(const std::string& /*name*/,
                                       const std::map<std::string, std::string>& /*properties*/)
{
    // Intentionally empty — no-op telemetry.
}

} // namespace Forsetti
