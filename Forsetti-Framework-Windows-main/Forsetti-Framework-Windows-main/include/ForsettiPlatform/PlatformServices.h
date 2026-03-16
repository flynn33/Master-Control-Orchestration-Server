// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#pragma once
#include "ForsettiCore/ForsettiServices.h"

#include <mutex>
#include <unordered_map>

namespace Forsetti {

// ---------------------------------------------------------------------------
// WinHttpNetworkingService — Windows HTTP networking via WinHTTP.
// Stub: returns a future with an empty byte vector.
// Phase 2a: Replace with actual WinHTTP implementation.
// ---------------------------------------------------------------------------
class WinHttpNetworkingService final : public INetworkingService {
public:
    std::future<std::vector<uint8_t>> data(
        const std::string& url,
        const std::map<std::string, std::string>& headers = {}) override;
};

// ---------------------------------------------------------------------------
// RegistryStorageService — Persistent storage backed by Windows Registry.
// Stub: uses an in-memory std::unordered_map for now.
// Phase 2a: Replace with actual Windows Registry implementation.
// ---------------------------------------------------------------------------
class RegistryStorageService final : public IStorageService {
public:
    void set(const std::string& key, const std::string& value) override;
    std::optional<std::string> get(const std::string& key) override;
    void remove(const std::string& key) override;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> store_;
};

// ---------------------------------------------------------------------------
// DpapiSecureStorageService — Secure storage backed by Windows DPAPI.
// Stub: uses an in-memory std::unordered_map for now.
// Phase 2a: Replace with actual DPAPI implementation.
// ---------------------------------------------------------------------------
class DpapiSecureStorageService final : public ISecureStorageService {
public:
    void set(const std::string& key, const std::vector<uint8_t>& data) override;
    std::optional<std::vector<uint8_t>> get(const std::string& key) override;
    void remove(const std::string& key) override;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<uint8_t>> store_;
};

// ---------------------------------------------------------------------------
// LocalFileExportService — File export to the local filesystem.
// Stub: returns false (no-op).
// Phase 2a: Replace with actual file I/O implementation.
// ---------------------------------------------------------------------------
class LocalFileExportService final : public IFileExportService {
public:
    bool exportData(const std::vector<uint8_t>& data,
                    const std::string& filename) override;
};

// ---------------------------------------------------------------------------
// NoopTelemetryService — No-op telemetry (same as Swift).
// All calls are silently ignored.
// ---------------------------------------------------------------------------
class NoopTelemetryService final : public ITelemetryService {
public:
    void trackEvent(const std::string& name,
                    const std::map<std::string, std::string>& properties = {}) override;
};

} // namespace Forsetti
