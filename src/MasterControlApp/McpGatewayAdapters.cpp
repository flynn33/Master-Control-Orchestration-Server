// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

// winsock2 MUST come before windows.h. The header (McpGatewayAdapters.h)
// includes them in the right order; doing it here too defends against any
// future TU-level include-order regression.
//
// The native gateway uses HTTP.sys to listen and an in-process state
// check for Probe(), so winhttp.h / winhttp.lib are not needed in
// this TU.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <http.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "httpapi.lib")
#endif

#include "MasterControl/McpGatewayAdapters.h"
#include "MasterControl/McpToolNameResolver.h"
#include "MasterControl/MasterControlVersion.h"

#include <cctype>
#include <chrono>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace MasterControl {

namespace {

std::string trimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

std::string ensureLeadingSlash(std::string value) {
    if (value.empty()) {
        return "/";
    }
    if (value.front() != '/') {
        return "/" + value;
    }
    return value;
}

// Bracket an IPv6 literal per RFC 3986 so that the colons in the
// address cannot collide with the ":port" separator when the value
// is concatenated into a URL. Duplicated (intentionally trivially)
// from MasterControlRuntime.cpp where the same helper centralizes
// every URL composition. Keeping a copy local to this TU avoids
// pulling a runtime header into the adapter implementation; the
// helper is a one-liner.
//
// Behaviour:
//   - empty  -> empty
//   - "[v6]" -> "[v6]" (already bracketed)
//   - "1.2.3.4" / "host.local" -> unchanged (no ':')
//   - "2001:db8::1" -> "[2001:db8::1]"
std::string bracketIpv6Host(const std::string& host) {
    if (host.empty()) {
        return host;
    }
    if (host.front() == '[' && host.back() == ']') {
        return host;
    }
    return (host.find(':') != std::string::npos) ? ("[" + host + "]") : host;
}

std::string composeMcpUrl(const McpGatewayConfiguration& configuration) {
    // Defensive bracket. Self-heal in FileBackedConfigurationService
    // brackets on persist, but operator-edited / older-on-disk values
    // that bypass that path may still carry a raw IPv6 literal in
    // listenHost. Composing http:// + raw v6 + ":" + port would
    // produce an unparseable URL on IPv6-only hosts. Bracketing here
    // makes the operator-facing raw mcpUrl valid regardless of how
    // listenHost was written.
    const std::string host = configuration.listenHost.empty()
        ? std::string("0.0.0.0")
        : bracketIpv6Host(configuration.listenHost);
    std::ostringstream stream;
    stream << "http://"
           << host
           << ":"
           << configuration.listenPort
           << ensureLeadingSlash(configuration.mcpPath);
    return stream.str();
}

// v0.11.0-alpha.2: companion to composeMcpUrl for the dual-bind HTTPS
// surface. Returns an empty string when tlsEnabled=false so callers can
// branch on emptiness without checking the bool field themselves.
std::string composeMcpUrlTls(const McpGatewayConfiguration& configuration) {
    if (!configuration.tlsEnabled) {
        return std::string();
    }
    const std::string host = configuration.listenHost.empty()
        ? std::string("0.0.0.0")
        : bracketIpv6Host(configuration.listenHost);
    std::ostringstream stream;
    stream << "https://"
           << host
           << ":"
           << configuration.tlsListenPort
           << ensureLeadingSlash(configuration.mcpPath);
    return stream.str();
}

std::string composeHealthUrl(const McpGatewayConfiguration& configuration) {
    const std::string host = configuration.listenHost.empty()
        ? std::string("0.0.0.0")
        : bracketIpv6Host(configuration.listenHost);
    std::ostringstream stream;
    stream << "http://"
           << host
           << ":"
           << configuration.listenPort
           << ensureLeadingSlash(configuration.healthPath);
    return stream.str();
}

// The HTTPS health URL is composed inline by the discovery emit
// (MasterControlRuntime.cpp)
// using the runtime-resolved LAN IP host. We intentionally do NOT keep
// a composeHealthUrlTls helper here because the only consumer needs
// the LAN IP rather than configuration.listenHost (which is typically
// the 0.0.0.0 wildcard). Carrying an unused helper invites drift
// between the two compositions.

} // namespace

// ---------------------------------------------------------------------------
// FakeMcpGatewayAdapter
// ---------------------------------------------------------------------------

FakeMcpGatewayAdapter::FakeMcpGatewayAdapter(McpGatewayConfiguration configuration)
    : configuration_(std::move(configuration)) {
    status_.adapterType = AdapterType();
    status_.mcpUrl = composeMcpUrl(configuration_);
    status_.state = configuration_.enabled
        ? GatewayState::Configured
        : GatewayState::Disabled;
    status_.message = "FakeMcpGatewayAdapter constructed.";

    // Default probe: Unknown until a test scripts otherwise.
    nextProbe_.adapterType = AdapterType();
    nextProbe_.status = GatewayHealthStatus::Unknown;
    nextProbe_.message = "FakeMcpGatewayAdapter has no scripted probe; returning Unknown.";
}

GatewayStatus FakeMcpGatewayAdapter::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++startCalls_;
    if (startShouldFail_) {
        status_.state = GatewayState::Failed;
        status_.message = startFailureMessage_.empty()
            ? std::string("Fake gateway scripted Start() to fail.")
            : startFailureMessage_;
        return status_;
    }
    if (!configuration_.enabled) {
        status_.state = GatewayState::Disabled;
        status_.message = "Fake gateway is disabled in configuration.";
        return status_;
    }
    status_.state = GatewayState::Running;
    status_.startedAtUtc = timestampNowUtc();
    status_.message = "Fake gateway started.";
    return status_;
}

GatewayStatus FakeMcpGatewayAdapter::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stopCalls_;
    status_.state = GatewayState::Stopped;
    status_.startedAtUtc.clear();
    status_.message = "Fake gateway stopped.";
    return status_;
}

GatewayStatus FakeMcpGatewayAdapter::CurrentStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

GatewayHealth FakeMcpGatewayAdapter::Probe() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++probeCalls_;
    GatewayHealth health = nextProbe_;
    health.adapterType = AdapterType();
    health.mcpUrl = composeMcpUrl(configuration_);
    health.healthUrl = composeHealthUrl(configuration_);
    health.registeredServerCount = static_cast<int>(registry_.size());
    health.probedAtUtc = timestampNowUtc();
    return health;
}

namespace {

// Registration-validation parity: the fake and native adapters enforce
// the same rules through this single helper, so tests exercising the fake
// prove the native contract too. Returns false (and fills `result`) when
// the registration is invalid.
bool validateServerRegistration(const McpServerRegistration& server, RegistrationResult& result) {
    result.serverName = server.name;
    if (server.name.empty()) {
        result.succeeded = false;
        result.message = "Logical server name is required.";
        return false;
    }
    return true;
}

} // namespace

RegistrationResult FakeMcpGatewayAdapter::RegisterHttpServer(const McpServerRegistration& server) {
    RegistrationResult result;
    if (!validateServerRegistration(server, result)) {
        return result;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    McpServerRegistration copy = server;
    copy.transport = McpServerTransport::StreamableHttp;
    registry_[copy.name] = std::move(copy);
    result.succeeded = true;
    result.registeredAtUtc = timestampNowUtc();
    result.message = "Registered (fake).";
    return result;
}

RegistrationResult FakeMcpGatewayAdapter::RegisterStdioServer(const McpServerRegistration& server) {
    RegistrationResult result;
    if (!validateServerRegistration(server, result)) {
        return result;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    McpServerRegistration copy = server;
    copy.transport = McpServerTransport::Stdio;
    registry_[copy.name] = std::move(copy);
    result.succeeded = true;
    result.registeredAtUtc = timestampNowUtc();
    result.message = "Registered (fake stdio).";
    return result;
}

DeregistrationResult FakeMcpGatewayAdapter::DeregisterServer(const std::string& serverName) {
    DeregistrationResult result;
    result.serverName = serverName;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto erased = registry_.erase(serverName);
    result.succeeded = erased > 0;
    result.message = erased > 0 ? "Deregistered (fake)." : "No registration with that name.";
    return result;
}

std::vector<McpToolDescriptor> FakeMcpGatewayAdapter::ListTools() const {
    // FakeMcpGatewayAdapter does not invent tools. Tests that need to
    // simulate tool listings should subclass and override.
    return {};
}

std::string FakeMcpGatewayAdapter::GatewayMcpUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return composeMcpUrl(configuration_);
}

std::string FakeMcpGatewayAdapter::AdapterType() const {
    return "fake";
}

void FakeMcpGatewayAdapter::setNextProbe(GatewayHealth probe) {
    std::lock_guard<std::mutex> lock(mutex_);
    nextProbe_ = std::move(probe);
}

void FakeMcpGatewayAdapter::setStartShouldFail(bool shouldFail, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    startShouldFail_ = shouldFail;
    startFailureMessage_ = message;
}

std::size_t FakeMcpGatewayAdapter::startCallCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return startCalls_;
}

std::size_t FakeMcpGatewayAdapter::stopCallCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stopCalls_;
}

std::size_t FakeMcpGatewayAdapter::probeCallCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return probeCalls_;
}

std::vector<std::string> FakeMcpGatewayAdapter::registeredServerNames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(registry_.size());
    for (const auto& [name, _] : registry_) {
        names.push_back(name);
    }
    return names;
}

// trimTrailingSlash is currently unused but retained as a helper for future
// adapters that need to compose URL bases without trailing slashes.
[[maybe_unused]] static std::string consumeTrimTrailingSlash(std::string value) {
    return trimTrailingSlash(std::move(value));
}

// ---------------------------------------------------------------------------
// PHASE-12: NativeHttpSysGatewayAdapter
//
// Windows-native MCP gateway built on HTTP.sys (Win32 kernel-mode HTTP
// server). Replaces the v0.6.7 honest-503 listener with a real
// implementation. This is the only shipping gateway substrate;
// mcpGateway.type is retained in the JSON schema for back-compat
// deserialization only -- the runtime constructs this adapter
// unconditionally regardless of the persisted value.
//
// MVP scope (v0.6.9):
//   * HTTP.sys URL group + request queue bound to mcpGateway.listenPort
//   * Streamable-HTTP MCP transport: parses JSON-RPC envelopes
//   * `initialize` handshake -> returns server capabilities
//   * `tools/list` -> aggregated from registered McpServerRegistration
//     entries (PHASE-06 pools registered via RegisterStdioServer)
//   * `tools/call` -> honest "stdio bridge pending" error for now;
//     stdio bridge wiring lands in v0.6.10
//   * `/health` (configurable healthPath) returns adapter-state JSON
//   * 404 with structured JSON for unknown paths
//
// HTTP.sys URL ACL note: binding to http://+:8080/ requires either admin
// rights at first use, OR a pre-registered URL ACL via:
//     netsh http add urlacl url=http://+:8080/ user=Everyone
// MCOS runs as LocalSystem when installed as the Windows service (which
// has the privilege to bind without ACL), so the ACL step is unnecessary
// in the standard install. Console-mode runs (./MasterControlServiceHost
// --console as a regular user) need the ACL or admin rights. The
// bootstrapper's `install` action will run the netsh command starting in
// v0.6.10 to remove this manual step entirely.
// ---------------------------------------------------------------------------

NativeHttpSysGatewayAdapter::NativeHttpSysGatewayAdapter(McpGatewayConfiguration configuration)
    : configuration_(std::move(configuration)) {
    status_.adapterType = AdapterType();
    status_.mcpUrl = composeMcpUrl(configuration_);
    status_.state = configuration_.enabled
        ? GatewayState::Configured
        : GatewayState::Disabled;
    status_.message = configuration_.enabled
        ? "Native HTTP.sys adapter is configured. Call Start() to bind the listener."
        : "MCP Gateway is disabled. Set mcpGateway.enabled=true in configuration to opt in.";
}

NativeHttpSysGatewayAdapter::~NativeHttpSysGatewayAdapter() {
    Stop();
}

GatewayStatus NativeHttpSysGatewayAdapter::Start() {
    // Lifecycle serialization (review fix): Start() and Stop() are mutually
    // exclusive end-to-end via lifecycleMutex_. The admin listener now
    // serves requests on a worker pool, so /api/gateway/start|stop|restart
    // can genuinely race; without this lock a Start() could interleave
    // with Stop()'s unlocked join phase (joinable-thread reassignment ->
    // std::terminate, workerThreads_ vector races, jobQueueShutdown_
    // flag reversal). lifecycleMutex_ is DISTINCT from mutex_ so Probe()
    // and /health keep working while a Stop is joining threads.
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
    std::lock_guard<std::mutex> lock(mutex_);

    if (!configuration_.enabled) {
        status_.state = GatewayState::Disabled;
        status_.message = "Cannot start: mcpGateway.enabled is false.";
        return status_;
    }
#if !defined(_WIN32)
    status_.state = GatewayState::Failed;
    status_.message = "the in-process HTTP.sys adapter requires Windows.";
    return status_;
#else
    if (running_) {
        return status_;
    }

    HTTPAPI_VERSION httpVersion = HTTPAPI_VERSION_2;
    if (!httpInitialized_) {
        ULONG initResult = HttpInitialize(httpVersion, HTTP_INITIALIZE_SERVER, nullptr);
        if (initResult != NO_ERROR) {
            status_.state = GatewayState::Failed;
            status_.message = "HttpInitialize failed (code " + std::to_string(initResult) + ").";
            return status_;
        }
        httpInitialized_ = true;
    }

    // Create the server session.
    HTTP_SERVER_SESSION_ID sessionId = 0;
    ULONG sessionResult = HttpCreateServerSession(httpVersion, &sessionId, 0);
    if (sessionResult != NO_ERROR) {
        status_.state = GatewayState::Failed;
        status_.message = "HttpCreateServerSession failed (code " + std::to_string(sessionResult) + ").";
        return status_;
    }
    serverSessionId_ = sessionId;

    // Create the URL group within the session.
    HTTP_URL_GROUP_ID urlGroupId = 0;
    ULONG groupResult = HttpCreateUrlGroup(serverSessionId_, &urlGroupId, 0);
    if (groupResult != NO_ERROR) {
        status_.state = GatewayState::Failed;
        status_.message = "HttpCreateUrlGroup failed (code " + std::to_string(groupResult) + ").";
        closeHttpSysHandlesLocked();
        return status_;
    }
    urlGroupId_ = urlGroupId;

    // Build the URL prefix. http://+:PORT/ binds to all interfaces.
    std::wstring urlPrefix = L"http://+:"
        + std::to_wstring(configuration_.listenPort) + L"/";

    // v0.9.3: retry HttpAddUrlToUrlGroup on ERROR_ALREADY_EXISTS (183).
    // Pre-v0.9.3 a single boot-time failure was permanent: when a prior
    // process had registered the same URL prefix and HTTP.sys had not
    // yet GC'd that registration, the new gateway came up in Failed
    // state and stayed there until the operator manually POSTed
    // /api/gateway/start. The race window is tens-to-hundreds of ms
    // for HTTP.sys to clean up after a clean exit; longer (~seconds)
    // after a hard kill. 8 retries x 250ms = 2s total budget covers the
    // realistic worst case. ERROR_ACCESS_DENIED is NOT retried -- it's
    // a configuration problem (URL ACL missing, not running as
    // LocalSystem) that won't fix itself by waiting.
    constexpr int kMaxRetries = 8;
    constexpr DWORD kRetryDelayMs = 250;
    ULONG addUrlResult = NO_ERROR;
    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        addUrlResult = HttpAddUrlToUrlGroup(urlGroupId_, urlPrefix.c_str(), 0, 0);
        if (addUrlResult != ERROR_ALREADY_EXISTS) {
            break;
        }
        Sleep(kRetryDelayMs);
    }
    if (addUrlResult != NO_ERROR) {
        status_.state = GatewayState::Failed;
        // ERROR_ACCESS_DENIED (5) here is the URL ACL issue. Be specific
        // so operators know what to do.
        if (addUrlResult == ERROR_ACCESS_DENIED) {
            status_.message = "HttpAddUrlToUrlGroup access denied. "
                "Either run MCOS as the Windows service (LocalSystem has "
                "the privilege automatically), or pre-register the URL ACL "
                "from elevated PowerShell: netsh http add urlacl "
                "url=http://+:" + std::to_string(configuration_.listenPort)
                + "/ user=Everyone";
        } else if (addUrlResult == ERROR_ALREADY_EXISTS) {
            status_.message = "HttpAddUrlToUrlGroup: URL prefix http://+:"
                + std::to_string(configuration_.listenPort)
                + "/ is held by another process / leaked HTTP.sys URL group "
                  "and did not clear within the v0.9.3 retry budget (2s). "
                  "Identify the holder with: netsh http show servicestate "
                  "view=requestq, then stop it.";
        } else {
            status_.message = "HttpAddUrlToUrlGroup failed (code "
                + std::to_string(addUrlResult) + ").";
        }
        closeHttpSysHandlesLocked();
        return status_;
    }

    // v0.11.0-alpha.2: TLS dual-bind. When `tlsEnabled`, register a
    // second URL prefix `https://+:tlsListenPort/` on the same URL group
    // and same request queue. HTTP.sys multiplexes both prefixes through
    // the same handler -- the serve loop sees identical HTTP_REQUEST
    // structures regardless of which prefix terminated SSL.
    //
    // SSL termination is the OS's job: the cert is bound to
    // `ip:tlsListenPort` via `netsh http add sslcert ...` ahead of time
    // (operator runs scripts\Configure-LocalServerCert.ps1). This C++
    // code does not load, validate, or rotate the cert -- HTTP.sys does.
    //
    // Failure mode: if the operator forgot to bind a cert, HTTP.sys
    // returns NO_ERROR from HttpAddUrlToUrlGroup (the URL prefix is
    // registered) but every incoming HTTPS handshake will fail at the
    // TLS layer with the client seeing an SSL_ERROR. The runtime stays
    // up; the HTTP prefix keeps serving normally; the operator's first
    // diagnostic is a 5061 in the System event log
    // ("A fatal error occurred when attempting to access the SSL
    // server credential private key"). We surface a hint in the status
    // message so the operator can correlate.
    //
    // We do NOT fail Start() if the HTTPS prefix fails to register --
    // HTTP fallback is the more important behaviour. The error is
    // recorded in the status message instead.
    bool tlsBound = false;
    std::string tlsBindError;
    if (configuration_.tlsEnabled) {
        std::wstring tlsUrlPrefix = L"https://+:"
            + std::to_wstring(configuration_.tlsListenPort) + L"/";
        ULONG tlsAddResult = NO_ERROR;
        for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
            tlsAddResult = HttpAddUrlToUrlGroup(
                urlGroupId_, tlsUrlPrefix.c_str(), 0, 0);
            if (tlsAddResult != ERROR_ALREADY_EXISTS) {
                break;
            }
            Sleep(kRetryDelayMs);
        }
        if (tlsAddResult == NO_ERROR) {
            tlsBound = true;
        } else if (tlsAddResult == ERROR_ACCESS_DENIED) {
            tlsBindError = "TLS prefix bind: ACCESS_DENIED on https://+:"
                + std::to_string(configuration_.tlsListenPort)
                + "/ -- run scripts\\Configure-LocalServerCert.ps1 from "
                  "an elevated shell, or pre-register: netsh http add urlacl "
                  "url=https://+:" + std::to_string(configuration_.tlsListenPort)
                + "/ user=Everyone";
        } else if (tlsAddResult == ERROR_ALREADY_EXISTS) {
            tlsBindError = "TLS prefix bind: https://+:"
                + std::to_string(configuration_.tlsListenPort)
                + "/ is held by another process / leaked URL group.";
        } else {
            tlsBindError = "TLS prefix bind failed (code "
                + std::to_string(tlsAddResult) + ").";
        }
    }

    // Create the request queue.
    HANDLE queue = nullptr;
    ULONG queueResult = HttpCreateRequestQueue(
        httpVersion, nullptr, nullptr, 0, &queue);
    if (queueResult != NO_ERROR) {
        status_.state = GatewayState::Failed;
        status_.message = "HttpCreateRequestQueue failed (code "
            + std::to_string(queueResult) + ").";
        closeHttpSysHandlesLocked();
        return status_;
    }
    requestQueue_ = queue;

    // Bind queue to URL group.
    HTTP_BINDING_INFO binding{};
    binding.Flags.Present = 1;
    binding.RequestQueueHandle = requestQueue_;
    ULONG bindResult = HttpSetUrlGroupProperty(
        urlGroupId_, HttpServerBindingProperty,
        &binding, sizeof(binding));
    if (bindResult != NO_ERROR) {
        status_.state = GatewayState::Failed;
        status_.message = "HttpSetUrlGroupProperty(BindingProperty) failed (code "
            + std::to_string(bindResult) + ").";
        closeHttpSysHandlesLocked();
        return status_;
    }

    running_ = true;
    {
        std::lock_guard<std::mutex> queueLock(jobQueueMutex_);
        jobQueueShutdown_ = false;
        jobQueue_.clear();
    }
    serveThread_ = std::thread(&NativeHttpSysGatewayAdapter::serveLoop, this);
    // Bounded execution pool (concurrency remediation): MCP requests are
    // value-copied into jobQueue_ by the receive loop and executed here,
    // so a slow tools/call occupies one worker instead of the receive
    // path. /health never queues -- serveLoop answers it inline.
    workerThreads_.reserve(kGatewayWorkerCount);
    for (std::size_t workerIndex = 0; workerIndex < kGatewayWorkerCount; ++workerIndex) {
        workerThreads_.emplace_back(&NativeHttpSysGatewayAdapter::workerLoop, this);
    }

    status_.state = GatewayState::Running;
    // tlsBound on the gateway status is the runtime signal that downstream surfaces gate on. It
    // is true ONLY when:
    //   1. cfg.mcpGateway.tlsEnabled == true
    //   2. The HTTPS URL prefix successfully registered via
    //      HttpAddUrlToUrlGroup (captured in the local `tlsBound` above)
    //   3. The operator declared a cert by setting
    //      cfg.mcpGateway.tlsCertThumbprint -- non-empty thumbprint
    //
    // HTTP.sys returns NO_ERROR on URL-prefix registration even when no
    // sslcert is bound; the prior
    // emit would publish HTTPS URLs that any handshake would fail.
    // Treating tlsCertThumbprint as the operator's "I bound a cert"
    // signal (Configure-LocalServerCert.ps1 prints the thumbprint snippet
    // for exactly this purpose) closes that gap without requiring the
    // runtime to do its own TLS-handshake self-probe.
    const bool tlsCertDeclared = !configuration_.tlsCertThumbprint.empty();
    const bool tlsRuntimeReady = configuration_.tlsEnabled
                                 && tlsBound
                                 && tlsCertDeclared;
    status_.tlsBound = tlsRuntimeReady;
    if (tlsRuntimeReady) {
        status_.mcpUrlTls = composeMcpUrlTls(configuration_);
        status_.tlsCertThumbprint = configuration_.tlsCertThumbprint;
    } else {
        status_.mcpUrlTls.clear();
        status_.tlsCertThumbprint.clear();
    }

    // Operator-facing status message: surface enough state to triage
    // each failure mode (bind failed, cert missing, etc.) without
    // requiring a /api/config GET.
    std::string runningMessage = "the in-process HTTP.sys adapter listening on " + status_.mcpUrl;
    if (configuration_.tlsEnabled) {
        if (tlsRuntimeReady) {
            runningMessage += " (+ TLS dual-bind on " + status_.mcpUrlTls + ")";
        } else if (!tlsBound) {
            runningMessage += " (TLS configured but URL prefix bind FAILED: "
                + tlsBindError + ")";
        } else if (!tlsCertDeclared) {
            runningMessage += " (TLS configured + URL prefix registered, but "
                              "cfg.mcpGateway.tlsCertThumbprint is empty -- "
                              "run scripts\\Configure-LocalServerCert.ps1 and "
                              "merge the printed snippet into mcos.config.json. "
                              "HTTPS handshakes will fail until a cert is bound.)";
        }
    }
    runningMessage += ". MCP tools/list and tools/call routed through the supervisor + lease router.";
    status_.message = runningMessage;
    status_.startedAtUtc = timestampNowUtc();
    return status_;
#endif
}

GatewayStatus NativeHttpSysGatewayAdapter::Stop() {
#if defined(_WIN32)
    // Shutdown-deadlock remediation: never join the serve/worker threads
    // while holding mutex_. The serve thread's /health route calls Probe()
    // (which locks mutex_); pre-remediation Stop() held mutex_ across
    // teardownHttpSysLocked()'s serveThread_.join(), so a /health request
    // in flight at stop time deadlocked the process. Lifecycle now:
    //   1. under mutex_: validate state, mark Stopping, snapshot handles;
    //   2. no locks: shut down the HTTP.sys queue, signal the job queue,
    //      join receive + worker threads;
    //   3. under mutex_ again: close handles, finalize Stopped state.
    // Lifecycle serialization (review fix): held for the whole teardown so
    // no Start() or second Stop() can overlap the unlocked join phase.
    // Also makes destruction safe: ~NativeHttpSysGatewayAdapter's Stop()
    // waits here for any in-flight teardown instead of returning early
    // while another thread is still joining member threads.
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
    HANDLE queueToShutdown = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.state == GatewayState::Stopping) {
            // Defensive only: unreachable while lifecycleMutex_ serializes
            // Stop() callers, but kept so a crashed teardown cannot be
            // re-entered.
            return status_;
        }
        if (!running_ && requestQueue_ == nullptr) {
            // Nothing to do.
            if (status_.state != GatewayState::Disabled) {
                status_.state = GatewayState::Stopped;
                status_.startedAtUtc.clear();
                status_.message = "the in-process HTTP.sys adapter stopped.";
            }
            return status_;
        }
        status_.state = GatewayState::Stopping;
        running_ = false;
        queueToShutdown = requestQueue_;
    }

    if (queueToShutdown != nullptr) {
        // Triggers ERROR_OPERATION_ABORTED in the blocked
        // HttpReceiveHttpRequest call inside serveLoop, which causes the
        // receive loop to exit promptly.
        HttpShutdownRequestQueue(queueToShutdown);
    }
    {
        std::lock_guard<std::mutex> queueLock(jobQueueMutex_);
        jobQueueShutdown_ = true;
    }
    jobQueueCv_.notify_all();
    if (serveThread_.joinable()) {
        serveThread_.join();
    }
    for (auto& worker : workerThreads_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workerThreads_.clear();
    {
        std::lock_guard<std::mutex> queueLock(jobQueueMutex_);
        jobQueue_.clear();
        jobQueueShutdown_ = false;   // allow a future Start()
    }

    std::lock_guard<std::mutex> lock(mutex_);
    closeHttpSysHandlesLocked();
    status_.state = GatewayState::Stopped;
    status_.startedAtUtc.clear();
    status_.message = "the in-process HTTP.sys adapter stopped. Registry preserved in-memory.";
    return status_;
#else
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
#endif
}

#if defined(_WIN32)
void NativeHttpSysGatewayAdapter::closeHttpSysHandlesLocked() {
    running_ = false;
    if (requestQueue_ != nullptr) {
        HttpCloseRequestQueue(requestQueue_);
        requestQueue_ = nullptr;
    }
    if (urlGroupId_ != 0) {
        HttpCloseUrlGroup(urlGroupId_);
        urlGroupId_ = 0;
    }
    if (serverSessionId_ != 0) {
        HttpCloseServerSession(serverSessionId_);
        serverSessionId_ = 0;
    }
    if (httpInitialized_) {
        HttpTerminate(HTTP_INITIALIZE_SERVER, nullptr);
        httpInitialized_ = false;
    }
}
#endif

GatewayStatus NativeHttpSysGatewayAdapter::CurrentStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

GatewayHealth NativeHttpSysGatewayAdapter::Probe() {
    GatewayHealth health;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        health.adapterType = AdapterType();
        health.probedAtUtc = timestampNowUtc();
        health.mcpUrl = status_.mcpUrl;
        health.healthUrl = composeHealthUrl(configuration_);
        health.registeredServerCount = static_cast<int>(registry_.size());
#if defined(_WIN32)
        if (running_ && requestQueue_ != nullptr) {
            health.status = GatewayHealthStatus::Healthy;
            health.reachable = true;
            health.httpStatusCode = 200;
            health.message = "HTTP.sys request queue is bound and accepting requests.";
        } else if (status_.state == GatewayState::Disabled) {
            health.status = GatewayHealthStatus::Unknown;
            health.message = "Adapter is disabled.";
        } else {
            health.status = GatewayHealthStatus::Unhealthy;
            health.message = "Adapter is not running. Last status: " + status_.message;
        }
#else
        health.status = GatewayHealthStatus::Unhealthy;
        health.message = "the in-process HTTP.sys adapter requires Windows.";
#endif
    }
    return health;
}

RegistrationResult NativeHttpSysGatewayAdapter::RegisterHttpServer(const McpServerRegistration& server) {
    RegistrationResult result;
    if (!validateServerRegistration(server, result)) {
        return result;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    registry_[server.name] = server;
    result.succeeded = true;
    result.registeredAtUtc = timestampNowUtc();
    result.message = "Logical HTTP-server endpoint registered with the native gateway.";
    return result;
}

RegistrationResult NativeHttpSysGatewayAdapter::RegisterStdioServer(const McpServerRegistration& server) {
    RegistrationResult result;
    if (!validateServerRegistration(server, result)) {
        return result;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    registry_[server.name] = server;
    result.succeeded = true;
    result.registeredAtUtc = timestampNowUtc();
    result.message = "Logical stdio-backed pool registered with the native gateway.";
    return result;
}

DeregistrationResult NativeHttpSysGatewayAdapter::DeregisterServer(const std::string& serverName) {
    std::lock_guard<std::mutex> lock(mutex_);
    DeregistrationResult result;
    result.serverName = serverName;
    if (registry_.erase(serverName) > 0) {
        result.succeeded = true;
        result.message = "Server deregistered.";
    } else {
        result.succeeded = false;
        result.message = "Server name not found in native gateway registry.";
    }
    return result;
}

std::vector<McpToolDescriptor> NativeHttpSysGatewayAdapter::ListTools() const {
    // v0.9.3 primed the cache here; v0.10.19 made every call defer to the
    // refresh path so InvalidateToolCatalog() surfaces immediately. The
    // lock-hygiene remediation keeps both behaviors but moves the child
    // stdio RPC outside the gateway mutex -- see currentToolCatalog().
    return currentToolCatalog();
}

std::string NativeHttpSysGatewayAdapter::GatewayMcpUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.mcpUrl;
}

std::string NativeHttpSysGatewayAdapter::AdapterType() const {
    return "native";
}

void NativeHttpSysGatewayAdapter::AttachWorkerBridge(
    std::shared_ptr<IWorkerSupervisor> workerSupervisor,
    std::shared_ptr<ILeaseRouter> leaseRouter) {
    std::lock_guard<std::mutex> lock(mutex_);
    workerSupervisor_ = std::move(workerSupervisor);
    leaseRouter_ = std::move(leaseRouter);
}

void NativeHttpSysGatewayAdapter::AttachCapabilityResolver(
    CapabilityResolver resolver,
    CapabilityDenialAuditSink auditSink) {
    std::lock_guard<std::mutex> lock(mutex_);
    capabilityResolver_ = std::move(resolver);
    capabilityDenialAuditSink_ = std::move(auditSink);
}

// PHASE-12 follow-up (v0.6.10): walk every pool, find its first Ready
// instance, ask it tools/list via the stdio bridge, and merge the results
// into a fresh catalog. Each tool entry is tagged with serverName=poolId
// so tools/call can route by name. Caller holds mutex_.
//
// v0.9.5: TTL cache. If the catalog was refreshed within the last
// kToolCatalogCacheTtlSeconds (currently 30s) we return the cached
// vector verbatim instead of re-fanning-out tools/list to every pool's
// child via stdio. This drops the per-LAN-client tools/list cost from
// O(pools) stdio round-trips down to a single pointer copy when the
// cache is warm. Cache is invalidated implicitly by the time-based TTL;
// operator actions that change the catalog shape (POST /api/pools, POST
// /api/pools/{poolId}/{remove,scale,drain}) currently do not bump the
// cache -- the TTL is the only revalidation mechanism. A sub-second
// cache for the catalog is still acceptable because tools rarely
// change registration in flight; if a pool gets registered or removed,
// the next LAN-client tools/list within 30s sees stale state, and the
// one after that sees the new shape. v0.9.6 candidate: explicit cache
// bump from upsertPool/removePool/{remove,scale,drain} routes.
namespace {
constexpr int kToolCatalogCacheTtlSeconds = 30;
} // namespace

std::vector<McpToolDescriptor> NativeHttpSysGatewayAdapter::currentToolCatalog() const {
    // Lock-hygiene remediation (was refreshToolCatalogLocked, which ran
    // child stdio RPC for up to 5s per pool WHILE holding mutex_ and
    // stalled every other gateway method). Three phases:
    //   1. under mutex_: TTL check + supervisor snapshot;
    //   2. no gateway lock: collectToolCatalog() fans out tools/list to
    //      pool children over stdio;
    //   3. under mutex_: publish the cache + new TTL stamp.
    // Review follow-up: a single refresh runs at a time. A second caller
    // that misses the TTL while a refresh is in flight serves the current
    // (possibly stale) cache instead of doubling the child RPC fan-out --
    // stale-while-revalidate, well within the 30s TTL tolerance. Note: an
    // InvalidateToolCatalog() racing an in-flight refresh may be
    // overwritten by that refresh's publish; the next TTL expiry (<= 30s)
    // picks up the change, matching the pre-existing last-writer window.
    std::shared_ptr<IWorkerSupervisor> supervisor;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        if (now < toolCatalogCacheValidUntil_) {
            return toolCatalogCache_;
        }
        if (!workerSupervisor_) {
            toolCatalogCache_.clear();
            // Even on the no-supervisor path, mark the cache valid for the
            // TTL window so we don't loop on this branch every call.
            toolCatalogCacheValidUntil_ = now + std::chrono::seconds(kToolCatalogCacheTtlSeconds);
            return toolCatalogCache_;
        }
        if (toolCatalogRefreshInFlight_) {
            return toolCatalogCache_;
        }
        toolCatalogRefreshInFlight_ = true;
        supervisor = workerSupervisor_;
    }

    std::vector<McpToolDescriptor> aggregated;
    try {
        aggregated = collectToolCatalog(supervisor);
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        toolCatalogRefreshInFlight_ = false;
        throw;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    toolCatalogRefreshInFlight_ = false;
    toolCatalogCache_ = aggregated;
    toolCatalogCacheValidUntil_ =
        std::chrono::steady_clock::now() + std::chrono::seconds(kToolCatalogCacheTtlSeconds);
    return aggregated;
}

std::vector<McpToolDescriptor> NativeHttpSysGatewayAdapter::collectToolCatalog(
    const std::shared_ptr<IWorkerSupervisor>& supervisor) const {
    // Caller must NOT hold mutex_: every sendStdioJsonRpc below can block
    // for up to 5s per pool. bridgeRequestIdCounter_ is atomic precisely
    // so id generation here needs no gateway lock.
    std::vector<McpToolDescriptor> aggregated;
    if (!supervisor) {
        return aggregated;
    }
    const auto pools = supervisor->listPools();
    for (const auto& pool : pools) {
        // Find a Ready instance to query. Drained, Starting, Failed, and
        // Stopped instances are skipped. If none is Ready we silently skip
        // -- ADR-002 §9: don't fabricate.
        std::string instanceId;
        for (const auto& instance : pool.instances) {
            if (instance.state == EndpointInstanceState::Ready
                && instance.supervised) {
                instanceId = instance.instanceId;
                break;
            }
        }
        if (instanceId.empty()) {
            continue;
        }

        // Build a tools/list JSON-RPC envelope. id is a unique scalar so
        // the supervisor's stdio bridge correlator can match the response.
        const uint64_t bridgeId = bridgeRequestIdCounter_++;
        nlohmann::json env = {
            { "jsonrpc", "2.0" },
            { "id", bridgeId },
            { "method", "tools/list" },
            { "params", nlohmann::json::object() }
        };
        const std::string envelope = env.dump();

        // Forward via the stdio bridge with a short timeout (5s) -- if
        // the child is wedged we don't want LAN client tools/list to
        // hang on it.
        const auto bridgeResult = supervisor->sendStdioJsonRpc(
            instanceId, envelope, /*timeoutMs=*/5000);
        if (!bridgeResult.succeeded || bridgeResult.responseBody.empty()) {
            // Child unreachable, timed out, or did not implement
            // tools/list. Skip silently; pool simply contributes no tools.
            continue;
        }

        // Parse and walk the .result.tools array. The MCP wire format is:
        //   { "jsonrpc":"2.0", "id":N, "result": { "tools": [ {...}, ... ] } }
        try {
            const auto reply = nlohmann::json::parse(bridgeResult.responseBody);
            if (!reply.contains("result")
                || !reply["result"].is_object()
                || !reply["result"].contains("tools")
                || !reply["result"]["tools"].is_array()) {
                continue;
            }
            for (const auto& tool : reply["result"]["tools"]) {
                if (!tool.is_object() || !tool.contains("name")) {
                    continue;
                }
                McpToolDescriptor descriptor;
                descriptor.serverName  = pool.poolId;
                descriptor.toolName    = tool.value("name", std::string{});
                descriptor.description = tool.value("description", std::string{});
                // v0.9.4: capture the child-reported inputSchema verbatim
                // as a serialized JSON object string. Pre-v0.9.4 the
                // descriptor model didn't carry the schema, so the
                // gateway's tools/list always emitted the open
                // `{"type":"object"}` placeholder even when the worker
                // advertised properties + required + descriptions for
                // every argument. Schema-driven LAN clients (Claude
                // Code, MCP SDK code generators) consequently couldn't
                // discover argument names from tools/list; they had to
                // either read the human description or guess. Now the
                // schema flows end-to-end. Empty / non-object schemas
                // are normalized to empty string so the consumer side
                // handles "no schema" with a single condition.
                if (tool.contains("inputSchema") && tool["inputSchema"].is_object()) {
                    descriptor.inputSchemaJson = tool["inputSchema"].dump();
                }
                descriptor.requiredCapabilities =
                    requiredCapabilitiesForMcpTool(descriptor.serverName, descriptor.toolName);
                descriptor.risk = highestCapabilityRisk(descriptor.requiredCapabilities);
                descriptor.highRisk = !descriptor.requiredCapabilities.empty();
                aggregated.push_back(std::move(descriptor));
            }
        } catch (const std::exception&) {
            // Garbage from child; skip.
            continue;
        }
    }
    return aggregated;
}

void NativeHttpSysGatewayAdapter::InvalidateToolCatalog() {
    // v0.9.6: bumps the validity stamp to the past so the next
    // refreshToolCatalogLocked call inside tools/list does a fresh
    // fan-out instead of returning the stale cached vector. Called
    // by the runtime's pool-admin handlers after upsertPool /
    // removePool / scale / drain so operator pool changes surface
    // immediately rather than after the 30s TTL.
    std::lock_guard<std::mutex> lock(mutex_);
    toolCatalogCacheValidUntil_ = std::chrono::steady_clock::time_point{};
}

#if defined(_WIN32)
namespace {
// Build a JSON-RPC error response body.
//
// v0.9.48: nlohmann::json::dump() throws type_error.316 on invalid UTF-8
// bytes in any string field (including the embedded `message` text and
// any `id` echoed from the client). The pre-v0.9.48 caller path was:
// serveLoop -> handleMcpRequest -> json::parse(body) -> catch -> build
// JsonRpcError(..., ex.what()) -> envelope.dump() THROWS. The throw
// escaped both buildJsonRpcError and handleMcpRequest, leaving serveLoop
// with an uncaught std::exception which propagated out of the worker
// thread function and tripped std::terminate, taking the entire MCOS
// service down. Operator-visible: any tools/* request with malformed
// UTF-8 in its body (e.g. a Latin-1 byte 0xE9 from a CP1252 client)
// crashed MCOS. The bug-hunt found this by accident sending `é` from
// Git Bash on a CP1252 host.
//
// Fix: dump() with error_handler_t::replace so invalid byte sequences
// become U+FFFD in the output instead of throwing. This is the standard
// "lenient JSON serializer" behavior. We also wrap the dump in try/
// catch as defense-in-depth in case future nlohmann versions throw on
// other content; if dump() ever does throw we return a hand-rolled
// minimal valid JSON-RPC error envelope.
std::string buildJsonRpcError(int code, const std::string& message,
                              const nlohmann::json& id = nullptr) {
    nlohmann::json envelope = {
        { "jsonrpc", "2.0" },
        { "id", id },
        { "error", {
            { "code", code },
            { "message", message }
        } }
    };
    try {
        return envelope.dump(/*indent=*/-1,
                             /*indent_char=*/' ',
                             /*ensure_ascii=*/false,
                             /*error_handler=*/nlohmann::json::error_handler_t::replace);
    } catch (const std::exception&) {
        // Defense-in-depth: hand-roll a minimal valid JSON-RPC error.
        // The client gets a generic -32603 internal-error response
        // instead of a service crash.
        return std::string(R"({"jsonrpc":"2.0","id":null,"error":{"code":-32603,"message":"Internal serialization error."}})");
    }
}
} // namespace

CapabilityAuthorizationContext NativeHttpSysGatewayAdapter::resolveCapabilities(
    const std::string& clientId,
    const std::string& clientIpAddress) const {
    CapabilityResolver resolver;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        resolver = capabilityResolver_;
    }
    if (!resolver) {
        CapabilityAuthorizationContext context;
        context.actor = clientId.empty() ? std::string("anonymous") : clientId;
        context.clientId = clientId;
        context.denialReason = "MCP capability resolver is not configured.";
        return context;
    }
    return resolver(clientId, clientIpAddress);
}

void NativeHttpSysGatewayAdapter::auditCapabilityDenial(
    const CapabilityDenialAuditEvent& event) const {
    CapabilityDenialAuditSink auditSink;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auditSink = capabilityDenialAuditSink_;
    }
    if (auditSink) {
        auditSink(event);
    }
}

std::string BuildGatewayInternalErrorBody(const std::string& detail) {
    // JSON-safe internal-error envelope (exception-escaping remediation):
    // built with nlohmann so quotes, control characters, and invalid
    // UTF-8 in exception text (error_handler_t::replace) still produce a
    // parseable JSON-RPC error document. The pre-remediation handler
    // concatenated ex.what() into a raw string literal, which corrupted
    // the JSON whenever the message contained a quote or newline.
    try {
        const nlohmann::json envelope = {
            { "jsonrpc", "2.0" },
            { "id", nullptr },
            { "error", { { "code", -32603 }, { "message", detail } } }
        };
        return envelope.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (...) {
        return R"({"jsonrpc":"2.0","id":null,"error":{"code":-32603,"message":"Internal error."}})";
    }
}

std::string NativeHttpSysGatewayAdapter::handleMcpRequest(const std::string& path,
                                                          const std::string& body,
                                                          const std::string& clientIpAddress,
                                                          const std::string& clientType,
                                                          const std::string& clientId,
                                                          const std::string& sessionId) {
    // path is reserved for v0.6.11 path-based pool scoping (/mcp/{poolId}).
    // v0.6.10 routes by `params.name` -> cached toolCatalog -> serverName.
    // v0.7.2: clientIpAddress + clientType are best-effort attribution
    // fields used only when forwarding tools/call so the lease carries
    // who-acquired-it metadata into the dashboard.
    (void)path;
    nlohmann::json req;
    try {
        req = nlohmann::json::parse(body.empty() ? std::string("{}") : body);
    } catch (const std::exception& ex) {
        return buildJsonRpcError(-32700, std::string("Parse error: ") + ex.what());
    }

    // v0.9.51: reject JSON-RPC batches (an array of request objects per
    // JSON-RPC 2.0 §6) with -32600 Invalid Request. This MCP gateway
    // handles single-request envelopes only; pre-v0.9.51 a batch body
    // reached `req.value("method", ...)` which threw type_error.306
    // ("cannot use value() with array"), bounced into the v0.9.48
    // serveLoop catch, and the client got HTTP 500 with the nlohmann
    // exception in the body. The right answer per JSON-RPC 2.0 is a
    // single Invalid Request envelope -- not the per-element batch
    // response array, since we don't process batches at all.
    if (req.is_array()) {
        return buildJsonRpcError(-32600,
            "Invalid Request: JSON-RPC batches are not supported by this gateway. Send a single request object per HTTP POST.");
    }
    if (!req.is_object()) {
        return buildJsonRpcError(-32600,
            "Invalid Request: top-level JSON must be a request object.");
    }

    const std::string method = req.value("method", std::string{});
    // nlohmann::json{nullptr} would create [null] (an array containing one
    // null element) via the initializer_list ctor; use parens to invoke the
    // basic_json(std::nullptr_t) ctor and produce the actual null scalar.
    nlohmann::json id;
    if (req.contains("id")) {
        id = req["id"];
    }
    // v0.9.4: per JSON-RPC 2.0 §4.1, a request with no `id` member is a
    // Notification and the server MUST NOT reply. The MCP wire protocol
    // uses this for `notifications/initialized` (sent by the client right
    // after `initialize` to complete the handshake) and `notifications/
    // cancelled`. Pre-v0.9.4 this dispatcher always built a response
    // envelope; for an unrecognized notification it returned a -32601
    // error with `id:null`, which strict clients (the MCP TypeScript +
    // Python SDKs included) treat as a protocol violation. Now: if the
    // request has no id, the dispatcher returns an empty string and the
    // HTTP serving loop translates that to a 204 No Content response.
    const bool isNotification = !req.contains("id");

    if (method.empty()) {
        if (isNotification) {
            return std::string{};
        }
        return buildJsonRpcError(-32600, "Invalid Request: missing method.", id);
    }

    // v0.9.4: explicit accept-and-ignore for the standard MCP
    // post-initialize notification. Even though the catch-all below also
    // returns an empty string for any notification, naming this method
    // here makes the dispatcher self-documenting and lets the activity
    // ring (added in a later increment) attribute the handshake-complete
    // event correctly without parsing the unknown-method fallthrough.
    if (isNotification && (method == "notifications/initialized"
                        || method == "notifications/cancelled")) {
        return std::string{};
    }

    if (method == "initialize") {
        nlohmann::json result = {
            // v0.9.4: advertise the protocol version that this gateway
            // actually implements end-to-end -- Streamable HTTP
            // transport (introduced in MCP rev 2025-03-26) is what the
            // onboarding profiles tell every client to use, and it is
            // what the DNS-SD TXT advertises as `protovers=2025-03-26`.
            // Pre-v0.9.4 the initialize handshake replied with the
            // older `2024-11-05` literal, which was inconsistent with
            // both the advertised protovers and the transport actually
            // in use; strict clients negotiated down to 2024-11-05
            // even when they could speak 2025-03-26.
            { "protocolVersion", "2025-03-26" },
            { "serverInfo", {
                { "name", "MCOS Native Gateway" },
                // v0.9.3: report the running MCOS version through the
                // initialize handshake. Pre-v0.9.3 this was hard-coded
                // "0.7.2" -- a relic from when the gateway adapter was
                // first added; clients consuming the version field for
                // compat decisions saw a stale value.
                { "version", MASTERCONTROL_VERSION }
            } },
            { "capabilities", {
                { "tools", { { "listChanged", false } } }
            } },
            // Model Parity (A3.12.0): concise governance instructions surfaced
            // through the optional top-level `instructions` field of the MCP
            // initialize result (legal for protocol 2025-03-26). Deliberately
            // does NOT overclaim: the gateway remains the POST-only Streamable
            // HTTP subset with no SSE upgrade.
            { "instructions",
              "This is a governed MCOS LAN orchestration gateway. Use read-only inspection tools by "
              "default. Mutating or destructive operations require explicit operator confirmation and "
              "must be verified after execution. Do not bypass governance, confirm guards, or Forsetti "
              "boundary rules. Treat governance bundles as authoritative policy contracts." }
        };
        nlohmann::json envelope = {
            { "jsonrpc", "2.0" },
            { "id", id },
            { "result", result }
        };
        return envelope.dump();
    }

    // v0.9.1: ping is part of the MCP wire protocol (used as a liveness
    // check by Claude Code, Codex, and the dashboard). Per spec the
    // response is an empty result object on a JSON-RPC 2.0 envelope.
    // Pre-v0.9.1 the gateway returned -32601 which made every ping
    // probe count as a failed call in the activity log even though
    // the gateway was demonstrably alive.
    if (method == "ping") {
        nlohmann::json envelope = {
            { "jsonrpc", "2.0" },
            { "id", id },
            { "result", nlohmann::json::object() }
        };
        return envelope.dump();
    }

    if (method == "tools/list") {
        // PHASE-12 follow-up (v0.6.10): aggregate tools/list from every
        // supervised pool's first Ready instance via the stdio bridge.
        // currentToolCatalog builds a fresh catalog (child RPC outside the
        // gateway mutex) and updates the cache so subsequent tools/call can
        // resolve names. If the bridge is not attached (workerSupervisor_
        // is null), returns an empty honest array per ADR-002 §9.
        const std::vector<McpToolDescriptor> catalog = currentToolCatalog();
        const auto capabilityContext = resolveCapabilities(clientId, clientIpAddress);
        nlohmann::json toolsArray = nlohmann::json::array();
        for (const auto& descriptor : catalog) {
            if (!hasAllCapabilities(capabilityContext.capabilities, descriptor.requiredCapabilities)) {
                continue;
            }
            // Each tool is exposed as `{poolName}__{toolName}` so AI
            // clients have unambiguous routing across multiple pools that
            // happen to expose the same local tool name. tools/call
            // accepts either the prefixed or the unprefixed form
            // (see below for resolution rules).
            const std::string qualifiedName = descriptor.serverName + "__" + descriptor.toolName;
            // v0.9.4: forward the child-reported inputSchema verbatim
            // (captured into descriptor.inputSchemaJson by
            // refreshToolCatalogLocked). Falls back to the open
            // `{"type":"object"}` placeholder only when the child
            // didn't supply a schema or the captured string failed to
            // parse -- the gateway never fabricates a schema.
            nlohmann::json inputSchema = nlohmann::json{ { "type", "object" } };
            if (!descriptor.inputSchemaJson.empty()) {
                try {
                    auto parsed = nlohmann::json::parse(descriptor.inputSchemaJson);
                    if (parsed.is_object()) {
                        inputSchema = std::move(parsed);
                    }
                } catch (const std::exception&) {
                    // Keep the placeholder; do not silently drop the
                    // tool. The child reported a malformed schema; we
                    // surface the tool so it stays callable, just
                    // without typed argument hints in tools/list.
                }
            }
            toolsArray.push_back({
                { "name", qualifiedName },
                { "description", descriptor.description },
                { "inputSchema", std::move(inputSchema) },
                { "requiredCapabilities", descriptor.requiredCapabilities },
                { "risk", descriptor.risk },
                { "highRisk", descriptor.highRisk }
            });
        }
        nlohmann::json envelope = {
            { "jsonrpc", "2.0" },
            { "id", id },
            { "result", { { "tools", std::move(toolsArray) } } }
        };
        return envelope.dump();
    }

    if (method == "tools/call") {
        // PHASE-12 follow-up (v0.6.10): forward to a lease-router-selected
        // pool instance via the stdio bridge.
        //
        // 1. Verify bridge is attached.
        // 2. Pull tool name out of params.
        // 3. Resolve to poolId by inspecting the cached catalog. The name
        //    may arrive prefixed (`{poolName}__{toolName}`, the form we
        //    advertise via tools/list) or unprefixed; both work as long as
        //    the lookup is unambiguous.
        // 4. Acquire a lease against that pool.
        // 5. Send the request envelope to the lease's instanceId. The
        //    upstream client envelope is forwarded verbatim except the
        //    `name` field is stripped of any pool prefix before relay.
        // 6. Return the bridge's responseBody as the LAN-client response.
        std::shared_ptr<IWorkerSupervisor> supervisor;
        std::shared_ptr<ILeaseRouter> router;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            supervisor = workerSupervisor_;
            router = leaseRouter_;
        }
        if (!supervisor || !router) {
            return buildJsonRpcError(-32603,
                "tools/call: stdio bridge is not attached. The native gateway "
                "was started before WorkerSupervisor and LeaseRouter were ready. "
                "This is an internal wiring bug -- restart MCOS to retry.", id);
        }

        if (!req.contains("params") || !req["params"].is_object()
            || !req["params"].contains("name")
            || !req["params"]["name"].is_string()) {
            return buildJsonRpcError(-32602,
                "tools/call: params.name is required (string).", id);
        }
        const std::string requestedName = req["params"]["name"].get<std::string>();

        // Resolve poolId from the cached catalog.
        std::string poolId;
        std::string localToolName = requestedName;
        std::vector<std::string> requiredCapabilities;
        const auto ambiguousToolError = [&]() {
            return buildJsonRpcError(-32602,
                "tools/call: tool name '" + requestedName
                + "' is exposed by multiple pools. Use the qualified "
                  "form '<poolId>__<toolName>' returned by tools/list.", id);
        };
        const auto applyResolution = [&](const McpToolNameResolution& resolution) {
            poolId.clear();
            if (resolution.status != McpToolNameResolutionStatus::Found) {
                return;
            }
            poolId = resolution.poolId;
            localToolName = resolution.localToolName;
            requiredCapabilities = resolution.requiredCapabilities;
        };
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto resolution =
                McpToolNameResolver::resolve(toolCatalogCache_, requestedName);
            if (resolution.status == McpToolNameResolutionStatus::Ambiguous) {
                return ambiguousToolError();
            }
            applyResolution(resolution);
        }
        if (poolId.empty()) {
            // Tool catalog may be stale (no tools/list since the child started).
            // Refresh once and retry the lookup before giving up. The refresh
            // performs child stdio RPC, so it runs WITHOUT the gateway mutex.
            const auto refreshedCatalog = currentToolCatalog();
            const auto resolution =
                McpToolNameResolver::resolve(refreshedCatalog, requestedName);
            if (resolution.status == McpToolNameResolutionStatus::Ambiguous) {
                return ambiguousToolError();
            }
            applyResolution(resolution);
        }
        if (poolId.empty()) {
            return buildJsonRpcError(-32601,
                "tools/call: tool '" + requestedName
                + "' not found in any supervised pool. Call tools/list to see "
                  "the current catalog.", id);
        }

        const auto capabilityContext = resolveCapabilities(clientId, clientIpAddress);
        if (!hasAllCapabilities(capabilityContext.capabilities, requiredCapabilities)) {
            std::string required = requiredCapabilities.empty()
                ? std::string("none")
                : requiredCapabilities.front();
            for (std::size_t i = 1; i < requiredCapabilities.size(); ++i) {
                required += ", " + requiredCapabilities[i];
            }
            const auto reason = capabilityContext.denialReason.empty()
                ? std::string("Required capability missing: ") + required + "."
                : capabilityContext.denialReason;
            auditCapabilityDenial(CapabilityDenialAuditEvent{
                capabilityContext.actor,
                capabilityContext.clientId,
                clientIpAddress,
                method,
                requestedName,
                requiredCapabilities,
                reason
            });
            return buildJsonRpcError(
                -32001,
                "Capability denied for tool '" + requestedName + "': " + reason,
                id);
        }

        // Build the forwarded envelope BEFORE acquiring the lease so a
        // serialization throw (e.g. invalid UTF-8 in the body) cannot
        // happen between acquire and release. We REPLACE the request id
        // with a bridge-internal id so the child's response can be matched
        // by the supervisor's stdio correlator without colliding with
        // other in-flight bridge requests. We also rewrite params.name to
        // the unprefixed local form so the child sees its own tool name,
        // not ours. The id counter is atomic -- no gateway lock needed.
        nlohmann::json forwarded = req;
        const uint64_t bridgeId = bridgeRequestIdCounter_++;
        forwarded["id"] = bridgeId;
        forwarded["params"]["name"] = localToolName;
        const std::string forwardedBody = forwarded.dump();

        // Acquire a lease for the resolved pool. Session contract
        // (session remediation): a client-provided session id (from the
        // Mcp-Session-Id header, X-MCOS-Session-Id fallback) makes the
        // lease stateful -- the router pins the session to one instance
        // and repeat calls reuse that lease. The gateway never invents
        // session ids for clients that sent none.
        // v0.7.2: client identity stamped so LeaseRouter::bindLeaseLocked
        // carries it into the bound lease and the dashboard's
        // per-sub-agent active-clients panel can attribute usage. Any of
        // these fields may be empty -- the dashboard renders 'unknown'.
        LeaseRequest leaseRequest;
        leaseRequest.poolId          = poolId;
        leaseRequest.sessionId       = sessionId;
        leaseRequest.stateful        = !sessionId.empty();
        leaseRequest.clientIpAddress = clientIpAddress;
        leaseRequest.clientType      = clientType;
        EndpointLease lease = router->acquireLease(leaseRequest);
        if (lease.state != LeaseState::Active) {
            return buildJsonRpcError(-32603,
                "tools/call: could not acquire instance lease for pool '"
                + poolId + "'. " + lease.statusMessage, id);
        }

        // Lease lifetime (continues the v0.9.34 leak fix, now exception-
        // safe via a scope guard): stateless calls release on completion;
        // stateful session leases stay bound for the session lifetime and
        // are reclaimed by the router's idle timeout or an explicit
        // /api/leases/{leaseId}/release. releaseLease is idempotent on
        // unknown ids, so racing scenarios (e.g. supervisor reaping the
        // instance mid-call) are safe.
        struct StatelessLeaseReleaseGuard final {
            std::shared_ptr<ILeaseRouter> router;
            std::string leaseId;
            bool engaged = false;
            ~StatelessLeaseReleaseGuard() {
                if (engaged && router) {
                    router->releaseLease(leaseId, "tools/call complete");
                }
            }
        } releaseGuard{ router, lease.leaseId, !leaseRequest.stateful };

        const auto bridgeResult = supervisor->sendStdioJsonRpc(
            lease.instanceId, forwardedBody, /*timeoutMs=*/30000);

        if (!bridgeResult.succeeded) {
            // Review fix: a failed bridge call must not keep a stateful
            // session pinned to a (likely dead) instance -- release the
            // sticky lease so the next call for this session rebinds to a
            // healthy instance (the router also validates instance
            // liveness on the sticky path). releaseLease is idempotent.
            if (leaseRequest.stateful) {
                router->releaseLease(lease.leaseId,
                    "stdio bridge failure; session will rebind on the next call");
            }
            return buildJsonRpcError(-32603,
                "tools/call: stdio bridge to instance '" + lease.instanceId
                + "' failed. " + bridgeResult.errorMessage, id);
        }

        // Re-stamp the response id back to the LAN client's original id
        // so the client's JSON-RPC correlator can match. The child sent
        // us bridgeId; the LAN client expects its own id.
        try {
            auto reply = nlohmann::json::parse(bridgeResult.responseBody);
            reply["id"] = id;
            return reply.dump();
        } catch (const std::exception&) {
            return buildJsonRpcError(-32603,
                "tools/call: child returned a malformed JSON-RPC envelope.", id);
        }
    }

    // v0.9.4: any unrecognized notification (no id) is silently
    // accepted -- per JSON-RPC 2.0 the server has nowhere to send the
    // error and the client has no correlator to match it to anyway.
    // Pre-v0.9.4 this branch always emitted an envelope, which strict
    // MCP clients reject as a protocol error.
    if (isNotification) {
        return std::string{};
    }
    return buildJsonRpcError(-32601, "Method not implemented: " + method, id);
}

namespace {

// Single response-send path shared by the receive loop (inline health /
// 405 / 404 / 503 answers) and the worker pool (MCP responses). 204 No
// Content suppresses body + Content-Type per RFC 7230; an optional Allow
// header (must point at storage that outlives the call, e.g. a string
// literal) backs accurate 405 semantics. Send failures are benign during
// shutdown races and are intentionally not checked, matching the
// pre-remediation behavior.
void sendGatewayHttpResponse(HANDLE requestQueue,
                             HTTP_REQUEST_ID requestId,
                             USHORT statusCode,
                             const std::string& reason,
                             const std::string& contentType,
                             const std::string& responseBody,
                             const char* allowHeader) {
    HTTP_RESPONSE response{};
    response.StatusCode = statusCode;
    response.pReason = reason.c_str();
    response.ReasonLength = static_cast<USHORT>(reason.size());

    std::string lengthStr;
    HTTP_DATA_CHUNK dataChunk{};
    if (statusCode != 204) {
        response.Headers.KnownHeaders[HttpHeaderContentType].pRawValue = contentType.c_str();
        response.Headers.KnownHeaders[HttpHeaderContentType].RawValueLength
            = static_cast<USHORT>(contentType.size());

        lengthStr = std::to_string(responseBody.size());
        response.Headers.KnownHeaders[HttpHeaderContentLength].pRawValue = lengthStr.c_str();
        response.Headers.KnownHeaders[HttpHeaderContentLength].RawValueLength
            = static_cast<USHORT>(lengthStr.size());

        dataChunk.DataChunkType = HttpDataChunkFromMemory;
        dataChunk.FromMemory.pBuffer = const_cast<char*>(responseBody.data());
        dataChunk.FromMemory.BufferLength = static_cast<ULONG>(responseBody.size());
        response.EntityChunkCount = 1;
        response.pEntityChunks = &dataChunk;
    } else {
        response.EntityChunkCount = 0;
        response.pEntityChunks = nullptr;
    }
    if (allowHeader != nullptr) {
        response.Headers.KnownHeaders[HttpHeaderAllow].pRawValue = allowHeader;
        response.Headers.KnownHeaders[HttpHeaderAllow].RawValueLength
            = static_cast<USHORT>(std::char_traits<char>::length(allowHeader));
    }

    ULONG bytesSent = 0;
    HttpSendHttpResponse(
        requestQueue,
        requestId,
        0,
        &response,
        nullptr,
        &bytesSent,
        nullptr,
        0,
        nullptr,
        nullptr);
}

} // namespace

void NativeHttpSysGatewayAdapter::serveLoop() {
    // Working buffer for HTTP_REQUEST. Per HTTP.sys docs we typically
    // need 4 KB for headers + a separate body buffer. Allocate 16 KB to
    // cover larger MCP request envelopes inline.
    std::vector<uint8_t> requestBuffer(16 * 1024);
    // Review fix (ERROR_MORE_DATA): once a receive fails with
    // ERROR_MORE_DATA the request is pinned to the RequestId returned in
    // the partial HTTP_REQUEST and MUST be re-received by that id -- a
    // HTTP_NULL_ID receive only returns other requests, orphaning the
    // oversized one until the connection times out. Track the id across
    // the retry.
    HTTP_REQUEST_ID pendingRequestId = HTTP_NULL_ID;

    while (running_) {
        ULONG bytesRead = 0;
        ULONG receiveResult = HttpReceiveHttpRequest(
            requestQueue_,
            pendingRequestId,
            HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY,
            reinterpret_cast<PHTTP_REQUEST>(requestBuffer.data()),
            static_cast<ULONG>(requestBuffer.size()),
            &bytesRead,
            nullptr);

        if (receiveResult == ERROR_OPERATION_ABORTED) {
            break; // queue shut down
        }
        if (receiveResult == ERROR_MORE_DATA) {
            // The request didn't fit. Remember its id, enlarge the buffer,
            // and re-receive THAT request.
            pendingRequestId = reinterpret_cast<PHTTP_REQUEST>(requestBuffer.data())->RequestId;
            requestBuffer.resize(bytesRead);
            continue;
        }
        if (receiveResult == ERROR_CONNECTION_INVALID && pendingRequestId != HTTP_NULL_ID) {
            // The pinned request's connection went away between receives;
            // drop back to normal intake.
            pendingRequestId = HTTP_NULL_ID;
            continue;
        }
        pendingRequestId = HTTP_NULL_ID;
        if (receiveResult != NO_ERROR) {
            // Transient or fatal -- backoff briefly and retry.
            Sleep(50);
            continue;
        }

        const PHTTP_REQUEST request = reinterpret_cast<PHTTP_REQUEST>(requestBuffer.data());

        // Compose absolute path from pRawUrl. RawUrl is the percent-
        // decoded path+query as the client sent it.
        std::string path;
        if (request->pRawUrl != nullptr && request->RawUrlLength > 0) {
            path.assign(request->pRawUrl, request->RawUrlLength);
            const auto query = path.find('?');
            if (query != std::string::npos) path = path.substr(0, query);
        }

        // Pull the inline body chunks. HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY
        // copies as much of the body as fits in the receive buffer; if
        // the body is fragmented across packets or larger than fits with
        // headers, the remainder must be drained via
        // HttpReceiveRequestEntityBody. Also: certain client libraries
        // (e.g. PowerShell Invoke-RestMethod with Expect:100-continue or
        // chunked transfer-encoding) send the body in a way that makes
        // HTTP.sys leave it for explicit retrieval rather than inlining.
        //
        // Review follow-up: the drain no longer happens here. A blocking
        // HttpReceiveRequestEntityBody loop on the receive thread let one
        // slow-trickle client stall /health and all request intake for the
        // duration of its dribbled upload. The receive thread now only
        // copies the inline chunks; MCP jobs carry a drain flag and the
        // WORKER retrieves the remainder before executing.
        std::string body;
        for (USHORT i = 0; i < request->EntityChunkCount; ++i) {
            const auto& chunk = request->pEntityChunks[i];
            if (chunk.DataChunkType == HttpDataChunkFromMemory) {
                body.append(reinterpret_cast<const char*>(chunk.FromMemory.pBuffer),
                            chunk.FromMemory.BufferLength);
            }
        }
        const bool drainRemainingBody =
            (request->Flags & HTTP_REQUEST_FLAG_MORE_ENTITY_BODY_EXISTS) != 0
            || body.empty();

        // Size cap: reject an oversized declared Content-Length up front
        // (the worker-side drain enforces the same cap on the accumulated
        // bytes for chunked bodies that declare no length).
        bool payloadTooLarge = body.size() > kMaxGatewayRequestBytes;
        {
            const auto& lengthHeader = request->Headers.KnownHeaders[HttpHeaderContentLength];
            if (lengthHeader.pRawValue != nullptr && lengthHeader.RawValueLength > 0) {
                const std::string declared(lengthHeader.pRawValue, lengthHeader.RawValueLength);
                bool numeric = !declared.empty() && declared.size() <= 18;
                unsigned long long declaredBytes = 0;
                for (const char c : declared) {
                    if (c < '0' || c > '9') { numeric = false; break; }
                    declaredBytes = declaredBytes * 10 + static_cast<unsigned long long>(c - '0');
                }
                if (numeric && declaredBytes > kMaxGatewayRequestBytes) {
                    payloadTooLarge = true;
                }
            }
        }

        // v0.7.2: extract LAN-client identity from the HTTP request so
        // tools/call leases can carry attribution into the dashboard's
        // per-sub-agent active-clients panel. clientIpAddress comes from
        // HTTP_REQUEST::Address.pRemoteAddress (sockaddr* over AF_INET /
        // AF_INET6), formatted to a printable form via inet_ntop. clientType
        // is read from X-MCOS-Client-Type or X-MCOS-Client-Id header
        // (operator-set), falling back to best-effort User-Agent inference
        // (claude-code / codex / grok / chatgpt / generic-mcp). Both fields
        // are best-effort: empty strings flow through fine and the
        // dashboard simply renders 'unknown'.
        std::string clientIpAddress;
        if (request->Address.pRemoteAddress != nullptr) {
            char buffer[64] = { 0 };
            const auto* family = request->Address.pRemoteAddress;
            if (family->sa_family == AF_INET) {
                const auto* in4 = reinterpret_cast<const sockaddr_in*>(family);
                if (inet_ntop(AF_INET, &in4->sin_addr, buffer, sizeof(buffer)) != nullptr) {
                    clientIpAddress.assign(buffer);
                }
            } else if (family->sa_family == AF_INET6) {
                const auto* in6 = reinterpret_cast<const sockaddr_in6*>(family);
                if (inet_ntop(AF_INET6, &in6->sin6_addr, buffer, sizeof(buffer)) != nullptr) {
                    clientIpAddress.assign(buffer);
                }
            }
        }

        std::string clientId;
        std::string clientType;
        std::string mcpSessionId;
        std::string mcosSessionId;
        // Walk the unknown-headers list (HTTP.sys puts custom X- headers
        // here) and the User-Agent slot in known-headers.
        for (USHORT h = 0; h < request->Headers.UnknownHeaderCount; ++h) {
            const auto& uh = request->Headers.pUnknownHeaders[h];
            if (uh.pName == nullptr || uh.pRawValue == nullptr) continue;
            std::string name(uh.pName, uh.NameLength);
            // Case-insensitive compare against X-MCOS-Client-Type / -Id
            // and the session headers.
            std::string lower;
            lower.reserve(name.size());
            for (char c : name) {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            if (lower == "x-mcos-client-id") {
                clientId.assign(uh.pRawValue, uh.RawValueLength);
            } else if (lower == "x-mcos-client-type") {
                clientType.assign(uh.pRawValue, uh.RawValueLength);
            } else if (lower == "mcp-session-id") {
                mcpSessionId.assign(uh.pRawValue, uh.RawValueLength);
            } else if (lower == "x-mcos-session-id") {
                mcosSessionId.assign(uh.pRawValue, uh.RawValueLength);
            }
        }
        // Session contract (session remediation): the standard MCP
        // Mcp-Session-Id header wins; X-MCOS-Session-Id is the
        // MCOS-specific fallback. Empty when the client sent neither --
        // the gateway never invents a session id.
        const std::string sessionId = !mcpSessionId.empty() ? mcpSessionId : mcosSessionId;
        if (clientType.empty() && !clientId.empty()) {
            clientType = clientId;
        }
        if (clientType.empty()) {
            const auto& uaHeader = request->Headers.KnownHeaders[HttpHeaderUserAgent];
            if (uaHeader.pRawValue != nullptr && uaHeader.RawValueLength > 0) {
                std::string ua(uaHeader.pRawValue, uaHeader.RawValueLength);
                std::string uaLower;
                uaLower.reserve(ua.size());
                for (char c : ua) {
                    uaLower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                }
                if (uaLower.find("claude") != std::string::npos)        clientType = "claude-code";
                else if (uaLower.find("codex") != std::string::npos)    clientType = "codex";
                else if (uaLower.find("grok") != std::string::npos)     clientType = "grok";
                else if (uaLower.find("chatgpt") != std::string::npos
                      || uaLower.find("openai") != std::string::npos)   clientType = "chatgpt";
                else                                                    clientType = "generic-mcp";
            }
        }

        // Route (concurrency remediation). The receive thread answers
        // /health, wrong-verb, unknown-path, and queue-saturated responses
        // inline -- all cheap, none touch child processes. Well-formed MCP
        // POSTs are value-copied into the bounded job queue and executed
        // by the worker pool, so one slow tools/call can never block
        // /health or the receive loop.
        std::string responseBody;
        std::string contentType = "application/json";
        USHORT statusCode = 200;
        std::string reason = "OK";
        const char* allowHeader = nullptr;

        const auto& healthPath = configuration_.healthPath.empty()
            ? std::string("/health") : configuration_.healthPath;
        const auto& mcpPath = configuration_.mcpPath.empty()
            ? std::string("/mcp") : configuration_.mcpPath;

        if (path == healthPath || (path.size() > healthPath.size()
                                   && path.rfind(healthPath, 0) == 0
                                   && path[healthPath.size()] == '/')) {
            const auto health = Probe();
            // Locked snapshot -- the pre-remediation code read status_
            // unlocked here, racing Stop()'s state transitions.
            const GatewayStatus statusSnapshot = CurrentStatus();
            nlohmann::json body_j = {
                { "adapterType", AdapterType() },
                { "state", to_string(statusSnapshot.state) },
                { "health", to_string(health.status) },
                { "message", statusSnapshot.message }
            };
            responseBody = body_j.dump();
        } else if (path == mcpPath || (path.size() > mcpPath.size()
                                       && path.rfind(mcpPath, 0) == 0
                                       && path[mcpPath.size()] == '/')) {
            // v0.9.10: only POST belongs on the MCP path. JSON-RPC over
            // Streamable HTTP uses POST for client->server messages; GET
            // on /mcp is reserved for opening an SSE stream, which this
            // build intentionally does not implement (POST-only alpha
            // transport -- onboarding profiles say the same). The 405
            // carries an accurate Allow: POST header and a valid JSON
            // body.
            const HTTP_VERB verb = request->Verb;
            if (payloadTooLarge) {
                statusCode = 413;
                reason = "Payload Too Large";
                nlohmann::json err = {
                    { "error", "MCP request body exceeds the gateway limit" },
                    { "maxRequestBytes", kMaxGatewayRequestBytes }
                };
                responseBody = err.dump();
            } else if (verb != HttpVerbPOST) {
                statusCode = 405;
                reason = "Method Not Allowed";
                allowHeader = "POST";
                nlohmann::json err = {
                    { "error", "MCP path requires POST" },
                    { "path", path },
                    { "allow", "POST" },
                    { "method_received",
                      verb == HttpVerbGET     ? "GET"     :
                      verb == HttpVerbPUT     ? "PUT"     :
                      verb == HttpVerbDELETE  ? "DELETE"  :
                      verb == HttpVerbHEAD    ? "HEAD"    :
                      verb == HttpVerbOPTIONS ? "OPTIONS" :
                      verb == HttpVerbTRACE   ? "TRACE"   :
                      verb == HttpVerbCONNECT ? "CONNECT" :
                                                "OTHER" },
                    // Note: PATCH was added to HTTP_VERB in the Win10
                    // SDK; older Windows SDKs don't expose it. We
                    // deliberately don't reference HttpVerbPATCH here
                    // so this file builds on every targeted SDK.
                    // PATCH callers (rare) appear as "OTHER" above.
                    { "hint", "MCP traffic is JSON-RPC 2.0 over POST. SSE upgrade is not implemented in this build." }
                };
                responseBody = err.dump();
            } else {
                bool enqueued = false;
                std::size_t queueDepth = 0;
                {
                    std::lock_guard<std::mutex> queueLock(jobQueueMutex_);
                    queueDepth = jobQueue_.size();
                    if (!jobQueueShutdown_ && queueDepth < kGatewayJobQueueMaxDepth) {
                        GatewayRequestJob job;
                        job.requestId          = request->RequestId;
                        job.path               = path;
                        job.body               = body;
                        job.drainRemainingBody = drainRemainingBody;
                        job.clientIpAddress    = clientIpAddress;
                        job.clientType         = clientType;
                        job.clientId           = clientId;
                        job.sessionId          = sessionId;
                        jobQueue_.push_back(std::move(job));
                        enqueued = true;
                    }
                }
                if (enqueued) {
                    jobQueueCv_.notify_one();
                    // The worker pool owns the response; receive the next
                    // request immediately.
                    continue;
                }
                // Bounded-queue backpressure: a saturated queue answers
                // with a structured 503 instead of allocating without
                // bound.
                statusCode = 503;
                reason = "Service Unavailable";
                nlohmann::json err = {
                    { "error", "Gateway request queue is saturated" },
                    { "queueDepth", queueDepth },
                    { "maxQueueDepth", kGatewayJobQueueMaxDepth },
                    { "hint", "Retry shortly. Long-running tools/call requests are occupying "
                              "all gateway workers and the bounded request queue is full." }
                };
                responseBody = err.dump();
            }
        } else {
            statusCode = 404;
            reason = "Not Found";
            nlohmann::json err = {
                { "error", "Path not handled by native MCP gateway" },
                { "path", path },
                { "hint", "Use " + mcpPath + " for MCP traffic, " + healthPath + " for health." }
            };
            responseBody = err.dump();
        }

        sendGatewayHttpResponse(requestQueue_, request->RequestId,
                                statusCode, reason, contentType,
                                responseBody, allowHeader);
    }
}

void NativeHttpSysGatewayAdapter::workerLoop() {
    for (;;) {
        GatewayRequestJob job;
        {
            std::unique_lock<std::mutex> lock(jobQueueMutex_);
            jobQueueCv_.wait(lock, [this]() {
                return jobQueueShutdown_ || !jobQueue_.empty();
            });
            if (jobQueueShutdown_) {
                // Stop() has shut the HTTP.sys queue; queued requests can
                // no longer be answered. Exit promptly so Stop()'s join
                // is bounded by at most one in-flight handleMcpRequest.
                return;
            }
            job = std::move(jobQueue_.front());
            jobQueue_.pop_front();
        }
        processGatewayJob(job);
    }
}

void NativeHttpSysGatewayAdapter::processGatewayJob(const GatewayRequestJob& job) {
    std::string responseBody;
    USHORT statusCode = 200;
    std::string reason = "OK";

    // Review follow-up: drain any remaining entity body HERE, on the
    // worker, so a slow-trickle upload occupies one bounded worker slot
    // instead of the receive thread. The accumulated size is capped; an
    // oversized or broken body answers 413/400 without executing.
    std::string body = job.body;
    if (job.drainRemainingBody) {
        constexpr ULONG kBodyChunkBytes = 8 * 1024;
        std::vector<uint8_t> bodyChunk(kBodyChunkBytes);
        bool bodyBroken = false;
        for (;;) {
            ULONG bytesReturned = 0;
            const ULONG entityResult = HttpReceiveRequestEntityBody(
                requestQueue_,
                job.requestId,
                0,
                bodyChunk.data(),
                static_cast<ULONG>(bodyChunk.size()),
                &bytesReturned,
                nullptr);
            if (entityResult == NO_ERROR) {
                if (bytesReturned > 0) {
                    body.append(reinterpret_cast<const char*>(bodyChunk.data()),
                                bytesReturned);
                } else {
                    break;
                }
            } else if (entityResult == ERROR_HANDLE_EOF) {
                break;
            } else if (entityResult == ERROR_CONNECTION_INVALID
                       || entityResult == ERROR_OPERATION_ABORTED) {
                // Client vanished or queue shut down mid-drain: nothing to
                // answer.
                bodyBroken = true;
                break;
            } else {
                // ERROR_NO_MORE_BYTES (38) and others -- treat as end of
                // body, matching the pre-split behavior.
                break;
            }
            if (body.size() > kMaxGatewayRequestBytes) {
                nlohmann::json err = {
                    { "error", "MCP request body exceeds the gateway limit" },
                    { "maxRequestBytes", kMaxGatewayRequestBytes }
                };
                sendGatewayHttpResponse(requestQueue_, job.requestId,
                                        413, "Payload Too Large",
                                        "application/json", err.dump(), nullptr);
                return;
            }
        }
        if (bodyBroken) {
            return;
        }
    }

    // v0.9.48 defense-in-depth retained: any uncaught exception becomes a
    // 500 with a JSON-safe error envelope. BuildGatewayInternalErrorBody
    // escapes the exception text (the pre-remediation handler concatenated
    // ex.what() into raw JSON, corrupting the envelope on quotes/newlines).
    try {
        responseBody = handleMcpRequest(job.path, body, job.clientIpAddress,
                                        job.clientType, job.clientId, job.sessionId);
    } catch (const std::exception& ex) {
        statusCode = 500;
        reason = "Internal Server Error";
        responseBody = BuildGatewayInternalErrorBody(std::string("Internal error: ") + ex.what());
    } catch (...) {
        statusCode = 500;
        reason = "Internal Server Error";
        responseBody = BuildGatewayInternalErrorBody("Internal error: unknown exception.");
    }
    // v0.9.4: empty MCP response means handleMcpRequest recognized a
    // JSON-RPC notification (no `id` field) and intentionally produced no
    // envelope per spec -- reply 204 No Content (no body, no Content-Type)
    // so strict MCP clients don't see a protocol violation.
    if (statusCode == 200 && responseBody.empty()) {
        statusCode = 204;
        reason = "No Content";
    }
    // requestQueue_ stays valid for the worker's lifetime: Stop() closes
    // the handle only after joining the worker threads. A send racing
    // HttpShutdownRequestQueue fails benignly.
    sendGatewayHttpResponse(requestQueue_, job.requestId,
                            statusCode, reason, "application/json",
                            responseBody, nullptr);
}
#endif

} // namespace MasterControl
