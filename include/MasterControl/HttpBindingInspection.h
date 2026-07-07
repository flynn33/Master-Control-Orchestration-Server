// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// Platform-agnostic HTTP.sys binding inspection contracts. A "binding" is a
// host-level integration that a listener depends on: a Windows Firewall rule,
// an HTTP.sys URL ACL reservation, or an HTTP.sys TLS certificate binding.
//
// The working-alpha acceptance path must distinguish REQUIRED bindings (whose
// absence is a blocking failure) from OPTIONAL bindings (whose absence is
// merely reported). This header owns that decision as a pure function so the
// bootstrapper JSON, the acceptance scripts, and the unit tests all reason
// about the same rule. Windows-specific observation (netsh / firewall queries)
// lives behind IHttpBindingInspector in the bootstrapper/platform layer; this
// header stays free of Windows and runtime-model dependencies so both the
// bootstrapper translation unit and the test translation unit compile it
// without include cycles.

#pragma once

#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace MasterControl {

// Which host integration an expectation/observation refers to.
enum class HttpBindingKind {
    Firewall,
    UrlAcl,
    Tls,
};

// Verdict for a single binding after comparing expectation to observation.
enum class HttpBindingVerdict {
    Passed,           // required && present (TLS: thumbprint matches when pinned)
    OptionalMissing,  // !required && !present -> reported, never fatal
    Failed,           // required && (!present || thumbprint mismatch), or a
                      // present-but-mismatched TLS cert even when optional
};

// What the selected install mode expects for a single surface/binding.
struct BindingExpectation final {
    HttpBindingKind kind = HttpBindingKind::Firewall;
    std::string surface;              // "admin" | "gateway" | "beacon" | "mdns"
    bool required = false;            // required in the selected install mode
    std::uint16_t port = 0;
    std::string urlPrefix;            // URL ACL prefix, e.g. "http://+:8080/"
    std::string ruleName;             // firewall rule display name
    std::string expectedThumbprint;   // TLS only; empty => any bound cert accepted
    std::string profile;              // firewall profile scope (informational)
    std::string protocol;             // "tcp" | "udp" (informational)
};

// What was actually observed on the host. Implementations only report; they do
// not decide pass/fail.
struct BindingObservation final {
    bool present = false;             // the binding actually exists on the host
    std::string thumbprint;           // TLS only; observed bound cert thumbprint
    std::string detail;               // raw tool output / human-readable note
};

// The classified result: expectation + observation + verdict.
struct HttpBindingStatus final {
    HttpBindingKind kind = HttpBindingKind::Firewall;
    std::string surface;
    bool required = false;
    bool present = false;
    bool optionalMissing = false;
    HttpBindingVerdict verdict = HttpBindingVerdict::OptionalMissing;
    std::uint16_t port = 0;
    std::string thumbprint;
    std::string detail;

    bool failed() const { return verdict == HttpBindingVerdict::Failed; }
};

inline const char* bindingKindToString(HttpBindingKind kind) {
    switch (kind) {
        case HttpBindingKind::Firewall: return "firewall";
        case HttpBindingKind::UrlAcl:   return "urlAcl";
        case HttpBindingKind::Tls:      return "tls";
    }
    return "unknown";
}

inline const char* bindingVerdictToString(HttpBindingVerdict verdict) {
    switch (verdict) {
        case HttpBindingVerdict::Passed:          return "passed";
        case HttpBindingVerdict::OptionalMissing: return "optionalMissing";
        case HttpBindingVerdict::Failed:          return "failed";
    }
    return "unknown";
}

// Normalize a certificate thumbprint for comparison: drop separators/spaces and
// lowercase. netsh renders thumbprints uppercase and sometimes with spaces;
// operators may paste colon- or space-delimited forms.
inline std::string normalizeThumbprint(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (const unsigned char ch : raw) {
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return out;
}

inline bool thumbprintsMatch(const std::string& expected, const std::string& observed) {
    return normalizeThumbprint(expected) == normalizeThumbprint(observed);
}

// Pure, platform-agnostic decision. Given what the selected mode required and
// what was observed on the host, decide the verdict. No Windows/netsh calls.
inline HttpBindingStatus classifyHttpBinding(const BindingExpectation& expectation,
                                             const BindingObservation& observation) {
    HttpBindingStatus status;
    status.kind = expectation.kind;
    status.surface = expectation.surface;
    status.required = expectation.required;
    status.present = observation.present;
    status.port = expectation.port;
    status.thumbprint = observation.thumbprint;
    status.detail = observation.detail;

    // A pinned TLS thumbprint must match; firewall/urlAcl and unpinned TLS have
    // no thumbprint constraint.
    const bool thumbprintOk =
        expectation.kind != HttpBindingKind::Tls ||
        expectation.expectedThumbprint.empty() ||
        thumbprintsMatch(expectation.expectedThumbprint, observation.thumbprint);

    if (expectation.required) {
        status.optionalMissing = false;
        status.verdict = (observation.present && thumbprintOk)
            ? HttpBindingVerdict::Passed
            : HttpBindingVerdict::Failed;
        return status;
    }

    if (!observation.present) {
        status.optionalMissing = true;
        status.verdict = HttpBindingVerdict::OptionalMissing;
        return status;
    }

    // Present though optional. A cert that is bound but does not match a pinned
    // thumbprint is a real misconfiguration, so surface it as failed rather
    // than silently accepting the wrong certificate.
    status.optionalMissing = false;
    status.verdict = thumbprintOk ? HttpBindingVerdict::Passed : HttpBindingVerdict::Failed;
    return status;
}

// Narrow seam over "observe a binding on this host". The Windows concrete
// (NetshHttpBindingInspector) lives in the bootstrapper/platform layer and
// shells netsh / firewall queries; tests use FakeHttpBindingInspector. The
// pass/fail decision is always owned by classifyHttpBinding().
class IHttpBindingInspector {
public:
    virtual ~IHttpBindingInspector() = default;

    // Report the current on-host state for the given expectation. Reporting
    // only; no classification.
    virtual BindingObservation Observe(const BindingExpectation& expectation) const = 0;

    // Observe + classify in one call.
    HttpBindingStatus Inspect(const BindingExpectation& expectation) const {
        return classifyHttpBinding(expectation, Observe(expectation));
    }
};

// Test double: returns scripted observations keyed by surface + kind. A missing
// key yields a default (absent) observation.
class FakeHttpBindingInspector final : public IHttpBindingInspector {
public:
    void setObservation(const std::string& surface, HttpBindingKind kind,
                        BindingObservation observation) {
        observations_[key(surface, kind)] = std::move(observation);
    }

    BindingObservation Observe(const BindingExpectation& expectation) const override {
        const auto it = observations_.find(key(expectation.surface, expectation.kind));
        return it == observations_.end() ? BindingObservation{} : it->second;
    }

private:
    static std::string key(const std::string& surface, HttpBindingKind kind) {
        return surface + "/" + std::to_string(static_cast<int>(kind));
    }

    std::map<std::string, BindingObservation> observations_;
};

} // namespace MasterControl
