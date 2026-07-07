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

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "MasterControl/MasterControlVersion.h"
#include "MasterControl/DatabaseQuery.h"

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
#include <shellapi.h>
#pragma comment(lib, "Shell32.lib")
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#include <fcntl.h>
#include <io.h>
#endif

namespace {

// v0.9.61: specialization mode. Pre-v0.9.61 this worker only served the
// "baseline-tools" specialization (mcos.echo / mcos.now / mcos.host_info /
// mcos.add) -- enough to prove end-to-end gateway -> worker -> client
// routing but not enough to back any of the other 23 catalog entries
// the operator inventory advertises. v0.9.61 turns the same .exe into
// a multi-specialization dispatch by reading --specialization=<id> from
// argv. A specialization choses its tool catalog and tools/call dispatch;
// each pool is registered with a distinct --specialization arg so the
// gateway sees it as a separate logical MCP. The default ("baseline-tools")
// preserves the v0.9.1 contract.
enum class Specialization {
    BaselineTools,       // mcos.echo, mcos.now, mcos.host_info, mcos.add
    TerminalShell,       // shell.exec(command, [cwd], [timeoutMs]) -> {stdout, stderr, exitCode}
    LocalGit,            // git.run(args, [cwd], [timeoutMs]) -> {stdout, stderr, exitCode}
    FileSearch,          // search.grep(pattern, [path], [glob], [maxMatches]) -> matches
    ClientTracker,       // clients.list -- HTTP bridge to MCOS /api/clients
    Metrics,             // metrics.host / metrics.gateway -- HTTP bridges to MCOS telemetry
    CodeExecutionRepl,   // repl.exec(language, code, [timeoutMs])
    LocalTestRunner,     // test.run(framework, args, [cwd], [timeoutMs])
    LocalBuildTool,      // build.run(tool, args, [cwd], [timeoutMs])
    LocalLinter,         // lint.run(tool, args, [cwd], [timeoutMs])
    LocalIndexer,        // index.list_files(path, [glob], [maxFiles])
    PersistentContext,   // ctx.set / ctx.get / ctx.list / ctx.delete -- JSON-file backed K/V
    KnowledgeGraph,      // kg.entities.* / kg.relations.* / kg.read_graph -- JSON entity/relation store
    FileWatcher,         // watch.add / watch.poll / watch.list / watch.remove -- polling snapshot diff
    KeyboardMouseControl,// input.keyboard / input.click / input.move / input.scroll via SendInput
    ScreenCaptureVision, // screen.capture (BitBlt + BMP) / screen.size
    DesktopControl,      // desktop.list_windows / desktop.focus / desktop.launch / desktop.foreground
    ComputerUse,         // computer.* aliases combining the three above
    // v0.9.67: 7 default sub-agents. Each is a stdio MCP server
    // exposing role-specific tools that mostly bridge to MCOS admin
    // API endpoints + filesystem reads. The "role" lives in the tool
    // names, descriptions, and the structured guidance the tools
    // emit -- the LAN AI client is what actually drives the role's
    // judgment. The sub-agents are first-class supervised pools so
    // the gateway can list / lease / route them like any MCP server.
    AgentSentinel,
    AgentArchitect,
    AgentForge,
    AgentScribe,
    AgentRecon,
    AgentNexus,
    AgentWatchtower,
    // v0.9.68: stub backings for the externally-configured MCPs.
    // These specializations expose a single status tool that
    // describes the configuration the operator needs to provide.
    // Once configured (operator-supplied connection string), the
    // operator updates the pool template (or registers a fresh one)
    // with the real executable.
    // v0.10.0: DockerControl removed at operator direction. The
    // container surface is no longer part of the MCOS baseline catalog.
    LocalDatabase
};

Specialization gSpecialization = Specialization::BaselineTools;
std::string gSpecializationName = "baseline-tools";

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
    out["workerVersion"] = MASTERCONTROL_VERSION;
    return out;
}

// -----------------------------------------------------------------------
// v0.9.61: terminal-shell specialization. shell.exec(command, [cwd],
// [timeoutMs]) runs the given command line under cmd.exe /C, captures
// stdout + stderr through anonymous pipes, and returns
// { stdout, stderr, exitCode, timedOut } as the MCP text-content body
// (JSON-encoded). Default timeout 30s; max enforced at 5 minutes so a
// runaway tools/call can't hold the supervisor forever.
// -----------------------------------------------------------------------

#if defined(_WIN32)
struct ProcessRunResult {
    std::string stdoutText;
    std::string stderrText;
    int  exitCode = 0;
    bool timedOut = false;
    bool launchFailed = false;
    std::string launchError;
};

std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int length = MultiByteToWideChar(CP_UTF8, 0,
                                            utf8.c_str(), static_cast<int>(utf8.size()),
                                            nullptr, 0);
    std::wstring out(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
                        utf8.c_str(), static_cast<int>(utf8.size()),
                        out.data(), length);
    return out;
}

std::string trimAscii(std::string value) {
    value.erase(
        value.begin(),
        std::find_if(
            value.begin(),
            value.end(),
            [](unsigned char character) { return std::isspace(character) == 0; }));
    value.erase(
        std::find_if(
            value.rbegin(),
            value.rend(),
            [](unsigned char character) { return std::isspace(character) == 0; })
            .base(),
        value.end());
    return value;
}

std::string environmentString(const char* name) {
    char* raw = nullptr;
    size_t length = 0;
    if (_dupenv_s(&raw, &length, name) != 0 || raw == nullptr) {
        return {};
    }
    std::string value(raw, length > 0 ? length - 1 : 0);
    std::free(raw);
    return trimAscii(std::move(value));
}

std::string trimTrailingUrlSlash(std::string value) {
    while (!value.empty() && (value.back() == '/' || value.back() == '\\')) {
        value.pop_back();
    }
    return value;
}

std::string adminBaseUrl() {
    auto configured = trimTrailingUrlSlash(environmentString("MCOS_ADMIN_BASE_URL"));
    if (!configured.empty()) {
        return configured;
    }
    const auto configuredPort = environmentString("MCOS_ADMIN_PORT");
    const auto port = configuredPort.empty() ? std::string("7300") : configuredPort;
    return std::string("http://127.0.0.1:") + port;
}

std::string adminUrl(const std::string& path) {
    if (path.empty()) {
        return adminBaseUrl();
    }
    if (path.front() == '/') {
        return adminBaseUrl() + path;
    }
    return adminBaseUrl() + "/" + path;
}

ProcessRunResult runProcessCaptured(const std::wstring& commandLine,
                                    const std::wstring& cwd,
                                    DWORD timeoutMs) {
    ProcessRunResult result;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE outRead = nullptr, outWrite = nullptr;
    HANDLE errRead = nullptr, errWrite = nullptr;
    if (!CreatePipe(&outRead, &outWrite, &sa, 0)
        || !SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0)) {
        result.launchFailed = true;
        result.launchError  = "CreatePipe(stdout) failed.";
        if (outRead) CloseHandle(outRead);
        if (outWrite) CloseHandle(outWrite);
        return result;
    }
    if (!CreatePipe(&errRead, &errWrite, &sa, 0)
        || !SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0)) {
        result.launchFailed = true;
        result.launchError  = "CreatePipe(stderr) failed.";
        CloseHandle(outRead); CloseHandle(outWrite);
        if (errRead) CloseHandle(errRead);
        if (errWrite) CloseHandle(errWrite);
        return result;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = outWrite;
    si.hStdError  = errWrite;
    si.hStdInput  = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi{};

    // CreateProcessW with a writable buffer (CREATE_UNICODE_ENVIRONMENT
    // would matter only if we were passing an env block; we inherit the
    // parent env so it's fine). The first param is nullptr because we
    // pass the command line via lpCommandLine.
    std::wstring cmdMutable = commandLine;
    const BOOL createOk = CreateProcessW(
        nullptr, cmdMutable.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW, nullptr,
        cwd.empty() ? nullptr : cwd.c_str(),
        &si, &pi);

    // Close write ends in this process so child's exits trigger EOF.
    CloseHandle(outWrite);
    CloseHandle(errWrite);

    if (!createOk) {
        result.launchFailed = true;
        std::ostringstream os;
        os << "CreateProcessW failed (Win32 error " << GetLastError() << ")";
        result.launchError = os.str();
        CloseHandle(outRead);
        CloseHandle(errRead);
        return result;
    }

    // Read stdout + stderr until child exits or timeout. We read in
    // alternating non-blocking peeks so neither pipe starves the other.
    auto readFromPipe = [](HANDLE h, std::string& dest) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) {
            return;
        }
        std::vector<char> buf(avail);
        DWORD got = 0;
        if (ReadFile(h, buf.data(), avail, &got, nullptr) && got > 0) {
            dest.append(buf.data(), got);
        }
    };

    const auto deadline = (timeoutMs == INFINITE)
        ? std::chrono::steady_clock::time_point::max()
        : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    DWORD waitResult = WAIT_TIMEOUT;
    while (true) {
        readFromPipe(outRead, result.stdoutText);
        readFromPipe(errRead, result.stderrText);
        waitResult = WaitForSingleObject(pi.hProcess, 50);
        if (waitResult == WAIT_OBJECT_0) break;
        if (timeoutMs != INFINITE && std::chrono::steady_clock::now() >= deadline) {
            result.timedOut = true;
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 1000);
            break;
        }
    }
    // Drain any remaining bytes after the child exited.
    readFromPipe(outRead, result.stdoutText);
    readFromPipe(errRead, result.stderrText);

    DWORD exitCode = 0;
    if (GetExitCodeProcess(pi.hProcess, &exitCode)) {
        result.exitCode = static_cast<int>(exitCode);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(outRead);
    CloseHandle(errRead);

    // Trim massive output to keep MCP responses sane (1 MB cap each).
    constexpr size_t kCapBytes = 1024 * 1024;
    if (result.stdoutText.size() > kCapBytes) {
        result.stdoutText.resize(kCapBytes);
        result.stdoutText.append("\n...[truncated to 1 MB]");
    }
    if (result.stderrText.size() > kCapBytes) {
        result.stderrText.resize(kCapBytes);
        result.stderrText.append("\n...[truncated to 1 MB]");
    }
    return result;
}

// v0.9.62: shared helper that escapes a single argument for cmd.exe so
// runProcessCaptured (which takes a single command-line string) can run
// multi-arg invocations safely. Wraps the arg in double quotes if it
// contains whitespace or quotes; doubles up embedded quotes per
// CommandLineToArgvW's quoting rules.
std::wstring quoteArg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";
    bool needsQuote = false;
    for (wchar_t c : arg) {
        if (c == L' ' || c == L'\t' || c == L'"' || c == L'\\') {
            needsQuote = true;
            break;
        }
    }
    if (!needsQuote) return arg;
    std::wstring out;
    out.reserve(arg.size() + 2);
    out.push_back(L'"');
    for (size_t i = 0; i < arg.size(); ++i) {
        if (arg[i] == L'"') {
            out.append(L"\\\"");
        } else if (arg[i] == L'\\') {
            // Count consecutive backslashes and double them only if
            // they precede a quote (or end of string before a closing quote).
            size_t backslashes = 0;
            while (i < arg.size() && arg[i] == L'\\') { ++backslashes; ++i; }
            if (i == arg.size() || arg[i] == L'"') {
                out.append(backslashes * 2, L'\\');
                if (i < arg.size()) {
                    out.append(L"\\\"");
                }
            } else {
                out.append(backslashes, L'\\');
                out.push_back(arg[i]);
            }
            // Compensate for the loop's ++i.
            if (i < arg.size()) {
                // We consumed one extra char; step back so the for-loop
                // increments match.
                --i;
            }
        } else {
            out.push_back(arg[i]);
        }
    }
    out.push_back(L'"');
    return out;
}

nlohmann::json shellExec(const std::string& command,
                         const std::string& cwd,
                         int timeoutMsRequested) {
    // Clamp timeout: default 30s, max 5 min, min 100ms.
    constexpr int kDefaultTimeoutMs = 30000;
    constexpr int kMaxTimeoutMs     = 5 * 60 * 1000;
    constexpr int kMinTimeoutMs     = 100;
    int timeoutMs = timeoutMsRequested <= 0 ? kDefaultTimeoutMs : timeoutMsRequested;
    if (timeoutMs > kMaxTimeoutMs) timeoutMs = kMaxTimeoutMs;
    if (timeoutMs < kMinTimeoutMs) timeoutMs = kMinTimeoutMs;

    // Run through cmd.exe so shell features (pipes, &&, redirection,
    // built-ins like `dir`, `set`) work. The /S /C combination treats
    // the rest of the command line as the command to execute.
    std::wstring full = L"cmd.exe /S /C \"" + utf8ToWide(command) + L"\"";
    const std::wstring cwdW = cwd.empty() ? L"" : utf8ToWide(cwd);

    const auto runResult = runProcessCaptured(full, cwdW, static_cast<DWORD>(timeoutMs));
    nlohmann::json out = {
        { "command",   command },
        { "exitCode",  runResult.exitCode },
        { "timedOut",  runResult.timedOut },
        { "stdout",    runResult.stdoutText },
        { "stderr",    runResult.stderrText },
        { "timeoutMs", timeoutMs }
    };
    if (!cwd.empty()) out["cwd"] = cwd;
    if (runResult.launchFailed) {
        out["launchFailed"] = true;
        out["launchError"]  = runResult.launchError;
    }
    return out;
}

// v0.9.62: git.run — wraps git.exe via runProcessCaptured. We resolve
// git through a "git" command line so the OS PATH lookup applies; if
// git isn't installed launchFailed bubbles back to the operator
// honestly instead of faking a zero-result.
nlohmann::json gitRun(const std::vector<std::string>& args,
                      const std::string& cwd,
                      int timeoutMsRequested) {
    constexpr int kDefaultTimeoutMs = 60000;
    constexpr int kMaxTimeoutMs     = 5 * 60 * 1000;
    constexpr int kMinTimeoutMs     = 100;
    int timeoutMs = timeoutMsRequested <= 0 ? kDefaultTimeoutMs : timeoutMsRequested;
    if (timeoutMs > kMaxTimeoutMs) timeoutMs = kMaxTimeoutMs;
    if (timeoutMs < kMinTimeoutMs) timeoutMs = kMinTimeoutMs;

    std::wstring full = L"git";
    for (const auto& a : args) {
        full.push_back(L' ');
        full.append(quoteArg(utf8ToWide(a)));
    }
    const std::wstring cwdW = cwd.empty() ? L"" : utf8ToWide(cwd);
    const auto runResult = runProcessCaptured(full, cwdW, static_cast<DWORD>(timeoutMs));

    nlohmann::json out = {
        { "args",     args },
        { "exitCode", runResult.exitCode },
        { "timedOut", runResult.timedOut },
        { "stdout",   runResult.stdoutText },
        { "stderr",   runResult.stderrText },
        { "timeoutMs", timeoutMs }
    };
    if (!cwd.empty()) out["cwd"] = cwd;
    if (runResult.launchFailed) {
        out["launchFailed"] = true;
        out["launchError"]  = runResult.launchError;
    }
    return out;
}

// v0.9.62: search.grep — use ripgrep (fast, structured JSON output via
// --json) and parse the output into a uniform
// {file,line,text} array so downstream consumers don't care which
// implementation ran. Returns an honest result on launchFailed --
// no fake matches.
nlohmann::json searchGrep(const std::string& pattern,
                          const std::string& path,
                          const std::string& glob,
                          int maxMatchesRequested,
                          int timeoutMsRequested) {
    constexpr int kDefaultMax  = 200;
    constexpr int kMaxAllowed  = 5000;
    int maxMatches = maxMatchesRequested <= 0 ? kDefaultMax : maxMatchesRequested;
    if (maxMatches > kMaxAllowed) maxMatches = kMaxAllowed;
    if (maxMatches < 1) maxMatches = 1;

    constexpr int kDefaultTimeoutMs = 30000;
    int timeoutMs = timeoutMsRequested <= 0 ? kDefaultTimeoutMs : timeoutMsRequested;

    const std::string searchRoot = path.empty() ? "." : path;

    // Build the rg command line first; rg --json emits one JSON object
    // per line, with type:"match" containing the file + match data.
    std::wstring rgCmd = L"rg --json --max-count " + std::to_wstring(maxMatches);
    if (!glob.empty()) {
        rgCmd.append(L" --glob ");
        rgCmd.append(quoteArg(utf8ToWide(glob)));
    }
    rgCmd.append(L" -- ");
    rgCmd.append(quoteArg(utf8ToWide(pattern)));
    rgCmd.push_back(L' ');
    rgCmd.append(quoteArg(utf8ToWide(searchRoot)));

    auto rgResult = runProcessCaptured(rgCmd, std::wstring(), static_cast<DWORD>(timeoutMs));
    nlohmann::json matches = nlohmann::json::array();
    std::string toolUsed;
    bool ranFallback = false;

    if (!rgResult.launchFailed && (rgResult.exitCode == 0 || rgResult.exitCode == 1)) {
        // exitCode 0 = matches found, 1 = no matches (both legitimate).
        toolUsed = "ripgrep";
        std::istringstream iss(rgResult.stdoutText);
        std::string line;
        while (std::getline(iss, line) && static_cast<int>(matches.size()) < maxMatches) {
            if (line.empty()) continue;
            try {
                auto j = nlohmann::json::parse(line);
                if (j.value("type", std::string{}) != "match") continue;
                if (!j.contains("data")) continue;
                const auto& data = j["data"];
                std::string file;
                if (data.contains("path") && data["path"].is_object()) {
                    file = data["path"].value("text", std::string{});
                }
                int lineNumber = data.value("line_number", 0);
                std::string text;
                if (data.contains("lines") && data["lines"].is_object()) {
                    text = data["lines"].value("text", std::string{});
                    if (!text.empty() && text.back() == '\n') text.pop_back();
                    if (!text.empty() && text.back() == '\r') text.pop_back();
                }
                matches.push_back({
                    { "file", file },
                    { "line", lineNumber },
                    { "text", text }
                });
            } catch (...) { /* skip malformed line */ }
        }
    } else {
        toolUsed = "ripgrep";
        return nlohmann::json{
            { "tool",       toolUsed },
            { "ranFallback", ranFallback },
            { "pattern",    pattern },
            { "path",       searchRoot },
            { "glob",       glob },
            { "maxMatches", maxMatches },
            { "matchCount", 0 },
            { "matches",    matches },
            { "error",      "ripgrep (rg.exe) is required for search.grep and was not available or returned an execution error." },
            { "rgExitCode", rgResult.exitCode },
            { "rgTimedOut", rgResult.timedOut },
            { "rgError",    rgResult.launchError },
            { "rgStderr",   rgResult.stderrText }
        };
    }

    return nlohmann::json{
        { "tool",       toolUsed },
        { "ranFallback", ranFallback },
        { "pattern",    pattern },
        { "path",       searchRoot },
        { "glob",       glob },
        { "maxMatches", maxMatches },
        { "matchCount", static_cast<int>(matches.size()) },
        { "matches",    matches }
    };
}
#endif // _WIN32

// -----------------------------------------------------------------------
// Tool catalog (static, per-specialization).
// -----------------------------------------------------------------------

nlohmann::json baselineToolsCatalog() {
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

nlohmann::json terminalShellCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "shell.exec" },
            { "description", "Execute a shell command line on the worker host via cmd.exe /S /C. Captures stdout, stderr, and exitCode. Default timeout 30 seconds (max 5 minutes). Output is capped at 1 MB per stream. Returns a JSON object with stdout/stderr/exitCode/timedOut fields." },
            { "inputSchema", {
                { "type", "object" },
                { "properties", {
                    { "command",   { { "type", "string"  }, { "description", "Command line to execute. Forwarded to cmd.exe /S /C; supports pipes, redirection, and built-ins." } } },
                    { "cwd",       { { "type", "string"  }, { "description", "Optional absolute working directory. Defaults to the worker process's cwd." } } },
                    { "timeoutMs", { { "type", "integer" }, { "description", "Optional timeout in milliseconds. Default 30000, min 100, max 300000." } } }
                } },
                { "required", nlohmann::json::array({ "command" }) }
            } }
        }
    });
}

nlohmann::json localGitCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "git.run" },
            { "description", "Run a git command on the worker host. The args array is passed verbatim to git.exe (resolved via the PATH); cwd controls the repository root. Captures stdout, stderr, exitCode. Default timeout 60 seconds (max 5 minutes). Output capped at 1 MB per stream." },
            { "inputSchema", {
                { "type", "object" },
                { "properties", {
                    { "args",      { { "type", "array"   }, { "items", { { "type", "string" } } }, { "description", "Git argument list, e.g. [\"status\",\"--porcelain\"] or [\"log\",\"-n\",\"10\",\"--oneline\"]." } } },
                    { "cwd",       { { "type", "string"  }, { "description", "Repository root (or any path inside the working tree). Defaults to the worker's cwd if omitted." } } },
                    { "timeoutMs", { { "type", "integer" }, { "description", "Optional timeout in milliseconds. Default 60000, min 100, max 300000." } } }
                } },
                { "required", nlohmann::json::array({ "args" }) }
            } }
        }
    });
}

nlohmann::json clientTrackerCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "clients.list" },
            { "description", "List the LAN AI clients currently registered with MCOS. Bridges to GET /api/clients through MCOS_ADMIN_BASE_URL. Returns the full client roster with ip / clientType / sessionId / lastHeartbeat fields. Read-only." },
            { "inputSchema", {
                { "type", "object" },
                { "properties", nlohmann::json::object() }
            } }
        }
    });
}

nlohmann::json metricsCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "metrics.host" },
            { "description", "Return the MCOS host telemetry snapshot (CPU, memory, disk, network, hostname, primary IP, OS). Bridges to GET /api/host/telemetry on the MCOS admin port. Honors the project's no-fake-telemetry contract: capturedAtUtc='' means the sampler hasn't taken a reading yet." },
            { "inputSchema", {
                { "type", "object" },
                { "properties", nlohmann::json::object() }
            } }
        },
        nlohmann::json{
            { "name", "metrics.gateway" },
            { "description", "Return the gateway throughput + tool-aggregate snapshot. Bridges to GET /api/telemetry/gateway on the MCOS admin port. Includes total tool count, per-pool counts, and recent tools/list / tools/call activity if the aggregator has captured it." },
            { "inputSchema", {
                { "type", "object" },
                { "properties", nlohmann::json::object() }
            } }
        }
    });
}

nlohmann::json fileSearchCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "search.grep" },
            { "description", "Substring search for `pattern` across files under `path`. Uses ripgrep (rg.exe) on PATH and returns matches as a JSON array of {file, line, text}. maxMatches caps the result count (default 200, max 5000)." },
            { "inputSchema", {
                { "type", "object" },
                { "properties", {
                    { "pattern",    { { "type", "string"  }, { "description", "Substring to search for. Treated literally in the fallback; ripgrep treats it as a regex by default." } } },
                    { "path",       { { "type", "string"  }, { "description", "Directory to search recursively. Defaults to the worker's cwd." } } },
                    { "glob",       { { "type", "string"  }, { "description", "Optional file glob to restrict the search (e.g. '*.cpp')." } } },
                    { "maxMatches", { { "type", "integer" }, { "description", "Maximum number of match lines to return. Default 200, min 1, max 5000." } } },
                    { "timeoutMs",  { { "type", "integer" }, { "description", "Optional timeout in milliseconds. Default 30000." } } }
                } },
                { "required", nlohmann::json::array({ "pattern" }) }
            } }
        }
    });
}

nlohmann::json codeExecutionReplCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "repl.exec" },
            { "description", "Execute a code snippet in the named language. Supported languages: 'python' (python.exe / py -3), 'node' (node.exe), 'powershell' (powershell.exe). Captures stdout, stderr, exitCode. Default timeout 30s, max 5 min. Output capped at 1 MB per stream. NOT sandboxed -- the code runs under the worker's permissions, so treat untrusted inputs accordingly." },
            { "inputSchema", {
                { "type", "object" },
                { "properties", {
                    { "language", { { "type", "string" }, { "description", "One of: python, node, powershell." } } },
                    { "code",     { { "type", "string" }, { "description", "Source snippet to execute. Forwarded to the interpreter via stdin pipe (python -c / node -e / powershell -Command)." } } },
                    { "timeoutMs", { { "type", "integer" }, { "description", "Optional timeout in milliseconds. Default 30000, min 100, max 300000." } } }
                } },
                { "required", nlohmann::json::array({ "language", "code" }) }
            } }
        }
    });
}

nlohmann::json localTestRunnerCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "test.run" },
            { "description", "Run a test command (ctest, pytest, jest, npm test, dotnet test, cargo test) and stream the result. The framework arg picks the binary (or 'shell' for an arbitrary command line); args[] is forwarded verbatim." },
            { "inputSchema", {
                { "type", "object" },
                { "properties", {
                    { "framework", { { "type", "string" }, { "description", "One of: ctest, pytest, jest, npm, dotnet, cargo, shell. 'shell' runs args[0] as the command verbatim." } } },
                    { "args",      { { "type", "array"  }, { "items", { { "type", "string" } } } } },
                    { "cwd",       { { "type", "string" }, { "description", "Working directory; defaults to the worker cwd." } } },
                    { "timeoutMs", { { "type", "integer" }, { "description", "Default 300000 (5 min)." } } }
                } },
                { "required", nlohmann::json::array({ "framework" }) }
            } }
        }
    });
}

nlohmann::json localBuildToolCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "build.run" },
            { "description", "Drive a build tool (cmake, msbuild, cargo, npm, dotnet, gradle, make) and stream stdout/stderr. The tool arg picks the binary; args[] is forwarded verbatim. Output capped at 1 MB per stream." },
            { "inputSchema", {
                { "type", "object" },
                { "properties", {
                    { "tool",      { { "type", "string" }, { "description", "One of: cmake, msbuild, cargo, npm, dotnet, gradle, make." } } },
                    { "args",      { { "type", "array"  }, { "items", { { "type", "string" } } } } },
                    { "cwd",       { { "type", "string" } } },
                    { "timeoutMs", { { "type", "integer" }, { "description", "Default 600000 (10 min)." } } }
                } },
                { "required", nlohmann::json::array({ "tool" }) }
            } }
        }
    });
}

nlohmann::json localLinterCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "lint.run" },
            { "description", "Run a linter (eslint, pylint, ruff, clang-tidy, dotnet-format, rustfmt) and stream output. The tool arg picks the binary; args[] is forwarded verbatim." },
            { "inputSchema", {
                { "type", "object" },
                { "properties", {
                    { "tool",      { { "type", "string" }, { "description", "One of: eslint, pylint, ruff, clang-tidy, dotnet-format, rustfmt." } } },
                    { "args",      { { "type", "array"  }, { "items", { { "type", "string" } } } } },
                    { "cwd",       { { "type", "string" } } },
                    { "timeoutMs", { { "type", "integer" }, { "description", "Default 120000 (2 min)." } } }
                } },
                { "required", nlohmann::json::array({ "tool" }) }
            } }
        }
    });
}

nlohmann::json persistentContextCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "ctx.set" },
            { "description", "Set or replace the value at key. value can be any JSON. Persists to %ProgramData%\\MCOS\\persistent-context.json on the worker host. Returns {key, previousValue (or null)}." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "key",   { { "type", "string" } } },
                { "value", { { "description", "Any JSON value." } } }
            } }, { "required", nlohmann::json::array({ "key", "value" }) } } }
        },
        nlohmann::json{
            { "name", "ctx.get" },
            { "description", "Read the value at key. Returns {key, value (or null), exists}." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "key", { { "type", "string" } } }
            } }, { "required", nlohmann::json::array({ "key" }) } } }
        },
        nlohmann::json{
            { "name", "ctx.list" },
            { "description", "List all keys. Returns {keys: [...], count}." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        },
        nlohmann::json{
            { "name", "ctx.delete" },
            { "description", "Delete the value at key. Returns {key, deleted (bool), previousValue}." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "key", { { "type", "string" } } }
            } }, { "required", nlohmann::json::array({ "key" }) } } }
        }
    });
}

nlohmann::json knowledgeGraphCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "kg.entity.upsert" },
            { "description", "Create or update an entity. Existing entities of the same name have their entityType updated and observations merged (deduplicated by exact-string match). Returns the resulting entity." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "name",       { { "type", "string" } } },
                { "entityType", { { "type", "string" } } },
                { "observations", { { "type", "array" }, { "items", { { "type", "string" } } } } }
            } }, { "required", nlohmann::json::array({ "name", "entityType" }) } } }
        },
        nlohmann::json{
            { "name", "kg.relation.create" },
            { "description", "Create a directed relation from -> to with the given relationType. Idempotent on identical (from, to, relationType) triples." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "from",         { { "type", "string" } } },
                { "to",           { { "type", "string" } } },
                { "relationType", { { "type", "string" } } }
            } }, { "required", nlohmann::json::array({ "from", "to", "relationType" }) } } }
        },
        nlohmann::json{
            { "name", "kg.search" },
            { "description", "Substring search across entity names + entityType + observations. Returns matching entities and relations connected to them." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "query", { { "type", "string" } } }
            } }, { "required", nlohmann::json::array({ "query" }) } } }
        },
        nlohmann::json{
            { "name", "kg.read_graph" },
            { "description", "Return the full graph: {entities, relations}. Use sparingly on large graphs." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        },
        nlohmann::json{
            { "name", "kg.delete_entity" },
            { "description", "Delete an entity by name and all relations referencing it. Returns counts of removed objects." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "name", { { "type", "string" } } }
            } }, { "required", nlohmann::json::array({ "name" }) } } }
        }
    });
}

nlohmann::json keyboardMouseControlCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "input.keyboard" },
            { "description", "Type a text string at the current keyboard focus using SendInput. Honors layout-independent keystroke synthesis (KEYEVENTF_UNICODE) so non-ASCII characters work. Use \\n for Enter; \\t for Tab; \\b for Backspace." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "text",     { { "type", "string"  } } },
                { "delayMs",  { { "type", "integer" }, { "description", "Optional inter-keystroke delay in ms; default 5." } } }
            } }, { "required", nlohmann::json::array({ "text" }) } } }
        },
        nlohmann::json{
            { "name", "input.click" },
            { "description", "Click at (x, y) on the primary display. button is 'left' (default), 'right', or 'middle'. count defaults to 1 (single click); use 2 for double-click." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "x",      { { "type", "integer" } } },
                { "y",      { { "type", "integer" } } },
                { "button", { { "type", "string"  }, { "description", "left | right | middle" } } },
                { "count",  { { "type", "integer" }, { "description", "Default 1." } } }
            } }, { "required", nlohmann::json::array({ "x", "y" }) } } }
        },
        nlohmann::json{
            { "name", "input.move" },
            { "description", "Move the cursor to (x, y) on the primary display. No clicks are issued." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "x", { { "type", "integer" } } },
                { "y", { { "type", "integer" } } }
            } }, { "required", nlohmann::json::array({ "x", "y" }) } } }
        },
        nlohmann::json{
            { "name", "input.scroll" },
            { "description", "Scroll the mouse wheel. dy > 0 scrolls up; dy < 0 scrolls down. dx is horizontal; many apps ignore it." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "dy", { { "type", "integer" } } },
                { "dx", { { "type", "integer" } } }
            } } } }
        }
    });
}

nlohmann::json screenCaptureVisionCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "screen.capture" },
            { "description", "Capture the primary screen (or a sub-region) as a base64 image. Encoded as 24-bit BMP (mime image/bmp) for portability. Region is optional; default is full primary monitor. Returned in the MCP image content shape so dashboards / clients can render it directly." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "x",      { { "type", "integer" }, { "description", "Region origin X (default 0)." } } },
                { "y",      { { "type", "integer" }, { "description", "Region origin Y (default 0)." } } },
                { "width",  { { "type", "integer" }, { "description", "Region width  (default = screen width)." } } },
                { "height", { { "type", "integer" }, { "description", "Region height (default = screen height)." } } }
            } } } }
        },
        nlohmann::json{
            { "name", "screen.size" },
            { "description", "Return the primary monitor's pixel dimensions: {width, height, left, top}." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        }
    });
}

nlohmann::json desktopControlCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "desktop.list_windows" },
            { "description", "Enumerate visible top-level windows. Returns {hwnd, title, visible, foreground, bounds:{left,top,right,bottom}} for each." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "includeHidden", { { "type", "boolean" }, { "description", "Default false. When true, also includes invisible windows (shell internals, hidden tray, etc.)." } } }
            } } } }
        },
        nlohmann::json{
            { "name", "desktop.focus" },
            { "description", "Bring a window matching titleSubstring to the foreground (case-insensitive substring match). Returns {matched, hwnd, title, foreground}." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "titleSubstring", { { "type", "string" } } }
            } }, { "required", nlohmann::json::array({ "titleSubstring" }) } } }
        },
        nlohmann::json{
            { "name", "desktop.launch" },
            { "description", "Launch an application via ShellExecuteW. path can be an absolute exe path, a registered protocol/URL, or any document the shell can open. Returns {launched, instanceHandle}." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "path", { { "type", "string" } } },
                { "args", { { "type", "string" }, { "description", "Optional argument string passed to the launched process." } } }
            } }, { "required", nlohmann::json::array({ "path" }) } } }
        },
        nlohmann::json{
            { "name", "desktop.foreground" },
            { "description", "Return info about the currently-foreground window: {hwnd, title, processId, bounds}." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        }
    });
}

nlohmann::json computerUseCatalog() {
    // Anthropic-style umbrella that re-exports the most common
    // input/screen/desktop tools under stable computer.* names so
    // clients written against the Anthropic computer-use spec can
    // use MCOS as a drop-in.
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "computer.screenshot" },
            { "description", "Capture the primary screen (full screen). Same backend as screen-capture-vision__screen.capture; returned as base64 BMP under the MCP image content shape." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        },
        nlohmann::json{
            { "name", "computer.click" },
            { "description", "Click at (x, y). Same backend as keyboard-mouse-control__input.click." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "x",      { { "type", "integer" } } },
                { "y",      { { "type", "integer" } } },
                { "button", { { "type", "string"  } } }
            } }, { "required", nlohmann::json::array({ "x", "y" }) } } }
        },
        nlohmann::json{
            { "name", "computer.type" },
            { "description", "Type text at the current focus. Same backend as keyboard-mouse-control__input.keyboard." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "text", { { "type", "string" } } }
            } }, { "required", nlohmann::json::array({ "text" }) } } }
        },
        nlohmann::json{
            { "name", "computer.move_mouse" },
            { "description", "Move the cursor to (x, y). Same backend as keyboard-mouse-control__input.move." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "x", { { "type", "integer" } } },
                { "y", { { "type", "integer" } } }
            } }, { "required", nlohmann::json::array({ "x", "y" }) } } }
        },
        nlohmann::json{
            { "name", "computer.window_list" },
            { "description", "Enumerate visible top-level windows. Same backend as desktop-control__desktop.list_windows." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        }
    });
}

// v0.9.67: sub-agent catalogs. Each sub-agent is a default MCOS
// orchestration role exposed as a supervised stdio MCP. Tools are
// thin: they bridge to MCOS admin endpoints, read from the project
// tree, or emit structured advice templates. The cognitive work
// happens at the LAN AI client (the operator's chat surface); MCOS
// provides the wire and the role-named addressable tools.

nlohmann::json sentinelCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "sentinel.list_rules" },
            { "description", "Return the MCOS realignment hard-rule set as structured text. Source of truth: GET /api/forsetti/surface (project rule descriptors) plus the well-known forbidden-contract list. Use this before reviewing any change for compliance." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        },
        nlohmann::json{
            { "name", "sentinel.recent_activity" },
            { "description", "Return the latest activity-ring events (gateway/worker/governance/audit). Useful for spotting recent hard-rule violations or supervisor-reaped workers. Defaults to last 50 events; max 500." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "max", { { "type", "integer" }, { "description", "1..500; default 50." } } }
            } } } }
        },
        nlohmann::json{
            { "name", "sentinel.health_summary" },
            { "description", "Return the single-URL operational health view (gateway state + tool count, pool roster, activity-ring persistence health, host telemetry). The same payload /api/health/summary serves; sentinel surfaces it under its own role for narrative purposes." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        }
    });
}

nlohmann::json architectCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "architect.list_phases" },
            { "description", "Return the realignment manifest phases (id, label, objective, deliverables) so a design review starts from the same canonical phase plan the rest of the project enforces." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        },
        nlohmann::json{
            { "name", "architect.discovery_doc" },
            { "description", "Return /.well-known/mcos.json -- the LAN-discoverable architecture document advertising MCOS instance identity, gateway URL, governance endpoints, onboarding URLs, and capabilities. Anchor for any architectural conversation." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        },
        nlohmann::json{
            { "name", "architect.draft_adr" },
            { "description", "Synthesize a templated ADR markdown block (Status / Context / Decision / Consequences). The tool returns the markdown for the LAN AI client to fill in; it is intentionally an outline, not an opinion -- the architect role's judgment is the client's responsibility." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "title",   { { "type", "string" } } },
                { "context", { { "type", "string" } } },
                { "decision",{ { "type", "string" } } }
            } }, { "required", nlohmann::json::array({ "title" }) } } }
        }
    });
}

nlohmann::json forgeCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "forge.list_pools" },
            { "description", "Return the active managed pool roster (poolId, kind, displayName, instances, scalePolicy). Source: GET /api/pools." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        },
        nlohmann::json{
            { "name", "forge.suggest_pool_template" },
            { "description", "Build a ready-to-POST pool template for a given poolId, mapping it to the worker's --specialization arg when one of the in-tree specializations matches. Returns a JSON body suitable for POST /api/pools." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "poolId",      { { "type", "string" } } },
                { "displayName", { { "type", "string" } } }
            } }, { "required", nlohmann::json::array({ "poolId" }) } } }
        },
        nlohmann::json{
            { "name", "forge.list_specializations" },
            { "description", "Return the worker specialization registry: every in-tree --specialization=<id> the multi-spec worker accepts, plus the tools each one exposes. Reflective; the source of truth is this binary." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        }
    });
}

nlohmann::json scribeCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "scribe.list_release_reports" },
            { "description", "Enumerate handoff/realignment/v*-release-report.md files with their byte sizes. Useful for picking the next version number or auditing recent release notes." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        },
        nlohmann::json{
            { "name", "scribe.draft_release_report" },
            { "description", "Return a templated release-report markdown block (Scope completed / Files changed / Validation performed / Risks / Deferred work). Intended to be filled in by the LAN AI client." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "version", { { "type", "string" } } },
                { "summary", { { "type", "string" } } }
            } }, { "required", nlohmann::json::array({ "version" }) } } }
        },
        nlohmann::json{
            { "name", "scribe.version_state" },
            { "description", "Return the current VERSION.json (currentVersion + history head) so a release author starts from canonical state." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        }
    });
}

nlohmann::json reconCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "recon.dashboard" },
            { "description", "Return the full /api/dashboard payload -- catalog endpoints, runtime stats, governance, telemetry, surface descriptors -- in one shot. Useful as the initial reconnaissance step." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        },
        nlohmann::json{
            { "name", "recon.diagnostics" },
            { "description", "Return /api/diagnostics/runtime-stats: per-entry installState, unavailableReason, lastErrorMessage, installHint plus aggregate bucket counts. The fastest answer to 'what's actually live and what's missing'." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        },
        nlohmann::json{
            { "name", "recon.gateway_tools" },
            { "description", "Return the gateway tools/list aggregate -- every MCP tool currently routable through MCOS. Ground truth for 'what does my LAN AI client see'." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        }
    });
}

nlohmann::json nexusCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "nexus.health_summary" },
            { "description", "One-shot operational health view (same payload as /api/health/summary). For a coordinator agent, this is the right opening probe." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        },
        nlohmann::json{
            { "name", "nexus.discovery" },
            { "description", "Return /api/discovery -- MCOS instance identity, gateway URL, governance/onboarding endpoints. Bind clients to this before further coordination." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        },
        nlohmann::json{
            { "name", "nexus.list_clients" },
            { "description", "Return the connected-client roster -- which LAN AI clients are leasing which pools right now. Used by a coordinator to pick a free instance or rebalance." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        }
    });
}

nlohmann::json localDatabaseCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "db.status" },
            { "description", "Report the local-database provider state: provider (sqlite), whether a database file is configured, and whether it opens read-only. Alpha provider is read-only SQLite; other providers (ODBC/Postgres/MySQL) are deferred." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        },
        nlohmann::json{
            { "name", "db.set_connection_string" },
            { "description", "Configure the local-database source. For the SQLite provider, pass the absolute path to a SQLite database file. Stored under persistent-context on the worker host's ProgramData (machine-local)." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "connectionString", { { "type", "string" } } }
            } }, { "required", nlohmann::json::array({ "connectionString" }) } } }
        },
        nlohmann::json{
            { "name", "db.list_tables" },
            { "description", "List user tables in the configured SQLite database (read-only)." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        },
        nlohmann::json{
            { "name", "db.describe_table" },
            { "description", "Return column metadata (name, type, nullability, default, primary-key) for a table in the configured SQLite database (read-only)." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "table", { { "type", "string" } } }
            } }, { "required", nlohmann::json::array({ "table" }) } } }
        },
        nlohmann::json{
            { "name", "db.query_readonly" },
            { "description", "Execute a single read-only SQL SELECT against the configured SQLite database. Mutating statements, multiple statements, and ATTACH/DETACH are rejected and the connection is opened read-only. Results are row-limited and time-bounded." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "sql", { { "type", "string" } } },
                { "rowLimit", { { "type", "integer" } } }
            } }, { "required", nlohmann::json::array({ "sql" }) } } }
        }
    });
}

nlohmann::json watchtowerCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "watchtower.health_summary" },
            { "description", "Single-URL operational health: gateway state + tool count, pool roster, activity-ring persistence, host telemetry. Use as the heartbeat probe in continuous monitoring loops." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        },
        nlohmann::json{
            { "name", "watchtower.gateway_status" },
            { "description", "Return /api/gateway/status -- adapter type, MCP URL, run state, started timestamp. Anchor for change-detection on the gateway substrate." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        },
        nlohmann::json{
            { "name", "watchtower.activity_tail" },
            { "description", "Return the last N activity-ring events. Default 50, max 500. The tail of the operator audit log." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "max", { { "type", "integer" } } }
            } } } }
        }
    });
}

nlohmann::json fileWatcherCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "watch.add" },
            { "description", "Start watching a directory. Snapshot is taken immediately; subsequent watch.poll calls report files added/modified/removed since the previous poll. recursive defaults to true. The watch persists for the lifetime of this worker process; relaunching the pool clears all watches." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "path",      { { "type", "string"  } } },
                { "recursive", { { "type", "boolean" }, { "description", "Default true." } } }
            } }, { "required", nlohmann::json::array({ "path" }) } } }
        },
        nlohmann::json{
            { "name", "watch.poll" },
            { "description", "Compute changes since the last snapshot for the named watched path (or all watched paths when path is omitted). Updates the snapshot in place. Returns {watchPath, added[], modified[], removed[], totalEvents}." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "path", { { "type", "string" }, { "description", "Optional: limit to one watched path." } } }
            } } } }
        },
        nlohmann::json{
            { "name", "watch.list" },
            { "description", "List all currently active watches with their snapshot fileCount + creation time." },
            { "inputSchema", { { "type", "object" }, { "properties", nlohmann::json::object() } } }
        },
        nlohmann::json{
            { "name", "watch.remove" },
            { "description", "Stop watching the named path." },
            { "inputSchema", { { "type", "object" }, { "properties", {
                { "path", { { "type", "string" } } }
            } }, { "required", nlohmann::json::array({ "path" }) } } }
        }
    });
}

nlohmann::json localIndexerCatalog() {
    return nlohmann::json::array({
        nlohmann::json{
            { "name", "index.list_files" },
            { "description", "Recursively enumerate files under path (Win32 FindFirstFileW). Honors a maxFiles cap (default 5000, max 100000) and an optional glob filter. Returns {file, sizeBytes} entries -- no content read. Skips reparse points (junctions) to avoid loops." },
            { "inputSchema", {
                { "type", "object" },
                { "properties", {
                    { "path",     { { "type", "string"  }, { "description", "Root directory to enumerate." } } },
                    { "glob",     { { "type", "string"  }, { "description", "Optional file glob, e.g. '*.cpp'. Matches the file name (not full path)." } } },
                    { "maxFiles", { { "type", "integer" }, { "description", "Max entries returned. Default 5000, max 100000." } } }
                } },
                { "required", nlohmann::json::array({ "path" }) }
            } }
        }
    });
}

nlohmann::json toolCatalog() {
    switch (gSpecialization) {
        case Specialization::TerminalShell:      return terminalShellCatalog();
        case Specialization::LocalGit:           return localGitCatalog();
        case Specialization::FileSearch:         return fileSearchCatalog();
        case Specialization::ClientTracker:      return clientTrackerCatalog();
        case Specialization::Metrics:            return metricsCatalog();
        case Specialization::CodeExecutionRepl:  return codeExecutionReplCatalog();
        case Specialization::LocalTestRunner:    return localTestRunnerCatalog();
        case Specialization::LocalBuildTool:     return localBuildToolCatalog();
        case Specialization::LocalLinter:        return localLinterCatalog();
        case Specialization::LocalIndexer:       return localIndexerCatalog();
        case Specialization::PersistentContext:  return persistentContextCatalog();
        case Specialization::KnowledgeGraph:     return knowledgeGraphCatalog();
        case Specialization::FileWatcher:        return fileWatcherCatalog();
        case Specialization::KeyboardMouseControl: return keyboardMouseControlCatalog();
        case Specialization::ScreenCaptureVision:  return screenCaptureVisionCatalog();
        case Specialization::DesktopControl:       return desktopControlCatalog();
        case Specialization::ComputerUse:          return computerUseCatalog();
        case Specialization::AgentSentinel:    return sentinelCatalog();
        case Specialization::AgentArchitect:   return architectCatalog();
        case Specialization::AgentForge:       return forgeCatalog();
        case Specialization::AgentScribe:      return scribeCatalog();
        case Specialization::AgentRecon:       return reconCatalog();
        case Specialization::AgentNexus:       return nexusCatalog();
        case Specialization::AgentWatchtower:  return watchtowerCatalog();
        case Specialization::LocalDatabase:    return localDatabaseCatalog();
        case Specialization::BaselineTools:      // fallthrough
        default:                                  return baselineToolsCatalog();
    }
}

#if defined(_WIN32)
// v0.9.64: generic command-runner shared by code-execution-repl,
// local-test-runner, local-build-tool, local-linter. Builds a quoted
// command line out of (executable, args[]) and defers to
// runProcessCaptured. Returns a JSON envelope identical in shape to
// shellExec / gitRun so the caller's parsing path is uniform.
nlohmann::json runCommandLineTool(const std::wstring& exe,
                                  const std::vector<std::string>& args,
                                  const std::string& cwd,
                                  int timeoutMs,
                                  const std::string& toolLabel) {
    if (timeoutMs < 100) timeoutMs = 100;
    if (timeoutMs > 10 * 60 * 1000) timeoutMs = 10 * 60 * 1000;

    std::wstring full = exe;
    for (const auto& a : args) {
        full.push_back(L' ');
        full.append(quoteArg(utf8ToWide(a)));
    }
    const std::wstring cwdW = cwd.empty() ? L"" : utf8ToWide(cwd);
    const auto runResult = runProcessCaptured(full, cwdW, static_cast<DWORD>(timeoutMs));

    nlohmann::json out = {
        { "tool",     toolLabel },
        { "args",     args },
        { "exitCode", runResult.exitCode },
        { "timedOut", runResult.timedOut },
        { "stdout",   runResult.stdoutText },
        { "stderr",   runResult.stderrText },
        { "timeoutMs", timeoutMs }
    };
    if (!cwd.empty()) out["cwd"] = cwd;
    if (runResult.launchFailed) {
        out["launchFailed"] = true;
        out["launchError"]  = runResult.launchError;
    }
    return out;
}

// v0.9.64: code-execution-repl. The interpreter is selected by language;
// each invocation runs the snippet through the interpreter's `-c` /
// `-e` / `-Command` arg so we don't need to write a temp file.
nlohmann::json replExec(const std::string& language,
                        const std::string& code,
                        int timeoutMsRequested) {
    int timeoutMs = timeoutMsRequested <= 0 ? 30000 : timeoutMsRequested;
    if (timeoutMs > 5 * 60 * 1000) timeoutMs = 5 * 60 * 1000;
    if (timeoutMs < 100) timeoutMs = 100;

    std::wstring exe;
    std::vector<std::string> args;
    std::string label = language;
    if (language == "python" || language == "py") {
        // Prefer 'py -3' (the Windows launcher) -- it works whether the
        // operator installed Python via Microsoft Store or via the
        // python.org installer. Fall back to 'python' is left to
        // CreateProcessW's PATH lookup.
        exe = L"py";
        args = { "-3", "-c", code };
    } else if (language == "node" || language == "javascript" || language == "js") {
        exe = L"node";
        args = { "-e", code };
    } else if (language == "powershell" || language == "ps1") {
        exe = L"powershell";
        args = { "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", code };
    } else {
        return nlohmann::json{
            { "language", language },
            { "error",    "Unsupported language. Supported: python, node, powershell." }
        };
    }
    auto out = runCommandLineTool(exe, args, std::string(), timeoutMs, label);
    out["language"] = language;
    out["code"]     = code;
    return out;
}

// v0.9.64: local-test-runner / local-build-tool / local-linter.
// Each maps a friendly framework/tool name to an executable and
// passes args[] through. 'shell' is a special case for test.run that
// treats args[0] as a command line.
struct ToolBinding { std::wstring exe; bool joinShell = false; };
ToolBinding pickTestBinding(const std::string& framework) {
    if (framework == "ctest")    return { L"ctest", false };
    if (framework == "pytest")   return { L"pytest", false };
    if (framework == "jest")     return { L"jest", false };
    if (framework == "npm")      return { L"npm.cmd", false };
    if (framework == "dotnet")   return { L"dotnet", false };
    if (framework == "cargo")    return { L"cargo", false };
    if (framework == "shell")    return { L"", true };
    return { L"", false };
}
ToolBinding pickBuildBinding(const std::string& tool) {
    if (tool == "cmake")   return { L"cmake", false };
    if (tool == "msbuild") return { L"msbuild", false };
    if (tool == "cargo")   return { L"cargo", false };
    if (tool == "npm")     return { L"npm.cmd", false };
    if (tool == "dotnet")  return { L"dotnet", false };
    if (tool == "gradle")  return { L"gradle.bat", false };
    if (tool == "make")    return { L"make", false };
    return { L"", false };
}
ToolBinding pickLinterBinding(const std::string& tool) {
    if (tool == "eslint")        return { L"eslint.cmd", false };
    if (tool == "pylint")        return { L"pylint", false };
    if (tool == "ruff")          return { L"ruff", false };
    if (tool == "clang-tidy")    return { L"clang-tidy", false };
    if (tool == "dotnet-format") return { L"dotnet", false }; // pass 'format' as args[0]
    if (tool == "rustfmt")       return { L"rustfmt", false };
    return { L"", false };
}

// v0.9.64: local-indexer. FindFirstFileW recursive walk that returns
// {file, sizeBytes} entries. Skips reparse points (junctions) to avoid
// directory loops. Honors a maxFiles cap.
nlohmann::json indexListFiles(const std::string& root,
                              const std::string& glob,
                              int maxFilesRequested) {
    int maxFiles = maxFilesRequested <= 0 ? 5000 : maxFilesRequested;
    if (maxFiles > 100000) maxFiles = 100000;
    if (maxFiles < 1)      maxFiles = 1;

    nlohmann::json files = nlohmann::json::array();
    int total = 0;
    bool truncated = false;

    std::vector<std::wstring> stack;
    stack.push_back(utf8ToWide(root));
    const std::wstring globW = glob.empty() ? L"*" : utf8ToWide(glob);

    while (!stack.empty() && !truncated) {
        std::wstring dir = std::move(stack.back());
        stack.pop_back();
        // Walk children: enumerate with "<dir>\\*" then filter by glob
        // for matches; recurse into subdirs unconditionally.
        std::wstring pattern = dir + L"\\*";
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring full = dir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                stack.push_back(full);
            } else {
                // Glob match against the file name only.
                bool matches = (globW == L"*" || PathMatchSpecW(name.c_str(), globW.c_str()));
                if (!matches) continue;
                LARGE_INTEGER sz{};
                sz.LowPart  = fd.nFileSizeLow;
                sz.HighPart = static_cast<LONG>(fd.nFileSizeHigh);
                files.push_back({
                    { "file",      utf8FromWide(full) },
                    { "sizeBytes", static_cast<int64_t>(sz.QuadPart) }
                });
                ++total;
                if (total >= maxFiles) { truncated = true; break; }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return nlohmann::json{
        { "root",       root },
        { "glob",       glob },
        { "fileCount",  total },
        { "maxFiles",   maxFiles },
        { "truncated",  truncated },
        { "files",      files }
    };
}

// v0.9.66: base64 encoder used for screen.capture image payloads.
// Standard alphabet, no line breaks. Public-domain table-based.
std::string base64Encode(const unsigned char* data, size_t len) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= len) {
        const unsigned int v = (data[i] << 16) | (data[i+1] << 8) | data[i+2];
        out.push_back(tbl[(v >> 18) & 0x3F]);
        out.push_back(tbl[(v >> 12) & 0x3F]);
        out.push_back(tbl[(v >>  6) & 0x3F]);
        out.push_back(tbl[ v        & 0x3F]);
        i += 3;
    }
    if (i < len) {
        const unsigned int v0 = data[i];
        const unsigned int v1 = (i + 1 < len) ? data[i + 1] : 0;
        const unsigned int v  = (v0 << 16) | (v1 << 8);
        out.push_back(tbl[(v >> 18) & 0x3F]);
        out.push_back(tbl[(v >> 12) & 0x3F]);
        if (i + 1 < len) out.push_back(tbl[(v >> 6) & 0x3F]);
        else             out.push_back('=');
        out.push_back('=');
    }
    return out;
}

// v0.9.66: keyboard-mouse-control. Each tool is a small SendInput
// wrapper. Layout-independent KEYEVENTF_UNICODE for keyboard so
// non-ASCII works without keyboard layout queries.
nlohmann::json inputKeyboard(const std::string& text, int delayMs) {
    if (delayMs < 0) delayMs = 0;
    if (delayMs > 1000) delayMs = 1000;
    // UTF-8 -> UTF-16 for the Unicode keystroke synthesis.
    std::wstring w = utf8ToWide(text);
    int sent = 0;
    for (wchar_t ch : w) {
        // Translate \n/\t/\b to virtual-key equivalents.
        if (ch == L'\n') {
            INPUT down{}; down.type = INPUT_KEYBOARD; down.ki.wVk = VK_RETURN;
            INPUT up = down; up.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &down, sizeof(INPUT)); SendInput(1, &up, sizeof(INPUT));
            ++sent;
        } else if (ch == L'\t') {
            INPUT down{}; down.type = INPUT_KEYBOARD; down.ki.wVk = VK_TAB;
            INPUT up = down; up.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &down, sizeof(INPUT)); SendInput(1, &up, sizeof(INPUT));
            ++sent;
        } else if (ch == L'\b') {
            INPUT down{}; down.type = INPUT_KEYBOARD; down.ki.wVk = VK_BACK;
            INPUT up = down; up.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &down, sizeof(INPUT)); SendInput(1, &up, sizeof(INPUT));
            ++sent;
        } else {
            INPUT down{}; down.type = INPUT_KEYBOARD;
            down.ki.wScan = static_cast<WORD>(ch);
            down.ki.dwFlags = KEYEVENTF_UNICODE;
            INPUT up = down; up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            SendInput(1, &down, sizeof(INPUT)); SendInput(1, &up, sizeof(INPUT));
            ++sent;
        }
        if (delayMs > 0) ::Sleep(delayMs);
    }
    return { { "characters", sent }, { "delayMs", delayMs } };
}

nlohmann::json inputClick(int x, int y, const std::string& button, int count) {
    if (count <= 0) count = 1;
    if (count > 10) count = 10;
    SetCursorPos(x, y);
    DWORD downFlag = MOUSEEVENTF_LEFTDOWN, upFlag = MOUSEEVENTF_LEFTUP;
    if (button == "right")  { downFlag = MOUSEEVENTF_RIGHTDOWN;  upFlag = MOUSEEVENTF_RIGHTUP;  }
    if (button == "middle") { downFlag = MOUSEEVENTF_MIDDLEDOWN; upFlag = MOUSEEVENTF_MIDDLEUP; }
    for (int i = 0; i < count; ++i) {
        INPUT d{}; d.type = INPUT_MOUSE; d.mi.dwFlags = downFlag;
        INPUT u{}; u.type = INPUT_MOUSE; u.mi.dwFlags = upFlag;
        SendInput(1, &d, sizeof(INPUT)); SendInput(1, &u, sizeof(INPUT));
    }
    return { { "x", x }, { "y", y }, { "button", button }, { "count", count } };
}

nlohmann::json inputMove(int x, int y) {
    SetCursorPos(x, y);
    return { { "x", x }, { "y", y } };
}

nlohmann::json inputScroll(int dx, int dy) {
    if (dy != 0) {
        INPUT i{}; i.type = INPUT_MOUSE; i.mi.dwFlags = MOUSEEVENTF_WHEEL;
        i.mi.mouseData = static_cast<DWORD>(dy * WHEEL_DELTA);
        SendInput(1, &i, sizeof(INPUT));
    }
    if (dx != 0) {
        INPUT i{}; i.type = INPUT_MOUSE; i.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        i.mi.mouseData = static_cast<DWORD>(dx * WHEEL_DELTA);
        SendInput(1, &i, sizeof(INPUT));
    }
    return { { "dx", dx }, { "dy", dy } };
}

// v0.9.66: screen-capture-vision. BitBlt the primary monitor (or a
// sub-region) into a 24-bit DIB, prepend a BITMAPFILEHEADER + INFO
// header so the byte stream is a valid .bmp file, base64-encode, and
// return as the MCP image content shape. No PNG -- BMP is universally
// rendered and avoids a WIC dependency for this iteration.
nlohmann::json screenCapture(int x, int y, int width, int height) {
    HDC hdcScreen = ::GetDC(nullptr);
    if (!hdcScreen) {
        return { { "error", "GetDC(nullptr) failed." } };
    }
    if (width <= 0)  width  = ::GetSystemMetrics(SM_CXSCREEN) - x;
    if (height <= 0) height = ::GetSystemMetrics(SM_CYSCREEN) - y;
    if (width <= 0 || height <= 0) {
        ::ReleaseDC(nullptr, hdcScreen);
        return { { "error", "Region has non-positive dimensions." },
                 { "x", x }, { "y", y }, { "width", width }, { "height", height } };
    }
    HDC hdcMem = ::CreateCompatibleDC(hdcScreen);
    HBITMAP hbm = ::CreateCompatibleBitmap(hdcScreen, width, height);
    HGDIOBJ oldBm = ::SelectObject(hdcMem, hbm);
    ::BitBlt(hdcMem, 0, 0, width, height, hdcScreen, x, y, SRCCOPY);
    ::SelectObject(hdcMem, oldBm);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = width;
    bmi.bmiHeader.biHeight      = height;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 24;
    bmi.bmiHeader.biCompression = BI_RGB;
    const int rowSize = ((width * 3 + 3) / 4) * 4;
    const int dataSize = rowSize * height;
    std::vector<unsigned char> pixels(static_cast<size_t>(dataSize));
    ::GetDIBits(hdcScreen, hbm, 0, height, pixels.data(), &bmi, DIB_RGB_COLORS);

    ::DeleteObject(hbm);
    ::DeleteDC(hdcMem);
    ::ReleaseDC(nullptr, hdcScreen);

    BITMAPFILEHEADER bfh{};
    bfh.bfType    = 0x4D42;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize    = bfh.bfOffBits + dataSize;
    std::vector<unsigned char> bmp;
    bmp.reserve(bfh.bfSize);
    bmp.insert(bmp.end(),
               reinterpret_cast<unsigned char*>(&bfh),
               reinterpret_cast<unsigned char*>(&bfh) + sizeof(bfh));
    bmp.insert(bmp.end(),
               reinterpret_cast<unsigned char*>(&bmi.bmiHeader),
               reinterpret_cast<unsigned char*>(&bmi.bmiHeader) + sizeof(bmi.bmiHeader));
    bmp.insert(bmp.end(), pixels.begin(), pixels.end());

    std::string b64 = base64Encode(bmp.data(), bmp.size());
    return {
        { "type",     "image" },
        { "data",     b64 },
        { "mimeType", "image/bmp" },
        { "width",    width },
        { "height",   height },
        { "originX",  x },
        { "originY",  y },
        { "bytesEncoded", static_cast<int>(bmp.size()) }
    };
}

nlohmann::json screenSize() {
    return {
        { "width",  ::GetSystemMetrics(SM_CXSCREEN) },
        { "height", ::GetSystemMetrics(SM_CYSCREEN) },
        { "left",   0 },
        { "top",    0 }
    };
}

// v0.9.66: desktop-control. EnumWindows callback collects HWND +
// title + visibility + bounds; SetForegroundWindow + ShellExecuteW
// for the action tools.
struct EnumState {
    nlohmann::json* out = nullptr;
    bool includeHidden = false;
    HWND fgHwnd = nullptr;
};

BOOL CALLBACK enumWindowsCallback(HWND hwnd, LPARAM lparam) {
    auto* st = reinterpret_cast<EnumState*>(lparam);
    const bool visible = ::IsWindowVisible(hwnd) != 0;
    if (!visible && !st->includeHidden) return TRUE;
    wchar_t buf[512];
    int len = ::GetWindowTextW(hwnd, buf, ARRAYSIZE(buf));
    if (len <= 0 && !st->includeHidden) return TRUE;
    std::string title = (len > 0)
        ? utf8FromWide(std::wstring(buf, len))
        : std::string{};
    RECT r{};
    ::GetWindowRect(hwnd, &r);
    st->out->push_back({
        { "hwnd",       reinterpret_cast<uintptr_t>(hwnd) },
        { "title",      title },
        { "visible",    visible },
        { "foreground", hwnd == st->fgHwnd },
        { "bounds",     { { "left", r.left }, { "top", r.top },
                          { "right", r.right }, { "bottom", r.bottom } } }
    });
    return TRUE;
}

nlohmann::json desktopListWindows(bool includeHidden) {
    nlohmann::json out = nlohmann::json::array();
    EnumState st;
    st.out = &out;
    st.includeHidden = includeHidden;
    st.fgHwnd = ::GetForegroundWindow();
    ::EnumWindows(enumWindowsCallback, reinterpret_cast<LPARAM>(&st));
    return { { "windows", out }, { "count", static_cast<int>(out.size()) } };
}

nlohmann::json desktopFocus(const std::string& titleSubstring) {
    std::wstring needle = utf8ToWide(titleSubstring);
    std::transform(needle.begin(), needle.end(), needle.begin(), ::towlower);
    HWND match = nullptr;
    std::string matchedTitle;
    struct FindState { const std::wstring* needle; HWND* match; std::string* title; } fs{ &needle, &match, &matchedTitle };
    ::EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* fs2 = reinterpret_cast<FindState*>(lp);
        if (!::IsWindowVisible(hwnd)) return TRUE;
        wchar_t buf[512];
        int len = ::GetWindowTextW(hwnd, buf, ARRAYSIZE(buf));
        if (len <= 0) return TRUE;
        std::wstring t(buf, len);
        std::wstring lower = t;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
        if (lower.find(*fs2->needle) != std::wstring::npos) {
            *fs2->match = hwnd;
            *fs2->title = utf8FromWide(t);
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&fs));
    if (!match) {
        return { { "matched", false }, { "titleSubstring", titleSubstring } };
    }
    ::SetForegroundWindow(match);
    return {
        { "matched",    true },
        { "hwnd",       reinterpret_cast<uintptr_t>(match) },
        { "title",      matchedTitle },
        { "foreground", ::GetForegroundWindow() == match }
    };
}

nlohmann::json desktopLaunch(const std::string& path, const std::string& args) {
    std::wstring pathW = utf8ToWide(path);
    std::wstring argsW = utf8ToWide(args);
    HINSTANCE rc = ::ShellExecuteW(nullptr, L"open",
                                   pathW.c_str(),
                                   args.empty() ? nullptr : argsW.c_str(),
                                   nullptr, SW_SHOWNORMAL);
    const auto rcInt = reinterpret_cast<INT_PTR>(rc);
    const bool launched = rcInt > 32;
    return {
        { "launched",       launched },
        { "instanceHandle", static_cast<int64_t>(rcInt) },
        { "path",           path },
        { "args",           args }
    };
}

nlohmann::json desktopForeground() {
    HWND hwnd = ::GetForegroundWindow();
    if (!hwnd) return { { "hwnd", 0 }, { "title", "" } };
    wchar_t buf[512];
    int len = ::GetWindowTextW(hwnd, buf, ARRAYSIZE(buf));
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    RECT r{};
    ::GetWindowRect(hwnd, &r);
    return {
        { "hwnd",      reinterpret_cast<uintptr_t>(hwnd) },
        { "title",     len > 0 ? utf8FromWide(std::wstring(buf, len)) : std::string{} },
        { "processId", static_cast<int64_t>(pid) },
        { "bounds",    { { "left", r.left }, { "top", r.top },
                         { "right", r.right }, { "bottom", r.bottom } } }
    };
}

// v0.9.65: shared MCOS data directory resolution. ProgramData is the
// service-context-friendly choice -- works whether the worker is
// spawned by MCOS running as Local System or as the operator user.
// Returns "<ProgramData>\\MCOS" with the directory created on demand.
std::filesystem::path mcosDataDirectory() {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD len = ::GetEnvironmentVariableW(L"ProgramData", buf, ARRAYSIZE(buf));
    std::filesystem::path base;
    if (len > 0 && len < ARRAYSIZE(buf)) {
        base = std::filesystem::path(std::wstring(buf, len));
    } else {
        base = std::filesystem::path(L"C:\\ProgramData");
    }
    base /= L"MCOS";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    return base;
}

// v0.9.65: persistent-context. Each call rereads + rewrites the JSON
// file; not optimal for high-frequency writes but trivially correct
// for the operator-context-as-K/V use case. A process-wide mutex
// guards the read-modify-write cycle so concurrent leases on the same
// pool can't trample each other.
std::mutex& persistentContextMutex() {
    static std::mutex m;
    return m;
}

std::filesystem::path persistentContextPath() {
    return mcosDataDirectory() / L"persistent-context.json";
}

nlohmann::json loadPersistentContext() {
    auto path = persistentContextPath();
    std::ifstream in(path);
    if (!in.is_open()) return nlohmann::json::object();
    try {
        nlohmann::json j;
        in >> j;
        if (!j.is_object()) return nlohmann::json::object();
        return j;
    } catch (...) {
        return nlohmann::json::object();
    }
}

void savePersistentContext(const nlohmann::json& store) {
    auto path = persistentContextPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return;
    out << store.dump(2);
}

nlohmann::json persistentCtxSet(const std::string& key,
                                const nlohmann::json& value) {
    std::lock_guard<std::mutex> lock(persistentContextMutex());
    auto store = loadPersistentContext();
    nlohmann::json previous = store.contains(key) ? store[key] : nlohmann::json(nullptr);
    store[key] = value;
    savePersistentContext(store);
    return { { "key", key }, { "previousValue", previous } };
}
nlohmann::json persistentCtxGet(const std::string& key) {
    std::lock_guard<std::mutex> lock(persistentContextMutex());
    auto store = loadPersistentContext();
    const bool exists = store.contains(key);
    return {
        { "key",    key },
        { "value",  exists ? store[key] : nlohmann::json(nullptr) },
        { "exists", exists }
    };
}
nlohmann::json persistentCtxList() {
    std::lock_guard<std::mutex> lock(persistentContextMutex());
    auto store = loadPersistentContext();
    nlohmann::json keys = nlohmann::json::array();
    for (auto it = store.begin(); it != store.end(); ++it) keys.push_back(it.key());
    return { { "keys", keys }, { "count", static_cast<int>(keys.size()) } };
}
nlohmann::json persistentCtxDelete(const std::string& key) {
    std::lock_guard<std::mutex> lock(persistentContextMutex());
    auto store = loadPersistentContext();
    nlohmann::json previous = store.contains(key) ? store[key] : nlohmann::json(nullptr);
    const bool deleted = store.erase(key) > 0;
    if (deleted) savePersistentContext(store);
    return { { "key", key }, { "deleted", deleted }, { "previousValue", previous } };
}

// local-database: read-only SQLite provider. The connection store reads the
// operator-configured database file path from persistent-context; the executor
// (MasterControl::SqliteReadonlyQueryExecutor) opens it read-only.
class PersistentContextDatabaseConnectionStore final
    : public MasterControl::IDatabaseConnectionStore {
public:
    std::optional<std::string> currentDatabasePath() const override {
        std::lock_guard<std::mutex> lock(persistentContextMutex());
        auto store = loadPersistentContext();
        const std::string key = "local-database.connection-string";
        if (store.contains(key) && store[key].is_string()) {
            auto value = store[key].get<std::string>();
            if (!value.empty()) {
                return value;
            }
        }
        return std::nullopt;
    }
};

// Render a DatabaseQueryResult as MCP tool content (structured JSON). Both
// success and query-level failure are returned as tool content so the client
// sees the honest ok flag + errorKind; protocol errors (bad args) use makeError.
nlohmann::json databaseResultPayload(const MasterControl::DatabaseQueryResult& result) {
    nlohmann::json payload = result.data.is_null() ? nlohmann::json::object() : result.data;
    payload["ok"] = result.ok;
    if (result.truncated) {
        payload["truncated"] = true;
    }
    if (!result.ok) {
        payload["errorKind"] = result.errorKind;
        payload["error"] = result.error;
    }
    return payload;
}

// v0.9.65: knowledge-graph. Same JSON-on-disk pattern as
// persistent-context but with two arrays (entities + relations). Each
// entity is { name, entityType, observations[] }; each relation is
// { from, to, relationType }. Search is case-insensitive substring
// across name / entityType / observations.
std::mutex& knowledgeGraphMutex() {
    static std::mutex m;
    return m;
}

std::filesystem::path knowledgeGraphPath() {
    return mcosDataDirectory() / L"knowledge-graph.json";
}

nlohmann::json loadKnowledgeGraph() {
    auto path = knowledgeGraphPath();
    std::ifstream in(path);
    if (!in.is_open()) {
        return nlohmann::json{
            { "entities", nlohmann::json::array() },
            { "relations", nlohmann::json::array() }
        };
    }
    try {
        nlohmann::json j;
        in >> j;
        if (!j.is_object() || !j.contains("entities") || !j.contains("relations")) {
            return nlohmann::json{
                { "entities", nlohmann::json::array() },
                { "relations", nlohmann::json::array() }
            };
        }
        return j;
    } catch (...) {
        return nlohmann::json{
            { "entities", nlohmann::json::array() },
            { "relations", nlohmann::json::array() }
        };
    }
}

void saveKnowledgeGraph(const nlohmann::json& g) {
    auto path = knowledgeGraphPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return;
    out << g.dump(2);
}

nlohmann::json kgEntityUpsert(const std::string& name,
                              const std::string& entityType,
                              const std::vector<std::string>& observations) {
    std::lock_guard<std::mutex> lock(knowledgeGraphMutex());
    auto g = loadKnowledgeGraph();
    auto& entities = g["entities"];
    nlohmann::json* existing = nullptr;
    for (auto& e : entities) {
        if (e.value("name", std::string{}) == name) {
            existing = &e;
            break;
        }
    }
    if (existing) {
        (*existing)["entityType"] = entityType;
        if (!observations.empty()) {
            std::set<std::string> seen;
            if ((*existing).contains("observations") && (*existing)["observations"].is_array()) {
                for (const auto& o : (*existing)["observations"]) {
                    if (o.is_string()) seen.insert(o.get<std::string>());
                }
            }
            nlohmann::json merged = nlohmann::json::array();
            if ((*existing).contains("observations") && (*existing)["observations"].is_array()) {
                for (const auto& o : (*existing)["observations"]) {
                    if (o.is_string()) merged.push_back(o);
                }
            }
            for (const auto& o : observations) {
                if (seen.insert(o).second) merged.push_back(o);
            }
            (*existing)["observations"] = merged;
        }
        saveKnowledgeGraph(g);
        return *existing;
    }
    nlohmann::json entity = {
        { "name",         name },
        { "entityType",   entityType },
        { "observations", observations }
    };
    entities.push_back(entity);
    saveKnowledgeGraph(g);
    return entity;
}

nlohmann::json kgRelationCreate(const std::string& from,
                                const std::string& to,
                                const std::string& relationType) {
    std::lock_guard<std::mutex> lock(knowledgeGraphMutex());
    auto g = loadKnowledgeGraph();
    auto& relations = g["relations"];
    for (const auto& r : relations) {
        if (r.value("from", std::string{}) == from
            && r.value("to", std::string{}) == to
            && r.value("relationType", std::string{}) == relationType) {
            return { { "from", from }, { "to", to },
                     { "relationType", relationType }, { "created", false } };
        }
    }
    nlohmann::json rel = {
        { "from",         from },
        { "to",           to },
        { "relationType", relationType }
    };
    relations.push_back(rel);
    saveKnowledgeGraph(g);
    rel["created"] = true;
    return rel;
}

nlohmann::json kgSearch(const std::string& query) {
    std::lock_guard<std::mutex> lock(knowledgeGraphMutex());
    auto g = loadKnowledgeGraph();
    std::string needle = query;
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    auto containsCi = [&](const std::string& s) {
        std::string lower = s;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return lower.find(needle) != std::string::npos;
    };
    nlohmann::json hits = nlohmann::json::array();
    std::set<std::string> hitNames;
    for (const auto& e : g["entities"]) {
        bool match = containsCi(e.value("name", std::string{}))
                  || containsCi(e.value("entityType", std::string{}));
        if (!match && e.contains("observations") && e["observations"].is_array()) {
            for (const auto& o : e["observations"]) {
                if (o.is_string() && containsCi(o.get<std::string>())) { match = true; break; }
            }
        }
        if (match) {
            hits.push_back(e);
            hitNames.insert(e.value("name", std::string{}));
        }
    }
    nlohmann::json relatedRelations = nlohmann::json::array();
    for (const auto& r : g["relations"]) {
        if (hitNames.count(r.value("from", std::string{}))
            || hitNames.count(r.value("to", std::string{}))) {
            relatedRelations.push_back(r);
        }
    }
    return { { "query", query },
             { "entities", hits },
             { "relations", relatedRelations },
             { "matchCount", static_cast<int>(hits.size()) } };
}

nlohmann::json kgDeleteEntity(const std::string& name) {
    std::lock_guard<std::mutex> lock(knowledgeGraphMutex());
    auto g = loadKnowledgeGraph();
    nlohmann::json keptEntities = nlohmann::json::array();
    int removedEntities = 0;
    for (const auto& e : g["entities"]) {
        if (e.value("name", std::string{}) == name) {
            ++removedEntities;
        } else {
            keptEntities.push_back(e);
        }
    }
    nlohmann::json keptRelations = nlohmann::json::array();
    int removedRelations = 0;
    for (const auto& r : g["relations"]) {
        if (r.value("from", std::string{}) == name
            || r.value("to", std::string{}) == name) {
            ++removedRelations;
        } else {
            keptRelations.push_back(r);
        }
    }
    g["entities"]  = keptEntities;
    g["relations"] = keptRelations;
    saveKnowledgeGraph(g);
    return { { "name", name },
             { "removedEntities",  removedEntities },
             { "removedRelations", removedRelations } };
}

// v0.9.65: file-watcher. Maintains a per-watch snapshot map of
// {fullPath -> {sizeBytes, lastWriteTimeFiletime}}. watch.poll
// re-walks the directory and computes added / modified / removed
// entries against the previous snapshot, then atomically replaces
// the snapshot. Lifecycle is process-scoped: relaunching the pool
// (Job Object kill/respawn) clears the watch table.
struct FileSnapshot {
    int64_t sizeBytes = 0;
    int64_t lastWriteTime = 0;
};
struct DirectoryWatch {
    std::string path;
    bool recursive = true;
    std::map<std::string, FileSnapshot> snapshot;
    std::string addedAtUtc;
};
std::mutex& fileWatcherMutex() {
    static std::mutex m;
    return m;
}
std::map<std::string, DirectoryWatch>& fileWatchTable() {
    static std::map<std::string, DirectoryWatch> table;
    return table;
}

std::map<std::string, FileSnapshot> takeSnapshot(const std::wstring& root, bool recursive) {
    std::map<std::string, FileSnapshot> out;
    std::vector<std::wstring> stack;
    stack.push_back(root);
    while (!stack.empty()) {
        std::wstring dir = std::move(stack.back());
        stack.pop_back();
        std::wstring pattern = dir + L"\\*";
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring full = dir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (recursive) stack.push_back(full);
            } else {
                LARGE_INTEGER sz{};
                sz.LowPart  = fd.nFileSizeLow;
                sz.HighPart = static_cast<LONG>(fd.nFileSizeHigh);
                LARGE_INTEGER lwt{};
                lwt.LowPart  = fd.ftLastWriteTime.dwLowDateTime;
                lwt.HighPart = static_cast<LONG>(fd.ftLastWriteTime.dwHighDateTime);
                FileSnapshot s;
                s.sizeBytes     = static_cast<int64_t>(sz.QuadPart);
                s.lastWriteTime = static_cast<int64_t>(lwt.QuadPart);
                out[utf8FromWide(full)] = s;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return out;
}

nlohmann::json fileWatchAdd(const std::string& path, bool recursive) {
    std::lock_guard<std::mutex> lock(fileWatcherMutex());
    DirectoryWatch w;
    w.path      = path;
    w.recursive = recursive;
    w.snapshot  = takeSnapshot(utf8ToWide(path), recursive);
    w.addedAtUtc = nowIso8601Utc();
    fileWatchTable()[path] = std::move(w);
    auto& cur = fileWatchTable()[path];
    return { { "path", path },
             { "recursive", recursive },
             { "snapshotFileCount", static_cast<int>(cur.snapshot.size()) },
             { "addedAtUtc", cur.addedAtUtc } };
}

nlohmann::json fileWatchPoll(const std::string& specificPath) {
    std::lock_guard<std::mutex> lock(fileWatcherMutex());
    auto& table = fileWatchTable();
    nlohmann::json results = nlohmann::json::array();
    int totalEvents = 0;
    auto pollOne = [&](DirectoryWatch& w) {
        auto fresh = takeSnapshot(utf8ToWide(w.path), w.recursive);
        nlohmann::json added = nlohmann::json::array();
        nlohmann::json modified = nlohmann::json::array();
        nlohmann::json removed = nlohmann::json::array();
        for (const auto& [k, v] : fresh) {
            auto it = w.snapshot.find(k);
            if (it == w.snapshot.end()) {
                added.push_back(k);
            } else if (it->second.lastWriteTime != v.lastWriteTime
                       || it->second.sizeBytes != v.sizeBytes) {
                modified.push_back(k);
            }
        }
        for (const auto& [k, _] : w.snapshot) {
            if (fresh.find(k) == fresh.end()) removed.push_back(k);
        }
        const int events = static_cast<int>(added.size())
                         + static_cast<int>(modified.size())
                         + static_cast<int>(removed.size());
        totalEvents += events;
        w.snapshot = std::move(fresh);
        results.push_back({
            { "watchPath", w.path },
            { "added",     added },
            { "modified",  modified },
            { "removed",   removed },
            { "events",    events }
        });
    };
    if (!specificPath.empty()) {
        auto it = table.find(specificPath);
        if (it != table.end()) pollOne(it->second);
    } else {
        for (auto& [path, w] : table) pollOne(w);
    }
    return { { "watches", results }, { "totalEvents", totalEvents } };
}

nlohmann::json fileWatchList() {
    std::lock_guard<std::mutex> lock(fileWatcherMutex());
    nlohmann::json out = nlohmann::json::array();
    for (const auto& [path, w] : fileWatchTable()) {
        out.push_back({
            { "path", path },
            { "recursive", w.recursive },
            { "snapshotFileCount", static_cast<int>(w.snapshot.size()) },
            { "addedAtUtc", w.addedAtUtc }
        });
    }
    return { { "watches", out }, { "count", static_cast<int>(out.size()) } };
}

nlohmann::json fileWatchRemove(const std::string& path) {
    std::lock_guard<std::mutex> lock(fileWatcherMutex());
    auto& t = fileWatchTable();
    const bool removed = t.erase(path) > 0;
    return { { "path", path }, { "removed", removed } };
}

// v0.9.63: HTTP-bridge helper. Shell out to the system curl.exe (which
// ships with Windows 10+ and Server 2019+) to fetch a URL. Pre-v0.9.63
// the worker had no HTTP capability; the obvious in-process path
// (WinHttpOpen + WinHttpConnect + WinHttpSendRequest + WinHttpReadData)
// is ~150 lines per call, while curl shells out in 5. Since curl is
// already a vendor-blessed system component on every supported MCOS
// host we use it. The bridge is read-only (GET); writes (POST) would
// need explicit confirmation surface and are out of scope here.
nlohmann::json httpBridgeGet(const std::string& url, int timeoutMsRequested) {
    constexpr int kDefaultTimeoutMs = 5000;
    constexpr int kMaxTimeoutMs     = 60000;
    int timeoutMs = timeoutMsRequested <= 0 ? kDefaultTimeoutMs : timeoutMsRequested;
    if (timeoutMs > kMaxTimeoutMs) timeoutMs = kMaxTimeoutMs;
    if (timeoutMs < 100) timeoutMs = 100;

    // curl args: -s silent, -m timeoutSeconds, -w writes a status-code
    // tail we can split off, then the URL.
    const int timeoutSec = (timeoutMs + 999) / 1000;
    std::wstring cmd = L"curl.exe -s -m " + std::to_wstring(timeoutSec)
                     + L" -w \"\\n__MCOS_HTTP_STATUS__:%{http_code}\"";
    const auto token = environmentString("MCOS_ADMIN_TOKEN");
    if (!token.empty()) {
        cmd += L" -H ";
        cmd += quoteArg(L"X-MCOS-Admin-Token: " + utf8ToWide(token));
    }
    cmd.push_back(L' ');
    cmd += quoteArg(utf8ToWide(url));
    auto runResult = runProcessCaptured(cmd, std::wstring(),
                                        static_cast<DWORD>(timeoutMs + 1000));
    nlohmann::json out;
    out["url"]      = url;
    out["timeoutMs"] = timeoutMs;
    if (runResult.launchFailed) {
        out["bridgeFailed"] = true;
        out["bridgeError"]  = runResult.launchError;
        return out;
    }
    if (runResult.timedOut) {
        out["bridgeFailed"] = true;
        out["bridgeError"]  = "curl invocation timed out before response.";
        return out;
    }
    // Split the trailing __MCOS_HTTP_STATUS__:<code> off the body.
    std::string body = runResult.stdoutText;
    int statusCode = 0;
    const std::string sentinel = "\n__MCOS_HTTP_STATUS__:";
    const auto pos = body.rfind(sentinel);
    if (pos != std::string::npos) {
        try {
            statusCode = std::stoi(body.substr(pos + sentinel.size()));
        } catch (...) { statusCode = 0; }
        body.resize(pos);
    }
    out["httpStatus"] = statusCode;
    out["curlExitCode"] = runResult.exitCode;
    if (!runResult.stderrText.empty()) {
        out["curlStderr"] = runResult.stderrText;
    }
    // Try to parse the response body as JSON. If it parses, surface as
    // structured data; otherwise pass through as a text body so the
    // operator can still see what came back.
    try {
        out["body"] = nlohmann::json::parse(body);
    } catch (...) {
        out["bodyText"] = body;
    }
    return out;
}
#endif // _WIN32

nlohmann::json dispatchToolCall(const std::string& toolName,
                                const nlohmann::json& arguments,
                                const nlohmann::json& id) {
#if defined(_WIN32)
    if (gSpecialization == Specialization::TerminalShell && toolName == "shell.exec") {
        if (!arguments.is_object() || !arguments.contains("command")
            || !arguments["command"].is_string()) {
            return makeError(-32602,
                "shell.exec: arguments.command (string) is required.", id);
        }
        const std::string command = arguments["command"].get<std::string>();
        const std::string cwd = arguments.value("cwd", std::string{});
        const int timeoutMs = arguments.value("timeoutMs", 0);
        nlohmann::json result = shellExec(command, cwd, timeoutMs);
        // Surface as MCP text-content with the JSON body inside the
        // text field (MCP convention: tools/call.result is a content
        // array; structured data is rendered as JSON-encoded text).
        return makeResult(id, textContent(result.dump(2)));
    }
    if (gSpecialization == Specialization::LocalGit && toolName == "git.run") {
        if (!arguments.is_object() || !arguments.contains("args")
            || !arguments["args"].is_array()) {
            return makeError(-32602,
                "git.run: arguments.args (array of strings) is required.", id);
        }
        std::vector<std::string> args;
        args.reserve(arguments["args"].size());
        for (const auto& a : arguments["args"]) {
            if (!a.is_string()) {
                return makeError(-32602,
                    "git.run: every entry in arguments.args must be a string.", id);
            }
            args.push_back(a.get<std::string>());
        }
        const std::string cwd = arguments.value("cwd", std::string{});
        const int timeoutMs = arguments.value("timeoutMs", 0);
        nlohmann::json result = gitRun(args, cwd, timeoutMs);
        return makeResult(id, textContent(result.dump(2)));
    }
    if (gSpecialization == Specialization::FileSearch && toolName == "search.grep") {
        if (!arguments.is_object() || !arguments.contains("pattern")
            || !arguments["pattern"].is_string()) {
            return makeError(-32602,
                "search.grep: arguments.pattern (string) is required.", id);
        }
        const std::string pattern    = arguments["pattern"].get<std::string>();
        const std::string searchPath = arguments.value("path",  std::string{});
        const std::string glob       = arguments.value("glob",  std::string{});
        const int maxMatches = arguments.value("maxMatches", 0);
        const int timeoutMs  = arguments.value("timeoutMs", 0);
        nlohmann::json result = searchGrep(pattern, searchPath, glob, maxMatches, timeoutMs);
        return makeResult(id, textContent(result.dump(2)));
    }
    if (gSpecialization == Specialization::ClientTracker && toolName == "clients.list") {
        nlohmann::json result = httpBridgeGet(adminUrl("/api/clients"), 5000);
        return makeResult(id, textContent(result.dump(2)));
    }
    if (gSpecialization == Specialization::Metrics && toolName == "metrics.host") {
        nlohmann::json result = httpBridgeGet(adminUrl("/api/host/telemetry"), 5000);
        return makeResult(id, textContent(result.dump(2)));
    }
    if (gSpecialization == Specialization::Metrics && toolName == "metrics.gateway") {
        nlohmann::json result = httpBridgeGet(adminUrl("/api/telemetry/gateway"), 5000);
        return makeResult(id, textContent(result.dump(2)));
    }
    if (gSpecialization == Specialization::CodeExecutionRepl && toolName == "repl.exec") {
        if (!arguments.is_object()
            || !arguments.contains("language") || !arguments["language"].is_string()
            || !arguments.contains("code") || !arguments["code"].is_string()) {
            return makeError(-32602,
                "repl.exec: arguments.language and arguments.code (strings) are required.", id);
        }
        const std::string language = arguments["language"].get<std::string>();
        const std::string code     = arguments["code"].get<std::string>();
        const int timeoutMs = arguments.value("timeoutMs", 0);
        return makeResult(id, textContent(replExec(language, code, timeoutMs).dump(2)));
    }
    if (gSpecialization == Specialization::LocalTestRunner && toolName == "test.run") {
        if (!arguments.is_object() || !arguments.contains("framework")
            || !arguments["framework"].is_string()) {
            return makeError(-32602,
                "test.run: arguments.framework (string) is required.", id);
        }
        const std::string framework = arguments["framework"].get<std::string>();
        std::vector<std::string> args;
        if (arguments.contains("args") && arguments["args"].is_array()) {
            for (const auto& a : arguments["args"]) {
                if (a.is_string()) args.push_back(a.get<std::string>());
            }
        }
        const std::string cwd = arguments.value("cwd", std::string{});
        const int timeoutMs = arguments.value("timeoutMs", 300000);
        ToolBinding b = pickTestBinding(framework);
        if (b.exe.empty() && !b.joinShell) {
            return makeError(-32602,
                "test.run: unsupported framework '" + framework
                + "'. Supported: ctest, pytest, jest, npm, dotnet, cargo, shell.", id);
        }
        if (b.joinShell) {
            // 'shell' framework: args[0] is the full command line.
            if (args.empty()) {
                return makeError(-32602,
                    "test.run framework='shell' requires args[0] to be the command line.", id);
            }
            return makeResult(id, textContent(
                shellExec(args[0], cwd, timeoutMs).dump(2)));
        }
        return makeResult(id, textContent(
            runCommandLineTool(b.exe, args, cwd, timeoutMs, framework).dump(2)));
    }
    if (gSpecialization == Specialization::LocalBuildTool && toolName == "build.run") {
        if (!arguments.is_object() || !arguments.contains("tool")
            || !arguments["tool"].is_string()) {
            return makeError(-32602,
                "build.run: arguments.tool (string) is required.", id);
        }
        const std::string tool = arguments["tool"].get<std::string>();
        std::vector<std::string> args;
        if (arguments.contains("args") && arguments["args"].is_array()) {
            for (const auto& a : arguments["args"]) {
                if (a.is_string()) args.push_back(a.get<std::string>());
            }
        }
        const std::string cwd = arguments.value("cwd", std::string{});
        const int timeoutMs = arguments.value("timeoutMs", 600000);
        ToolBinding b = pickBuildBinding(tool);
        if (b.exe.empty()) {
            return makeError(-32602,
                "build.run: unsupported tool '" + tool
                + "'. Supported: cmake, msbuild, cargo, npm, dotnet, gradle, make.", id);
        }
        return makeResult(id, textContent(
            runCommandLineTool(b.exe, args, cwd, timeoutMs, tool).dump(2)));
    }
    if (gSpecialization == Specialization::LocalLinter && toolName == "lint.run") {
        if (!arguments.is_object() || !arguments.contains("tool")
            || !arguments["tool"].is_string()) {
            return makeError(-32602,
                "lint.run: arguments.tool (string) is required.", id);
        }
        const std::string tool = arguments["tool"].get<std::string>();
        std::vector<std::string> args;
        if (arguments.contains("args") && arguments["args"].is_array()) {
            for (const auto& a : arguments["args"]) {
                if (a.is_string()) args.push_back(a.get<std::string>());
            }
        }
        const std::string cwd = arguments.value("cwd", std::string{});
        const int timeoutMs = arguments.value("timeoutMs", 120000);
        ToolBinding b = pickLinterBinding(tool);
        if (b.exe.empty()) {
            return makeError(-32602,
                "lint.run: unsupported tool '" + tool
                + "'. Supported: eslint, pylint, ruff, clang-tidy, dotnet-format, rustfmt.", id);
        }
        // Special case: dotnet-format is invoked as `dotnet format ...`,
        // so prepend 'format' to args.
        if (tool == "dotnet-format") {
            args.insert(args.begin(), "format");
        }
        return makeResult(id, textContent(
            runCommandLineTool(b.exe, args, cwd, timeoutMs, tool).dump(2)));
    }
    if (gSpecialization == Specialization::LocalIndexer && toolName == "index.list_files") {
        if (!arguments.is_object() || !arguments.contains("path")
            || !arguments["path"].is_string()) {
            return makeError(-32602,
                "index.list_files: arguments.path (string) is required.", id);
        }
        const std::string path = arguments["path"].get<std::string>();
        const std::string glob = arguments.value("glob", std::string{});
        const int maxFiles     = arguments.value("maxFiles", 0);
        return makeResult(id, textContent(
            indexListFiles(path, glob, maxFiles).dump(2)));
    }
    if (gSpecialization == Specialization::PersistentContext) {
        if (toolName == "ctx.set") {
            if (!arguments.is_object() || !arguments.contains("key")
                || !arguments["key"].is_string() || !arguments.contains("value")) {
                return makeError(-32602,
                    "ctx.set: arguments.key (string) and arguments.value are required.", id);
            }
            return makeResult(id, textContent(
                persistentCtxSet(arguments["key"].get<std::string>(), arguments["value"]).dump(2)));
        }
        if (toolName == "ctx.get") {
            if (!arguments.is_object() || !arguments.contains("key")
                || !arguments["key"].is_string()) {
                return makeError(-32602, "ctx.get: arguments.key (string) is required.", id);
            }
            return makeResult(id, textContent(
                persistentCtxGet(arguments["key"].get<std::string>()).dump(2)));
        }
        if (toolName == "ctx.list") {
            return makeResult(id, textContent(persistentCtxList().dump(2)));
        }
        if (toolName == "ctx.delete") {
            if (!arguments.is_object() || !arguments.contains("key")
                || !arguments["key"].is_string()) {
                return makeError(-32602, "ctx.delete: arguments.key (string) is required.", id);
            }
            return makeResult(id, textContent(
                persistentCtxDelete(arguments["key"].get<std::string>()).dump(2)));
        }
    }
    if (gSpecialization == Specialization::KnowledgeGraph) {
        if (toolName == "kg.entity.upsert") {
            if (!arguments.is_object() || !arguments.contains("name")
                || !arguments["name"].is_string()
                || !arguments.contains("entityType")
                || !arguments["entityType"].is_string()) {
                return makeError(-32602,
                    "kg.entity.upsert: arguments.name and arguments.entityType (strings) are required.", id);
            }
            std::vector<std::string> obs;
            if (arguments.contains("observations") && arguments["observations"].is_array()) {
                for (const auto& o : arguments["observations"]) {
                    if (o.is_string()) obs.push_back(o.get<std::string>());
                }
            }
            return makeResult(id, textContent(kgEntityUpsert(
                arguments["name"].get<std::string>(),
                arguments["entityType"].get<std::string>(),
                obs).dump(2)));
        }
        if (toolName == "kg.relation.create") {
            if (!arguments.is_object()
                || !arguments.contains("from") || !arguments["from"].is_string()
                || !arguments.contains("to")   || !arguments["to"].is_string()
                || !arguments.contains("relationType") || !arguments["relationType"].is_string()) {
                return makeError(-32602,
                    "kg.relation.create: arguments.from, arguments.to, arguments.relationType (strings) are required.", id);
            }
            return makeResult(id, textContent(kgRelationCreate(
                arguments["from"].get<std::string>(),
                arguments["to"].get<std::string>(),
                arguments["relationType"].get<std::string>()).dump(2)));
        }
        if (toolName == "kg.search") {
            if (!arguments.is_object() || !arguments.contains("query")
                || !arguments["query"].is_string()) {
                return makeError(-32602, "kg.search: arguments.query (string) is required.", id);
            }
            return makeResult(id, textContent(
                kgSearch(arguments["query"].get<std::string>()).dump(2)));
        }
        if (toolName == "kg.read_graph") {
            std::lock_guard<std::mutex> lock(knowledgeGraphMutex());
            return makeResult(id, textContent(loadKnowledgeGraph().dump(2)));
        }
        if (toolName == "kg.delete_entity") {
            if (!arguments.is_object() || !arguments.contains("name")
                || !arguments["name"].is_string()) {
                return makeError(-32602, "kg.delete_entity: arguments.name (string) is required.", id);
            }
            return makeResult(id, textContent(
                kgDeleteEntity(arguments["name"].get<std::string>()).dump(2)));
        }
    }
    if (gSpecialization == Specialization::FileWatcher) {
        if (toolName == "watch.add") {
            if (!arguments.is_object() || !arguments.contains("path")
                || !arguments["path"].is_string()) {
                return makeError(-32602, "watch.add: arguments.path (string) is required.", id);
            }
            const bool recursive = arguments.value("recursive", true);
            return makeResult(id, textContent(
                fileWatchAdd(arguments["path"].get<std::string>(), recursive).dump(2)));
        }
        if (toolName == "watch.poll") {
            const std::string path = arguments.is_object()
                ? arguments.value("path", std::string{})
                : std::string{};
            return makeResult(id, textContent(fileWatchPoll(path).dump(2)));
        }
        if (toolName == "watch.list") {
            return makeResult(id, textContent(fileWatchList().dump(2)));
        }
        if (toolName == "watch.remove") {
            if (!arguments.is_object() || !arguments.contains("path")
                || !arguments["path"].is_string()) {
                return makeError(-32602, "watch.remove: arguments.path (string) is required.", id);
            }
            return makeResult(id, textContent(
                fileWatchRemove(arguments["path"].get<std::string>()).dump(2)));
        }
    }
    if (gSpecialization == Specialization::KeyboardMouseControl
        || gSpecialization == Specialization::ComputerUse) {
        const bool isComputerUse = (gSpecialization == Specialization::ComputerUse);
        if ((toolName == "input.keyboard")
            || (isComputerUse && toolName == "computer.type")) {
            if (!arguments.is_object() || !arguments.contains("text")
                || !arguments["text"].is_string()) {
                return makeError(-32602,
                    "input.keyboard / computer.type: arguments.text (string) is required.", id);
            }
            const int delayMs = arguments.value("delayMs", 5);
            return makeResult(id, textContent(
                inputKeyboard(arguments["text"].get<std::string>(), delayMs).dump(2)));
        }
        if ((toolName == "input.click")
            || (isComputerUse && toolName == "computer.click")) {
            if (!arguments.is_object()
                || !arguments.contains("x") || !arguments["x"].is_number_integer()
                || !arguments.contains("y") || !arguments["y"].is_number_integer()) {
                return makeError(-32602,
                    "input.click / computer.click: arguments.x and arguments.y (integers) are required.", id);
            }
            const std::string button = arguments.value("button", std::string("left"));
            const int count = arguments.value("count", 1);
            return makeResult(id, textContent(
                inputClick(arguments["x"].get<int>(),
                           arguments["y"].get<int>(),
                           button, count).dump(2)));
        }
        if ((toolName == "input.move")
            || (isComputerUse && toolName == "computer.move_mouse")) {
            if (!arguments.is_object()
                || !arguments.contains("x") || !arguments["x"].is_number_integer()
                || !arguments.contains("y") || !arguments["y"].is_number_integer()) {
                return makeError(-32602,
                    "input.move / computer.move_mouse: arguments.x and arguments.y (integers) are required.", id);
            }
            return makeResult(id, textContent(
                inputMove(arguments["x"].get<int>(), arguments["y"].get<int>()).dump(2)));
        }
        if (toolName == "input.scroll") {
            const int dx = arguments.is_object() ? arguments.value("dx", 0) : 0;
            const int dy = arguments.is_object() ? arguments.value("dy", 0) : 0;
            return makeResult(id, textContent(inputScroll(dx, dy).dump(2)));
        }
    }
    if (gSpecialization == Specialization::ScreenCaptureVision
        || gSpecialization == Specialization::ComputerUse) {
        const bool isComputerUse = (gSpecialization == Specialization::ComputerUse);
        if ((toolName == "screen.capture")
            || (isComputerUse && toolName == "computer.screenshot")) {
            const int x = arguments.is_object() ? arguments.value("x", 0) : 0;
            const int y = arguments.is_object() ? arguments.value("y", 0) : 0;
            const int w = arguments.is_object() ? arguments.value("width", 0)  : 0;
            const int h = arguments.is_object() ? arguments.value("height", 0) : 0;
            nlohmann::json img = screenCapture(x, y, w, h);
            // MCP image content: pass through directly as result.content
            // when the encoding succeeded; otherwise fall back to text.
            if (img.contains("type") && img["type"] == "image") {
                nlohmann::json content = nlohmann::json::array({
                    nlohmann::json{
                        { "type",     "image" },
                        { "data",     img["data"] },
                        { "mimeType", img["mimeType"] }
                    }
                });
                // Append a metadata text part so the caller gets
                // dimensions without parsing the image.
                nlohmann::json meta = {
                    { "width", img["width"] }, { "height", img["height"] },
                    { "originX", img["originX"] }, { "originY", img["originY"] },
                    { "bytesEncoded", img["bytesEncoded"] }
                };
                content.push_back(nlohmann::json{
                    { "type", "text" }, { "text", meta.dump(2) }
                });
                return makeResult(id, nlohmann::json{ { "content", content } });
            }
            return makeResult(id, textContent(img.dump(2)));
        }
        if (toolName == "screen.size") {
            return makeResult(id, textContent(screenSize().dump(2)));
        }
    }
    // v0.9.67: sub-agent dispatchers. Each sub-agent role exposes a
    // small set of tools that mostly bridge to MCOS admin endpoints
    // or emit structured templates. Heavy lifting happens at the LAN
    // AI client; the sub-agent role is what makes the tools
    // addressable.
    if (gSpecialization == Specialization::AgentSentinel) {
        if (toolName == "sentinel.list_rules") {
            return makeResult(id, textContent(httpBridgeGet(
                adminUrl("/api/forsetti/surface"), 5000).dump(2)));
        }
        if (toolName == "sentinel.recent_activity") {
            const int max = arguments.is_object() ? arguments.value("max", 50) : 50;
            const int clamped = (max < 1) ? 1 : (max > 500 ? 500 : max);
            const std::string url = adminUrl("/api/activity?max=" + std::to_string(clamped));
            return makeResult(id, textContent(httpBridgeGet(url, 5000).dump(2)));
        }
        if (toolName == "sentinel.health_summary") {
            return makeResult(id, textContent(httpBridgeGet(
                adminUrl("/api/health/summary"), 5000).dump(2)));
        }
    }
    if (gSpecialization == Specialization::AgentArchitect) {
        if (toolName == "architect.list_phases") {
            return makeResult(id, textContent(httpBridgeGet(
                adminUrl("/api/forsetti/surface"), 5000).dump(2)));
        }
        if (toolName == "architect.discovery_doc") {
            return makeResult(id, textContent(httpBridgeGet(
                adminUrl("/.well-known/mcos.json"), 5000).dump(2)));
        }
        if (toolName == "architect.draft_adr") {
            const std::string title    = arguments.is_object() ? arguments.value("title",   std::string("(set title)")) : "(set title)";
            const std::string context  = arguments.is_object() ? arguments.value("context", std::string("(describe context)")) : "(describe context)";
            const std::string decision = arguments.is_object() ? arguments.value("decision",std::string("(state decision)")) : "(state decision)";
            std::ostringstream md;
            md << "# ADR: " << title << "\n\n"
               << "## Status\n\nProposed.\n\n"
               << "## Context\n\n" << context << "\n\n"
               << "## Decision\n\n" << decision << "\n\n"
               << "## Consequences\n\n- (positive)\n- (negative)\n- (neutral)\n";
            return makeResult(id, textContent(md.str()));
        }
    }
    if (gSpecialization == Specialization::AgentForge) {
        if (toolName == "forge.list_pools") {
            return makeResult(id, textContent(httpBridgeGet(
                adminUrl("/api/pools"), 5000).dump(2)));
        }
        if (toolName == "forge.suggest_pool_template") {
            if (!arguments.is_object() || !arguments.contains("poolId")
                || !arguments["poolId"].is_string()) {
                return makeError(-32602,
                    "forge.suggest_pool_template: arguments.poolId (string) is required.", id);
            }
            const std::string poolId = arguments["poolId"].get<std::string>();
            const std::string displayName = arguments.value("displayName",
                std::string("MCP — ") + poolId);
            // Default to mcos-baseline-tools-worker.exe + --specialization=<id>;
            // the LAN AI client can adjust before POSTing.
            nlohmann::json tmpl = {
                { "poolId",      poolId },
                { "kind",        "mcp-server" },
                { "displayName", displayName },
                { "logicalMcpUrl", "" },
                { "template", {
                    { "executable", "C:\\Program Files\\Master Control Orchestration Server\\mcos-baseline-tools-worker.exe" },
                    { "args",       nlohmann::json::array({ "--specialization=" + poolId }) },
                    { "workingDirectory", "C:\\Program Files\\Master Control Orchestration Server" },
                    { "environment", nlohmann::json::object() },
                    { "transport",   "stdio_jsonrpc" },
                    { "healthProbe", { { "transport", "stdio_handshake" }, { "intervalMs", 5000 }, { "timeoutMs", 1500 }, { "unhealthyThreshold", 3 } } }
                } },
                { "scalePolicy", {
                    { "minInstances", 1 }, { "maxInstances", 2 },
                    { "maxActiveLeasesPerInstance", 4 },
                    { "scaleOutQueueWaitMs", 1500 },
                    { "scaleInIdleSeconds", 300 }
                } }
            };
            return makeResult(id, textContent(tmpl.dump(2)));
        }
        if (toolName == "forge.list_specializations") {
            // Self-describe: every --specialization id this binary
            // accepts, plus the tools each one exposes (we ask
            // ourselves via the catalog functions).
            nlohmann::json out = nlohmann::json::array();
            const std::vector<std::pair<std::string, nlohmann::json>> registry = {
                { "baseline-tools",         baselineToolsCatalog() },
                { "terminal-shell",         terminalShellCatalog() },
                { "local-git",              localGitCatalog() },
                { "file-search",            fileSearchCatalog() },
                { "client-tracker",         clientTrackerCatalog() },
                { "metrics",                metricsCatalog() },
                { "code-execution-repl",    codeExecutionReplCatalog() },
                { "local-test-runner",      localTestRunnerCatalog() },
                { "local-build-tool",       localBuildToolCatalog() },
                { "local-linter",           localLinterCatalog() },
                { "local-indexer",          localIndexerCatalog() },
                { "persistent-context",     persistentContextCatalog() },
                { "knowledge-graph",        knowledgeGraphCatalog() },
                { "file-watcher",           fileWatcherCatalog() },
                { "keyboard-mouse-control", keyboardMouseControlCatalog() },
                { "screen-capture-vision",  screenCaptureVisionCatalog() },
                { "desktop-control",        desktopControlCatalog() },
                { "computer-use",           computerUseCatalog() },
                { "sentinel",     sentinelCatalog() },
                { "architect",    architectCatalog() },
                { "forge",        forgeCatalog() },
                { "scribe",       scribeCatalog() },
                { "recon",        reconCatalog() },
                { "nexus",        nexusCatalog() },
                { "watchtower",   watchtowerCatalog() },
                { "local-database", localDatabaseCatalog() }
            };
            for (const auto& [specId, tools] : registry) {
                nlohmann::json toolNames = nlohmann::json::array();
                for (const auto& t : tools) {
                    toolNames.push_back(t.value("name", std::string{}));
                }
                out.push_back({
                    { "specialization", specId },
                    { "tools",          toolNames }
                });
            }
            return makeResult(id, textContent(nlohmann::json{
                { "specializations", out },
                { "count", static_cast<int>(out.size()) }
            }.dump(2)));
        }
    }
    if (gSpecialization == Specialization::AgentScribe) {
        if (toolName == "scribe.list_release_reports") {
            // Derive the handoff/realignment path from the running executable's
            // location so the tool works on any install without hardcoded paths.
            // The worker binary sits at <install-root>\bin\ (or directly under
            // <install-root>\); we walk up until we find a parent that contains
            // the handoff\realignment subdirectory, trying exe-dir itself, then
            // its parent.  If neither resolves, we return a clear message rather
            // than a hard failure or a machine-specific absolute path.
            std::filesystem::path handoffDir;
            {
                wchar_t exeBuf[MAX_PATH * 2] = {};
                DWORD exeLen = ::GetModuleFileNameW(nullptr, exeBuf, ARRAYSIZE(exeBuf));
                if (exeLen > 0 && exeLen < ARRAYSIZE(exeBuf)) {
                    std::filesystem::path exePath(std::wstring(exeBuf, exeLen));
                    std::filesystem::path exeDir = exePath.parent_path();
                    for (const auto& candidate : { exeDir, exeDir.parent_path() }) {
                        auto p = candidate / L"handoff" / L"realignment";
                        std::error_code ec;
                        if (std::filesystem::is_directory(p, ec) && !ec) {
                            handoffDir = p;
                            break;
                        }
                    }
                }
            }
            if (handoffDir.empty()) {
                nlohmann::json notAvail = {
                    { "note", "Release reports not available in this install. "
                              "The handoff/realignment directory was not found "
                              "relative to the worker executable." },
                    { "files", nlohmann::json::array() }
                };
                return makeResult(id, textContent(notAvail.dump(2)));
            }
            // Reuse indexListFiles pointed at the resolved handoff dir.
            return makeResult(id, textContent(indexListFiles(
                handoffDir.string(), "v*-release-report.md", 200).dump(2)));
        }
        if (toolName == "scribe.draft_release_report") {
            const std::string version = arguments.is_object() ? arguments.value("version", std::string("(set version)")) : "(set version)";
            const std::string summary = arguments.is_object() ? arguments.value("summary", std::string("(one-paragraph summary)")) : "(one-paragraph summary)";
            std::ostringstream md;
            md << "# Release Completion Report — " << version << "\n\n"
               << summary << "\n\n"
               << "## Scope completed\n\n- (deliverable)\n\n"
               << "## Files changed\n\n| File | Change |\n|---|---|\n| | |\n\n"
               << "## Validation performed\n\n| Command | Result |\n|---|---|\n| | |\n\n"
               << "## Risks and blockers\n\n- \n\n"
               << "## Deferred work\n\n- \n";
            return makeResult(id, textContent(md.str()));
        }
        if (toolName == "scribe.version_state") {
            return makeResult(id, textContent(httpBridgeGet(
                adminUrl("/api/version"), 5000).dump(2)));
        }
    }
    if (gSpecialization == Specialization::AgentRecon) {
        if (toolName == "recon.dashboard") {
            return makeResult(id, textContent(httpBridgeGet(
                adminUrl("/api/dashboard"), 10000).dump(2)));
        }
        if (toolName == "recon.diagnostics") {
            return makeResult(id, textContent(httpBridgeGet(
                adminUrl("/api/diagnostics/runtime-stats"), 5000).dump(2)));
        }
        if (toolName == "recon.gateway_tools") {
            return makeResult(id, textContent(httpBridgeGet(
                adminUrl("/api/gateway/tools"), 5000).dump(2)));
        }
    }
    if (gSpecialization == Specialization::AgentNexus) {
        if (toolName == "nexus.health_summary") {
            return makeResult(id, textContent(httpBridgeGet(
                adminUrl("/api/health/summary"), 5000).dump(2)));
        }
        if (toolName == "nexus.discovery") {
            return makeResult(id, textContent(httpBridgeGet(
                adminUrl("/api/discovery"), 5000).dump(2)));
        }
        if (toolName == "nexus.list_clients") {
            return makeResult(id, textContent(httpBridgeGet(
                adminUrl("/api/clients"), 5000).dump(2)));
        }
    }
    if (gSpecialization == Specialization::AgentWatchtower) {
        if (toolName == "watchtower.health_summary") {
            return makeResult(id, textContent(httpBridgeGet(
                adminUrl("/api/health/summary"), 5000).dump(2)));
        }
        if (toolName == "watchtower.gateway_status") {
            return makeResult(id, textContent(httpBridgeGet(
                adminUrl("/api/gateway/status"), 5000).dump(2)));
        }
        if (toolName == "watchtower.activity_tail") {
            const int max = arguments.is_object() ? arguments.value("max", 50) : 50;
            const int clamped = (max < 1) ? 1 : (max > 500 ? 500 : max);
            const std::string url = adminUrl("/api/activity?max=" + std::to_string(clamped));
            return makeResult(id, textContent(httpBridgeGet(url, 5000).dump(2)));
        }
    }
    // v0.10.0: DockerControl execution branch removed alongside the
    // catalog and enum value. Operators who still need container
    // automation can register a custom pool template that points at
    // `docker.exe` directly.
    if (gSpecialization == Specialization::LocalDatabase) {
        auto connectionStore = std::make_shared<PersistentContextDatabaseConnectionStore>();
        MasterControl::SqliteReadonlyQueryExecutor executor(connectionStore);
        if (toolName == "db.status") {
            return makeResult(id, textContent(databaseResultPayload(executor.status()).dump(2)));
        }
        if (toolName == "db.set_connection_string") {
            if (!arguments.is_object() || !arguments.contains("connectionString")
                || !arguments["connectionString"].is_string()) {
                return makeError(-32602,
                    "db.set_connection_string: arguments.connectionString (string, a SQLite database file path) is required.", id);
            }
            return makeResult(id, textContent(persistentCtxSet(
                "local-database.connection-string",
                arguments["connectionString"]).dump(2)));
        }
        if (toolName == "db.list_tables") {
            return makeResult(id, textContent(databaseResultPayload(executor.listTables()).dump(2)));
        }
        if (toolName == "db.describe_table") {
            if (!arguments.is_object() || !arguments.contains("table")
                || !arguments["table"].is_string() || arguments["table"].get<std::string>().empty()) {
                return makeError(-32602,
                    "db.describe_table: arguments.table (non-empty string) is required.", id);
            }
            return makeResult(id, textContent(databaseResultPayload(
                executor.describeTable(arguments["table"].get<std::string>())).dump(2)));
        }
        if (toolName == "db.query_readonly") {
            if (!arguments.is_object() || !arguments.contains("sql")
                || !arguments["sql"].is_string() || arguments["sql"].get<std::string>().empty()) {
                return makeError(-32602,
                    "db.query_readonly: arguments.sql (non-empty string) is required.", id);
            }
            const int rowLimit = arguments.value("rowLimit", 0);  // 0 -> executor default
            return makeResult(id, textContent(databaseResultPayload(
                executor.queryReadonly(arguments["sql"].get<std::string>(), rowLimit)).dump(2)));
        }
    }
    if (gSpecialization == Specialization::DesktopControl
        || gSpecialization == Specialization::ComputerUse) {
        const bool isComputerUse = (gSpecialization == Specialization::ComputerUse);
        if ((toolName == "desktop.list_windows")
            || (isComputerUse && toolName == "computer.window_list")) {
            const bool includeHidden = arguments.is_object()
                ? arguments.value("includeHidden", false) : false;
            return makeResult(id, textContent(desktopListWindows(includeHidden).dump(2)));
        }
        if (toolName == "desktop.focus") {
            if (!arguments.is_object() || !arguments.contains("titleSubstring")
                || !arguments["titleSubstring"].is_string()) {
                return makeError(-32602,
                    "desktop.focus: arguments.titleSubstring (string) is required.", id);
            }
            return makeResult(id, textContent(
                desktopFocus(arguments["titleSubstring"].get<std::string>()).dump(2)));
        }
        if (toolName == "desktop.launch") {
            if (!arguments.is_object() || !arguments.contains("path")
                || !arguments["path"].is_string()) {
                return makeError(-32602, "desktop.launch: arguments.path (string) is required.", id);
            }
            const std::string args = arguments.value("args", std::string{});
            return makeResult(id, textContent(
                desktopLaunch(arguments["path"].get<std::string>(), args).dump(2)));
        }
        if (toolName == "desktop.foreground") {
            return makeResult(id, textContent(desktopForeground().dump(2)));
        }
    }
#endif

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
        // v0.9.4: align worker protocol-version advertisement with the
        // gateway and the discovery doc (Streamable HTTP transport, MCP
        // rev 2025-03-26). Pre-v0.9.4 the worker reported 2024-11-05
        // even though it speaks the same JSON-RPC envelopes the
        // 2025-03-26 transport mandates; the lower advertisement
        // dragged any client negotiating with the worker directly
        // (test harnesses, Forsetti compliance probes) down to the
        // older protocol revision.
        // v0.9.61: serverInfo.name now reflects the active
        // specialization so tools/list at the gateway aggregator can
        // tell pools apart in operator-visible logs.
        nlohmann::json result = {
            { "protocolVersion", "2025-03-26" },
            { "serverInfo", {
                { "name", "MCOS " + gSpecializationName + " Worker" },
                { "version", MASTERCONTROL_VERSION }
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

int main(int argc, char** argv) {
    // parse --specialization=<id> if present. No --specialization (or an
    // explicit empty value) means the legacy baseline-tools contract. An
    // unknown NON-EMPTY specialization fails closed: it writes a clear error
    // to stderr and exits nonzero before accepting any JSON-RPC. Serving the
    // baseline-tools catalog under a mislabeled pool would mask a
    // misconfiguration and make the gateway appear healthier than it is.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        const std::string prefix = "--specialization=";
        if (arg.rfind(prefix, 0) == 0) {
            const std::string id = arg.substr(prefix.size());
            if (id == "terminal-shell") {
                gSpecialization = Specialization::TerminalShell;
                gSpecializationName = "terminal-shell";
            } else if (id == "local-git") {
                gSpecialization = Specialization::LocalGit;
                gSpecializationName = "local-git";
            } else if (id == "file-search") {
                gSpecialization = Specialization::FileSearch;
                gSpecializationName = "file-search";
            } else if (id == "client-tracker") {
                gSpecialization = Specialization::ClientTracker;
                gSpecializationName = "client-tracker";
            } else if (id == "metrics") {
                gSpecialization = Specialization::Metrics;
                gSpecializationName = "metrics";
            } else if (id == "code-execution-repl") {
                gSpecialization = Specialization::CodeExecutionRepl;
                gSpecializationName = "code-execution-repl";
            } else if (id == "local-test-runner") {
                gSpecialization = Specialization::LocalTestRunner;
                gSpecializationName = "local-test-runner";
            } else if (id == "local-build-tool") {
                gSpecialization = Specialization::LocalBuildTool;
                gSpecializationName = "local-build-tool";
            } else if (id == "local-linter") {
                gSpecialization = Specialization::LocalLinter;
                gSpecializationName = "local-linter";
            } else if (id == "local-indexer") {
                gSpecialization = Specialization::LocalIndexer;
                gSpecializationName = "local-indexer";
            } else if (id == "persistent-context") {
                gSpecialization = Specialization::PersistentContext;
                gSpecializationName = "persistent-context";
            } else if (id == "knowledge-graph") {
                gSpecialization = Specialization::KnowledgeGraph;
                gSpecializationName = "knowledge-graph";
            } else if (id == "file-watcher") {
                gSpecialization = Specialization::FileWatcher;
                gSpecializationName = "file-watcher";
            } else if (id == "keyboard-mouse-control") {
                gSpecialization = Specialization::KeyboardMouseControl;
                gSpecializationName = "keyboard-mouse-control";
            } else if (id == "screen-capture-vision") {
                gSpecialization = Specialization::ScreenCaptureVision;
                gSpecializationName = "screen-capture-vision";
            } else if (id == "desktop-control") {
                gSpecialization = Specialization::DesktopControl;
                gSpecializationName = "desktop-control";
            } else if (id == "computer-use") {
                gSpecialization = Specialization::ComputerUse;
                gSpecializationName = "computer-use";
            } else if (id == "sentinel") {
                gSpecialization = Specialization::AgentSentinel;
                gSpecializationName = "sentinel";
            } else if (id == "architect") {
                gSpecialization = Specialization::AgentArchitect;
                gSpecializationName = "architect";
            } else if (id == "forge") {
                gSpecialization = Specialization::AgentForge;
                gSpecializationName = "forge";
            } else if (id == "scribe") {
                gSpecialization = Specialization::AgentScribe;
                gSpecializationName = "scribe";
            } else if (id == "recon") {
                gSpecialization = Specialization::AgentRecon;
                gSpecializationName = "recon";
            } else if (id == "nexus") {
                gSpecialization = Specialization::AgentNexus;
                gSpecializationName = "nexus";
            } else if (id == "watchtower") {
                gSpecialization = Specialization::AgentWatchtower;
                gSpecializationName = "watchtower";
            } else if (id == "local-database") {
                gSpecialization = Specialization::LocalDatabase;
                gSpecializationName = "local-database";
            } else if (id == "baseline-tools" || id.empty()) {
                // The canonical baseline-tools id (explicit, or an empty value
                // that behaves like no --specialization).
                gSpecialization = Specialization::BaselineTools;
                gSpecializationName = "baseline-tools";
            } else {
                // Fail closed on an unknown non-empty specialization: refuse to
                // start before serving any JSON-RPC rather than silently
                // falling back to baseline-tools.
                std::fprintf(stderr,
                    "mcos-baseline-tools-worker: unknown --specialization '%s'; refusing to start.\n",
                    id.c_str());
                std::fflush(stderr);
                return 2;
            }
        }
    }

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
