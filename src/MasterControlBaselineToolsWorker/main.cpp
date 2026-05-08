// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// MCOS Baseline Tools Worker (mcos-baseline-tools-worker.exe).
//
// Purpose
// -------
// A minimal native C++20 MCP worker that ships in-tree so the gateway's
// `tools/list` returns real tools out of the box on a fresh install. Pre-
// v0.9.1 the runtime registered the 23-server baseline catalog (filesystem,
// memory, sequential-thinking, etc.) by *name only*; no worker process
// was bound to any pool, so `tools/list` always returned [] and the LAN
// AI clients saw an empty toolset. v0.9.1 wires this worker as the default
// pool so the gateway has at least one supervised pool publishing real
// tools at boot. The 23-server catalog stays the operator-facing inventory
// goal -- the operator binds real implementations to those pools as they
// deploy them; this worker provides the always-present minimum so the LAN
// AI client side of the contract is end-to-end exercisable from day one.
//
// Wire protocol
// -------------
// MCOS WorkerSupervisor::sendStdioJsonRpc speaks line-delimited JSON-RPC
// 2.0 (each request and response is a single \n-terminated UTF-8 line).
// We implement just enough of MCP for the gateway's aggregate tools/list
// + tools/call routing path:
//
//   - initialize  -> reports protocol version + serverInfo + capabilities
//   - ping        -> empty result (liveness)
//   - tools/list  -> static tool catalog (mcos.echo, mcos.now, mcos.host_info,
//                    mcos.add)
//   - tools/call  -> dispatches by tool name, returns content-array result
//                    in the MCP shape: { content: [ {type,text} ] }
//
// Anything else returns -32601 with a clear message. Notifications (no id)
// are silently consumed.
//
// Lifecycle
// ---------
// The worker is spawned by MCOS WorkerSupervisor under a Job Object with
// JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE; closing the supervisor's stdin
// triggers EOF on this side, the read loop exits, and the process
// terminates. We never daemonize, never bind a port, never write outside
// stdout/stderr.

#include <chrono>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
// We use Win32 host-info APIs (GetComputerNameExW, GetVersionExW via
// VerifyVersionInfoW... actually simpler: GetComputerNameExW + GetSystemInfo
// + reading PROCESSOR_ARCHITECTURE from environment).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace {

// -----------------------------------------------------------------------
// JSON-RPC envelope helpers.
// -----------------------------------------------------------------------

nlohmann::json makeError(int code, const std::string& message,
                         const nlohmann::json& id = nullptr) {
    return nlohmann::json{
        { "jsonrpc", "2.0" },
        { "id", id },
        { "error", {
            { "code", code },
            { "message", message }
        } }
    };
}

nlohmann::json makeResult(const nlohmann::json& id, nlohmann::json result) {
    return nlohmann::json{
        { "jsonrpc", "2.0" },
        { "id", id },
        { "result", std::move(result) }
    };
}

// Wrap a plain string into the MCP tools/call result shape:
// { content: [ { type: "text", text: "<value>" } ] }
nlohmann::json textContent(const std::string& text) {
    return nlohmann::json{
        { "content", nlohmann::json::array({
            nlohmann::json{ { "type", "text" }, { "text", text } }
        }) }
    };
}

// -----------------------------------------------------------------------
// Tool implementations.
// -----------------------------------------------------------------------

std::string nowIso8601Utc() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto seconds = time_point_cast<std::chrono::seconds>(now);
    const auto secondsSinceEpoch = static_cast<std::time_t>(seconds.time_since_epoch().count());
    std::tm utcTm{};
#if defined(_WIN32)
    gmtime_s(&utcTm, &secondsSinceEpoch);
#else
    gmtime_r(&secondsSinceEpoch, &utcTm);
#endif
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer),
                  "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  utcTm.tm_year + 1900,
                  utcTm.tm_mon + 1,
                  utcTm.tm_mday,
                  utcTm.tm_hour,
                  utcTm.tm_min,
                  utcTm.tm_sec);
    return std::string(buffer);
}

#if defined(_WIN32)
std::string utf8FromWide(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    const int length = WideCharToMultiByte(CP_UTF8, 0,
                                            wide.c_str(), static_cast<int>(wide.size()),
                                            nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0,
                        wide.c_str(), static_cast<int>(wide.size()),
                        out.data(), length, nullptr, nullptr);
    return out;
}
#endif

nlohmann::json hostInfo() {
    nlohmann::json out = nlohmann::json::object();
#if defined(_WIN32)
    // Computer name (DNS hostname).
    {
        DWORD size = 0;
        GetComputerNameExW(ComputerNamePhysicalDnsHostname, nullptr, &size);
        std::wstring buffer(size, L'\0');
        if (size > 0
            && GetComputerNameExW(ComputerNamePhysicalDnsHostname,
                                  buffer.data(), &size)) {
            buffer.resize(size);
            out["hostname"] = utf8FromWide(buffer);
        }
    }
    // OS version: Windows-recommended path is RtlGetVersion (GetVersionExW
    // is shimmed for AppCompat). We pick the simpler GetVersionExW path
    // here -- this worker is consumed by MCOS itself, never by an
    // unrelated app, so the manifest-shimming concern doesn't apply.
    OSVERSIONINFOEXW versionInfo{};
    versionInfo.dwOSVersionInfoSize = sizeof(versionInfo);
    // GetVersionExW is deprecated but still works for telemetry display.
    // We suppress the deprecation warning for this single call.
#pragma warning(push)
#pragma warning(disable : 4996)
    if (GetVersionExW(reinterpret_cast<LPOSVERSIONINFOW>(&versionInfo))) {
        std::ostringstream os;
        os << "Windows " << versionInfo.dwMajorVersion
           << "." << versionInfo.dwMinorVersion
           << " (build " << versionInfo.dwBuildNumber << ")";
        out["operatingSystem"] = os.str();
    }
#pragma warning(pop)
    // Architecture.
    SYSTEM_INFO sysInfo{};
    GetNativeSystemInfo(&sysInfo);
    const char* arch = "unknown";
    switch (sysInfo.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: arch = "x64";   break;
        case PROCESSOR_ARCHITECTURE_ARM64: arch = "arm64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: arch = "x86";   break;
        default:                           arch = "unknown"; break;
    }
    out["architecture"] = arch;
    out["logicalProcessorCount"] = static_cast<int>(sysInfo.dwNumberOfProcessors);
#else
    out["operatingSystem"] = "non-windows";
#endif
    out["worker"] = "mcos-baseline-tools-worker";
    out["workerVersion"] = "0.9.1";
    return out;
}

// -----------------------------------------------------------------------
// Tool catalog (static).
// -----------------------------------------------------------------------

nlohmann::json toolCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "mcos.echo" },
            { "description", "Echo the provided text back. Smoke-test tool that proves end-to-end gateway -> worker -> client routing." },
            { "inputSchema", {
                { "type", "object" },
                { "properties", {
                    { "text", { { "type", "string" }, { "description", "Text to echo." } } }
                } },
                { "required", nlohmann::json::array({ "text" }) }
            } }
        },
        nlohmann::json{
            { "name", "mcos.now" },
            { "description", "Return the worker host's current UTC time as an ISO-8601 string. Useful for verifying the worker has Win32 access (vs being a fabricated catalog entry)." },
            { "inputSchema", {
                { "type", "object" },
                { "properties", nlohmann::json::object() }
            } }
        },
        nlohmann::json{
            { "name", "mcos.host_info" },
            { "description", "Return the worker host's identity: hostname, OS, architecture, logical CPU count. Proves real-process supervision (the worker is observing live Win32 state, not a static reply)." },
            { "inputSchema", {
                { "type", "object" },
                { "properties", nlohmann::json::object() }
            } }
        },
        nlohmann::json{
            { "name", "mcos.add" },
            { "description", "Sum two integers and return the result. Verifies typed argument handling end-to-end through gateway -> stdio bridge -> worker." },
            { "inputSchema", {
                { "type", "object" },
                { "properties", {
                    { "a", { { "type", "integer" }, { "description", "First operand." } } },
                    { "b", { { "type", "integer" }, { "description", "Second operand." } } }
                } },
                { "required", nlohmann::json::array({ "a", "b" }) }
            } }
        }
    });
}

nlohmann::json dispatchToolCall(const std::string& toolName,
                                const nlohmann::json& arguments,
                                const nlohmann::json& id) {
    if (toolName == "mcos.echo") {
        if (!arguments.is_object() || !arguments.contains("text")
            || !arguments["text"].is_string()) {
            return makeError(-32602, "mcos.echo: arguments.text (string) is required.", id);
        }
        return makeResult(id, textContent(arguments["text"].get<std::string>()));
    }

    if (toolName == "mcos.now") {
        return makeResult(id, textContent(nowIso8601Utc()));
    }

    if (toolName == "mcos.host_info") {
        // Render the host-info object as pretty JSON inside the MCP
        // text-content shape. Clients that want structured access can
        // re-parse the text body; the simpler surface keeps us aligned
        // with the MCP content-array convention.
        return makeResult(id, textContent(hostInfo().dump(2)));
    }

    if (toolName == "mcos.add") {
        if (!arguments.is_object()
            || !arguments.contains("a") || !arguments.contains("b")
            || !arguments["a"].is_number_integer()
            || !arguments["b"].is_number_integer()) {
            return makeError(-32602, "mcos.add: arguments.a and arguments.b (integers) are required.", id);
        }
        const auto sum = arguments["a"].get<int64_t>() + arguments["b"].get<int64_t>();
        return makeResult(id, textContent(std::to_string(sum)));
    }

    return makeError(-32601,
        "Tool not implemented: " + toolName
        + ". Call tools/list for the supported set.", id);
}

// -----------------------------------------------------------------------
// JSON-RPC dispatcher.
// -----------------------------------------------------------------------

nlohmann::json handleEnvelope(const nlohmann::json& request) {
    nlohmann::json id;
    const bool hasId = request.contains("id") && !request["id"].is_null();
    if (hasId) {
        id = request["id"];
    }

    const std::string method = request.value("method", std::string{});
    if (method.empty()) {
        if (!hasId) {
            return nullptr; // notification with no method -- ignore
        }
        return makeError(-32600, "Invalid Request: missing method.", id);
    }

    if (method == "initialize") {
        nlohmann::json result = {
            { "protocolVersion", "2024-11-05" },
            { "serverInfo", {
                { "name", "MCOS Baseline Tools Worker" },
                { "version", "0.9.1" }
            } },
            { "capabilities", {
                { "tools", { { "listChanged", false } } }
            } }
        };
        return makeResult(id, std::move(result));
    }

    if (method == "ping") {
        return makeResult(id, nlohmann::json::object());
    }

    if (method == "tools/list") {
        return makeResult(id, nlohmann::json{ { "tools", toolCatalog() } });
    }

    if (method == "tools/call") {
        if (!request.contains("params") || !request["params"].is_object()
            || !request["params"].contains("name")
            || !request["params"]["name"].is_string()) {
            return makeError(-32602,
                "tools/call: params.name (string) is required.", id);
        }
        const std::string toolName = request["params"]["name"].get<std::string>();
        nlohmann::json arguments = request["params"].value("arguments", nlohmann::json::object());
        return dispatchToolCall(toolName, arguments, id);
    }

    if (!hasId) {
        // Notification for an unrecognized method -- no response required.
        return nullptr;
    }
    return makeError(-32601, "Method not implemented: " + method, id);
}

} // namespace

int main() {
#if defined(_WIN32)
    // Force stdin/stdout into binary mode so newline translation does not
    // corrupt JSON-RPC framing. The supervisor frames each envelope with
    // a single \n; if Windows translates that to \r\n on output, the
    // supervisor's '\n' line splitter still works (\r is whitespace), but
    // staying binary is the contract-correct choice.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    // Disable stdout buffering so each response flushes immediately. The
    // supervisor polls the read end of the stdout pipe with PeekNamedPipe
    // every ~25ms; if our stdout sat in the C-runtime FILE buffer we'd
    // appear hung up to the gateway under load.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // Line-delimited JSON-RPC read loop. We rely on std::getline so each
    // call returns when the next '\n' arrives -- this matches the
    // supervisor's framing (each envelope is a single \n-terminated line)
    // and avoids the trap that std::istream::read blocks until the
    // requested buffer fills, which would deadlock on the supervisor's
    // anonymous-pipe stdin (the request envelope is far smaller than any
    // sensible read buffer).
    std::string line;
    while (std::getline(std::cin, line)) {
        // Strip trailing CR if the supervisor (or an interactive
        // tester) wrote CRLF.
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Skip blank or comment lines.
        bool nonBlank = false;
        for (char c : line) {
            if (!std::isspace(static_cast<unsigned char>(c))) {
                nonBlank = (c != '#');
                break;
            }
        }
        if (!nonBlank) continue;

        nlohmann::json request;
        try {
            request = nlohmann::json::parse(line);
        } catch (const std::exception& ex) {
            const auto err = makeError(-32700,
                std::string("Parse error: ") + ex.what());
            std::cout << err.dump() << '\n';
            std::cout.flush();
            continue;
        }

        const auto response = handleEnvelope(request);
        if (!response.is_null()) {
            std::cout << response.dump() << '\n';
            std::cout.flush();
        }
    }

    return 0;
}
