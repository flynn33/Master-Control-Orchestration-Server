// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "SupervisorAssignmentService.h"

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <bcrypt.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace MasterControl {

namespace {

const char* kSchemaId = "mcos.supervisor.config.v1";

// Provider-id strings are serialized lowercase per schema enum.
const std::unordered_map<std::string, SupervisorProvider>& providerLookup() {
    static const std::unordered_map<std::string, SupervisorProvider> kMap = {
        {"chatgpt", SupervisorProvider::ChatGpt},
        {"claude",  SupervisorProvider::Claude},
        {"grok",    SupervisorProvider::Grok}
    };
    return kMap;
}

const std::unordered_map<std::string, SupervisorMode>& modeLookup() {
    static const std::unordered_map<std::string, SupervisorMode> kMap = {
        {"disabled",              SupervisorMode::Disabled},
        {"read_only_supervisor",  SupervisorMode::ReadOnlySupervisor},
        {"decision_supervisor",   SupervisorMode::DecisionSupervisor},
        {"autonomous_supervisor", SupervisorMode::AutonomousSupervisor},
        {"break_glass_admin",     SupervisorMode::BreakGlassAdmin}
    };
    return kMap;
}

const std::unordered_map<std::string, SupervisorState>& stateLookup() {
    static const std::unordered_map<std::string, SupervisorState> kMap = {
        {"off",                SupervisorState::Off},
        {"config_generated",   SupervisorState::ConfigGenerated},
        {"pending_connection", SupervisorState::PendingConnection},
        {"connected",          SupervisorState::Connected},
        {"disconnected",       SupervisorState::Disconnected},
        {"revoked",            SupervisorState::Revoked},
        {"error",              SupervisorState::Error}
    };
    return kMap;
}

std::string toLower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (auto c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

std::string nowIso8601Utc() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto time = system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string addSecondsIso8601(const std::string& isoNow, int64_t seconds) {
    using namespace std::chrono;
    std::tm tm{};
    std::istringstream in(isoNow);
    in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (in.fail()) {
        // Fallback: drop expiration if base parse failed.
        return isoNow;
    }
#if defined(_WIN32)
    auto t = _mkgmtime(&tm);
#else
    auto t = timegm(&tm);
#endif
    t += static_cast<time_t>(seconds);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

// Compact UTC stamp suitable for an id suffix: 20260510T184500Z.
std::string compactUtcStamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto time = system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y%m%dT%H%M%SZ");
    return out.str();
}

std::string randomHexBytes(size_t bytes) {
    std::vector<uint8_t> data(bytes);
    bool filled = data.empty();
#if defined(_WIN32)
    if (!data.empty()) {
        filled = BCryptGenRandom(nullptr,
                                 data.data(),
                                 static_cast<ULONG>(data.size()),
                                 BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
    }
#endif
    if (!filled) {
        std::random_device rd;
        for (auto& byte : data) {
            byte = static_cast<uint8_t>(rd());
        }
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : data) {
        out << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return out.str();
}

// Hex random suffix so concurrent select calls within one second do
// not collide on the same id.
std::string randomHexSuffix(size_t bytes) {
    return randomHexBytes(bytes);
}

std::string supervisorTokenRef() {
    return std::string("mcos-supervisor-token:") + randomHexBytes(32);
}

bool isLegacyDerivedTokenRef(const SupervisorAssignment& assignment) {
    return !assignment.configId.empty()
        && assignment.tokenRef == std::string("mcos-supervisor-token:") + assignment.configId;
}

// Stable-ish fingerprint hash. Not cryptographic, but distinct enough
// across hosts for client-side identification and obvious to spot in
// audit logs. SHA-256 is the natural choice; we fold to 64-bit here to
// avoid pulling in <bcrypt> for one helper. Operator-facing name keeps
// "sha256" prefix because the schema field reads as a fingerprint, not
// a strong hash.
std::string fingerprint(const std::string& seed) {
    uint64_t h = 1469598103934665603ULL;  // FNV-1a offset basis
    for (auto c : seed) {
        h ^= static_cast<uint8_t>(c);
        h *= 1099511628211ULL;
    }
    std::ostringstream out;
    out << "sha256:" << std::hex << std::setfill('0') << std::setw(16) << h;
    return out.str();
}

void writeFileAtomic(const std::filesystem::path& target, const std::string& body) {
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    const auto tmp = target.parent_path() / (target.filename().string() + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return;
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
    }
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        // Fallback: copy + remove. Keeps the previous file if rename fails.
        std::filesystem::copy_file(tmp, target,
            std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(tmp, ec);
    }
}

std::string readFileAll(const std::filesystem::path& source) {
    std::ifstream in(source, std::ios::binary);
    if (!in.is_open()) return std::string{};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

class SupervisorAssignmentServiceImpl final : public ISupervisorAssignmentService {
public:
    explicit SupervisorAssignmentServiceImpl(SupervisorAssignmentServiceContext context)
        : context_(std::move(context)) {
        normalizeEndpointPlanContext();
        loadFromDisk();
    }

    SupervisorAssignment getCurrentAssignment() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return assignment_;
    }

    SupervisorIssueResult selectAndIssue(const SupervisorSelectRequest& request) override {
        SupervisorIssueResult result;
        if (request.provider == SupervisorProvider::Unknown) {
            result.ok = false;
            result.errorMessage = "providerId must be one of chatgpt, claude, grok.";
            return result;
        }
        std::lock_guard<std::mutex> lock(mutex_);

        // Revoke previous assignment first if it isn't already off.
        if (assignment_.state != SupervisorState::Off
            && assignment_.state != SupervisorState::Revoked) {
            assignment_.revokedAtUtc = nowIso8601Utc();
            assignment_.revocationReason =
                std::string("Superseded by selection of ")
                + providerIdString(request.provider) + ".";
            assignment_.state = SupervisorState::Revoked;
            persistLocked();
            // Fresh assignment record below replaces it.
        }

        const auto issuedAt = nowIso8601Utc();
        const auto expiresAt = addSecondsIso8601(issuedAt,
            static_cast<int64_t>(context_.defaultConfigTtl.count()));

        SupervisorAssignment fresh;
        fresh.assignmentId = std::string("SUP-") + compactUtcStamp() + "-" + randomHexSuffix(2);
        fresh.provider = request.provider;
        fresh.mode = (request.mode == SupervisorMode::Disabled)
            ? SupervisorMode::AutonomousSupervisor
            : request.mode;
        fresh.exclusive = request.exclusive;
        fresh.state = SupervisorState::ConfigGenerated;
        fresh.configId = std::string("CFG-") + compactUtcStamp() + "-" + randomHexSuffix(2);
        fresh.issuedAtUtc = issuedAt;
        fresh.expiresAtUtc = expiresAt;
        fresh.allowedCapabilities = capabilitiesForMode(fresh.mode);
        fresh.tokenRef = supervisorTokenRef();
        fresh.serverFingerprint = fingerprint(context_.fingerprintSeed
            + "|" + fresh.assignmentId + "|" + fresh.configId);
        fresh.auditCorrelationId = std::string("AUD-") + randomHexSuffix(4);

        assignment_ = fresh;
        persistLocked();

        result.ok = true;
        result.assignment = assignment_;
        result.fileName = std::string("mcos-supervisor-")
            + providerIdString(assignment_.provider) + ".config.json";
        result.contentType = "application/json";
        result.configJson = buildConfigJson(assignment_).dump(2);
        return result;
    }

    SupervisorIssueResult regenerateConfig() override {
        SupervisorIssueResult result;
        std::lock_guard<std::mutex> lock(mutex_);
        if (assignment_.state == SupervisorState::Off
            || assignment_.state == SupervisorState::Revoked) {
            result.ok = false;
            result.errorMessage = "No active supervisor assignment to regenerate.";
            return result;
        }
        // Re-issue config id + expiration, keep assignment id stable.
        const auto issuedAt = nowIso8601Utc();
        assignment_.configId = std::string("CFG-") + compactUtcStamp() + "-" + randomHexSuffix(2);
        assignment_.issuedAtUtc = issuedAt;
        assignment_.expiresAtUtc = addSecondsIso8601(issuedAt,
            static_cast<int64_t>(context_.defaultConfigTtl.count()));
        assignment_.tokenRef = supervisorTokenRef();
        assignment_.state = SupervisorState::ConfigGenerated;
        assignment_.connectedAtUtc.clear();
        assignment_.lastHeartbeatUtc.clear();
        // v0.10.1: clear any stale rejection message from a previous
        // lifecycle. The new config id supersedes the prior one, so a
        // "Config id does not match the active assignment." error from
        // a long-ago failed confirm attempt should not haunt the
        // dashboard after the operator regenerates.
        assignment_.lastErrorMessage.clear();
        persistLocked();
        result.ok = true;
        result.assignment = assignment_;
        result.fileName = std::string("mcos-supervisor-")
            + providerIdString(assignment_.provider) + ".config.json";
        result.contentType = "application/json";
        result.configJson = buildConfigJson(assignment_).dump(2);
        return result;
    }

    void revoke(const std::string& reason) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (assignment_.state == SupervisorState::Off
            || assignment_.state == SupervisorState::Revoked) {
            return;
        }
        assignment_.state = SupervisorState::Revoked;
        assignment_.revokedAtUtc = nowIso8601Utc();
        assignment_.revocationReason = reason.empty()
            ? std::string("Operator-initiated revoke.") : reason;
        assignment_.connectedAtUtc.clear();
        assignment_.lastHeartbeatUtc.clear();
        // v0.10.1: clear any stale rejection error from the previous
        // lifecycle. After voluntary revoke the operator's intent is
        // "tear this assignment down"; a leftover lastErrorMessage
        // from a long-past failed confirm attempt would render in the
        // dashboard as a confusing tail on a state the operator just
        // cleaned up themselves. The revocationReason field already
        // captures why the assignment ended.
        assignment_.lastErrorMessage.clear();
        persistLocked();
    }

    SupervisorConnectionResult confirmConnection(const SupervisorConnectionClaim& claim) override {
        SupervisorConnectionResult result;
        std::lock_guard<std::mutex> lock(mutex_);
        if (assignment_.state == SupervisorState::Off
            || assignment_.state == SupervisorState::Revoked) {
            result.ok = false;
            result.newState = SupervisorState::Error;
            result.errorMessage = "No active supervisor assignment.";
            return result;
        }
        if (claim.provider != assignment_.provider) {
            result.ok = false;
            result.newState = assignment_.state;
            result.errorMessage = "Provider mismatch with active assignment.";
            assignment_.lastErrorMessage = result.errorMessage;
            persistLocked();
            return result;
        }
        if (claim.configId != assignment_.configId) {
            result.ok = false;
            result.newState = assignment_.state;
            result.errorMessage = "Config id does not match the active assignment.";
            assignment_.lastErrorMessage = result.errorMessage;
            persistLocked();
            return result;
        }
        if (claim.token.empty() || claim.token != assignment_.tokenRef) {
            result.ok = false;
            result.newState = assignment_.state;
            result.errorMessage = "Supervisor assignment token is missing or does not match the active assignment.";
            assignment_.lastErrorMessage = result.errorMessage;
            persistLocked();
            return result;
        }
        if (!claim.fingerprint.empty() && claim.fingerprint != assignment_.serverFingerprint) {
            result.ok = false;
            result.newState = assignment_.state;
            result.errorMessage = "Server fingerprint does not match the active assignment.";
            assignment_.lastErrorMessage = result.errorMessage;
            persistLocked();
            return result;
        }
        if (assignment_.expiresAtUtc <= nowIso8601Utc()) {
            result.ok = false;
            result.newState = SupervisorState::Error;
            result.errorMessage = "Supervisor configuration has expired; generate a new one.";
            assignment_.state = SupervisorState::Error;
            assignment_.lastErrorMessage = result.errorMessage;
            persistLocked();
            return result;
        }

        // Capability validation: every requested capability must be in
        // the mode's allowed set, and none may be in the forbidden set.
        //
        // v0.9.81 hardening: default-deny when the mode's allowed set is
        // empty. Pre-v0.9.81 the loop short-circuited `if (!allowedSet
        // .empty() && allowedSet.count(cap) == 0)` so a mode that returns
        // an empty allowed list (Disabled) would only block capabilities
        // already in the forbidden set, accepting everything else. That
        // violates the security model where Disabled means "no supervisor
        // capabilities". Now allowedSet.count(cap)==0 always rejects
        // regardless of whether the set is empty -- which means a
        // Disabled mode rejects every capability claim.
        const auto allowed = capabilitiesForMode(assignment_.mode);
        const auto forbidden = forbiddenAutonomousSupervisorCapabilities();
        std::unordered_set<std::string> allowedSet(allowed.begin(), allowed.end());
        std::unordered_set<std::string> forbiddenSet(forbidden.begin(), forbidden.end());
        for (const auto& cap : claim.capabilities) {
            if (forbiddenSet.count(cap) != 0) {
                result.ok = false;
                result.newState = assignment_.state;
                result.errorMessage = "Forbidden capability requested: " + cap;
                assignment_.lastErrorMessage = result.errorMessage;
                persistLocked();
                return result;
            }
            if (allowedSet.count(cap) == 0) {
                result.ok = false;
                result.newState = assignment_.state;
                result.errorMessage = "Capability not allowed in current mode ("
                    + std::string(supervisorModeString(assignment_.mode)) + "): " + cap;
                assignment_.lastErrorMessage = result.errorMessage;
                persistLocked();
                return result;
            }
        }

        assignment_.state = SupervisorState::Connected;
        assignment_.clientId = claim.clientId;
        assignment_.connectedAtUtc = nowIso8601Utc();
        assignment_.lastHeartbeatUtc = assignment_.connectedAtUtc;
        assignment_.lastErrorMessage.clear();
        persistLocked();

        result.ok = true;
        result.newState = SupervisorState::Connected;
        result.assignmentId = assignment_.assignmentId;
        result.clientId = assignment_.clientId;
        result.connectedAtUtc = assignment_.connectedAtUtc;
        return result;
    }

    void recordHeartbeat(const std::string& clientId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (assignment_.state != SupervisorState::Connected) return;
        if (!assignment_.clientId.empty() && clientId != assignment_.clientId) return;
        assignment_.lastHeartbeatUtc = nowIso8601Utc();
        persistLocked();
    }

    void setEndpoints(const AdvertisedEndpointPlan& plan,
                      const std::string& fingerprintSeed) override {
        // Late-binding endpoint refresh. Called by the route layer before
        // select/generate/confirm so supervisor configs use the same active
        // advertisement decision as discovery.
        std::lock_guard<std::mutex> lock(mutex_);
        context_.endpointPlan = plan;
        if (!plan.mcpEndpoint.empty()) context_.mcpEndpoint = plan.mcpEndpoint;
        if (!plan.discoveryEndpoint.empty()) context_.discoveryEndpoint = plan.discoveryEndpoint;
        normalizeEndpointPlanContext();
        if (!fingerprintSeed.empty()) context_.fingerprintSeed = fingerprintSeed;
    }

    bool expireConnectionIfStale(std::chrono::seconds threshold) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (assignment_.state != SupervisorState::Connected) return false;
        if (assignment_.lastHeartbeatUtc.empty()) return false;
        // Parse the lastHeartbeatUtc ISO-8601 timestamp back into a
        // time_t so we can compare against now - threshold. timegm is
        // the UTC variant of mktime on POSIX; _mkgmtime is the MSVC
        // equivalent (called above too).
        std::tm tm{};
        std::istringstream in(assignment_.lastHeartbeatUtc);
        in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        if (in.fail()) return false;
#if defined(_WIN32)
        auto last = _mkgmtime(&tm);
#else
        auto last = timegm(&tm);
#endif
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        if (now - last <= static_cast<time_t>(threshold.count())) return false;
        assignment_.state = SupervisorState::Disconnected;
        assignment_.lastErrorMessage = "Supervisor heartbeat watchdog: no heartbeat received within threshold; marked disconnected.";
        persistLocked();
        return true;
    }

    SupervisorStatus getStatus() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        SupervisorStatus status;
        status.provider = assignment_.provider;
        status.providerDisplayName = providerDisplayName(assignment_.provider);
        status.mode = assignment_.mode;
        status.state = assignment_.state;
        status.assignmentId = assignment_.assignmentId;
        status.configId = assignment_.configId;
        status.clientId = assignment_.clientId;
        status.issuedAtUtc = assignment_.issuedAtUtc;
        status.expiresAtUtc = assignment_.expiresAtUtc;
        status.connectedAtUtc = assignment_.connectedAtUtc;
        status.lastHeartbeatUtc = assignment_.lastHeartbeatUtc;
        status.lastErrorMessage = assignment_.lastErrorMessage;
        status.active = (assignment_.state == SupervisorState::ConfigGenerated
                        || assignment_.state == SupervisorState::PendingConnection
                        || assignment_.state == SupervisorState::Connected);
        return status;
    }

private:
    void normalizeEndpointPlanContext() {
        if (context_.endpointPlan.mcpEndpoint.empty()) {
            context_.endpointPlan.mcpEndpoint = context_.mcpEndpoint;
        }
        if (context_.endpointPlan.discoveryEndpoint.empty()) {
            context_.endpointPlan.discoveryEndpoint = context_.discoveryEndpoint;
        }
        if (context_.endpointPlan.networkMode.empty()) {
            context_.endpointPlan.networkMode = "local-only";
        }
        if (context_.endpointPlan.reason.empty()) {
            context_.endpointPlan.reason =
                "Endpoint advertisement plan was not supplied; using configured endpoints.";
        }
    }

    std::vector<std::string> capabilitiesForMode(SupervisorMode mode) const {
        if (mode == SupervisorMode::AutonomousSupervisor) {
            return defaultAutonomousSupervisorCapabilities();
        }
        if (mode == SupervisorMode::DecisionSupervisor) {
            return std::vector<std::string>{
                "supervisor.get_context",
                "supervisor.list_pending_decisions",
                "supervisor.get_decision_packet",
                "supervisor.submit_decision",
                "supervisor.get_worker_report",
                "supervisor.get_task_evidence",
                "supervisor.list_active_tasks"
            };
        }
        if (mode == SupervisorMode::ReadOnlySupervisor) {
            return std::vector<std::string>{
                "supervisor.get_context",
                "supervisor.list_pending_decisions",
                "supervisor.get_decision_packet",
                "supervisor.get_worker_report",
                "supervisor.get_task_evidence",
                "supervisor.list_active_tasks"
            };
        }
        // Disabled / BreakGlassAdmin are not handled here; the wizard
        // does not surface BreakGlassAdmin in this iteration per
        // SECURITY_AND_CAPABILITY_MODEL.md.
        return std::vector<std::string>{};
    }

    nlohmann::json buildConfigJson(const SupervisorAssignment& a) const {
        nlohmann::json doc = nlohmann::json::object();
        doc["schema"] = kSchemaId;
        doc["configId"] = a.configId;
        doc["issuedAtUtc"] = a.issuedAtUtc;
        doc["expiresAtUtc"] = a.expiresAtUtc;

        nlohmann::json server = nlohmann::json::object();
        server["name"] = context_.serverDisplayName.empty()
            ? std::string("Master Control Orchestration Server")
            : context_.serverDisplayName;
        server["mcpEndpoint"] = context_.mcpEndpoint;
        server["discoveryEndpoint"] = context_.discoveryEndpoint;
        server["fingerprint"] = a.serverFingerprint;
        server["networkMode"] = context_.endpointPlan.networkMode.empty()
            ? std::string("local-only")
            : context_.endpointPlan.networkMode;
        server["endpointAdvertisement"] = nlohmann::json{
            { "lanModeEnabled", context_.endpointPlan.lanModeEnabled },
            { "gatewayRunning", context_.endpointPlan.gatewayRunning },
            { "adminLanAdvertised", context_.endpointPlan.adminLanAdvertised },
            { "mcpLanAdvertised", context_.endpointPlan.mcpLanAdvertised },
            { "adminBaseUrl", context_.endpointPlan.adminBaseUrl },
            { "mcpHealthEndpoint", context_.endpointPlan.mcpHealthEndpoint },
            { "reason", context_.endpointPlan.reason }
        };
        doc["server"] = server;

        nlohmann::json supervisor = nlohmann::json::object();
        supervisor["providerId"] = providerIdString(a.provider);
        supervisor["role"] = "supervisor";
        supervisor["mode"] = supervisorModeString(a.mode);
        supervisor["exclusive"] = a.exclusive;
        doc["supervisor"] = supervisor;

        doc["capabilities"] = a.allowedCapabilities;

        nlohmann::json auth = nlohmann::json::object();
        auth["mode"] = "token_reference";
        auth["tokenRef"] = a.tokenRef;
        doc["auth"] = auth;

        // v0.9.83: provider-specific instructions block. Each receiver
        // (Claude, ChatGPT, Grok) has different import mechanics, and the
        // generic "Import or provide this file to the selected AI model"
        // step was actionable for none of them. The schema permits any
        // {summary, steps} shape; we now emit step text tailored to the
        // chosen provider so the operator on the LAN-client side has a
        // concrete path forward.
        nlohmann::json instructions = buildInstructionsForProvider(a);
        doc["instructions"] = instructions;
        return doc;
    }

    // v0.9.88: build a copy-paste-ready curl example for the
    // connect/confirm step. The supervisor config gets imported into
    // the receiving client's machine; the operator on that side may
    // not know the JSON body shape /api/supervisor/connect/confirm
    // expects, so emitting a concrete bash example with the actual
    // configId + capability set spelled out removes the friction.
    std::string buildConfirmCurlExample(const SupervisorAssignment& a) const {
        // Derive the admin port from the gateway endpoint. context_
        // .mcpEndpoint is "http://<host>:<gatewayPort>/mcp" while
        // /api/supervisor/connect/confirm lives at the admin port
        // (typically the dashboard URL host:browserPort). Operators
        // can override via the discovery endpoint which already lives
        // at the admin port; default to it for the example.
        std::string adminBase = context_.discoveryEndpoint;
        // Trim trailing "/.well-known/mcos.json" to recover the admin
        // host:port.
        const std::string wellKnownSuffix = "/.well-known/mcos.json";
        if (adminBase.size() > wellKnownSuffix.size()
            && adminBase.compare(adminBase.size() - wellKnownSuffix.size(),
                                 wellKnownSuffix.size(), wellKnownSuffix) == 0) {
            adminBase.resize(adminBase.size() - wellKnownSuffix.size());
        }
        if (adminBase.empty()) {
            adminBase = "http://127.0.0.1:7300";
        }
        std::ostringstream curl;
        curl << "curl -X POST " << adminBase << "/api/supervisor/connect/confirm "
             << "-H 'Content-Type: application/json' "
             << "-d '{"
             << "\"configId\":\"" << a.configId << "\","
             << "\"providerId\":\"" << providerIdString(a.provider) << "\","
             << "\"clientId\":\"" << providerIdString(a.provider) << "-supervisor-client\","
             << "\"capabilities\":[";
        bool first = true;
        for (const auto& cap : a.allowedCapabilities) {
            if (!first) curl << ",";
            first = false;
            curl << "\"" << cap << "\"";
        }
        curl << "],\"fingerprint\":\"" << a.serverFingerprint << "\","
             << "\"token\":\"" << a.tokenRef << "\"}'";
        return curl.str();
    }

    nlohmann::json buildInstructionsForProvider(const SupervisorAssignment& a) const {
        nlohmann::json instructions = nlohmann::json::object();
        const std::string mcpEndpoint = context_.mcpEndpoint;
        if (a.provider == SupervisorProvider::Claude) {
            instructions["summary"] =
                "Use this file on the LAN client that will act as the Claude supervisor for MCOS.";
            instructions["steps"] = nlohmann::json::array({
                "Move this file to the LAN machine that runs Claude (Claude Desktop or Claude Code).",
                "For Claude Code: add an entry to ~/.claude.json (or the project's .mcp.json) under "
                    "mcpServers pointing the 'url' field at this config's server.mcpEndpoint ("
                    + mcpEndpoint + ").",
                "For Claude Desktop: open Settings -> Developer -> Edit Config and add an mcpServers "
                    "block with the same url + a transport of 'streamable-http'.",
                "Optionally store the auth.tokenRef value as MCOS_SUPERVISOR_TOKEN_REF on that client "
                    "machine so future supervisor capability calls authenticate against the right "
                    "assignment.",
                "Confirm the supervisor connection so the MCOS shell flips from 'pending connection' "
                    "to 'connected'. Sample curl on the client machine: "
                    + buildConfirmCurlExample(a),
                "Optionally enable periodic heartbeats by having the client POST /api/supervisor/"
                    "heartbeat every ~30s; the MCOS watchdog flips state to 'disconnected' after "
                    "120s without a heartbeat."
            });
        } else if (a.provider == SupervisorProvider::ChatGpt) {
            instructions["summary"] =
                "Use this file on the LAN client that will act as the ChatGPT supervisor for MCOS.";
            instructions["steps"] = nlohmann::json::array({
                "Move this file to the LAN machine that runs the ChatGPT custom connector / Custom MCP "
                    "App (ChatGPT macOS app v1.2024.x+ or the ChatGPT desktop preview).",
                "Open the ChatGPT app's Custom Connector / MCP setup and register a new MCP endpoint "
                    "with the URL from server.mcpEndpoint (" + mcpEndpoint + ").",
                "If the ChatGPT client supports header injection, add X-MCOS-Supervisor-ConfigId set "
                    "to this file's configId so MCOS can correlate ChatGPT's calls with this "
                    "assignment.",
                "Confirm the supervisor connection so MCOS flips to 'connected'. Sample curl: "
                    + buildConfirmCurlExample(a),
                "Note: ChatGPT MCP support is evolving; if direct LAN MCP connection isn't available "
                    "in your installed ChatGPT build, MCOS also accepts the supervisor as a tools-"
                    "via-cloud-connector mode (set auth.mode to 'oauth' in your client wrapper)."
            });
        } else if (a.provider == SupervisorProvider::Grok) {
            instructions["summary"] =
                "Use this file on the LAN client that will act as the Grok supervisor for MCOS.";
            instructions["steps"] = nlohmann::json::array({
                "Move this file to the LAN machine that runs the Grok MCP bridge or your Grok-API-"
                    "compatible supervisor client.",
                "Configure the bridge with the URL from server.mcpEndpoint (" + mcpEndpoint
                    + ") and the auth.tokenRef as the client identification header.",
                "Confirm the supervisor connection so MCOS flips to 'connected'. Sample curl on the "
                    "bridge machine: " + buildConfirmCurlExample(a),
                "Note: A first-party Grok MCP client is not yet announced as of 2026-05. The "
                    "configuration here is intentionally Grok-agnostic at the wire level so any "
                    "future Grok bridge that speaks streamable-http MCP can use this file unchanged.",
                "Enable periodic heartbeats (~30s) at POST /api/supervisor/heartbeat to keep the "
                    "assignment from flipping to 'disconnected' after the 120s watchdog window."
            });
        } else {
            // Defensive fallback for unknown / future providers. Should
            // never run because the wizard surface constrains provider
            // to the three values above.
            instructions["summary"] = "Use this file on the LAN client that will act as the supervisor for MCOS.";
            instructions["steps"] = nlohmann::json::array({
                "Move this file to the selected supervisor client machine.",
                "Configure the client's MCP endpoint to " + mcpEndpoint + ".",
                "Confirm the supervisor connection by POSTing /api/supervisor/connect/confirm with "
                    "this file's providerId + configId.",
                "Optionally enable heartbeats via POST /api/supervisor/heartbeat (~30s cadence)."
            });
        }
        return instructions;
    }

    void persistLocked() const {
        const auto target = context_.dataDirectory / "supervisor-assignment.json";
        const auto body = toJson(assignment_).dump(2);
        writeFileAtomic(target, body);
    }

    void loadFromDisk() {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto source = context_.dataDirectory / "supervisor-assignment.json";
        if (!std::filesystem::exists(source)) return;
        const auto raw = readFileAll(source);
        if (raw.empty()) return;
        try {
            const auto body = nlohmann::json::parse(raw);
            SupervisorAssignment loaded;
            loaded.assignmentId       = body.value("assignmentId", std::string{});
            loaded.provider           = providerFromString(body.value("providerId", std::string{}));
            loaded.clientId           = body.value("clientId", std::string{});
            loaded.mode               = supervisorModeFromString(body.value("mode", std::string{"autonomous_supervisor"}));
            loaded.exclusive          = body.value("exclusive", true);
            loaded.state              = supervisorStateFromString(body.value("state", std::string{"off"}));
            loaded.configId           = body.value("configId", std::string{});
            loaded.issuedAtUtc        = body.value("issuedAtUtc", std::string{});
            loaded.expiresAtUtc       = body.value("expiresAtUtc", std::string{});
            loaded.connectedAtUtc     = body.value("connectedAtUtc", std::string{});
            loaded.lastHeartbeatUtc   = body.value("lastHeartbeatUtc", std::string{});
            loaded.revokedAtUtc       = body.value("revokedAtUtc", std::string{});
            loaded.revocationReason   = body.value("revocationReason", std::string{});
            loaded.tokenRef           = body.value("tokenRef", std::string{});
            loaded.serverFingerprint  = body.value("serverFingerprint", std::string{});
            loaded.auditCorrelationId = body.value("auditCorrelationId", std::string{});
            loaded.lastErrorMessage   = body.value("lastErrorMessage", std::string{});
            if (body.contains("allowedCapabilities") && body["allowedCapabilities"].is_array()) {
                for (const auto& c : body["allowedCapabilities"]) {
                    if (c.is_string()) loaded.allowedCapabilities.push_back(c.get<std::string>());
                }
            }
            // v0.10.1: load-time migration for the dashboard "Config id
            // does not match the active assignment." sticky-error bug.
            // Pre-v0.10.1, revoke() and regenerateConfig() did not clear
            // lastErrorMessage, so a long-past confirm rejection could
            // sit in the persisted Revoked / Off record indefinitely
            // and render in the dashboard's supervisor card forever.
            // For terminal states (Off, Revoked) the message is no
            // longer meaningful — revocationReason already explains why
            // the assignment ended — so drop it on load and immediately
            // persist the cleaned record so the on-disk file converges
            // to the new shape without waiting for the next operator
            // mutation.
            bool needsMigrationPersist =
                (loaded.state == SupervisorState::Off
                 || loaded.state == SupervisorState::Revoked)
                && !loaded.lastErrorMessage.empty();
            if (needsMigrationPersist) {
                loaded.lastErrorMessage.clear();
            }
            if (loaded.state != SupervisorState::Off
                && loaded.state != SupervisorState::Revoked
                && isLegacyDerivedTokenRef(loaded)) {
                loaded.tokenRef = supervisorTokenRef();
                loaded.state = SupervisorState::ConfigGenerated;
                loaded.clientId.clear();
                loaded.connectedAtUtc.clear();
                loaded.lastHeartbeatUtc.clear();
                loaded.lastErrorMessage = "Supervisor assignment token was rotated; regenerate config.";
                needsMigrationPersist = true;
            }
            assignment_ = loaded;
            if (needsMigrationPersist) {
                persistLocked();
            }
        } catch (...) {
            // Corrupted on-disk record; leave assignment_ at default Off.
        }
    }

    SupervisorAssignmentServiceContext context_;
    mutable std::mutex mutex_;
    SupervisorAssignment assignment_{};  // default: state=Off
};

} // namespace

const char* providerIdString(SupervisorProvider provider) {
    switch (provider) {
        case SupervisorProvider::ChatGpt: return "chatgpt";
        case SupervisorProvider::Claude:  return "claude";
        case SupervisorProvider::Grok:    return "grok";
        case SupervisorProvider::Unknown:
        default:                           return "";
    }
}

SupervisorProvider providerFromString(const std::string& id) {
    const auto& m = providerLookup();
    auto it = m.find(toLower(id));
    if (it == m.end()) return SupervisorProvider::Unknown;
    return it->second;
}

const char* providerDisplayName(SupervisorProvider provider) {
    switch (provider) {
        case SupervisorProvider::ChatGpt: return "ChatGPT";
        case SupervisorProvider::Claude:  return "Claude";
        case SupervisorProvider::Grok:    return "Grok";
        case SupervisorProvider::Unknown:
        default:                           return "";
    }
}

const char* supervisorModeString(SupervisorMode mode) {
    switch (mode) {
        case SupervisorMode::Disabled:              return "disabled";
        case SupervisorMode::ReadOnlySupervisor:    return "read_only_supervisor";
        case SupervisorMode::DecisionSupervisor:    return "decision_supervisor";
        case SupervisorMode::AutonomousSupervisor:  return "autonomous_supervisor";
        case SupervisorMode::BreakGlassAdmin:       return "break_glass_admin";
    }
    return "disabled";
}

SupervisorMode supervisorModeFromString(const std::string& mode) {
    const auto& m = modeLookup();
    auto it = m.find(toLower(mode));
    if (it == m.end()) return SupervisorMode::AutonomousSupervisor;
    return it->second;
}

const char* supervisorStateString(SupervisorState state) {
    switch (state) {
        case SupervisorState::Off:                return "off";
        case SupervisorState::ConfigGenerated:    return "config_generated";
        case SupervisorState::PendingConnection:  return "pending_connection";
        case SupervisorState::Connected:          return "connected";
        case SupervisorState::Disconnected:       return "disconnected";
        case SupervisorState::Revoked:            return "revoked";
        case SupervisorState::Error:              return "error";
    }
    return "off";
}

SupervisorState supervisorStateFromString(const std::string& state) {
    const auto& m = stateLookup();
    auto it = m.find(toLower(state));
    if (it == m.end()) return SupervisorState::Off;
    return it->second;
}

std::vector<std::string> defaultAutonomousSupervisorCapabilities() {
    return std::vector<std::string>{
        "supervisor.get_context",
        "supervisor.list_pending_decisions",
        "supervisor.get_decision_packet",
        "supervisor.submit_decision",
        "supervisor.issue_remediation",
        "supervisor.get_worker_report",
        "supervisor.get_task_evidence",
        "supervisor.list_active_tasks"
    };
}

std::vector<std::string> forbiddenAutonomousSupervisorCapabilities() {
    return std::vector<std::string>{
        "admin.disable_governance",
        "admin.change_global_policy",
        "admin.erase_audit_logs",
        "admin.deploy",
        "admin.release",
        "admin.rotate_secrets",
        "admin.modify_certificates",
        "admin.modify_backups",
        "worker.run_shell",
        "worker.edit_file",
        "worker.delete_file",
        "worker.push_protected_branch"
    };
}

nlohmann::json toJson(const SupervisorAssignment& a) {
    nlohmann::json out = nlohmann::json::object();
    out["assignmentId"]      = a.assignmentId;
    out["providerId"]        = providerIdString(a.provider);
    out["clientId"]          = a.clientId;
    out["mode"]              = supervisorModeString(a.mode);
    out["exclusive"]         = a.exclusive;
    out["state"]             = supervisorStateString(a.state);
    out["configId"]          = a.configId;
    out["issuedAtUtc"]       = a.issuedAtUtc;
    out["expiresAtUtc"]      = a.expiresAtUtc;
    out["connectedAtUtc"]    = a.connectedAtUtc;
    out["lastHeartbeatUtc"]  = a.lastHeartbeatUtc;
    out["revokedAtUtc"]      = a.revokedAtUtc;
    out["revocationReason"]  = a.revocationReason;
    out["allowedCapabilities"] = a.allowedCapabilities;
    out["auditCorrelationId"] = a.auditCorrelationId;
    out["lastErrorMessage"]  = a.lastErrorMessage;
    out["tokenRef"]          = a.tokenRef;
    out["serverFingerprint"] = a.serverFingerprint;
    return out;
}

nlohmann::json toJson(const SupervisorStatus& s) {
    nlohmann::json out = nlohmann::json::object();
    out["activeProviderId"]   = providerIdString(s.provider);
    out["providerDisplayName"] = s.providerDisplayName;
    out["mode"]               = supervisorModeString(s.mode);
    out["state"]              = supervisorStateString(s.state);
    out["assignmentId"]       = s.assignmentId;
    out["configId"]           = s.configId;
    out["clientId"]           = s.clientId;
    out["issuedAtUtc"]        = s.issuedAtUtc;
    out["expiresAtUtc"]       = s.expiresAtUtc;
    out["connectedAtUtc"]     = s.connectedAtUtc;
    out["lastHeartbeatUtc"]   = s.lastHeartbeatUtc;
    out["lastErrorMessage"]   = s.lastErrorMessage;
    out["active"]             = s.active;
    return out;
}

nlohmann::json toJson(const SupervisorIssueResult& r) {
    nlohmann::json out = nlohmann::json::object();
    out["ok"]            = r.ok;
    out["errorMessage"]  = r.errorMessage;
    out["fileName"]      = r.fileName;
    out["contentType"]   = r.contentType;
    if (!r.configJson.empty()) {
        try {
            out["config"] = nlohmann::json::parse(r.configJson);
        } catch (...) {
            out["config"] = nlohmann::json::object();
        }
    }
    out["assignment"]    = toJson(r.assignment);
    return out;
}

nlohmann::json toJson(const SupervisorConnectionResult& r) {
    nlohmann::json out = nlohmann::json::object();
    out["ok"]              = r.ok;
    out["state"]           = supervisorStateString(r.newState);
    out["errorMessage"]    = r.errorMessage;
    out["assignmentId"]    = r.assignmentId;
    out["clientId"]        = r.clientId;
    out["connectedAtUtc"]  = r.connectedAtUtc;
    return out;
}

std::string parseSelectRequest(const nlohmann::json& body, SupervisorSelectRequest& out) {
    if (!body.is_object()) return "Request body must be a JSON object.";
    if (!body.contains("providerId") || !body["providerId"].is_string()) {
        return "Required field providerId missing or not a string.";
    }
    out.provider = providerFromString(body["providerId"].get<std::string>());
    if (out.provider == SupervisorProvider::Unknown) {
        return "providerId must be one of chatgpt, claude, grok.";
    }
    if (body.contains("mode") && body["mode"].is_string()) {
        out.mode = supervisorModeFromString(body["mode"].get<std::string>());
        if (out.mode == SupervisorMode::BreakGlassAdmin) {
            return "break_glass_admin is not selectable through this surface.";
        }
    } else {
        out.mode = SupervisorMode::AutonomousSupervisor;
    }
    if (body.contains("exclusive") && body["exclusive"].is_boolean()) {
        out.exclusive = body["exclusive"].get<bool>();
    } else {
        out.exclusive = true;
    }
    return std::string{};
}

std::string parseConnectionClaim(const nlohmann::json& body, SupervisorConnectionClaim& out) {
    if (!body.is_object()) return "Request body must be a JSON object.";
    if (!body.contains("configId") || !body["configId"].is_string()) {
        return "Required field configId missing or not a string.";
    }
    out.configId = body["configId"].get<std::string>();
    if (!body.contains("providerId") || !body["providerId"].is_string()) {
        return "Required field providerId missing or not a string.";
    }
    out.provider = providerFromString(body["providerId"].get<std::string>());
    if (out.provider == SupervisorProvider::Unknown) {
        return "providerId must be one of chatgpt, claude, grok.";
    }
    out.clientId = body.value("clientId", std::string{});
    out.fingerprint = body.value("fingerprint", std::string{});
    out.token = body.value("token", std::string{});
    if (body.contains("capabilities") && body["capabilities"].is_array()) {
        for (const auto& c : body["capabilities"]) {
            if (c.is_string()) out.capabilities.push_back(c.get<std::string>());
        }
    }
    return std::string{};
}

std::unique_ptr<ISupervisorAssignmentService> createSupervisorAssignmentService(
        SupervisorAssignmentServiceContext context) {
    return std::make_unique<SupervisorAssignmentServiceImpl>(std::move(context));
}

} // namespace MasterControl
