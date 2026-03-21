// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "MasterControl/MasterControlRuntime.h"

#include "ForsettiCore/ActivationStore.h"
#include "ForsettiCore/CapabilityPolicy.h"
#include "ForsettiCore/CompatibilityChecker.h"
#include "ForsettiCore/EntitlementProviders.h"
#include "ForsettiCore/ForsettiContext.h"
#include "ForsettiCore/ForsettiEventBus.h"
#include "ForsettiCore/ForsettiLogger.h"
#include "ForsettiCore/ForsettiRuntime.h"
#include "ForsettiCore/ForsettiServiceContainer.h"
#include "ForsettiCore/ModuleManager.h"
#include "ForsettiCore/StaticModuleRegistry.h"
#include "ForsettiCore/UISurfaceManager.h"
#include "ForsettiPlatform/DefaultPlatformServices.h"
#include "MasterControl/MasterControlDefaults.h"
#include "MasterControl/MasterControlModules.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <iphlpapi.h>
#include <urlmon.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <windns.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

namespace MasterControl {

namespace {

std::wstring wideFromUtf8(const std::string& input) {
    if (input.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        0,
        input.c_str(),
        static_cast<int>(input.size()),
        nullptr,
        0);

    std::wstring output(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        input.c_str(),
        static_cast<int>(input.size()),
        output.data(),
        required);
    return output;
}

std::string utf8FromWide(const std::wstring& input) {
    if (input.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        input.c_str(),
        static_cast<int>(input.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    std::string output(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        input.c_str(),
        static_cast<int>(input.size()),
        output.data(),
        required,
        nullptr,
        nullptr);
    return output;
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool startsWithInsensitive(const std::string& value, const std::string& prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }

    for (size_t index = 0; index < prefix.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(value[index])) !=
            std::tolower(static_cast<unsigned char>(prefix[index]))) {
            return false;
        }
    }
    return true;
}

bool isRemoteSource(const std::string& source) {
    return startsWith(source, "http://") || startsWith(source, "https://");
}

std::string extractHostFromUrl(const std::string& source) {
    const auto schemePosition = source.find("://");
    if (schemePosition == std::string::npos) {
        return {};
    }

    const auto hostStart = schemePosition + 3;
    const auto hostEnd = source.find_first_of("/:", hostStart);
    if (hostEnd == std::string::npos) {
        return source.substr(hostStart);
    }
    return source.substr(hostStart, hostEnd - hostStart);
}

std::string sanitizePathComponent(std::string value) {
    for (char& character : value) {
        if (!(std::isalnum(static_cast<unsigned char>(character)) || character == '-' || character == '_')) {
            character = '_';
        }
    }
    return value;
}

std::string trimCopy(std::string value) {
    value.erase(
        value.begin(),
        std::find_if(
            value.begin(),
            value.end(),
            [](unsigned char character) { return !std::isspace(character); }));
    value.erase(
        std::find_if(
            value.rbegin(),
            value.rend(),
            [](unsigned char character) { return !std::isspace(character); })
            .base(),
        value.end());
    return value;
}

std::wstring quoteWindowsArgument(const std::wstring& argument) {
    if (argument.empty()) {
        return L"\"\"";
    }

    const bool requiresQuotes = argument.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if (!requiresQuotes) {
        return argument;
    }

    std::wstring quoted = L"\"";
    size_t backslashCount = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashCount;
            continue;
        }
        if (character == L'"') {
            quoted.append(backslashCount * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashCount = 0;
            continue;
        }
        if (backslashCount > 0) {
            quoted.append(backslashCount, L'\\');
            backslashCount = 0;
        }
        quoted.push_back(character);
    }
    if (backslashCount > 0) {
        quoted.append(backslashCount * 2, L'\\');
    }
    quoted.push_back(L'"');
    return quoted;
}

std::wstring joinCommandArguments(const std::vector<std::wstring>& arguments) {
    std::wstring commandLine;
    for (size_t index = 0; index < arguments.size(); ++index) {
        if (index > 0) {
            commandLine.push_back(L' ');
        }
        commandLine += quoteWindowsArgument(arguments[index]);
    }
    return commandLine;
}

std::optional<std::filesystem::path> findCommandOnPath(const std::vector<std::wstring>& fileNames) {
    for (const auto& fileName : fileNames) {
        std::array<wchar_t, 4096> buffer{};
        const DWORD length = SearchPathW(nullptr, fileName.c_str(), nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (length > 0 && length < buffer.size()) {
            return std::filesystem::path(buffer.data());
        }
    }
    return std::nullopt;
}

struct ParsedUrl final {
    bool valid = false;
    bool secure = false;
    std::string host;
    INTERNET_PORT port = 0;
    std::wstring pathAndQuery = L"/";
};

ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl parsed;
    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);

    std::wstring wideUrl = wideFromUtf8(url);
    std::wstring host(256, L'\0');
    std::wstring path(2048, L'\0');
    std::wstring extraInfo(2048, L'\0');
    components.lpszHostName = host.data();
    components.dwHostNameLength = static_cast<DWORD>(host.size());
    components.lpszUrlPath = path.data();
    components.dwUrlPathLength = static_cast<DWORD>(path.size());
    components.lpszExtraInfo = extraInfo.data();
    components.dwExtraInfoLength = static_cast<DWORD>(extraInfo.size());

    if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &components)) {
        return parsed;
    }

    host.resize(components.dwHostNameLength);
    path.resize(components.dwUrlPathLength);
    extraInfo.resize(components.dwExtraInfoLength);

    parsed.valid = true;
    parsed.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    parsed.host = utf8FromWide(host);
    parsed.port = components.nPort;
    parsed.pathAndQuery = path.empty() ? L"/" : path;
    if (!extraInfo.empty()) {
        parsed.pathAndQuery += extraInfo;
    }
    return parsed;
}

struct HttpClientResponse final {
    bool succeeded = false;
    int statusCode = 0;
    std::string body;
    std::string errorMessage;
};

HttpClientResponse sendJsonRequest(const std::string& method,
                                   const std::string& url,
                                   const std::vector<std::pair<std::wstring, std::wstring>>& headers,
                                   const std::string& body) {
    HttpClientResponse response;
    const auto parsed = parseUrl(url);
    if (!parsed.valid) {
        response.errorMessage = "Invalid URL.";
        return response;
    }

    HINTERNET session = WinHttpOpen(
        L"MasterControlServiceHost/2.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (session == nullptr) {
        response.errorMessage = "Unable to initialize WinHTTP.";
        return response;
    }

    WinHttpSetTimeouts(session, 3000, 3000, 10000, 30000);

    HINTERNET connection = WinHttpConnect(session, wideFromUtf8(parsed.host).c_str(), parsed.port, 0);
    if (connection == nullptr) {
        response.errorMessage = "Unable to connect to the remote provider.";
        WinHttpCloseHandle(session);
        return response;
    }

    const DWORD requestFlags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(
        connection,
        wideFromUtf8(method).c_str(),
        parsed.pathAndQuery.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        requestFlags);
    if (request == nullptr) {
        response.errorMessage = "Unable to open the remote provider request.";
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }

    std::wstring headerBlock = L"Content-Type: application/json\r\n";
    for (const auto& [name, value] : headers) {
        headerBlock += name;
        headerBlock += L": ";
        headerBlock += value;
        headerBlock += L"\r\n";
    }

    LPVOID optionalData = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
    const DWORD optionalLength = static_cast<DWORD>(body.size());
    if (WinHttpSendRequest(
            request,
            headerBlock.c_str(),
            static_cast<DWORD>(headerBlock.size()),
            optionalData,
            optionalLength,
            optionalLength,
            0) == 0 ||
        WinHttpReceiveResponse(request, nullptr) == 0) {
        response.errorMessage = "The remote provider request failed.";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusCodeSize,
        WINHTTP_NO_HEADER_INDEX);
    response.statusCode = static_cast<int>(statusCode);

    std::string responseBody;
    for (;;) {
        DWORD available = 0;
        if (WinHttpQueryDataAvailable(request, &available) == 0 || available == 0) {
            break;
        }

        std::string chunk(static_cast<size_t>(available), '\0');
        DWORD bytesRead = 0;
        if (WinHttpReadData(request, chunk.data(), available, &bytesRead) == 0) {
            response.errorMessage = "Unable to read the remote provider response.";
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return response;
        }

        chunk.resize(static_cast<size_t>(bytesRead));
        responseBody += chunk;
    }

    response.succeeded = response.statusCode >= 200 && response.statusCode < 300;
    response.body = std::move(responseBody);
    if (!response.succeeded && response.errorMessage.empty()) {
        response.errorMessage = "Remote provider returned an unsuccessful status code.";
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return response;
}

struct ProcessCaptureResult final {
    bool launched = false;
    int exitCode = -1;
    std::string stdoutText;
    std::string stderrText;
};

ProcessCaptureResult runProcessCapture(const std::wstring& commandLine,
                                       const std::filesystem::path& workingDirectory,
                                       const std::vector<std::pair<std::wstring, std::wstring>>& environmentOverrides = {}) {
    ProcessCaptureResult result;

    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    HANDLE stderrRead = nullptr;
    HANDLE stderrWrite = nullptr;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &securityAttributes, 0) ||
        !SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0) ||
        !CreatePipe(&stderrRead, &stderrWrite, &securityAttributes, 0) ||
        !SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0)) {
        if (stdoutRead) CloseHandle(stdoutRead);
        if (stdoutWrite) CloseHandle(stdoutWrite);
        if (stderrRead) CloseHandle(stderrRead);
        if (stderrWrite) CloseHandle(stderrWrite);
        return result;
    }

    LPWCH environmentBlock = nullptr;
    if (!environmentOverrides.empty()) {
        const LPWCH currentEnvironment = GetEnvironmentStringsW();
        if (currentEnvironment != nullptr) {
            std::map<std::wstring, std::wstring> variables;
            for (const wchar_t* cursor = currentEnvironment; *cursor != L'\0'; cursor += wcslen(cursor) + 1) {
                std::wstring entry(cursor);
                const auto separator = entry.find(L'=');
                if (separator == std::wstring::npos) {
                    continue;
                }
                variables[entry.substr(0, separator)] = entry.substr(separator + 1);
            }
            FreeEnvironmentStringsW(currentEnvironment);

            for (const auto& [name, value] : environmentOverrides) {
                variables[name] = value;
            }

            std::wstring flatEnvironment;
            for (const auto& [name, value] : variables) {
                flatEnvironment += name;
                flatEnvironment += L'=';
                flatEnvironment += value;
                flatEnvironment.push_back(L'\0');
            }
            flatEnvironment.push_back(L'\0');

            environmentBlock = static_cast<LPWCH>(HeapAlloc(GetProcessHeap(), 0, (flatEnvironment.size() + 1) * sizeof(wchar_t)));
            if (environmentBlock != nullptr) {
                memcpy(environmentBlock, flatEnvironment.data(), flatEnvironment.size() * sizeof(wchar_t));
            }
        }
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startupInfo.hStdOutput = stdoutWrite;
    startupInfo.hStdError = stderrWrite;

    PROCESS_INFORMATION processInformation{};
    std::wstring mutableCommandLine = commandLine;
    const std::wstring workingDirectoryWide = workingDirectory.wstring();
    const DWORD creationFlags = CREATE_NO_WINDOW | (environmentBlock != nullptr ? CREATE_UNICODE_ENVIRONMENT : 0);
    result.launched = CreateProcessW(
        nullptr,
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        creationFlags,
        environmentBlock,
        workingDirectoryWide.empty() ? nullptr : workingDirectoryWide.c_str(),
        &startupInfo,
        &processInformation) != 0;

    if (environmentBlock != nullptr) {
        HeapFree(GetProcessHeap(), 0, environmentBlock);
    }
    CloseHandle(stdoutWrite);
    CloseHandle(stderrWrite);

    if (!result.launched) {
        CloseHandle(stdoutRead);
        CloseHandle(stderrRead);
        return result;
    }

    auto readPipe = [](HANDLE handle) {
        std::string captured;
        char buffer[4096];
        DWORD bytesRead = 0;
        while (ReadFile(handle, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
            captured.append(buffer, buffer + bytesRead);
        }
        return captured;
    };

    result.stdoutText = readPipe(stdoutRead);
    result.stderrText = readPipe(stderrRead);
    WaitForSingleObject(processInformation.hProcess, INFINITE);

    DWORD exitCode = 0;
    if (GetExitCodeProcess(processInformation.hProcess, &exitCode) != 0) {
        result.exitCode = static_cast<int>(exitCode);
    }

    CloseHandle(stdoutRead);
    CloseHandle(stderrRead);
    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);
    return result;
}

std::string buildEndpointUrl(const RuntimeEndpoint& endpoint) {
    const std::string scheme = endpoint.protocol.empty() ? "http" : endpoint.protocol;
    const std::string routePath = endpoint.routePath.empty()
        ? std::string("/")
        : (endpoint.routePath.front() == '/' ? endpoint.routePath : "/" + endpoint.routePath);
    return scheme + "://" + endpoint.host + ":" + std::to_string(endpoint.port) + routePath;
}

std::string generateExecutionId() {
    static std::atomic<uint64_t> counter{ 1 };
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "exec-" + std::to_string(stamp) + "-" + std::to_string(counter.fetch_add(1));
}

nlohmann::json readJsonFile(const std::filesystem::path& filePath) {
    std::ifstream stream(filePath);
    if (!stream.is_open()) {
        return nlohmann::json{};
    }

    nlohmann::json json;
    stream >> json;
    return json;
}

void writeJsonFile(const std::filesystem::path& filePath, const nlohmann::json& json) {
    std::filesystem::create_directories(filePath.parent_path());
    std::ofstream stream(filePath, std::ios::trunc);
    stream << json.dump(2);
}

std::string readTextFile(const std::filesystem::path& filePath) {
    std::ifstream stream(filePath, std::ios::binary);
    if (!stream.is_open()) {
        return {};
    }

    std::ostringstream output;
    output << stream.rdbuf();
    return output.str();
}

std::string encodeBase64(const std::string& bytes) {
    if (bytes.empty()) {
        return {};
    }

    DWORD required = 0;
    CryptBinaryToStringA(
        reinterpret_cast<const BYTE*>(bytes.data()),
        static_cast<DWORD>(bytes.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
        nullptr,
        &required);

    std::string encoded(static_cast<size_t>(required), '\0');
    CryptBinaryToStringA(
        reinterpret_cast<const BYTE*>(bytes.data()),
        static_cast<DWORD>(bytes.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
        encoded.data(),
        &required);

    if (!encoded.empty() && encoded.back() == '\0') {
        encoded.pop_back();
    }
    return encoded;
}

std::string decodeBase64(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    DWORD required = 0;
    CryptStringToBinaryA(
        text.c_str(),
        static_cast<DWORD>(text.size()),
        CRYPT_STRING_BASE64,
        nullptr,
        &required,
        nullptr,
        nullptr);

    std::string bytes(static_cast<size_t>(required), '\0');
    CryptStringToBinaryA(
        text.c_str(),
        static_cast<DWORD>(text.size()),
        CRYPT_STRING_BASE64,
        reinterpret_cast<BYTE*>(bytes.data()),
        &required,
        nullptr,
        nullptr);
    return bytes;
}

std::string protectSecretPayload(const std::string& plainText) {
    DATA_BLOB inputBlob{};
    inputBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plainText.data()));
    inputBlob.cbData = static_cast<DWORD>(plainText.size());

    DATA_BLOB outputBlob{};
    if (CryptProtectData(&inputBlob, L"MasterControlProviderCredentials", nullptr, nullptr, nullptr, 0, &outputBlob) == 0) {
        throw std::runtime_error("Unable to protect provider credential payload.");
    }

    std::string protectedBytes(reinterpret_cast<const char*>(outputBlob.pbData), outputBlob.cbData);
    LocalFree(outputBlob.pbData);
    return encodeBase64(protectedBytes);
}

std::string unprotectSecretPayload(const std::string& protectedText) {
    if (protectedText.empty()) {
        return {};
    }

    const auto protectedBytes = decodeBase64(protectedText);
    DATA_BLOB inputBlob{};
    inputBlob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(protectedBytes.data()));
    inputBlob.cbData = static_cast<DWORD>(protectedBytes.size());

    DATA_BLOB outputBlob{};
    if (CryptUnprotectData(&inputBlob, nullptr, nullptr, nullptr, nullptr, 0, &outputBlob) == 0) {
        throw std::runtime_error("Unable to unprotect provider credential payload.");
    }

    std::string plainText(reinterpret_cast<const char*>(outputBlob.pbData), outputBlob.cbData);
    LocalFree(outputBlob.pbData);
    return plainText;
}

std::string lowercase(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

bool isBlank(const std::string& value) {
    return std::all_of(
        value.begin(),
        value.end(),
        [](const unsigned char character) { return std::isspace(character) != 0; });
}

bool pathIsWithinRoot(const std::filesystem::path& candidate, const std::filesystem::path& root) {
    const auto normalizedCandidate = lowercase(std::filesystem::weakly_canonical(candidate).generic_string());
    auto normalizedRoot = lowercase(std::filesystem::weakly_canonical(root).generic_string());
    if (!normalizedRoot.empty() && normalizedRoot.back() != '/') {
        normalizedRoot.push_back('/');
    }

    return normalizedCandidate == normalizedRoot.substr(0, normalizedRoot.size() - 1) ||
        startsWith(normalizedCandidate, normalizedRoot);
}

class ScopedWinsock final {
public:
    ScopedWinsock() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }

    ~ScopedWinsock() {
        WSACleanup();
    }
};

struct SharedState final {
    mutable std::mutex mutex;
    AppConfiguration configuration;
    std::vector<InstallProvenance> installHistory;
    std::vector<ProviderExecutionRecord> providerExecutionHistory;
};

std::string platformKey(const PlatformTarget platform) {
    switch (platform) {
        case PlatformTarget::Windows: return "windows";
        case PlatformTarget::MacOS: return "macos";
        case PlatformTarget::IOS: return "ios";
        case PlatformTarget::Unknown: break;
    }
    return "unknown";
}

std::string dotLocalHostName(std::string hostName) {
    hostName = trimCopy(std::move(hostName));
    if (hostName.empty()) {
        return "localhost.local";
    }
    if (hostName.find('.') == std::string::npos) {
        hostName += ".local";
    }
    return hostName;
}

std::string platformGatewayConfigPath(const PlatformTarget platform) {
    return "/api/platform-services/config/" + platformKey(platform);
}

std::string platformGatewayRoutePath(const PlatformTarget platform) {
    return "/mcp/gateway/" + platformKey(platform);
}

std::string governanceRoutePath(const PlatformTarget platform) {
    return "/mcp/governance/" + platformKey(platform);
}

PlatformTarget platformFromKey(const std::string& value) {
    if (startsWithInsensitive(value, "windows")) {
        return PlatformTarget::Windows;
    }
    if (startsWithInsensitive(value, "macos") || startsWithInsensitive(value, "mac")) {
        return PlatformTarget::MacOS;
    }
    if (startsWithInsensitive(value, "ios")) {
        return PlatformTarget::IOS;
    }
    return PlatformTarget::Unknown;
}

EndpointStatus endpointStatusFromServiceState(const std::string& value) {
    if (value == "online" || value == "advertised") {
        return EndpointStatus::Online;
    }
    if (value == "configured" || value == "disabled") {
        return EndpointStatus::Degraded;
    }
    if (value == "offline") {
        return EndpointStatus::Offline;
    }
    return EndpointStatus::Unknown;
}

struct BootstrapManifestContract final {
    std::string version = "1.0.0";
    std::string bootstrapScript;
    std::string bootstrapArguments;
    std::vector<RuntimeEndpoint> seededEndpoints;
    std::vector<ProviderConnection> providers;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    BootstrapManifestContract,
    version,
    bootstrapScript,
    bootstrapArguments,
    seededEndpoints,
    providers)

struct ValidatedBootstrapManifest final {
    BootstrapManifestContract contract;
    std::filesystem::path bootstrapPath;
};

struct EntitlementStateDocument final {
    std::vector<std::string> unlockedModuleIDs;
    std::vector<std::string> unlockedProductIDs;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    EntitlementStateDocument,
    unlockedModuleIDs,
    unlockedProductIDs)

struct ProviderCredentialsDocument final {
    std::map<std::string, std::string> protectedPayloadsByProviderId;
    std::map<std::string, std::string> updatedAtUtcByProviderId;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ProviderCredentialsDocument,
    protectedPayloadsByProviderId,
    updatedAtUtcByProviderId)

EntitlementStateDocument buildDefaultEntitlementStateDocument() {
    return EntitlementStateDocument{
        {
            "com.mastercontrol.environment-discovery",
            "com.mastercontrol.host-telemetry",
            "com.mastercontrol.runtime-inventory",
            "com.mastercontrol.configuration",
            "com.mastercontrol.provider-codex",
            "com.mastercontrol.provider-claude-code",
            "com.mastercontrol.provider-xai",
            "com.mastercontrol.dashboard-ui"
        },
        {
            "mastercontrol.iap.installer-import",
            "mastercontrol.iap.provider-integration",
            "mastercontrol.iap.export",
            "mastercontrol.iap.command-logic-unit",
            "mastercontrol.iap.gateway-windows",
            "mastercontrol.iap.gateway-macos",
            "mastercontrol.iap.gateway-ios",
            "mastercontrol.iap.governance-windows",
            "mastercontrol.iap.governance-macos",
            "mastercontrol.iap.governance-ios",
            "mastercontrol.iap.beacon-gateway"
        }
    };
}

GovernanceProfile buildFallbackGovernanceProfile() {
    return GovernanceProfile{
        "Command Logic Unit",
        "Governance is not assumed. CLU keeps the local MCP command plane inside declared operator intent and surfaces runtime drift before it becomes invisible.",
        {
            GovernanceDocument{
                "clu-constitution",
                "CLU Constitution",
                "constitution",
                "Defines the baseline rules for agentic operation inside Master Control Program.",
                "Contract before action. Scope is binding. Truthfulness is mandatory. Governance overrides convenience."
            }
        },
        {
            GovernanceRole{
                "architect",
                "Architect",
                "planning",
                { "Define allowed scope", "Classify changes", "Establish delivery intent" },
                { "Expand scope without approval" },
                { "Task contract", "Scope definition" }
            },
            GovernanceRole{
                "builder",
                "Builder",
                "execution",
                { "Implement approved changes", "Report runtime outcomes" },
                { "Hide failures", "Edit unrelated surfaces" },
                { "Artifact changes", "Change summary" }
            },
            GovernanceRole{
                "validator",
                "Validator",
                "verification",
                { "Review compliance posture", "Block incomplete delivery" },
                { "Approve blocked findings" },
                { "Validation status", "Compliance report" }
            }
        },
        {
            GovernanceRule{
                "CLU-C001",
                "No meaningful autonomous action without declared scope",
                "critical",
                "block_operation",
                "Agent autonomy should remain disabled unless the operator has explicitly enabled it for the dashboard."
            },
            GovernanceRule{
                "CLU-C002",
                "No unsafe open-LAN posture without explicit operator intent",
                "critical",
                "block_operation",
                "Security protocols should not be disabled while the browser surface remains open on the LAN."
            },
            GovernanceRule{
                "CLU-C003",
                "Troubleshooting bypass must remain visible and temporary",
                "high",
                "warn_operator",
                "Troubleshooting bypass is allowed, but it should remain visible until normal protections are restored."
            }
        },
        {
            "Confirm the security posture before enabling AI autonomy.",
            "Review trusted hosts before allowing unattended remote imports."
        }
    };
}

class JsonActivationStore final : public Forsetti::IActivationStore {
public:
    explicit JsonActivationStore(std::filesystem::path filePath)
        : filePath_(std::move(filePath)) {}

    Forsetti::ActivationState loadState() const override {
        if (!std::filesystem::exists(filePath_)) {
            return {};
        }

        return readJsonFile(filePath_).get<Forsetti::ActivationState>();
    }

    void saveState(const Forsetti::ActivationState& state) override {
        writeJsonFile(filePath_, state);
    }

private:
    std::filesystem::path filePath_;
};

class FileBackedEntitlementProvider final : public Forsetti::IEntitlementProvider {
public:
    FileBackedEntitlementProvider(std::filesystem::path filePath,
                                  EntitlementStateDocument defaultState)
        : filePath_(std::move(filePath))
        , defaultState_(std::move(defaultState)) {
        ensureStateFile();
        refreshEntitlements();
        watcher_ = std::thread([this]() { watchForChanges(); });
    }

    ~FileBackedEntitlementProvider() override {
        stopRequested_.store(true, std::memory_order_release);
        if (watcher_.joinable()) {
            watcher_.join();
        }
    }

    bool isUnlocked(const std::string& moduleIDOrProductID) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return unlockedModuleIDs_.find(moduleIDOrProductID) != unlockedModuleIDs_.end() ||
            unlockedProductIDs_.find(moduleIDOrProductID) != unlockedProductIDs_.end();
    }

    void refreshEntitlements() override {
        ensureStateFile();
        const auto stateJson = readJsonFile(filePath_);
        const auto state = stateJson.empty()
            ? defaultState_
            : stateJson.get<EntitlementStateDocument>();

        std::set<std::string> nextModuleIDs(
            state.unlockedModuleIDs.begin(),
            state.unlockedModuleIDs.end());
        std::set<std::string> nextProductIDs(
            state.unlockedProductIDs.begin(),
            state.unlockedProductIDs.end());

        std::vector<std::function<void()>> callbacks;
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            changed = nextModuleIDs != unlockedModuleIDs_ ||
                nextProductIDs != unlockedProductIDs_;
            unlockedModuleIDs_ = std::move(nextModuleIDs);
            unlockedProductIDs_ = std::move(nextProductIDs);
            if (changed) {
                callbacks = callbacks_;
            }
        }

        for (const auto& callback : callbacks) {
            callback();
        }
    }

    void onEntitlementsChanged(std::function<void()> callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_.push_back(std::move(callback));
    }

    void restorePurchases() override {
        refreshEntitlements();
    }

private:
    void ensureStateFile() const {
        if (!std::filesystem::exists(filePath_)) {
            writeJsonFile(filePath_, defaultState_);
        }
    }

    std::filesystem::file_time_type lastWriteTime() const {
        std::error_code errorCode;
        if (!std::filesystem::exists(filePath_, errorCode)) {
            return {};
        }

        const auto value = std::filesystem::last_write_time(filePath_, errorCode);
        if (errorCode) {
            return {};
        }
        return value;
    }

    void watchForChanges() {
        auto knownWriteTime = lastWriteTime();
        while (!stopRequested_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(750));
            const auto currentWriteTime = lastWriteTime();
            if (currentWriteTime != knownWriteTime) {
                knownWriteTime = currentWriteTime;
                refreshEntitlements();
            }
        }
    }

    std::filesystem::path filePath_;
    EntitlementStateDocument defaultState_;
    mutable std::mutex mutex_;
    std::set<std::string> unlockedModuleIDs_;
    std::set<std::string> unlockedProductIDs_;
    std::vector<std::function<void()>> callbacks_;
    std::atomic<bool> stopRequested_{ false };
    std::thread watcher_;
};

class FileBackedConfigurationService final : public IConfigurationService {
public:
    FileBackedConfigurationService(std::shared_ptr<SharedState> state, std::filesystem::path filePath)
        : state_(std::move(state))
        , filePath_(std::move(filePath)) {
        if (std::filesystem::exists(filePath_)) {
            const auto json = readJsonFile(filePath_);
            if (!json.is_null() && !json.empty()) {
                state_->configuration = json.get<AppConfiguration>();
            }
        } else {
            state_->configuration = buildDefaultConfiguration();
            persistLocked();
        }
    }

    AppConfiguration current() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->configuration;
    }

    OperationResult update(const AppConfiguration& configuration,
                           bool confirmUnsafeChanges) override {
        if (!configuration.security.securityProtocolsEnabled && !confirmUnsafeChanges) {
            return OperationResult{
                false,
                true,
                "Disabling security protocols requires explicit confirmation."
            };
        }

        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->configuration = configuration;
            persistLocked();
        }

        return OperationResult{ true, false, "Configuration updated." };
    }

private:
    void persistLocked() const {
        writeJsonFile(filePath_, state_->configuration);
    }

    std::shared_ptr<SharedState> state_;
    std::filesystem::path filePath_;
};

class ResourceAllocationService final : public IResourceAllocationService {
public:
    ResourceAllocationService(std::shared_ptr<SharedState> state, std::filesystem::path filePath)
        : state_(std::move(state))
        , filePath_(std::move(filePath)) {}

    ResourceAllocationProfile current() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->configuration.resourceAllocation;
    }

    OperationResult update(const ResourceAllocationProfile& profile) override {
        const auto isValidPercent = [](const int value) {
            return value >= 0 && value <= 100;
        };

        if (!isValidPercent(profile.cpuPercent) ||
            !isValidPercent(profile.memoryPercent) ||
            !isValidPercent(profile.bandwidthPercent) ||
            !isValidPercent(profile.storagePercent)) {
            return OperationResult{ false, false, "Resource allocation values must be between 0 and 100." };
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->configuration.resourceAllocation = profile;
        writeJsonFile(filePath_, state_->configuration);
        return OperationResult{ true, false, "Resource allocation updated." };
    }

private:
    std::shared_ptr<SharedState> state_;
    std::filesystem::path filePath_;
};

class WindowsHostTelemetryService final : public ITelemetryService {
public:
    HostTelemetrySnapshot captureSnapshot() override;

private:
    struct NetworkSample final {
        std::chrono::steady_clock::time_point timestamp;
        uint64_t bytesSent = 0;
        uint64_t bytesReceived = 0;
    };

    static std::string readComputerName();
    double readCpuPercent();
    void readPrimaryNetworkIdentity(std::string& ipAddress,
                                    std::string& macAddress,
                                    uint64_t& bytesSentPerSecond,
                                    uint64_t& bytesReceivedPerSecond);

    std::mutex mutex_;
    ULONGLONG lastIdle_ = 0;
    ULONGLONG lastTotal_ = 0;
    std::optional<NetworkSample> lastNetworkSample_;
};

HostTelemetrySnapshot WindowsHostTelemetryService::captureSnapshot() {
    HostTelemetrySnapshot snapshot;
    snapshot.hostName = readComputerName();
    snapshot.operatingSystem = "Windows";
    snapshot.capturedAtUtc = timestampNowUtc();

    snapshot.cpuPercent = readCpuPercent();

    MEMORYSTATUSEX memoryStatus{};
    memoryStatus.dwLength = sizeof(memoryStatus);
    if (GlobalMemoryStatusEx(&memoryStatus) != 0) {
        snapshot.memoryPercent = static_cast<double>(memoryStatus.dwMemoryLoad);
        snapshot.totalMemoryBytes = memoryStatus.ullTotalPhys;
        snapshot.freeMemoryBytes = memoryStatus.ullAvailPhys;
    }

    ULARGE_INTEGER freeBytesAvailable{};
    ULARGE_INTEGER totalBytes{};
    ULARGE_INTEGER totalFreeBytes{};
    if (GetDiskFreeSpaceExW(L"C:\\", &freeBytesAvailable, &totalBytes, &totalFreeBytes) != 0) {
        snapshot.totalDiskBytes = totalBytes.QuadPart;
        snapshot.freeDiskBytes = totalFreeBytes.QuadPart;
        if (snapshot.totalDiskBytes != 0U) {
            const auto usedBytes = snapshot.totalDiskBytes - snapshot.freeDiskBytes;
            snapshot.diskPercent = (static_cast<double>(usedBytes) / static_cast<double>(snapshot.totalDiskBytes)) * 100.0;
        }
    }

    readPrimaryNetworkIdentity(snapshot.primaryIpAddress, snapshot.primaryMacAddress, snapshot.bytesSentPerSecond, snapshot.bytesReceivedPerSecond);
    return snapshot;
}

std::string WindowsHostTelemetryService::readComputerName() {
    char buffer[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameA(buffer, &size) == 0) {
        return "MASTER-CONTROL";
    }
    return std::string(buffer, size);
}

double WindowsHostTelemetryService::readCpuPercent() {
    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime) == 0) {
        return 0.0;
    }

    const ULONGLONG idle = (static_cast<ULONGLONG>(idleTime.dwHighDateTime) << 32) | idleTime.dwLowDateTime;
    const ULONGLONG kernel = (static_cast<ULONGLONG>(kernelTime.dwHighDateTime) << 32) | kernelTime.dwLowDateTime;
    const ULONGLONG user = (static_cast<ULONGLONG>(userTime.dwHighDateTime) << 32) | userTime.dwLowDateTime;

    std::lock_guard<std::mutex> lock(mutex_);
    if (lastTotal_ == 0U) {
        lastIdle_ = idle;
        lastTotal_ = kernel + user;
        return 0.0;
    }

    const ULONGLONG total = kernel + user;
    const ULONGLONG idleDelta = idle - lastIdle_;
    const ULONGLONG totalDelta = total - lastTotal_;
    lastIdle_ = idle;
    lastTotal_ = total;

    if (totalDelta == 0U) {
        return 0.0;
    }

    return static_cast<double>(totalDelta - idleDelta) * 100.0 / static_cast<double>(totalDelta);
}

void WindowsHostTelemetryService::readPrimaryNetworkIdentity(std::string& ipAddress,
                                                             std::string& macAddress,
                                                             uint64_t& bytesSentPerSecond,
                                                             uint64_t& bytesReceivedPerSecond) {
    ULONG outBufferLength = 16 * 1024;
    std::vector<unsigned char> buffer(outBufferLength);
    IP_ADAPTER_ADDRESSES* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    DWORD result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, addresses, &outBufferLength);
    if (result == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(outBufferLength);
        addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        result = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, addresses, &outBufferLength);
    }

    if (result != NO_ERROR) {
        return;
    }

    for (auto* adapter = addresses; adapter != nullptr; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }

        for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next) {
            char host[NI_MAXHOST]{};
            if (getnameinfo(
                    unicast->Address.lpSockaddr,
                    static_cast<socklen_t>(unicast->Address.iSockaddrLength),
                    host,
                    NI_MAXHOST,
                    nullptr,
                    0,
                    NI_NUMERICHOST) == 0) {
                ipAddress = host;
                break;
            }
        }

        if (adapter->PhysicalAddressLength > 0U) {
            std::ostringstream macStream;
            for (ULONG index = 0; index < adapter->PhysicalAddressLength; ++index) {
                if (index > 0U) {
                    macStream << ':';
                }
                macStream << std::hex << std::uppercase << static_cast<int>(adapter->PhysicalAddress[index]);
            }
            macAddress = macStream.str();
        }

        MIB_IF_ROW2 row{};
        row.InterfaceIndex = adapter->IfIndex;
        if (GetIfEntry2(&row) == NO_ERROR) {
            const auto now = std::chrono::steady_clock::now();
            const uint64_t sent = row.OutOctets;
            const uint64_t received = row.InOctets;

            std::lock_guard<std::mutex> lock(mutex_);
            if (lastNetworkSample_.has_value()) {
                const auto elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastNetworkSample_->timestamp).count();
                if (elapsedMilliseconds > 0) {
                    const auto elapsedSeconds = static_cast<double>(elapsedMilliseconds) / 1000.0;
                    bytesSentPerSecond = static_cast<uint64_t>(static_cast<double>(sent - lastNetworkSample_->bytesSent) / elapsedSeconds);
                    bytesReceivedPerSecond = static_cast<uint64_t>(static_cast<double>(received - lastNetworkSample_->bytesReceived) / elapsedSeconds);
                }
            }
            lastNetworkSample_ = NetworkSample{ now, sent, received };
        }
        break;
    }
}

class RuntimeInventoryService final : public IRuntimeInventoryService {
public:
    explicit RuntimeInventoryService(std::shared_ptr<SharedState> state)
        : state_(std::move(state)) {
        refresh();
    }

    std::vector<RuntimeEndpoint> listEndpoints() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return endpoints_;
    }

    void refresh() override;

private:
    static EndpointStatus probeEndpoint(const std::string& host, uint16_t port);
    static bool sameEndpointConfiguration(const std::vector<RuntimeEndpoint>& left,
                                          const std::vector<RuntimeEndpoint>& right);

    std::shared_ptr<SharedState> state_;
    std::mutex mutex_;
    std::vector<RuntimeEndpoint> endpoints_;
    std::chrono::steady_clock::time_point lastRefreshAt_{};
    std::chrono::seconds refreshInterval_{ 15 };
};

void RuntimeInventoryService::refresh() {
    std::vector<RuntimeEndpoint> endpoints;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        endpoints = state_->configuration.activeProfile.seededEndpoints;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        if (!endpoints_.empty() && lastRefreshAt_.time_since_epoch().count() != 0 &&
            now - lastRefreshAt_ < refreshInterval_ &&
            sameEndpointConfiguration(endpoints_, endpoints)) {
            return;
        }
    }

    for (auto& endpoint : endpoints) {
        endpoint.lastCheckedUtc = timestampNowUtc();
        if (endpoint.port == 0 || endpoint.host.empty() || endpoint.protocol == "virtual") {
            endpoint.status = EndpointStatus::Unknown;
            continue;
        }

        endpoint.status = probeEndpoint(endpoint.host, endpoint.port);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    endpoints_ = std::move(endpoints);
    lastRefreshAt_ = std::chrono::steady_clock::now();
}

bool RuntimeInventoryService::sameEndpointConfiguration(const std::vector<RuntimeEndpoint>& left,
                                                        const std::vector<RuntimeEndpoint>& right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (size_t index = 0; index < left.size(); ++index) {
        if (left[index].id != right[index].id ||
            left[index].host != right[index].host ||
            left[index].port != right[index].port ||
            left[index].protocol != right[index].protocol ||
            left[index].routePath != right[index].routePath ||
            left[index].kind != right[index].kind ||
            left[index].userDefined != right[index].userDefined ||
            left[index].specialization != right[index].specialization) {
            return false;
        }
    }
    return true;
}

EndpointStatus RuntimeInventoryService::probeEndpoint(const std::string& host, const uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* results = nullptr;
    const auto portText = std::to_string(port);
    if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &results) != 0) {
        return EndpointStatus::Offline;
    }

    EndpointStatus status = EndpointStatus::Offline;
    for (addrinfo* result = results; result != nullptr; result = result->ai_next) {
        SOCKET socketHandle = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (socketHandle == INVALID_SOCKET) {
            continue;
        }

        u_long nonBlocking = 1;
        ioctlsocket(socketHandle, FIONBIO, &nonBlocking);
        const int connectResult = connect(socketHandle, result->ai_addr, static_cast<int>(result->ai_addrlen));
        if (connectResult == 0) {
            status = EndpointStatus::Online;
            closesocket(socketHandle);
            break;
        }

        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(socketHandle, &writeSet);
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 400000;

        if (select(0, nullptr, &writeSet, nullptr, &timeout) > 0) {
            int socketError = 0;
            int socketErrorLength = sizeof(socketError);
            getsockopt(socketHandle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &socketErrorLength);
            status = (socketError == 0) ? EndpointStatus::Online : EndpointStatus::Offline;
        }

        closesocket(socketHandle);
        if (status == EndpointStatus::Online) {
            break;
        }
    }

    freeaddrinfo(results);
    return status;
}

class PackageTrustEvaluator final : public IPackageTrustEvaluator {
public:
    explicit PackageTrustEvaluator(std::shared_ptr<SharedState> state)
        : state_(std::move(state)) {}

    PackageTrustDecision evaluate(const std::string& source,
                                  bool allowUntrustedExecution) const override {
        if (!isRemoteSource(source)) {
            return PackageTrustDecision{ true, false, "Local package source." };
        }

        const auto host = extractHostFromUrl(source);
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            const auto& trustedHosts = state_->configuration.security.trustedRemoteHosts;
            const auto trusted = std::find(trustedHosts.begin(), trustedHosts.end(), host) != trustedHosts.end();
            if (trusted) {
                return PackageTrustDecision{ true, false, "Host is allowlisted for unattended execution." };
            }
        }

        if (allowUntrustedExecution) {
            return PackageTrustDecision{ false, false, "Explicit approval granted for an untrusted remote source." };
        }

        return PackageTrustDecision{ false, true, "Remote host is not trusted for unattended execution." };
    }

private:
    std::shared_ptr<SharedState> state_;
};

class ProviderCatalogService final : public IProviderCatalogService {
public:
    std::vector<ProviderCapabilityDescriptor> listCapabilities() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ProviderCapabilityDescriptor> capabilities;
        capabilities.reserve(capabilitiesById_.size());
        for (const auto& [providerId, capability] : capabilitiesById_) {
            capabilities.push_back(capability);
        }

        std::sort(
            capabilities.begin(),
            capabilities.end(),
            [](const ProviderCapabilityDescriptor& left, const ProviderCapabilityDescriptor& right) {
                return std::tie(left.displayName, left.providerId) < std::tie(right.displayName, right.providerId);
            });
        return capabilities;
    }

    void upsertCapability(const ProviderCapabilityDescriptor& capability) override {
        std::lock_guard<std::mutex> lock(mutex_);
        capabilitiesById_[capability.providerId] = capability;
    }

    void removeCapability(const std::string& providerId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        capabilitiesById_.erase(providerId);
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, ProviderCapabilityDescriptor> capabilitiesById_;
};

class ProviderRegistryService final : public IProviderRegistry {
public:
    ProviderRegistryService(std::shared_ptr<SharedState> state,
                            std::filesystem::path configurationFile,
                            std::shared_ptr<IProviderCatalogService> providerCatalogService)
        : state_(std::move(state))
        , configurationFile_(std::move(configurationFile))
        , providerCatalogService_(std::move(providerCatalogService)) {}

    std::vector<ProviderConnection> listProviders() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->configuration.providers;
    }

    OperationResult upsertProvider(const ProviderConnection& provider) override {
        if (provider.id.empty() || isBlank(provider.id)) {
            return OperationResult{ false, false, "Provider ID is required." };
        }
        if (provider.displayName.empty() || isBlank(provider.displayName)) {
            return OperationResult{ false, false, "Provider display name is required." };
        }
        if (provider.baseUrl.empty() || isBlank(provider.baseUrl)) {
            return OperationResult{ false, false, "Provider base URL is required." };
        }
        const auto capabilities = providerCatalogService_->listCapabilities();
        const auto capabilityIterator = std::find_if(
            capabilities.begin(),
            capabilities.end(),
            [&provider](const ProviderCapabilityDescriptor& capability) { return capability.kind == provider.kind; });
        if (capabilityIterator == capabilities.end()) {
            return OperationResult{ false, false, "The selected provider kind is not currently supported by an active provider module." };
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        auto& providers = state_->configuration.providers;
        ProviderConnection normalizedProvider = provider;
        if (normalizedProvider.modelId.empty()) {
            normalizedProvider.modelId = capabilityIterator->recommendedModel;
        }
        const auto iterator = std::find_if(
            providers.begin(),
            providers.end(),
            [&provider](const ProviderConnection& candidate) { return candidate.id == provider.id; });

        if (iterator == providers.end()) {
            providers.push_back(normalizedProvider);
        } else {
            normalizedProvider.credentialsConfigured = iterator->credentialsConfigured;
            *iterator = normalizedProvider;
        }

        writeJsonFile(configurationFile_, state_->configuration);
        return OperationResult{ true, false, "Provider settings updated." };
    }

private:
    std::shared_ptr<SharedState> state_;
    std::filesystem::path configurationFile_;
    std::shared_ptr<IProviderCatalogService> providerCatalogService_;
};

class ProviderCredentialStore final : public IProviderCredentialStore {
public:
    ProviderCredentialStore(std::filesystem::path filePath,
                            std::shared_ptr<IProviderRegistry> providerRegistry,
                            std::shared_ptr<IProviderCatalogService> providerCatalogService)
        : filePath_(std::move(filePath))
        , providerRegistry_(std::move(providerRegistry))
        , providerCatalogService_(std::move(providerCatalogService)) {
        if (!std::filesystem::exists(filePath_)) {
            writeJsonFile(filePath_, ProviderCredentialsDocument{});
        }
    }

    std::vector<ProviderCredentialStatus> listStatuses() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return loadStatusesLocked();
    }

    std::map<std::string, std::string> readCredentials(const std::string& providerId) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto document = loadDocumentLocked();
        const auto iterator = document.protectedPayloadsByProviderId.find(providerId);
        if (iterator == document.protectedPayloadsByProviderId.end()) {
            return {};
        }

        try {
            const auto payload = nlohmann::json::parse(unprotectSecretPayload(iterator->second));
            return payload.get<std::map<std::string, std::string>>();
        } catch (...) {
            return {};
        }
    }

    OperationResult upsertCredentials(const ProviderCredentialUpdate& update) override {
        if (update.providerId.empty() || isBlank(update.providerId)) {
            return OperationResult{ false, false, "Provider credentials must target a provider route." };
        }

        const auto providers = providerRegistry_->listProviders();
        const auto providerIterator = std::find_if(
            providers.begin(),
            providers.end(),
            [&update](const ProviderConnection& provider) { return provider.id == update.providerId; });
        if (providerIterator == providers.end()) {
            return OperationResult{ false, false, "Provider route was not found." };
        }

        const auto capabilities = providerCatalogService_->listCapabilities();
        const auto capabilityIterator = std::find_if(
            capabilities.begin(),
            capabilities.end(),
            [providerKind = providerIterator->kind](const ProviderCapabilityDescriptor& capability) {
                return capability.kind == providerKind;
            });
        if (capabilityIterator == capabilities.end()) {
            return OperationResult{ false, false, "Provider module metadata is not available for the selected route." };
        }

        for (const auto& field : capabilityIterator->credentialFields) {
            if (field.required && field.requirementGroup.empty()) {
                const auto iterator = update.values.find(field.fieldId);
                if (iterator == update.values.end() || iterator->second.empty() || isBlank(iterator->second)) {
                    return OperationResult{ false, false, field.label + " is required." };
                }
            }
        }

        std::set<std::string> requirementGroups;
        for (const auto& field : capabilityIterator->credentialFields) {
            if (!field.requirementGroup.empty()) {
                requirementGroups.insert(field.requirementGroup);
            }
        }
        for (const auto& requirementGroup : requirementGroups) {
            bool satisfied = false;
            for (const auto& field : capabilityIterator->credentialFields) {
                if (field.requirementGroup != requirementGroup) {
                    continue;
                }

                const auto iterator = update.values.find(field.fieldId);
                if (iterator != update.values.end() && !iterator->second.empty() && !isBlank(iterator->second)) {
                    satisfied = true;
                    break;
                }
            }
            if (!satisfied) {
                return OperationResult{ false, false, "One of the required " + requirementGroup + " credentials must be provided." };
            }
        }

        nlohmann::json payload = nlohmann::json::object();
        for (const auto& [fieldId, value] : update.values) {
            if (!value.empty() && !isBlank(value)) {
                payload[fieldId] = value;
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto document = loadDocumentLocked();
        document.protectedPayloadsByProviderId[update.providerId] = protectSecretPayload(payload.dump());
        document.updatedAtUtcByProviderId[update.providerId] = timestampNowUtc();
        writeJsonFile(filePath_, document);
        return OperationResult{ true, false, "Provider credentials were saved to the local secure store." };
    }

private:
    ProviderCredentialsDocument loadDocumentLocked() const {
        const auto json = readJsonFile(filePath_);
        if (json.is_null() || json.empty()) {
            return {};
        }
        return json.get<ProviderCredentialsDocument>();
    }

    std::vector<ProviderCredentialStatus> loadStatusesLocked() const {
        std::vector<ProviderCredentialStatus> statuses;
        const auto document = loadDocumentLocked();
        for (const auto& [providerId, protectedPayload] : document.protectedPayloadsByProviderId) {
            ProviderCredentialStatus status;
            status.providerId = providerId;
            if (const auto updatedAtIterator = document.updatedAtUtcByProviderId.find(providerId);
                updatedAtIterator != document.updatedAtUtcByProviderId.end()) {
                status.updatedAtUtc = updatedAtIterator->second;
            }

            try {
                const auto payload = nlohmann::json::parse(unprotectSecretPayload(protectedPayload));
                status.configured = !payload.empty();
                for (const auto& [fieldId, value] : payload.items()) {
                    if (value.is_string() && !value.get<std::string>().empty()) {
                        status.configuredFieldIds.push_back(fieldId);
                    }
                }
                status.message = status.configured
                    ? "Credentials are present in secure storage."
                    : "No provider credentials are currently configured.";
            } catch (...) {
                status.configured = false;
                status.message = "Stored credentials could not be read.";
            }

            statuses.push_back(std::move(status));
        }
        return statuses;
    }

    std::filesystem::path filePath_;
    std::shared_ptr<IProviderRegistry> providerRegistry_;
    std::shared_ptr<IProviderCatalogService> providerCatalogService_;
    mutable std::mutex mutex_;
};

class SubAgentCatalogService final : public ISubAgentCatalogService {
public:
    SubAgentCatalogService(std::shared_ptr<SharedState> state,
                           std::filesystem::path configurationFile,
                           std::shared_ptr<IRuntimeInventoryService> inventoryService)
        : state_(std::move(state))
        , configurationFile_(std::move(configurationFile))
        , inventoryService_(std::move(inventoryService)) {}

    std::vector<RuntimeEndpoint> listCustomSubAgents() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        std::vector<RuntimeEndpoint> subAgents;
        for (const auto& endpoint : state_->configuration.activeProfile.seededEndpoints) {
            if (endpoint.kind == EndpointKind::SubAgent && endpoint.userDefined) {
                subAgents.push_back(endpoint);
            }
        }

        std::sort(
            subAgents.begin(),
            subAgents.end(),
            [](const RuntimeEndpoint& left, const RuntimeEndpoint& right) {
                return std::tie(left.displayName, left.id) < std::tie(right.displayName, right.id);
            });
        return subAgents;
    }

    OperationResult upsertSubAgent(const RuntimeEndpoint& subAgent) override {
        if (subAgent.id.empty() || isBlank(subAgent.id)) {
            return OperationResult{ false, false, "Sub-agent ID is required." };
        }
        if (subAgent.displayName.empty() || isBlank(subAgent.displayName)) {
            return OperationResult{ false, false, "Sub-agent display name is required." };
        }

        const std::set<std::string> reservedTargetIds{
            "planner",
            "architect",
            "coding-specialists"
        };
        if (reservedTargetIds.contains(subAgent.id)) {
            return OperationResult{ false, false, "The requested sub-agent ID conflicts with a reserved orchestration target." };
        }

        RuntimeEndpoint normalized = subAgent;
        normalized.kind = EndpointKind::SubAgent;
        normalized.userDefined = true;
        normalized.id = trimCopy(normalized.id);
        normalized.displayName = trimCopy(normalized.displayName);
        normalized.host = trimCopy(normalized.host);
        normalized.protocol = trimCopy(normalized.protocol);
        normalized.description = trimCopy(normalized.description);
        normalized.routePath = trimCopy(normalized.routePath);
        normalized.specialization = trimCopy(normalized.specialization);
        normalized.status = EndpointStatus::Unknown;
        normalized.lastCheckedUtc = timestampNowUtc();
        if (normalized.host.empty()) {
            normalized.host = state_->configuration.activeProfile.preferredBindAddress;
        }
        if (normalized.protocol.empty()) {
            normalized.protocol = normalized.port == 0 ? "virtual" : "http";
        }
        if (normalized.description.empty() && !normalized.specialization.empty()) {
            normalized.description = normalized.specialization + " specialist lane";
        }

        {
            std::lock_guard<std::mutex> lock(state_->mutex);

            const auto conflictingGroup = std::find_if(
                state_->configuration.subAgentGroups.begin(),
                state_->configuration.subAgentGroups.end(),
                [&normalized](const SubAgentGroupDefinition& group) { return group.groupId == normalized.id; });
            if (conflictingGroup != state_->configuration.subAgentGroups.end()) {
                return OperationResult{ false, false, "The requested sub-agent ID conflicts with an existing sub-agent group." };
            }

            auto& endpoints = state_->configuration.activeProfile.seededEndpoints;
            const auto endpointIterator = std::find_if(
                endpoints.begin(),
                endpoints.end(),
                [&normalized](const RuntimeEndpoint& endpoint) { return endpoint.id == normalized.id; });
            if (endpointIterator != endpoints.end() && !endpointIterator->userDefined) {
                return OperationResult{ false, false, "The requested sub-agent ID is already owned by a seeded or imported runtime endpoint." };
            }

            if (endpointIterator == endpoints.end()) {
                endpoints.push_back(normalized);
            } else {
                *endpointIterator = normalized;
            }

            writeJsonFile(configurationFile_, state_->configuration);
        }
        inventoryService_->refresh();
        return OperationResult{ true, false, "Custom sub-agent settings updated." };
    }

    OperationResult removeSubAgent(const std::string& subAgentId) override {
        if (subAgentId.empty() || isBlank(subAgentId)) {
            return OperationResult{ false, false, "A custom sub-agent ID is required." };
        }

        {
            std::lock_guard<std::mutex> lock(state_->mutex);

            auto& endpoints = state_->configuration.activeProfile.seededEndpoints;
            const auto iterator = std::find_if(
                endpoints.begin(),
                endpoints.end(),
                [&subAgentId](const RuntimeEndpoint& endpoint) { return endpoint.id == subAgentId && endpoint.kind == EndpointKind::SubAgent; });
            if (iterator == endpoints.end()) {
                return OperationResult{ false, false, "The requested sub-agent was not found." };
            }
            if (!iterator->userDefined) {
                return OperationResult{ false, false, "Only user-defined sub-agents can be removed." };
            }

            endpoints.erase(iterator);

            auto& assignments = state_->configuration.providerAssignments;
            assignments.erase(
                std::remove_if(
                    assignments.begin(),
                    assignments.end(),
                    [&subAgentId](const ProviderAssignment& assignment) { return assignment.targetId == subAgentId; }),
                assignments.end());

            auto& groups = state_->configuration.subAgentGroups;
            for (auto& group : groups) {
                group.memberTargetIds.erase(
                    std::remove(group.memberTargetIds.begin(), group.memberTargetIds.end(), subAgentId),
                    group.memberTargetIds.end());
            }

            std::vector<std::string> removedGroupIds;
            groups.erase(
                std::remove_if(
                    groups.begin(),
                    groups.end(),
                    [&removedGroupIds](const SubAgentGroupDefinition& group) {
                        if (!group.memberTargetIds.empty()) {
                            return false;
                        }
                        removedGroupIds.push_back(group.groupId);
                        return true;
                    }),
                groups.end());

            if (!removedGroupIds.empty()) {
                assignments.erase(
                    std::remove_if(
                        assignments.begin(),
                        assignments.end(),
                        [&removedGroupIds](const ProviderAssignment& assignment) {
                            return std::find(removedGroupIds.begin(), removedGroupIds.end(), assignment.targetId) != removedGroupIds.end() ||
                                std::find(removedGroupIds.begin(), removedGroupIds.end(), assignment.sourceGroupId) != removedGroupIds.end();
                        }),
                    assignments.end());
            }

            writeJsonFile(configurationFile_, state_->configuration);
        }
        inventoryService_->refresh();
        return OperationResult{ true, false, "Custom sub-agent removed." };
    }

private:
    std::shared_ptr<SharedState> state_;
    std::filesystem::path configurationFile_;
    std::shared_ptr<IRuntimeInventoryService> inventoryService_;
};

class McpServerCatalogService final : public IMcpServerCatalogService {
public:
    McpServerCatalogService(std::shared_ptr<SharedState> state,
                            std::filesystem::path configurationFile,
                            std::shared_ptr<IRuntimeInventoryService> inventoryService)
        : state_(std::move(state))
        , configurationFile_(std::move(configurationFile))
        , inventoryService_(std::move(inventoryService)) {}

    std::vector<RuntimeEndpoint> listCustomMcpServers() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        std::vector<RuntimeEndpoint> mcpServers;
        for (const auto& endpoint : state_->configuration.activeProfile.seededEndpoints) {
            if (endpoint.kind == EndpointKind::MCPServer && endpoint.userDefined) {
                mcpServers.push_back(endpoint);
            }
        }

        std::sort(
            mcpServers.begin(),
            mcpServers.end(),
            [](const RuntimeEndpoint& left, const RuntimeEndpoint& right) {
                return std::tie(left.displayName, left.id) < std::tie(right.displayName, right.id);
            });
        return mcpServers;
    }

    OperationResult upsertMcpServer(const RuntimeEndpoint& mcpServer) override {
        if (mcpServer.id.empty() || isBlank(mcpServer.id)) {
            return OperationResult{ false, false, "MCP server ID is required." };
        }
        if (mcpServer.displayName.empty() || isBlank(mcpServer.displayName)) {
            return OperationResult{ false, false, "MCP server display name is required." };
        }
        if (mcpServer.port == 0) {
            return OperationResult{ false, false, "MCP servers require a listening port." };
        }

        RuntimeEndpoint normalized = mcpServer;
        normalized.kind = EndpointKind::MCPServer;
        normalized.userDefined = true;
        normalized.id = trimCopy(normalized.id);
        normalized.displayName = trimCopy(normalized.displayName);
        normalized.host = trimCopy(normalized.host);
        normalized.protocol = trimCopy(normalized.protocol);
        normalized.description = trimCopy(normalized.description);
        normalized.routePath = trimCopy(normalized.routePath);
        normalized.specialization.clear();
        normalized.status = EndpointStatus::Unknown;
        normalized.lastCheckedUtc = timestampNowUtc();
        if (normalized.host.empty()) {
            normalized.host = state_->configuration.activeProfile.preferredBindAddress;
        }
        if (normalized.protocol.empty()) {
            normalized.protocol = "http";
        }
        if (normalized.description.empty()) {
            normalized.description = "Custom MCP server lane.";
        }

        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            auto& endpoints = state_->configuration.activeProfile.seededEndpoints;
            const auto endpointIterator = std::find_if(
                endpoints.begin(),
                endpoints.end(),
                [&normalized](const RuntimeEndpoint& endpoint) { return endpoint.id == normalized.id; });
            if (endpointIterator != endpoints.end() && !endpointIterator->userDefined) {
                return OperationResult{ false, false, "The requested MCP server ID is already owned by a seeded or imported runtime endpoint." };
            }

            if (endpointIterator == endpoints.end()) {
                endpoints.push_back(normalized);
            } else {
                *endpointIterator = normalized;
            }

            writeJsonFile(configurationFile_, state_->configuration);
        }
        inventoryService_->refresh();
        return OperationResult{ true, false, "Custom MCP server settings updated." };
    }

    OperationResult removeMcpServer(const std::string& mcpServerId) override {
        if (mcpServerId.empty() || isBlank(mcpServerId)) {
            return OperationResult{ false, false, "A custom MCP server ID is required." };
        }

        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            auto& endpoints = state_->configuration.activeProfile.seededEndpoints;
            const auto iterator = std::find_if(
                endpoints.begin(),
                endpoints.end(),
                [&mcpServerId](const RuntimeEndpoint& endpoint) {
                    return endpoint.id == mcpServerId && endpoint.kind == EndpointKind::MCPServer;
                });
            if (iterator == endpoints.end()) {
                return OperationResult{ false, false, "The requested MCP server was not found." };
            }
            if (!iterator->userDefined) {
                return OperationResult{ false, false, "Only user-defined MCP servers can be removed." };
            }

            endpoints.erase(iterator);
            writeJsonFile(configurationFile_, state_->configuration);
        }
        inventoryService_->refresh();
        return OperationResult{ true, false, "Custom MCP server removed." };
    }

private:
    std::shared_ptr<SharedState> state_;
    std::filesystem::path configurationFile_;
    std::shared_ptr<IRuntimeInventoryService> inventoryService_;
};

class SubAgentGroupService final : public ISubAgentGroupService {
public:
    SubAgentGroupService(std::shared_ptr<SharedState> state,
                         std::filesystem::path configurationFile,
                         std::shared_ptr<IRuntimeInventoryService> inventoryService)
        : state_(std::move(state))
        , configurationFile_(std::move(configurationFile))
        , inventoryService_(std::move(inventoryService)) {}

    std::vector<SubAgentGroupDefinition> listGroups() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->configuration.subAgentGroups;
    }

    OperationResult upsertGroup(const SubAgentGroupDefinition& group) override {
        if (group.groupId.empty() || isBlank(group.groupId)) {
            return OperationResult{ false, false, "Sub-agent group ID is required." };
        }
        if (group.displayName.empty() || isBlank(group.displayName)) {
            return OperationResult{ false, false, "Sub-agent group display name is required." };
        }

        const auto endpoints = inventoryService_->listEndpoints();
        std::set<std::string> subAgentIds;
        for (const auto& endpoint : endpoints) {
            if (endpoint.kind == EndpointKind::SubAgent && !endpoint.id.empty()) {
                subAgentIds.insert(endpoint.id);
            }
        }

        if (subAgentIds.empty()) {
            return OperationResult{ false, false, "No sub-agents are currently available for grouping." };
        }

        const std::set<std::string> reservedTargetIds{
            "planner",
            "architect",
            "coding-specialists"
        };
        if (reservedTargetIds.contains(group.groupId) || subAgentIds.contains(group.groupId)) {
            return OperationResult{ false, false, "The requested sub-agent group ID conflicts with an existing orchestration target." };
        }

        std::vector<std::string> memberTargetIds;
        std::set<std::string> seenMembers;
        for (const auto& memberTargetId : group.memberTargetIds) {
            if (!subAgentIds.contains(memberTargetId) || seenMembers.contains(memberTargetId)) {
                continue;
            }
            seenMembers.insert(memberTargetId);
            memberTargetIds.push_back(memberTargetId);
        }

        if (memberTargetIds.empty()) {
            return OperationResult{ false, false, "Select at least one valid sub-agent for the group." };
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        auto& groups = state_->configuration.subAgentGroups;
        auto iterator = std::find_if(
            groups.begin(),
            groups.end(),
            [&group](const SubAgentGroupDefinition& candidate) { return candidate.groupId == group.groupId; });

        SubAgentGroupDefinition normalized = group;
        normalized.memberTargetIds = std::move(memberTargetIds);
        normalized.updatedAtUtc = timestampNowUtc();
        normalized.description = trimCopy(normalized.description);

        if (iterator == groups.end()) {
            groups.push_back(std::move(normalized));
        } else {
            *iterator = std::move(normalized);
        }

        std::sort(
            groups.begin(),
            groups.end(),
            [](const SubAgentGroupDefinition& left, const SubAgentGroupDefinition& right) {
                return std::tie(left.displayName, left.groupId) < std::tie(right.displayName, right.groupId);
            });
        writeJsonFile(configurationFile_, state_->configuration);
        return OperationResult{ true, false, "Sub-agent group settings updated." };
    }

    OperationResult removeGroup(const std::string& groupId) override {
        if (groupId.empty() || isBlank(groupId)) {
            return OperationResult{ false, false, "A sub-agent group ID is required." };
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        auto& groups = state_->configuration.subAgentGroups;
        const auto originalSize = groups.size();
        groups.erase(
            std::remove_if(
                groups.begin(),
                groups.end(),
                [&groupId](const SubAgentGroupDefinition& candidate) { return candidate.groupId == groupId; }),
            groups.end());

        if (groups.size() == originalSize) {
            return OperationResult{ false, false, "The requested sub-agent group was not found." };
        }

        auto& assignments = state_->configuration.providerAssignments;
        assignments.erase(
            std::remove_if(
                assignments.begin(),
                assignments.end(),
                [&groupId](const ProviderAssignment& assignment) {
                    return assignment.targetId == groupId || assignment.sourceGroupId == groupId;
                }),
            assignments.end());

        writeJsonFile(configurationFile_, state_->configuration);
        return OperationResult{ true, false, "Sub-agent group removed." };
    }

private:
    std::shared_ptr<SharedState> state_;
    std::filesystem::path configurationFile_;
    std::shared_ptr<IRuntimeInventoryService> inventoryService_;
};

class ProviderAssignmentService final : public IProviderAssignmentService {
public:
    ProviderAssignmentService(std::shared_ptr<SharedState> state,
                              std::filesystem::path configurationFile,
                              std::shared_ptr<IRuntimeInventoryService> inventoryService,
                              std::shared_ptr<ISubAgentGroupService> subAgentGroupService,
                              std::shared_ptr<IProviderRegistry> providerRegistry)
        : state_(std::move(state))
        , configurationFile_(std::move(configurationFile))
        , inventoryService_(std::move(inventoryService))
        , subAgentGroupService_(std::move(subAgentGroupService))
        , providerRegistry_(std::move(providerRegistry)) {}

    std::vector<ProviderAssignmentTarget> listTargets() const override {
        auto endpoints = inventoryService_->listEndpoints();
        std::set<std::string> availableSubAgentIds;
        std::vector<ProviderAssignmentTarget> targets{
            ProviderAssignmentTarget{
                "planner",
                ProviderAssignmentTargetKind::Role,
                "Planner",
                "Owns global planning and delivery sequencing.",
                {}
            },
            ProviderAssignmentTarget{
                "architect",
                ProviderAssignmentTargetKind::Role,
                "Architect",
                "Owns solution architecture and design decisions.",
                {}
            }
        };

        ProviderAssignmentTarget specialistGroup;
        specialistGroup.targetId = "coding-specialists";
        specialistGroup.kind = ProviderAssignmentTargetKind::SubAgentGroup;
        specialistGroup.displayName = "Coding Specialists";
        specialistGroup.description = "Assign one provider across the current sub-agent specialist pool.";

        for (const auto& endpoint : endpoints) {
            if (endpoint.kind != EndpointKind::SubAgent) {
                continue;
            }

            availableSubAgentIds.insert(endpoint.id);
            specialistGroup.memberTargetIds.push_back(endpoint.id);
            targets.push_back(ProviderAssignmentTarget{
                endpoint.id,
                ProviderAssignmentTargetKind::SubAgent,
                endpoint.displayName,
                endpoint.description.empty() ? "Sub-agent route" : endpoint.description,
                {}
            });
        }

        if (!specialistGroup.memberTargetIds.empty()) {
            targets.push_back(std::move(specialistGroup));
        }

        for (const auto& group : subAgentGroupService_->listGroups()) {
            ProviderAssignmentTarget customGroup;
            customGroup.targetId = group.groupId;
            customGroup.kind = ProviderAssignmentTargetKind::SubAgentGroup;
            customGroup.displayName = group.displayName;
            customGroup.description = group.description.empty()
                ? "Assign one provider across a named sub-agent specialist group."
                : group.description;

            for (const auto& memberTargetId : group.memberTargetIds) {
                if (availableSubAgentIds.contains(memberTargetId)) {
                    customGroup.memberTargetIds.push_back(memberTargetId);
                }
            }

            if (!customGroup.memberTargetIds.empty()) {
                targets.push_back(std::move(customGroup));
            }
        }

        std::sort(
            targets.begin(),
            targets.end(),
            [](const ProviderAssignmentTarget& left, const ProviderAssignmentTarget& right) {
                return std::tie(left.kind, left.displayName, left.targetId) < std::tie(right.kind, right.displayName, right.targetId);
            });
        return targets;
    }

    std::vector<ProviderAssignment> listAssignments() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->configuration.providerAssignments;
    }

    OperationResult upsertAssignment(const ProviderAssignment& assignment) override {
        const auto targets = listTargets();
        const auto targetIterator = std::find_if(
            targets.begin(),
            targets.end(),
            [&assignment](const ProviderAssignmentTarget& target) { return target.targetId == assignment.targetId; });
        if (targetIterator == targets.end()) {
            return OperationResult{ false, false, "The requested orchestration target is not available." };
        }

        if (!assignment.providerId.empty()) {
            const auto providers = providerRegistry_->listProviders();
            const auto providerIterator = std::find_if(
                providers.begin(),
                providers.end(),
                [&assignment](const ProviderConnection& provider) { return provider.id == assignment.providerId; });
            if (providerIterator == providers.end()) {
                return OperationResult{ false, false, "The selected provider route does not exist." };
            }
        }

        std::lock_guard<std::mutex> lock(state_->mutex);
        auto& assignments = state_->configuration.providerAssignments;

        auto clearTarget = [&assignments](const std::string& targetId) {
            assignments.erase(
                std::remove_if(
                    assignments.begin(),
                    assignments.end(),
                    [&targetId](const ProviderAssignment& candidate) { return candidate.targetId == targetId; }),
                assignments.end());
        };

        if (targetIterator->kind == ProviderAssignmentTargetKind::SubAgentGroup) {
            clearTarget(assignment.targetId);
            assignments.erase(
                std::remove_if(
                    assignments.begin(),
                    assignments.end(),
                    [&assignment](const ProviderAssignment& candidate) { return candidate.sourceGroupId == assignment.targetId; }),
                assignments.end());

            if (!assignment.providerId.empty()) {
                for (const auto& memberTargetId : targetIterator->memberTargetIds) {
                    clearTarget(memberTargetId);
                }

                assignments.push_back(ProviderAssignment{
                    assignment.targetId,
                    ProviderAssignmentTargetKind::SubAgentGroup,
                    assignment.providerId,
                    timestampNowUtc(),
                    ""
                });
                for (const auto& memberTargetId : targetIterator->memberTargetIds) {
                    assignments.push_back(ProviderAssignment{
                        memberTargetId,
                        ProviderAssignmentTargetKind::SubAgent,
                        assignment.providerId,
                        timestampNowUtc(),
                        assignment.targetId
                    });
                }
            }

            writeJsonFile(configurationFile_, state_->configuration);
            const std::string verb = assignment.providerId.empty() ? "Cleared" : "Assigned";
            return OperationResult{
                true,
                false,
                verb + " " + std::to_string(targetIterator->memberTargetIds.size()) + " sub-agent routes for " + targetIterator->displayName + "."
            };
        }

        clearTarget(assignment.targetId);
        if (!assignment.providerId.empty()) {
            assignments.push_back(ProviderAssignment{
                assignment.targetId,
                targetIterator->kind,
                assignment.providerId,
                timestampNowUtc(),
                ""
            });
        }

        writeJsonFile(configurationFile_, state_->configuration);
        return OperationResult{
            true,
            false,
            assignment.providerId.empty()
                ? "Provider ownership was cleared."
                : "Provider ownership was updated."
        };
    }

private:
    std::shared_ptr<SharedState> state_;
    std::filesystem::path configurationFile_;
    std::shared_ptr<IRuntimeInventoryService> inventoryService_;
    std::shared_ptr<ISubAgentGroupService> subAgentGroupService_;
    std::shared_ptr<IProviderRegistry> providerRegistry_;
};

class ProviderExecutionCatalogService final : public IProviderExecutionCatalogService {
public:
    std::vector<ProviderExecutionRegistration> listRegistrations() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ProviderExecutionRegistration> registrations;
        registrations.reserve(registrationsByKind_.size());
        for (const auto& [kind, registration] : registrationsByKind_) {
            (void)kind;
            registrations.push_back(registration);
        }

        std::sort(
            registrations.begin(),
            registrations.end(),
            [](const ProviderExecutionRegistration& left, const ProviderExecutionRegistration& right) {
                return std::tie(left.displayName, left.providerId) < std::tie(right.displayName, right.providerId);
            });
        return registrations;
    }

    void upsertRegistration(const ProviderExecutionRegistration& registration) override {
        std::lock_guard<std::mutex> lock(mutex_);
        registrationsByKind_[registration.kind] = registration;
    }

    void removeRegistration(const std::string& providerId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto iterator = registrationsByKind_.begin(); iterator != registrationsByKind_.end();) {
            if (iterator->second.providerId == providerId) {
                iterator = registrationsByKind_.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

private:
    mutable std::mutex mutex_;
    std::map<ProviderKind, ProviderExecutionRegistration> registrationsByKind_;
};

class ProviderExecutionService final : public IProviderExecutionService {
public:
    ProviderExecutionService(std::shared_ptr<SharedState> state,
                             std::shared_ptr<IProviderRegistry> providerRegistry,
                             std::shared_ptr<IProviderCredentialStore> providerCredentialStore,
                             std::shared_ptr<IProviderAssignmentService> providerAssignmentService,
                             std::shared_ptr<IRuntimeInventoryService> inventoryService,
                             std::shared_ptr<IProviderExecutionCatalogService> providerExecutionCatalogService)
        : state_(std::move(state))
        , providerRegistry_(std::move(providerRegistry))
        , providerCredentialStore_(std::move(providerCredentialStore))
        , providerAssignmentService_(std::move(providerAssignmentService))
        , inventoryService_(std::move(inventoryService))
        , providerExecutionCatalogService_(std::move(providerExecutionCatalogService)) {}

    std::vector<ProviderExecutionRecord> history() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->providerExecutionHistory;
    }

    ProviderExecutionRecord execute(const ProviderExecutionRequest& request) override {
        ProviderExecutionRecord record;
        record.executionId = generateExecutionId();
        record.targetId = request.targetId;
        record.status = ProviderExecutionStatus::Failed;
        record.startedAtUtc = timestampNowUtc();

        if (request.targetId.empty() || isBlank(request.targetId)) {
            record.errorMessage = "A provider execution target is required.";
            record.completedAtUtc = timestampNowUtc();
            persistRecord(record);
            return record;
        }
        if (request.prompt.empty() || isBlank(request.prompt)) {
            record.errorMessage = "A provider execution prompt is required.";
            record.completedAtUtc = timestampNowUtc();
            persistRecord(record);
            return record;
        }

        const auto targets = providerAssignmentService_->listTargets();
        const auto targetIterator = std::find_if(
            targets.begin(),
            targets.end(),
            [&request](const ProviderAssignmentTarget& target) { return target.targetId == request.targetId; });
        if (targetIterator == targets.end()) {
            record.errorMessage = "The requested orchestration target is not available.";
            record.completedAtUtc = timestampNowUtc();
            persistRecord(record);
            return record;
        }
        record.targetDisplayName = targetIterator->displayName;

        const auto assignments = providerAssignmentService_->listAssignments();
        const auto assignmentIterator = std::find_if(
            assignments.begin(),
            assignments.end(),
            [&request](const ProviderAssignment& assignment) { return assignment.targetId == request.targetId; });
        if (assignmentIterator == assignments.end() || assignmentIterator->providerId.empty()) {
            record.errorMessage = "No provider route owns the requested orchestration target.";
            record.completedAtUtc = timestampNowUtc();
            persistRecord(record);
            return record;
        }

        const auto providers = providerRegistry_->listProviders();
        const auto providerIterator = std::find_if(
            providers.begin(),
            providers.end(),
            [&assignmentIterator](const ProviderConnection& provider) { return provider.id == assignmentIterator->providerId; });
        if (providerIterator == providers.end()) {
            record.errorMessage = "The assigned provider route is no longer available.";
            record.completedAtUtc = timestampNowUtc();
            persistRecord(record);
            return record;
        }

        record.providerId = providerIterator->id;
        record.providerKind = providerIterator->kind;
        record.providerDisplayName = providerIterator->displayName;
        record.modelId = providerIterator->modelId;

        const auto registrations = providerExecutionCatalogService_->listRegistrations();
        const auto registrationIterator = std::find_if(
            registrations.begin(),
            registrations.end(),
            [providerKind = providerIterator->kind](const ProviderExecutionRegistration& registration) {
                return registration.kind == providerKind;
            });
        if (registrationIterator == registrations.end()) {
            record.errorMessage = "No active provider execution module is available for the assigned provider kind.";
            record.completedAtUtc = timestampNowUtc();
            persistRecord(record);
            return record;
        }

        const auto credentials = providerCredentialStore_->readCredentials(providerIterator->id);
        if (credentials.empty()) {
            record.errorMessage = "Provider credentials must be configured before execution can start.";
            record.completedAtUtc = timestampNowUtc();
            persistRecord(record);
            return record;
        }

        const auto accessibleEndpoints = resolveAccessibleMcpEndpoints(request);
        for (const auto& endpoint : accessibleEndpoints) {
            record.referencedMcpServerIds.push_back(endpoint.id);
        }

        record.status = ProviderExecutionStatus::Running;
        if (registrationIterator->transport == ProviderExecutionTransport::ClaudeCodeCli) {
            record = executeClaudeCodeCli(*providerIterator, request, accessibleEndpoints, credentials, record);
        } else {
            record = executeOpenAICompatibleChat(*providerIterator, request, accessibleEndpoints, credentials, record);
        }
        if (record.completedAtUtc.empty()) {
            record.completedAtUtc = timestampNowUtc();
        }
        persistRecord(record);
        return record;
    }

private:
    std::vector<RuntimeEndpoint> resolveAccessibleMcpEndpoints(const ProviderExecutionRequest& request) const {
        std::vector<RuntimeEndpoint> endpoints;
        for (const auto& endpoint : inventoryService_->listEndpoints()) {
            if (endpoint.kind != EndpointKind::MCPServer && endpoint.kind != EndpointKind::Gateway) {
                continue;
            }
            if (endpoint.host.empty() || endpoint.port == 0) {
                continue;
            }
            if (!request.preferredMcpServerIds.empty() &&
                std::find(request.preferredMcpServerIds.begin(), request.preferredMcpServerIds.end(), endpoint.id) ==
                    request.preferredMcpServerIds.end()) {
                continue;
            }
            endpoints.push_back(endpoint);
        }
        return endpoints;
    }

    static std::string firstCredentialValue(
        const std::map<std::string, std::string>& credentials,
        const std::initializer_list<const char*>& keys) {
        for (const char* key : keys) {
            const auto iterator = credentials.find(key);
            if (iterator != credentials.end() && !iterator->second.empty() && !isBlank(iterator->second)) {
                return iterator->second;
            }
        }
        return {};
    }

    static std::string normalizeBaseUrl(std::string baseUrl) {
        while (!baseUrl.empty() && baseUrl.back() == '/') {
            baseUrl.pop_back();
        }
        return baseUrl;
    }

    static std::string extractAssistantText(const nlohmann::json& message) {
        if (!message.contains("content")) {
            return {};
        }
        const auto& content = message.at("content");
        if (content.is_string()) {
            return content.get<std::string>();
        }
        if (!content.is_array()) {
            return {};
        }

        std::ostringstream stream;
        for (const auto& item : content) {
            if (!item.is_object()) {
                continue;
            }
            if (item.value("type", "") == "text") {
                if (item.contains("text")) {
                    if (item.at("text").is_string()) {
                        stream << item.at("text").get<std::string>();
                    } else if (item.at("text").is_object()) {
                        stream << item.at("text").value("value", "");
                    }
                }
            }
        }
        return trimCopy(stream.str());
    }

    std::string buildExecutionSystemPrompt(const ProviderConnection& provider,
                                           const ProviderExecutionRequest& request,
                                           const std::vector<RuntimeEndpoint>& endpoints,
                                           const bool useJsonRpcTools) const {
        std::ostringstream prompt;
        prompt << "You are operating inside Master Control Program as the provider assigned to target '" << request.targetId << "'.\n"
               << "Stay within the requested orchestration lane and do not assume ownership of other roles or sub-agents.\n"
               << "Assigned provider route: " << provider.displayName << " (" << provider.id << ").\n";
        if (!request.workingDirectory.empty()) {
            prompt << "Preferred working directory: " << request.workingDirectory << "\n";
        }
        if (endpoints.empty()) {
            prompt << "No shared MCP servers are currently published for this execution.\n";
        } else {
            prompt << "Shared MCP server access is available for these endpoints:\n";
            for (const auto& endpoint : endpoints) {
                prompt << "- " << endpoint.id << " | " << endpoint.displayName << " | " << buildEndpointUrl(endpoint) << "\n";
            }
        }
        if (useJsonRpcTools) {
            prompt << "Use the provided function tools to inspect shared MCP servers and invoke JSON-RPC requests against them when you need tool access.\n";
        } else {
            prompt << "Use the provided MCP configuration to access the shared MCP servers directly.\n";
        }
        return prompt.str();
    }

    static nlohmann::json buildClaudeMcpConfig(const std::vector<RuntimeEndpoint>& endpoints) {
        nlohmann::json servers = nlohmann::json::object();
        for (const auto& endpoint : endpoints) {
            servers[endpoint.id] = {
                { "type", "http" },
                { "url", buildEndpointUrl(endpoint) }
            };
        }
        return nlohmann::json{ { "mcpServers", servers } };
    }

    ProviderExecutionRecord executeOpenAICompatibleChat(const ProviderConnection& provider,
                                                        const ProviderExecutionRequest& request,
                                                        const std::vector<RuntimeEndpoint>& endpoints,
                                                        const std::map<std::string, std::string>& credentials,
                                                        ProviderExecutionRecord record) const {
        const std::string bearerToken = firstCredentialValue(credentials, { "openai_api_key", "xai_api_key" });
        if (bearerToken.empty()) {
            record.status = ProviderExecutionStatus::Failed;
            record.errorMessage = "A bearer API key is required for the selected provider route.";
            record.completedAtUtc = timestampNowUtc();
            return record;
        }

        nlohmann::json messages = nlohmann::json::array({
            {
                { "role", "system" },
                { "content", buildExecutionSystemPrompt(provider, request, endpoints, true) }
            },
            {
                { "role", "user" },
                { "content", request.prompt }
            }
        });

        const nlohmann::json tools = nlohmann::json::array({
            {
                { "type", "function" },
                { "function", {
                    { "name", "master_control_list_mcp_servers" },
                    { "description", "List the MCP server endpoints currently shared through Master Control Program for this execution." },
                    { "parameters", {
                        { "type", "object" },
                        { "properties", nlohmann::json::object() },
                        { "additionalProperties", false }
                    } }
                } }
            },
            {
                { "type", "function" },
                { "function", {
                    { "name", "master_control_invoke_mcp_jsonrpc" },
                    { "description", "Invoke a JSON-RPC request against a shared MCP server endpoint exposed by Master Control Program." },
                    { "parameters", {
                        { "type", "object" },
                        { "properties", {
                            { "server_id", { { "type", "string" }, { "description", "The MCP server id returned by master_control_list_mcp_servers." } } },
                            { "request", {
                                { "type", "object" },
                                { "description", "A complete JSON-RPC request body to send to the MCP server." }
                            } }
                        } },
                        { "required", nlohmann::json::array({ "server_id", "request" }) },
                        { "additionalProperties", false }
                    } }
                } }
            }
        });

        const std::string endpointUrl = normalizeBaseUrl(provider.baseUrl) + "/chat/completions";
        const int maxTurns = (std::max)(1, request.maxTurns);
        for (int turn = 0; turn < maxTurns; ++turn) {
            nlohmann::json requestBody = {
                { "model", provider.modelId },
                { "messages", messages }
            };
            if (request.allowToolAccess) {
                requestBody["tools"] = tools;
                requestBody["tool_choice"] = "auto";
            }

            const auto response = sendJsonRequest(
                "POST",
                endpointUrl,
                { { L"Authorization", L"Bearer " + wideFromUtf8(bearerToken) } },
                requestBody.dump());
            record.rawResponse = response.body;
            if (!response.succeeded) {
                record.status = ProviderExecutionStatus::Failed;
                record.errorMessage = response.errorMessage.empty() ? response.body : response.errorMessage;
                record.completedAtUtc = timestampNowUtc();
                return record;
            }

            nlohmann::json payload;
            try {
                payload = nlohmann::json::parse(response.body);
            } catch (...) {
                record.status = ProviderExecutionStatus::Failed;
                record.errorMessage = "The provider returned an unreadable JSON payload.";
                record.completedAtUtc = timestampNowUtc();
                return record;
            }

            const auto* choices = payload.contains("choices") && payload.at("choices").is_array() && !payload.at("choices").empty()
                ? &payload.at("choices")
                : nullptr;
            if (choices == nullptr || !choices->front().contains("message")) {
                record.status = ProviderExecutionStatus::Failed;
                record.errorMessage = "The provider response did not include a completion message.";
                record.completedAtUtc = timestampNowUtc();
                return record;
            }

            const auto assistantMessage = choices->front().at("message");
            messages.push_back(assistantMessage);
            if (request.allowToolAccess &&
                assistantMessage.contains("tool_calls") &&
                assistantMessage.at("tool_calls").is_array() &&
                !assistantMessage.at("tool_calls").empty()) {
                for (const auto& toolCall : assistantMessage.at("tool_calls")) {
                    const std::string toolName = toolCall.contains("function")
                        ? toolCall.at("function").value("name", "")
                        : std::string{};
                    std::string toolResponse = R"({"ok":false,"message":"Unsupported tool call."})";
                    if (toolName == "master_control_list_mcp_servers") {
                        nlohmann::json servers = nlohmann::json::array();
                        for (const auto& endpoint : endpoints) {
                            servers.push_back({
                                { "id", endpoint.id },
                                { "displayName", endpoint.displayName },
                                { "url", buildEndpointUrl(endpoint) }
                            });
                        }
                        toolResponse = nlohmann::json{ { "servers", servers } }.dump();
                    } else if (toolName == "master_control_invoke_mcp_jsonrpc") {
                        try {
                            const auto arguments = nlohmann::json::parse(
                                toolCall.at("function").value("arguments", "{}"));
                            const std::string serverId = arguments.value("server_id", "");
                            const auto endpointIterator = std::find_if(
                                endpoints.begin(),
                                endpoints.end(),
                                [&serverId](const RuntimeEndpoint& endpoint) { return endpoint.id == serverId; });
                            if (endpointIterator == endpoints.end()) {
                                toolResponse = R"({"ok":false,"message":"Requested MCP server is not available for this execution."})";
                            } else {
                                const auto proxyResponse = sendJsonRequest(
                                    "POST",
                                    buildEndpointUrl(*endpointIterator),
                                    {},
                                    arguments.at("request").dump());
                                if (proxyResponse.succeeded) {
                                    nlohmann::json proxyPayload = {
                                        { "ok", true },
                                        { "statusCode", proxyResponse.statusCode }
                                    };
                                    try {
                                        proxyPayload["body"] = nlohmann::json::parse(proxyResponse.body);
                                    } catch (...) {
                                        proxyPayload["body"] = proxyResponse.body;
                                    }
                                    toolResponse = proxyPayload.dump();
                                } else {
                                    toolResponse = nlohmann::json{
                                        { "ok", false },
                                        { "statusCode", proxyResponse.statusCode },
                                        { "message", proxyResponse.errorMessage.empty() ? proxyResponse.body : proxyResponse.errorMessage }
                                    }.dump();
                                }
                            }
                        } catch (...) {
                            toolResponse = R"({"ok":false,"message":"Tool call arguments were invalid JSON."})";
                        }
                    }

                    record.toolEvents.push_back(toolName);
                    messages.push_back({
                        { "role", "tool" },
                        { "tool_call_id", toolCall.value("id", "") },
                        { "content", toolResponse }
                    });
                }
                continue;
            }

            record.outputText = extractAssistantText(assistantMessage);
            if (record.outputText.empty()) {
                record.outputText = payload.dump(2);
            }
            record.status = ProviderExecutionStatus::Succeeded;
            record.completedAtUtc = timestampNowUtc();
            return record;
        }

        record.status = ProviderExecutionStatus::Failed;
        record.errorMessage = "The provider execution exhausted its turn budget before returning a final answer.";
        record.completedAtUtc = timestampNowUtc();
        return record;
    }

    ProviderExecutionRecord executeClaudeCodeCli(const ProviderConnection& provider,
                                                 const ProviderExecutionRequest& request,
                                                 const std::vector<RuntimeEndpoint>& endpoints,
                                                 const std::map<std::string, std::string>& credentials,
                                                 ProviderExecutionRecord record) const {
        std::optional<std::filesystem::path> claudeCommand;
        const DWORD configuredCommandLength = GetEnvironmentVariableW(L"MASTERCONTROL_CLAUDE_COMMAND", nullptr, 0);
        if (configuredCommandLength > 0) {
            std::wstring configured(static_cast<size_t>(configuredCommandLength - 1), L'\0');
            GetEnvironmentVariableW(L"MASTERCONTROL_CLAUDE_COMMAND", configured.data(), configuredCommandLength);
            claudeCommand = std::filesystem::path(configured);
        } else {
            claudeCommand = findCommandOnPath({ L"claude.exe", L"claude.cmd", L"claude.bat" });
        }
        if (!claudeCommand.has_value()) {
            record.status = ProviderExecutionStatus::Failed;
            record.errorMessage = "Claude Code was not found on PATH.";
            record.completedAtUtc = timestampNowUtc();
            return record;
        }

        const auto executionDirectory = std::filesystem::temp_directory_path() / "MasterControlProgram" / "provider-executions" / sanitizePathComponent(record.executionId);
        std::filesystem::create_directories(executionDirectory);
        const auto systemPromptFile = executionDirectory / "system-prompt.txt";
        {
            std::ofstream stream(systemPromptFile, std::ios::trunc);
            stream << buildExecutionSystemPrompt(provider, request, endpoints, false);
        }

        std::filesystem::path mcpConfigFile;
        if (request.allowToolAccess && !endpoints.empty()) {
            mcpConfigFile = executionDirectory / "mcp.json";
            writeJsonFile(mcpConfigFile, buildClaudeMcpConfig(endpoints));
        }

        std::vector<std::wstring> arguments{
            claudeCommand->wstring(),
            L"-p",
            wideFromUtf8(request.prompt),
            L"--output-format",
            L"json",
            L"--max-turns",
            std::to_wstring((std::max)(1, request.maxTurns)),
            L"--append-system-prompt-file",
            systemPromptFile.wstring()
        };
        if (!provider.modelId.empty()) {
            arguments.push_back(L"--model");
            arguments.push_back(wideFromUtf8(provider.modelId));
        }
        if (!mcpConfigFile.empty()) {
            arguments.push_back(L"--mcp-config");
            arguments.push_back(mcpConfigFile.wstring());
            arguments.push_back(L"--strict-mcp-config");
            arguments.push_back(L"--dangerously-skip-permissions");
            record.toolEvents.push_back("claude_code_cli_mcp");
        }

        std::wstring commandLine;
        if (startsWithInsensitive(claudeCommand->extension().string(), ".cmd") ||
            startsWithInsensitive(claudeCommand->extension().string(), ".bat")) {
            commandLine = L"cmd.exe /d /s /c ";
            commandLine += quoteWindowsArgument(joinCommandArguments(arguments));
        } else {
            commandLine = joinCommandArguments(arguments);
        }

        std::vector<std::pair<std::wstring, std::wstring>> environmentOverrides;
        const std::string apiKey = firstCredentialValue(credentials, { "anthropic_api_key" });
        const std::string authToken = firstCredentialValue(credentials, { "anthropic_auth_token" });
        if (!apiKey.empty()) {
            environmentOverrides.emplace_back(L"ANTHROPIC_API_KEY", wideFromUtf8(apiKey));
        }
        if (!authToken.empty()) {
            environmentOverrides.emplace_back(L"ANTHROPIC_AUTH_TOKEN", wideFromUtf8(authToken));
        }
        if (!provider.baseUrl.empty()) {
            environmentOverrides.emplace_back(L"ANTHROPIC_BASE_URL", wideFromUtf8(provider.baseUrl));
        }
        if (!provider.modelId.empty()) {
            environmentOverrides.emplace_back(L"ANTHROPIC_MODEL", wideFromUtf8(provider.modelId));
        }

        const auto workingDirectory = request.workingDirectory.empty()
            ? std::filesystem::current_path()
            : std::filesystem::path(wideFromUtf8(request.workingDirectory));
        const auto process = runProcessCapture(commandLine, workingDirectory, environmentOverrides);
        record.rawResponse = process.stdoutText.empty() ? process.stderrText : process.stdoutText;
        if (!process.launched) {
            record.status = ProviderExecutionStatus::Failed;
            record.errorMessage = "Claude Code could not be launched.";
            record.completedAtUtc = timestampNowUtc();
            return record;
        }
        if (process.exitCode != 0) {
            record.status = ProviderExecutionStatus::Failed;
            record.errorMessage = trimCopy(process.stderrText.empty() ? process.stdoutText : process.stderrText);
            if (record.errorMessage.empty()) {
                record.errorMessage = "Claude Code returned a non-zero exit code.";
            }
            record.completedAtUtc = timestampNowUtc();
            return record;
        }

        const std::string stdoutText = trimCopy(process.stdoutText);
        try {
            const auto payload = nlohmann::json::parse(stdoutText);
            if (payload.contains("result") && payload.at("result").is_string()) {
                record.outputText = payload.at("result").get<std::string>();
            } else if (payload.contains("content") && payload.at("content").is_string()) {
                record.outputText = payload.at("content").get<std::string>();
            } else if (payload.contains("message") && payload.at("message").is_string()) {
                record.outputText = payload.at("message").get<std::string>();
            } else {
                record.outputText = stdoutText;
            }
        } catch (...) {
            record.outputText = stdoutText;
        }
        if (record.outputText.empty()) {
            record.outputText = trimCopy(process.stderrText);
        }
        record.status = ProviderExecutionStatus::Succeeded;
        record.completedAtUtc = timestampNowUtc();
        return record;
    }

    void persistRecord(const ProviderExecutionRecord& record) const {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->providerExecutionHistory.insert(state_->providerExecutionHistory.begin(), record);
        if (state_->providerExecutionHistory.size() > 24) {
            state_->providerExecutionHistory.resize(24);
        }
    }

    std::shared_ptr<SharedState> state_;
    std::shared_ptr<IProviderRegistry> providerRegistry_;
    std::shared_ptr<IProviderCredentialStore> providerCredentialStore_;
    std::shared_ptr<IProviderAssignmentService> providerAssignmentService_;
    std::shared_ptr<IRuntimeInventoryService> inventoryService_;
    std::shared_ptr<IProviderExecutionCatalogService> providerExecutionCatalogService_;
};

class InstallerOrchestrator final : public IInstallerOrchestrator, public IBootstrapRepoService, public IZipBundleService {
public:
    InstallerOrchestrator(std::shared_ptr<SharedState> state,
                          std::shared_ptr<IPackageTrustEvaluator> trustEvaluator,
                          AppPaths paths)
        : state_(std::move(state))
        , trustEvaluator_(std::move(trustEvaluator))
        , paths_(std::move(paths)) {
        if (std::filesystem::exists(paths_.installHistoryFile)) {
            const auto json = readJsonFile(paths_.installHistoryFile);
            if (!json.is_null() && !json.empty()) {
                state_->installHistory = json.get<std::vector<InstallProvenance>>();
            }
        }
    }

    OperationResult installPackage(const InstallerPackageSpec& spec) override;
    OperationResult installFromRepository(const BootstrapRepoSpec& spec) override;
    OperationResult installFromZipBundle(const ZipBundleSpec& spec) override;

    std::vector<InstallProvenance> history() const override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->installHistory;
    }

private:
    std::optional<ValidatedBootstrapManifest> loadManifestContract(const std::filesystem::path& manifestPath,
                                                                   const std::filesystem::path& rootDirectory,
                                                                   std::string& errorMessage) const;
    void applyManifestRegistration(const BootstrapManifestContract& contract);
    std::filesystem::path resolvePackage(const std::string& source, const std::string& localPath) const;
    int executePackage(const std::filesystem::path& payloadPath,
                       InstallerKind kind,
                       const std::string& arguments) const;
    int executeBootstrap(const std::filesystem::path& bootstrapPath,
                         const std::string& arguments,
                         const std::filesystem::path& workingDirectory) const;
    static int executeCommand(const std::wstring& command, const std::filesystem::path& workingDirectory);
    void recordHistory(const InstallProvenance& provenance);

    std::shared_ptr<SharedState> state_;
    std::shared_ptr<IPackageTrustEvaluator> trustEvaluator_;
    AppPaths paths_;
};

OperationResult InstallerOrchestrator::installPackage(const InstallerPackageSpec& spec) {
    const auto decision = trustEvaluator_->evaluate(
        !spec.source.empty() ? spec.source : spec.localPath,
        spec.allowUntrustedExecution);
    if (decision.requiresExplicitApproval) {
        return OperationResult{ false, false, decision.reason };
    }

    const auto localPath = resolvePackage(spec.source, spec.localPath);
    if (localPath.empty() || !std::filesystem::exists(localPath)) {
        return OperationResult{ false, false, "Installer payload could not be resolved." };
    }

    const int exitCode = executePackage(localPath, spec.kind, spec.arguments);
    recordHistory(InstallProvenance{
        spec.kind,
        !spec.source.empty() ? spec.source : spec.localPath,
        timestampNowUtc(),
        "1.0.0",
        decision.isTrusted,
        "Process exited with code " + std::to_string(exitCode)
    });

    return OperationResult{
        exitCode == 0,
        false,
        exitCode == 0 ? "Installer completed successfully." : "Installer failed."
    };
}

OperationResult InstallerOrchestrator::installFromRepository(const BootstrapRepoSpec& spec) {
    const auto decision = trustEvaluator_->evaluate(spec.repositoryUrl, spec.allowUntrustedExecution);
    if (decision.requiresExplicitApproval) {
        return OperationResult{ false, false, decision.reason };
    }

    const auto repoDirectory = paths_.workDirectory / ("repo_" + sanitizePathComponent(spec.branch) + "_" + sanitizePathComponent(timestampNowUtc()));
    std::filesystem::create_directories(repoDirectory.parent_path());

    const std::wstring command = L"git clone --depth 1 --branch \"" +
        wideFromUtf8(spec.branch) + L"\" \"" + wideFromUtf8(spec.repositoryUrl) +
        L"\" \"" + repoDirectory.wstring() + L"\"";

    const int cloneExitCode = executeCommand(command, paths_.workDirectory);
    if (cloneExitCode != 0) {
        return OperationResult{ false, false, "Failed to clone bootstrap repository." };
    }

    const auto manifestPath = repoDirectory / spec.manifestFile;
    std::string manifestError;
    const auto manifest = loadManifestContract(manifestPath, repoDirectory, manifestError);
    if (!manifest.has_value()) {
        return OperationResult{ false, false, manifestError };
    }

    const int bootstrapExitCode = executeBootstrap(
        manifest->bootstrapPath,
        manifest->contract.bootstrapArguments,
        manifest->bootstrapPath.parent_path());
    if (bootstrapExitCode == 0) {
        applyManifestRegistration(manifest->contract);
    }

    const auto registeredEndpoints = manifest->contract.seededEndpoints.size();
    const auto registeredProviders = manifest->contract.providers.size();
    std::ostringstream summary;
    summary << "Bootstrap exited with code " << bootstrapExitCode;
    if (bootstrapExitCode == 0 && (registeredEndpoints > 0U || registeredProviders > 0U)) {
        summary << "; registered " << registeredEndpoints << " endpoints and " << registeredProviders << " providers";
    }

    recordHistory(InstallProvenance{
        InstallerKind::GitBootstrapRepo,
        spec.repositoryUrl,
        timestampNowUtc(),
        manifest->contract.version,
        decision.isTrusted,
        summary.str()
    });

    std::ostringstream message;
    message << (bootstrapExitCode == 0 ? "Bootstrap repository installed." : "Bootstrap repository failed.");
    if (bootstrapExitCode == 0 && (registeredEndpoints > 0U || registeredProviders > 0U)) {
        message << " Registered " << registeredEndpoints << " endpoints and " << registeredProviders << " providers.";
    }

    return OperationResult{
        bootstrapExitCode == 0,
        false,
        message.str()
    };
}

OperationResult InstallerOrchestrator::installFromZipBundle(const ZipBundleSpec& spec) {
    const auto decision = trustEvaluator_->evaluate(spec.source, spec.allowUntrustedExecution);
    if (decision.requiresExplicitApproval) {
        return OperationResult{ false, false, decision.reason };
    }

    const auto zipPath = resolvePackage(spec.source, "");
    if (zipPath.empty() || !std::filesystem::exists(zipPath)) {
        return OperationResult{ false, false, "Zip bundle could not be resolved." };
    }

    const auto extractDirectory = paths_.workDirectory / ("zip_" + sanitizePathComponent(timestampNowUtc()));
    std::filesystem::create_directories(extractDirectory);

    const std::wstring extractCommand =
        L"pwsh -NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -Path '" +
        zipPath.wstring() + L"' -DestinationPath '" + extractDirectory.wstring() + L"' -Force\"";

    const int extractExitCode = executeCommand(extractCommand, paths_.workDirectory);
    if (extractExitCode != 0) {
        return OperationResult{ false, false, "Zip bundle extraction failed." };
    }

    const auto manifestPath = extractDirectory / spec.manifestFile;
    std::string manifestError;
    const auto manifest = loadManifestContract(manifestPath, extractDirectory, manifestError);
    if (!manifest.has_value()) {
        return OperationResult{ false, false, manifestError };
    }

    const int bootstrapExitCode = executeBootstrap(
        manifest->bootstrapPath,
        manifest->contract.bootstrapArguments,
        manifest->bootstrapPath.parent_path());
    if (bootstrapExitCode == 0) {
        applyManifestRegistration(manifest->contract);
    }

    const auto registeredEndpoints = manifest->contract.seededEndpoints.size();
    const auto registeredProviders = manifest->contract.providers.size();
    std::ostringstream summary;
    summary << "Zip bootstrap exited with code " << bootstrapExitCode;
    if (bootstrapExitCode == 0 && (registeredEndpoints > 0U || registeredProviders > 0U)) {
        summary << "; registered " << registeredEndpoints << " endpoints and " << registeredProviders << " providers";
    }

    recordHistory(InstallProvenance{
        InstallerKind::ZipBundle,
        spec.source,
        timestampNowUtc(),
        manifest->contract.version,
        decision.isTrusted,
        summary.str()
    });

    std::ostringstream message;
    message << (bootstrapExitCode == 0 ? "Zip bundle installed." : "Zip bundle bootstrap failed.");
    if (bootstrapExitCode == 0 && (registeredEndpoints > 0U || registeredProviders > 0U)) {
        message << " Registered " << registeredEndpoints << " endpoints and " << registeredProviders << " providers.";
    }

    return OperationResult{
        bootstrapExitCode == 0,
        false,
        message.str()
    };
}

std::optional<ValidatedBootstrapManifest> InstallerOrchestrator::loadManifestContract(
    const std::filesystem::path& manifestPath,
    const std::filesystem::path& rootDirectory,
    std::string& errorMessage) const {
    if (!std::filesystem::exists(manifestPath)) {
        errorMessage = "Bootstrap manifest is missing.";
        return std::nullopt;
    }

    try {
        const auto manifestJson = readJsonFile(manifestPath);
        if (!manifestJson.is_object()) {
            errorMessage = "Bootstrap manifest must be a JSON object.";
            return std::nullopt;
        }

        const auto contract = manifestJson.get<BootstrapManifestContract>();
        if (contract.bootstrapScript.empty()) {
            errorMessage = "Bootstrap manifest must declare bootstrapScript.";
            return std::nullopt;
        }

        const auto bootstrapPath = std::filesystem::weakly_canonical(rootDirectory / contract.bootstrapScript);
        const auto canonicalRoot = std::filesystem::weakly_canonical(rootDirectory);
        if (!pathIsWithinRoot(bootstrapPath, canonicalRoot)) {
            errorMessage = "Bootstrap script must stay within the imported package root.";
            return std::nullopt;
        }

        if (!std::filesystem::exists(bootstrapPath) || !std::filesystem::is_regular_file(bootstrapPath)) {
            errorMessage = "Bootstrap script declared by the manifest was not found.";
            return std::nullopt;
        }

        return ValidatedBootstrapManifest{ contract, bootstrapPath };
    } catch (const std::exception& exception) {
        errorMessage = std::string("Bootstrap manifest could not be parsed: ") + exception.what();
        return std::nullopt;
    }
}

void InstallerOrchestrator::applyManifestRegistration(const BootstrapManifestContract& contract) {
    std::lock_guard<std::mutex> lock(state_->mutex);

    auto& endpoints = state_->configuration.activeProfile.seededEndpoints;
    auto& providers = state_->configuration.providers;
    const auto preferredHost = state_->configuration.activeProfile.preferredBindAddress.empty()
        ? std::string("127.0.0.1")
        : state_->configuration.activeProfile.preferredBindAddress;

    for (auto endpoint : contract.seededEndpoints) {
        if (endpoint.id.empty()) {
            continue;
        }
        if (endpoint.displayName.empty()) {
            endpoint.displayName = endpoint.id;
        }
        if (endpoint.host.empty()) {
            endpoint.host = preferredHost;
        }
        if (endpoint.protocol.empty()) {
            endpoint.protocol = "http";
        }
        if (endpoint.routePath.empty()) {
            endpoint.routePath = "/";
        }

        const auto endpointIterator = std::find_if(
            endpoints.begin(),
            endpoints.end(),
            [&endpoint](const RuntimeEndpoint& candidate) { return candidate.id == endpoint.id; });
        if (endpointIterator == endpoints.end()) {
            endpoints.push_back(std::move(endpoint));
        } else {
            *endpointIterator = std::move(endpoint);
        }
    }

    for (const auto& provider : contract.providers) {
        if (provider.id.empty()) {
            continue;
        }

        const auto providerIterator = std::find_if(
            providers.begin(),
            providers.end(),
            [&provider](const ProviderConnection& candidate) { return candidate.id == provider.id; });
        if (providerIterator == providers.end()) {
            providers.push_back(provider);
        } else {
            *providerIterator = provider;
        }
    }

    writeJsonFile(paths_.configurationFile, state_->configuration);
}

std::filesystem::path InstallerOrchestrator::resolvePackage(const std::string& source, const std::string& localPath) const {
    if (!localPath.empty()) {
        return std::filesystem::path(localPath);
    }

    if (source.empty()) {
        return {};
    }

    if (!isRemoteSource(source)) {
        return std::filesystem::path(source);
    }

    const auto destination = paths_.workDirectory / (sanitizePathComponent(extractHostFromUrl(source)) + "_" + sanitizePathComponent(timestampNowUtc()));
    const auto destinationWithExtension = destination.string() + ".payload";
    const HRESULT result = URLDownloadToFileW(
        nullptr,
        wideFromUtf8(source).c_str(),
        wideFromUtf8(destinationWithExtension).c_str(),
        0,
        nullptr);

    if (FAILED(result)) {
        return {};
    }

    return std::filesystem::path(destinationWithExtension);
}

int InstallerOrchestrator::executePackage(const std::filesystem::path& payloadPath,
                                          const InstallerKind kind,
                                          const std::string& arguments) const {
    std::wstring command;
    switch (kind) {
        case InstallerKind::Msi:
            command = L"msiexec.exe /i \"" + payloadPath.wstring() + L"\" /passive " + wideFromUtf8(arguments);
            break;
        case InstallerKind::Exe:
            command = L"\"" + payloadPath.wstring() + L"\" " + wideFromUtf8(arguments);
            break;
        case InstallerKind::PowerShell:
            command = L"pwsh -NoProfile -ExecutionPolicy Bypass -File \"" + payloadPath.wstring() + L"\" " + wideFromUtf8(arguments);
            break;
        default:
            return 1;
    }

    return executeCommand(command, payloadPath.parent_path());
}

int InstallerOrchestrator::executeBootstrap(const std::filesystem::path& bootstrapPath,
                                            const std::string& arguments,
                                            const std::filesystem::path& workingDirectory) const {
    if (!std::filesystem::exists(bootstrapPath)) {
        return 1;
    }

    std::wstring command;
    if (bootstrapPath.extension() == ".ps1") {
        command = L"pwsh -NoProfile -ExecutionPolicy Bypass -File \"" + bootstrapPath.wstring() + L"\" " + wideFromUtf8(arguments);
    } else {
        command = L"\"" + bootstrapPath.wstring() + L"\" " + wideFromUtf8(arguments);
    }

    return executeCommand(command, workingDirectory);
}

int InstallerOrchestrator::executeCommand(const std::wstring& command, const std::filesystem::path& workingDirectory) {
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInformation{};
    std::wstring mutableCommand = command;

    if (CreateProcessW(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            workingDirectory.empty() ? nullptr : workingDirectory.wstring().c_str(),
            &startupInfo,
            &processInformation) == 0) {
        return static_cast<int>(GetLastError());
    }

    WaitForSingleObject(processInformation.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(processInformation.hProcess, &exitCode);
    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);
    return static_cast<int>(exitCode);
}

void InstallerOrchestrator::recordHistory(const InstallProvenance& provenance) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->installHistory.push_back(provenance);
    writeJsonFile(paths_.installHistoryFile, state_->installHistory);
}

class ExportService final : public IExportService {
public:
    ExportService(std::shared_ptr<IRuntimeInventoryService> inventoryService,
                  std::shared_ptr<IConfigurationService> configurationService)
        : inventoryService_(std::move(inventoryService))
        , configurationService_(std::move(configurationService)) {}

    std::vector<ExportArtifact> generateExports() const override {
        const auto endpoints = inventoryService_->listEndpoints();
        const auto configuration = configurationService_->current();
        const auto iterator = std::find_if(
            endpoints.begin(),
            endpoints.end(),
            [](const RuntimeEndpoint& endpoint) {
                return endpoint.id == "aggregator-gateway" || endpoint.kind == EndpointKind::Gateway;
            });
        const auto browserIterator = std::find_if(
            endpoints.begin(),
            endpoints.end(),
            [](const RuntimeEndpoint& endpoint) {
                return endpoint.id == "browser-gateway" || endpoint.kind == EndpointKind::BrowserGateway;
            });

        const auto preferredHost = configuration.activeProfile.preferredBindAddress.empty()
            ? std::string("127.0.0.1")
            : configuration.activeProfile.preferredBindAddress;
        const auto gatewayHost = iterator != endpoints.end() && iterator->host != "0.0.0.0" ? iterator->host : preferredHost;
        const auto gatewayPort = iterator != endpoints.end() ? iterator->port : static_cast<uint16_t>(7200);
        const auto gatewayUrl = "http://" + gatewayHost + ":" + std::to_string(gatewayPort) + "/mcp/gateway";
        const auto browserHost = browserIterator != endpoints.end() && browserIterator->host != "0.0.0.0"
            ? browserIterator->host
            : preferredHost;
        const auto browserPort = browserIterator != endpoints.end() ? browserIterator->port : configuration.browserPort;
        const auto browserUrl = "http://" + browserHost + ":" + std::to_string(browserPort) + "/";

        nlohmann::json claudeJson = {
            { "mcpServers", {
                { "master-control-gateway", {
                    { "type", "http" },
                    { "url", gatewayUrl }
                } }
            } }
        };

        nlohmann::json codexJson = {
            { "name", "master-control-gateway" },
            { "transport", "http" },
            { "url", gatewayUrl }
        };

        nlohmann::json openAiJson = {
            { "gateway", gatewayUrl },
            { "provider", "openai" },
            { "recommendedModel", "gpt-5" }
        };

        nlohmann::json xAiJson = {
            { "gateway", gatewayUrl },
            { "provider", "xai" },
            { "recommendedModel", "grok-code" }
        };

        nlohmann::json gatewayProfile = {
            { "instanceName", configuration.instanceName },
            { "environmentName", configuration.activeProfile.environmentName },
            { "browserUrl", browserUrl },
            { "gatewayUrl", gatewayUrl },
            { "beaconPort", configuration.beaconPort },
            { "exportedAtUtc", timestampNowUtc() },
            { "endpoints", endpoints }
        };

        std::ostringstream claudeScript;
        claudeScript
            << "param(\n"
            << "  [string]$ConfigPath = (Join-Path $HOME '.claude.json')\n"
            << ")\n\n"
            << "$gatewayUrl = '" << gatewayUrl << "'\n"
            << "$config = @{}\n"
            << "if (Test-Path $ConfigPath) {\n"
            << "  $config = Get-Content $ConfigPath -Raw | ConvertFrom-Json -AsHashtable\n"
            << "}\n"
            << "if (-not $config) { $config = @{} }\n"
            << "if (-not $config.ContainsKey('mcpServers')) { $config['mcpServers'] = @{} }\n"
            << "$config['mcpServers']['master-control-gateway'] = @{ type = 'http'; url = $gatewayUrl }\n"
            << "$config | ConvertTo-Json -Depth 8 | Set-Content -Path $ConfigPath -Encoding UTF8\n"
            << "Write-Host \"Updated Claude Code MCP config at $ConfigPath\"\n";

        std::ostringstream codexScript;
        codexScript
            << "param(\n"
            << "  [string]$OutputPath = (Join-Path $PWD 'codex-mcp.json')\n"
            << ")\n\n"
            << "$payload = @'\n"
            << codexJson.dump(2) << '\n'
            << "'@\n"
            << "$payload | Set-Content -Path $OutputPath -Encoding UTF8\n"
            << "Write-Host \"Wrote Codex MCP config to $OutputPath\"\n";

        return {
            ExportArtifact{ "gateway-profile", "master-control-gateway-profile.json", "application/json", gatewayProfile.dump(2) },
            ExportArtifact{ "claude", ".claude.json", "application/json", claudeJson.dump(2) },
            ExportArtifact{ "claude-installer", "Install-ClaudeGateway.ps1", "text/plain", claudeScript.str() },
            ExportArtifact{ "codex", "codex-mcp.json", "application/json", codexJson.dump(2) },
            ExportArtifact{ "codex-installer", "Install-CodexGateway.ps1", "text/plain", codexScript.str() },
            ExportArtifact{ "openai", "openai-gateway.json", "application/json", openAiJson.dump(2) },
            ExportArtifact{ "xai", "xai-gateway.json", "application/json", xAiJson.dump(2) }
        };
    }

private:
    std::shared_ptr<IRuntimeInventoryService> inventoryService_;
    std::shared_ptr<IConfigurationService> configurationService_;
};

class CommandLogicUnitService final : public ICommandLogicUnitService {
public:
    CommandLogicUnitService(std::filesystem::path profileFile,
                            std::shared_ptr<IConfigurationService> configurationService,
                            std::shared_ptr<IProviderRegistry> providerRegistry,
                            std::shared_ptr<IProviderAssignmentService> providerAssignmentService,
                            std::shared_ptr<IInstallerOrchestrator> installerOrchestrator,
                            std::shared_ptr<IExportService> exportService,
                            std::shared_ptr<IPlatformServiceCatalogService> platformServiceCatalogService,
                            std::shared_ptr<IPlatformGovernanceToolService> platformGovernanceToolService)
        : profile_(loadProfile(std::move(profileFile)))
        , configurationService_(std::move(configurationService))
        , providerRegistry_(std::move(providerRegistry))
        , providerAssignmentService_(std::move(providerAssignmentService))
        , installerOrchestrator_(std::move(installerOrchestrator))
        , exportService_(std::move(exportService))
        , platformServiceCatalogService_(std::move(platformServiceCatalogService))
        , platformGovernanceToolService_(std::move(platformGovernanceToolService)) {}

    GovernanceSnapshot currentGovernance() const override {
        GovernanceSnapshot snapshot;
        snapshot.unitName = profile_.unitName.empty() ? std::string("Command Logic Unit") : profile_.unitName;
        snapshot.posture = "pass";
        snapshot.doctrine = profile_.doctrine;
        snapshot.lastEvaluatedUtc = timestampNowUtc();
        snapshot.documents = profile_.documents;
        snapshot.roles = profile_.roles;
        snapshot.rules = profile_.rules;
        snapshot.operatorChecklist = profile_.operatorChecklist;

        const auto configuration = configurationService_->current();
        const auto providers = providerRegistry_->listProviders();
        const auto assignments = providerAssignmentService_->listAssignments();
        const auto installHistory = installerOrchestrator_->history();
        const auto exports = exportService_->generateExports();
        snapshot.platformGateways = platformServiceCatalogService_ ? platformServiceCatalogService_->listGateways() : std::vector<PlatformGatewayDescriptor>{};
        snapshot.governanceServers = platformServiceCatalogService_
            ? platformServiceCatalogService_->listGovernanceServers()
            : std::vector<GovernanceServerDescriptor>{};
        snapshot.availableTools = platformGovernanceToolService_
            ? platformGovernanceToolService_->listTools()
            : std::vector<GovernanceToolDescriptor>{};
        snapshot.recentExecutions = platformGovernanceToolService_
            ? platformGovernanceToolService_->recentExecutions()
            : std::vector<GovernanceToolResult>{};

        auto appendFinding = [&](const std::string& ruleId,
                                 const std::string& status,
                                 const std::string& message,
                                 std::optional<std::string> severityOverride = std::nullopt) {
            GovernanceFinding finding;
            if (const auto* rule = findRule(ruleId); rule != nullptr) {
                finding.ruleId = rule->ruleId;
                finding.title = rule->title;
                finding.severity = severityOverride.value_or(rule->severity);
            } else {
                finding.ruleId = ruleId;
                finding.title = ruleId;
                finding.severity = severityOverride.value_or(std::string("medium"));
            }
            finding.status = status;
            finding.message = message;
            snapshot.findings.push_back(std::move(finding));

            if (status == "blocked") {
                snapshot.posture = "blocked";
            } else if (status == "warning" && snapshot.posture != "blocked") {
                snapshot.posture = "warning";
            }
        };

        const auto autonomousProviderCount = static_cast<size_t>(std::count_if(
            providers.begin(),
            providers.end(),
            [](const ProviderConnection& provider) {
                return provider.enabled && provider.allowAutonomousControl;
            }));

        if (!configuration.security.securityProtocolsEnabled && configuration.security.allowOpenLanAccess) {
            appendFinding(
                "CLU-C002",
                "blocked",
                "Security protocols are disabled while browser access remains open on the LAN. Restore protocols or close LAN exposure.");
            snapshot.recommendedActions.push_back("Re-enable security protocols or disable open LAN access before continuing unattended operations.");
        }

        if (configuration.security.allowTroubleshootingBypass) {
            appendFinding(
                "CLU-C003",
                "warning",
                "Troubleshooting bypass is enabled. CLU is treating the runtime as temporarily degraded until bypass mode is cleared.");
            snapshot.recommendedActions.push_back("Turn off troubleshooting bypass after diagnostics are complete.");
        }

        if (configuration.aiAutonomyEnabled && autonomousProviderCount == 0U) {
            appendFinding(
                "CLU-C001",
                "warning",
                "Global AI autonomy is enabled, but no provider route currently allows autonomous control.");
            snapshot.recommendedActions.push_back("Either disable global AI autonomy or explicitly authorize at least one provider for autonomous control.");
        }

        if (!configuration.aiAutonomyEnabled && autonomousProviderCount > 0U) {
            appendFinding(
                "CLU-C004",
                "warning",
                "One or more provider routes advertise autonomous control while global dashboard autonomy remains disabled.");
            snapshot.recommendedActions.push_back("Align provider autonomy flags with the global AI autonomy posture.");
        }

        if (std::none_of(providers.begin(), providers.end(), [](const ProviderConnection& provider) { return provider.enabled; })) {
            appendFinding(
                "CLU-C004",
                "warning",
                "No enabled provider routes are available for governed agent operations.");
            snapshot.recommendedActions.push_back("Enable at least one provider route before expecting CLU-managed agent workflows to run.");
        }

        if (!providers.empty() && assignments.empty()) {
            appendFinding(
                "CLU-C004",
                "warning",
                "Provider routes are configured, but no orchestration roles or sub-agents have been assigned to a provider owner yet.");
            snapshot.recommendedActions.push_back("Assign planner, architect, or sub-agent ownership before expecting governed provider execution.");
        }

        if (std::any_of(installHistory.begin(), installHistory.end(), [](const InstallProvenance& entry) { return !entry.trusted; })) {
            appendFinding(
                "CLU-C005",
                "warning",
                "One or more imported packages were recorded as untrusted. CLU is surfacing the provenance risk for operator review.");
            snapshot.recommendedActions.push_back("Review untrusted install history entries and replace or remove them if they are no longer needed.");
        }

        const bool hasGatewayProfile = std::any_of(
            exports.begin(),
            exports.end(),
            [](const ExportArtifact& artifact) { return artifact.fileName == "master-control-gateway-profile.json"; });
        const bool hasCodexHelper = std::any_of(
            exports.begin(),
            exports.end(),
            [](const ExportArtifact& artifact) { return artifact.fileName == "Install-CodexGateway.ps1"; });
        const bool hasClaudeHelper = std::any_of(
            exports.begin(),
            exports.end(),
            [](const ExportArtifact& artifact) { return artifact.fileName == "Install-ClaudeGateway.ps1"; });

        if (!(hasGatewayProfile && hasCodexHelper && hasClaudeHelper)) {
            appendFinding(
                "CLU-C006",
                "warning",
                "One or more expected governance and agent handoff artifacts are missing from the current export set.");
            snapshot.recommendedActions.push_back("Refresh exports and verify the gateway, Codex, and Claude handoff artifacts are still being generated.");
        }

        for (const auto platform : { PlatformTarget::Windows, PlatformTarget::MacOS, PlatformTarget::IOS }) {
            const auto platformName = platformKey(platform);
            const bool hasGateway = std::any_of(
                snapshot.platformGateways.begin(),
                snapshot.platformGateways.end(),
                [platform](const PlatformGatewayDescriptor& descriptor) {
                    return descriptor.platform == platform && descriptor.lanAdvertisementEnabled;
                });
            const bool hasGovernanceServer = std::any_of(
                snapshot.governanceServers.begin(),
                snapshot.governanceServers.end(),
                [platform](const GovernanceServerDescriptor& descriptor) {
                    return descriptor.platform == platform;
                });

            if (!hasGateway || !hasGovernanceServer) {
                appendFinding(
                    "CLU-C006",
                    "warning",
                    "The " + platformName + " governance lane is incomplete. Both the platform gateway module and governance MCP server module must be active.");
                snapshot.recommendedActions.push_back(
                    "Activate the " + platformName + " gateway and governance MCP server modules before advertising that lane to LAN clients.");
            }
        }

        if (std::any_of(
                snapshot.recentExecutions.begin(),
                snapshot.recentExecutions.end(),
                [](const GovernanceToolResult& result) { return result.status == GovernanceToolStatus::Failed; })) {
            appendFinding(
                "CLU-C007",
                "warning",
                "Recent governance tool execution failures were detected. Review the affected enforcement lanes before relying on autonomous governance.");
            snapshot.recommendedActions.push_back("Review recent governance tool failures and repair the affected platform governance lane.");
        }

        if (snapshot.findings.empty()) {
            snapshot.recommendedActions.push_back("Current runtime posture is aligned with the CLU governance profile.");
        }

        return snapshot;
    }

    GovernanceToolResult executeGovernanceTool(const GovernanceToolRequest& request) override {
        GovernanceToolResult result;
        result.platform = request.platform;
        result.toolId = request.toolId;
        result.startedAtUtc = timestampNowUtc();

        if (request.platform == PlatformTarget::Unknown) {
            result.status = GovernanceToolStatus::Failed;
            result.summary = "Governance tool execution requires a target platform.";
            result.completedAtUtc = timestampNowUtc();
            return result;
        }

        const auto governanceServers = platformServiceCatalogService_
            ? platformServiceCatalogService_->listGovernanceServers()
            : std::vector<GovernanceServerDescriptor>{};
        const auto serverIterator = std::find_if(
            governanceServers.begin(),
            governanceServers.end(),
            [&request](const GovernanceServerDescriptor& descriptor) { return descriptor.platform == request.platform; });
        if (serverIterator == governanceServers.end()) {
            result.status = GovernanceToolStatus::Failed;
            result.summary = "No governance server lane is active for platform " + platformKey(request.platform) + ".";
            result.completedAtUtc = timestampNowUtc();
            return result;
        }

        if (std::find(serverIterator->toolIds.begin(), serverIterator->toolIds.end(), request.toolId) == serverIterator->toolIds.end()) {
            result.status = GovernanceToolStatus::Failed;
            result.summary = "Governance tool '" + request.toolId + "' is not published by the active " +
                serverIterator->displayName + " lane.";
            result.completedAtUtc = timestampNowUtc();
            return result;
        }

        if (!platformGovernanceToolService_) {
            result.status = GovernanceToolStatus::Failed;
            result.summary = "Governance tool service is unavailable.";
            result.completedAtUtc = timestampNowUtc();
            return result;
        }

        return platformGovernanceToolService_->execute(request);
    }

private:
    static GovernanceProfile loadProfile(std::filesystem::path profileFile) {
        if (!profileFile.empty() && std::filesystem::exists(profileFile)) {
            const auto json = readJsonFile(profileFile);
            if (!json.is_null() && !json.empty()) {
                try {
                    return json.get<GovernanceProfile>();
                } catch (...) {
                }
            }
        }
        return buildFallbackGovernanceProfile();
    }

    const GovernanceRule* findRule(const std::string& ruleId) const {
        const auto iterator = std::find_if(
            profile_.rules.begin(),
            profile_.rules.end(),
            [&ruleId](const GovernanceRule& rule) { return rule.ruleId == ruleId; });
        return iterator == profile_.rules.end() ? nullptr : &(*iterator);
    }

    GovernanceProfile profile_;
    std::shared_ptr<IConfigurationService> configurationService_;
    std::shared_ptr<IProviderRegistry> providerRegistry_;
    std::shared_ptr<IProviderAssignmentService> providerAssignmentService_;
    std::shared_ptr<IInstallerOrchestrator> installerOrchestrator_;
    std::shared_ptr<IExportService> exportService_;
    std::shared_ptr<IPlatformServiceCatalogService> platformServiceCatalogService_;
    std::shared_ptr<IPlatformGovernanceToolService> platformGovernanceToolService_;
};

class ForsettiSurfaceService final : public IForsettiSurfaceService {
public:
    ForsettiSurfaceService(std::shared_ptr<Forsetti::UISurfaceManager> surfaceManager,
                           std::shared_ptr<IModuleControlSurfaceService> controlSurfaceService)
        : surfaceManager_(std::move(surfaceManager))
        , controlSurfaceService_(std::move(controlSurfaceService)) {}

    ForsettiSurfaceSnapshot currentSurface() const override {
        if (!surfaceManager_) {
            return {};
        }

        std::lock_guard<std::mutex> lock(mutex_);
        return ForsettiSurfaceSnapshot{
            surfaceManager_->currentThemeMask(),
            surfaceManager_->currentToolbarItems(),
            surfaceManager_->currentViewInjectionsBySlot(),
            surfaceManager_->currentOverlaySchema(),
            controlSurfaceService_ ? controlSurfaceService_->listControlSurfaceRequests() : std::vector<ModuleControlSurfaceRequest>{},
            publishedByModuleId_,
            publishedAtUtc_
        };
    }

    void publishModuleSurface(const std::string& moduleId,
                              const Forsetti::UIContributions& contributions) override {
        if (!surfaceManager_) {
            return;
        }

        surfaceManager_->addModuleContributions(moduleId, contributions);
        surfaceManager_->rebuildSurfaceState();

        std::lock_guard<std::mutex> lock(mutex_);
        publishedByModuleId_ = moduleId;
        publishedAtUtc_ = timestampNowUtc();
    }

    void removeModuleSurface(const std::string& moduleId) override {
        if (!surfaceManager_) {
            return;
        }

        surfaceManager_->removeModuleContributions(moduleId);
        surfaceManager_->rebuildSurfaceState();

        std::lock_guard<std::mutex> lock(mutex_);
        if (publishedByModuleId_ == moduleId) {
            publishedByModuleId_.clear();
            publishedAtUtc_.clear();
        }
    }

private:
    std::shared_ptr<Forsetti::UISurfaceManager> surfaceManager_;
    std::shared_ptr<IModuleControlSurfaceService> controlSurfaceService_;
    mutable std::mutex mutex_;
    std::string publishedByModuleId_;
    std::string publishedAtUtc_;
};

class ModuleControlSurfaceService final : public IModuleControlSurfaceService {
public:
    void upsertControlSurfaceRequest(const ModuleControlSurfaceRequest& request) override {
        std::lock_guard<std::mutex> lock(mutex_);
        requestsByKey_[makeKey(request.moduleId, request.featureId)] = request;
    }

    void removeControlSurfaceRequest(const std::string& moduleId,
                                     const std::string& featureId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        requestsByKey_.erase(makeKey(moduleId, featureId));
    }

    void removeControlSurfaceRequestsForModule(const std::string& moduleId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto iterator = requestsByKey_.begin(); iterator != requestsByKey_.end();) {
            if (iterator->second.moduleId == moduleId) {
                iterator = requestsByKey_.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    std::vector<ModuleControlSurfaceRequest> listControlSurfaceRequests() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ModuleControlSurfaceRequest> requests;
        requests.reserve(requestsByKey_.size());
        for (const auto& [key, request] : requestsByKey_) {
            requests.push_back(request);
        }
        return requests;
    }

private:
    static std::string makeKey(const std::string& moduleId, const std::string& featureId) {
        return moduleId + "::" + featureId;
    }

    mutable std::mutex mutex_;
    std::map<std::string, ModuleControlSurfaceRequest> requestsByKey_;
};

class PlatformServiceCatalogService final : public IPlatformServiceCatalogService {
public:
    PlatformServiceCatalogService(std::shared_ptr<IConfigurationService> configurationService,
                                  std::shared_ptr<ITelemetryService> telemetryService)
        : configurationService_(std::move(configurationService))
        , telemetryService_(std::move(telemetryService)) {}

    ~PlatformServiceCatalogService() override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [key, registration] : gatewaysByModuleId_) {
            deregisterGatewayLocked(registration);
        }
    }

    void upsertGateway(const PlatformGatewayDescriptor& descriptor) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto normalized = normalizeGatewayDescriptor(descriptor);
        auto& registration = gatewaysByModuleId_[normalized.moduleId];
        deregisterGatewayLocked(registration);
        registration.descriptor = std::move(normalized);
        registerGatewayLocked(registration);
    }

    void removeGateway(const std::string& moduleId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto iterator = gatewaysByModuleId_.find(moduleId);
        if (iterator == gatewaysByModuleId_.end()) {
            return;
        }
        deregisterGatewayLocked(iterator->second);
        gatewaysByModuleId_.erase(iterator);
    }

    std::vector<PlatformGatewayDescriptor> listGateways() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<PlatformGatewayDescriptor> descriptors;
        descriptors.reserve(gatewaysByModuleId_.size());
        for (const auto& [key, registration] : gatewaysByModuleId_) {
            descriptors.push_back(registration.descriptor);
        }
        std::sort(
            descriptors.begin(),
            descriptors.end(),
            [](const PlatformGatewayDescriptor& left, const PlatformGatewayDescriptor& right) {
                return std::tie(left.platform, left.displayName, left.serviceId) <
                    std::tie(right.platform, right.displayName, right.serviceId);
            });
        return descriptors;
    }

    void upsertGovernanceServer(const GovernanceServerDescriptor& descriptor) override {
        std::lock_guard<std::mutex> lock(mutex_);
        governanceByModuleId_[descriptor.moduleId] = descriptor;
    }

    void removeGovernanceServer(const std::string& moduleId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        governanceByModuleId_.erase(moduleId);
    }

    std::vector<GovernanceServerDescriptor> listGovernanceServers() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<GovernanceServerDescriptor> descriptors;
        descriptors.reserve(governanceByModuleId_.size());
        for (const auto& [key, descriptor] : governanceByModuleId_) {
            descriptors.push_back(descriptor);
        }
        std::sort(
            descriptors.begin(),
            descriptors.end(),
            [](const GovernanceServerDescriptor& left, const GovernanceServerDescriptor& right) {
                return std::tie(left.platform, left.displayName, left.serviceId) <
                    std::tie(right.platform, right.displayName, right.serviceId);
            });
        return descriptors;
    }

private:
    struct GatewayRegistration final {
        PlatformGatewayDescriptor descriptor;
        DNS_SERVICE_REGISTER_REQUEST request{};
        PDNS_SERVICE_INSTANCE instance = nullptr;
        bool registered = false;
    };

    PlatformGatewayDescriptor normalizeGatewayDescriptor(PlatformGatewayDescriptor descriptor) const {
        const auto configuration = configurationService_->current();
        const auto snapshot = telemetryService_->captureSnapshot();

        if (descriptor.displayName.empty()) {
            descriptor.displayName = "Platform Gateway";
        }
        if (descriptor.instanceLabel.empty()) {
            descriptor.instanceLabel = configuration.instanceName + " " + descriptor.displayName;
        }
        if (descriptor.hostName.empty()) {
            descriptor.hostName = snapshot.hostName;
        }
        descriptor.hostName = dotLocalHostName(descriptor.hostName);

        if (descriptor.ipAddress.empty() || descriptor.ipAddress == "0.0.0.0") {
            descriptor.ipAddress = snapshot.primaryIpAddress.empty() ? configuration.bindAddress : snapshot.primaryIpAddress;
        }
        if (descriptor.ipAddress.empty() || descriptor.ipAddress == "0.0.0.0") {
            descriptor.ipAddress = "127.0.0.1";
        }
        if (descriptor.port == 0) {
            descriptor.port = configuration.browserPort;
        }
        if (descriptor.gatewayPath.empty()) {
            descriptor.gatewayPath = platformGatewayRoutePath(descriptor.platform);
        }
        if (descriptor.configPath.empty()) {
            descriptor.configPath = platformGatewayConfigPath(descriptor.platform);
        }
        descriptor.properties["platform"] = platformKey(descriptor.platform);
        descriptor.properties["config_path"] = descriptor.configPath;
        descriptor.properties["gateway_path"] = descriptor.gatewayPath;
        descriptor.properties["service_id"] = descriptor.serviceId;
        if (descriptor.status.empty() || descriptor.status == "starting") {
            descriptor.status = "configured";
        }
        return descriptor;
    }

    void registerGatewayLocked(GatewayRegistration& registration) {
        const auto& descriptor = registration.descriptor;
        if (!descriptor.lanAdvertisementEnabled || descriptor.serviceType.empty() || descriptor.port == 0) {
            registration.descriptor.status = "disabled";
            return;
        }

        std::vector<std::wstring> keysWide;
        std::vector<std::wstring> valuesWide;
        keysWide.reserve(descriptor.properties.size());
        valuesWide.reserve(descriptor.properties.size());
        for (const auto& [key, value] : descriptor.properties) {
            keysWide.push_back(wideFromUtf8(key));
            valuesWide.push_back(wideFromUtf8(value));
        }

        std::vector<PCWSTR> keyPointers;
        std::vector<PCWSTR> valuePointers;
        keyPointers.reserve(keysWide.size());
        valuePointers.reserve(valuesWide.size());
        for (size_t index = 0; index < keysWide.size(); ++index) {
            keyPointers.push_back(keysWide[index].c_str());
            valuePointers.push_back(valuesWide[index].c_str());
        }

        const auto instanceName = wideFromUtf8(descriptor.instanceLabel + "." + descriptor.serviceType);
        const auto hostName = wideFromUtf8(descriptor.hostName);

        IP4_ADDRESS ipv4Address = 0;
        PIP4_ADDRESS ipv4Pointer = nullptr;
        IN_ADDR parsedAddress{};
        if (InetPtonW(AF_INET, wideFromUtf8(descriptor.ipAddress).c_str(), &parsedAddress) == 1) {
            ipv4Address = parsedAddress.S_un.S_addr;
            ipv4Pointer = &ipv4Address;
        }

        registration.instance = DnsServiceConstructInstance(
            instanceName.c_str(),
            hostName.c_str(),
            ipv4Pointer,
            nullptr,
            descriptor.port,
            0,
            0,
            static_cast<DWORD>(keyPointers.size()),
            keyPointers.empty() ? nullptr : keyPointers.data(),
            valuePointers.empty() ? nullptr : valuePointers.data());

        if (registration.instance == nullptr) {
            registration.descriptor.status = "registration_failed";
            return;
        }

        DNS_SERVICE_REGISTER_REQUEST request{};
        request.Version = 1;
        request.InterfaceIndex = 0;
        request.pServiceInstance = registration.instance;
        request.pRegisterCompletionCallback = nullptr;
        request.pQueryContext = nullptr;
        request.hCredentials = nullptr;
        request.unicastEnabled = FALSE;

        const auto status = DnsServiceRegister(&request, nullptr);
        registration.request = request;
        registration.registered = status == ERROR_SUCCESS;
        registration.descriptor.status = registration.registered ? "advertised" : "registration_failed";
    }

    void deregisterGatewayLocked(GatewayRegistration& registration) {
        if (registration.instance == nullptr) {
            return;
        }

        if (registration.registered) {
            DnsServiceDeRegister(&registration.request, nullptr);
            registration.registered = false;
        }

        DnsServiceFreeInstance(registration.instance);
        registration.instance = nullptr;
    }

    std::shared_ptr<IConfigurationService> configurationService_;
    std::shared_ptr<ITelemetryService> telemetryService_;
    mutable std::mutex mutex_;
    std::map<std::string, GatewayRegistration> gatewaysByModuleId_;
    std::map<std::string, GovernanceServerDescriptor> governanceByModuleId_;
};

class PlatformGovernanceToolService final : public IPlatformGovernanceToolService {
public:
    explicit PlatformGovernanceToolService(AppPaths paths)
        : paths_(std::move(paths)) {}

    void upsertTool(const GovernanceToolDescriptor& descriptor) override {
        std::lock_guard<std::mutex> lock(mutex_);
        toolsByKey_[makeKey(descriptor.moduleId, descriptor.toolId)] = descriptor;
    }

    void removeToolsForModule(const std::string& moduleId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto iterator = toolsByKey_.begin(); iterator != toolsByKey_.end();) {
            if (iterator->second.moduleId == moduleId) {
                iterator = toolsByKey_.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    std::vector<GovernanceToolDescriptor> listTools() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<GovernanceToolDescriptor> descriptors;
        descriptors.reserve(toolsByKey_.size());
        for (const auto& [key, descriptor] : toolsByKey_) {
            descriptors.push_back(descriptor);
        }
        std::sort(
            descriptors.begin(),
            descriptors.end(),
            [](const GovernanceToolDescriptor& left, const GovernanceToolDescriptor& right) {
                return std::tie(left.platform, left.displayName, left.toolId) <
                    std::tie(right.platform, right.displayName, right.toolId);
            });
        return descriptors;
    }

    std::vector<GovernanceToolResult> recentExecutions() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return recentExecutions_;
    }

    GovernanceToolResult execute(const GovernanceToolRequest& request) override {
        GovernanceToolResult result;
        result.platform = request.platform;
        result.toolId = request.toolId;
        result.startedAtUtc = timestampNowUtc();

        const auto descriptor = findDescriptor(request.platform, request.toolId);
        if (!descriptor.has_value()) {
            result.status = GovernanceToolStatus::Failed;
            result.summary = "Governance tool '" + request.toolId + "' is not registered for platform " +
                platformKey(request.platform) + ".";
            result.completedAtUtc = timestampNowUtc();
            recordExecution(result);
            return result;
        }

        result.displayName = descriptor->displayName;

        if (descriptor->requiresRemoteToolchain) {
            result.status = GovernanceToolStatus::Unsupported;
            result.summary = "Remote Apple governance routing is declared, but a remote Apple toolchain endpoint is not configured yet.";
            result.findings.push_back(makeFinding(
                "governance.remote-toolchain",
                descriptor->displayName,
                "medium",
                "warning",
                "This governance lane requires a remote Apple toolchain or companion runtime before it can enforce Forsetti rules."));
            result.completedAtUtc = timestampNowUtc();
            recordExecution(result);
            return result;
        }

        try {
            switch (request.platform) {
                case PlatformTarget::Windows:
                    executeWindowsTool(*descriptor, request, result);
                    break;
                case PlatformTarget::MacOS:
                case PlatformTarget::IOS:
                    result.status = GovernanceToolStatus::Unsupported;
                    result.summary = "Remote governance routing is declared but not yet configured for this platform.";
                    break;
                default:
                    result.status = GovernanceToolStatus::Failed;
                    result.summary = "Unknown governance platform.";
                    break;
            }
        } catch (const std::exception& exception) {
            result.status = GovernanceToolStatus::Failed;
            result.summary = exception.what();
        }

        result.completedAtUtc = timestampNowUtc();
        recordExecution(result);
        return result;
    }

private:
    static std::string makeKey(const std::string& moduleId, const std::string& toolId) {
        return moduleId + "::" + toolId;
    }

    static GovernanceFinding makeFinding(const std::string& ruleId,
                                         const std::string& title,
                                         const std::string& severity,
                                         const std::string& status,
                                         const std::string& message) {
        return GovernanceFinding{ ruleId, title, severity, status, message };
    }

    static void appendProcessFinding(std::vector<GovernanceFinding>& findings,
                                     const std::string& ruleId,
                                     const std::string& title,
                                     const std::string& scriptName,
                                     const ProcessCaptureResult& process) {
        const bool passed = process.launched && process.exitCode == 0;
        findings.push_back(makeFinding(
            ruleId,
            title,
            passed ? "low" : "high",
            passed ? "pass" : "blocked",
            scriptName + (passed
                    ? " completed successfully."
                    : " failed with exit code " + std::to_string(process.exitCode) + ".")));
    }

    std::optional<GovernanceToolDescriptor> findDescriptor(const PlatformTarget platform,
                                                           const std::string& toolId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto iterator = std::find_if(
            toolsByKey_.begin(),
            toolsByKey_.end(),
            [platform, &toolId](const auto& entry) {
                return entry.second.platform == platform && entry.second.toolId == toolId;
            });
        if (iterator == toolsByKey_.end()) {
            return std::nullopt;
        }
        return iterator->second;
    }

    void recordExecution(const GovernanceToolResult& result) {
        std::lock_guard<std::mutex> lock(mutex_);
        recentExecutions_.insert(recentExecutions_.begin(), result);
        if (recentExecutions_.size() > 20) {
            recentExecutions_.resize(20);
        }
    }

    std::filesystem::path resolveTargetRoot(const GovernanceToolRequest& request) const {
        if (!trimCopy(request.targetPath).empty()) {
            return std::filesystem::path(request.targetPath);
        }
        if (const auto iterator = request.options.find("targetRoot"); iterator != request.options.end() &&
            !trimCopy(iterator->second).empty()) {
            return std::filesystem::path(iterator->second);
        }
        return std::filesystem::current_path();
    }

    std::optional<std::filesystem::path> findVendoredForsettiScriptsRoot(const std::filesystem::path& targetRoot) const {
        const std::array<std::filesystem::path, 3> candidates = {
            targetRoot / "Forsetti-Framework-Windows-main" / "Forsetti-Framework-Windows-main" / "Scripts",
            targetRoot / "Forsetti-Framework-Windows-main" / "Scripts",
            std::filesystem::current_path() / "Forsetti-Framework-Windows-main" / "Forsetti-Framework-Windows-main" / "Scripts"
        };

        for (const auto& candidate : candidates) {
            if (std::filesystem::exists(candidate / "check-architecture.ps1")) {
                return candidate;
            }
        }
        return std::nullopt;
    }

    std::vector<std::filesystem::path> candidatePayloadRoots(const std::filesystem::path& targetRoot) const {
        std::vector<std::filesystem::path> candidates;
        candidates.push_back(targetRoot);
        const auto distRoot = targetRoot / "dist";
        if (std::filesystem::exists(distRoot) && std::filesystem::is_directory(distRoot)) {
            for (const auto& entry : std::filesystem::directory_iterator(distRoot)) {
                if (entry.is_directory()) {
                    candidates.push_back(entry.path());
                }
            }
        }
        return candidates;
    }

    static bool hasPayloadLayout(const std::filesystem::path& root) {
        return std::filesystem::exists(root / "MasterControlServiceHost.exe") &&
            std::filesystem::exists(root / "MasterControlShell.exe") &&
            std::filesystem::exists(root / "MasterControlBootstrapper.exe") &&
            std::filesystem::exists(root / "share" / "MasterControlProgram" / "web" / "index.html") &&
            std::filesystem::exists(root / "share" / "MasterControlProgram" / "ForsettiManifests" / "DashboardUIModule.json");
    }

    ProcessCaptureResult runPowerShellScript(const std::filesystem::path& scriptPath,
                                             const std::filesystem::path& workingDirectory) const {
        const auto powershell = findCommandOnPath({ L"pwsh.exe", L"powershell.exe" });
        if (!powershell.has_value()) {
            return {};
        }

        const auto commandLine = joinCommandArguments({
            powershell->wstring(),
            L"-NoProfile",
            L"-ExecutionPolicy",
            L"Bypass",
            L"-File",
            scriptPath.wstring()
        });
        return runProcessCapture(commandLine, workingDirectory);
    }

    void executeWindowsTool(const GovernanceToolDescriptor& descriptor,
                            const GovernanceToolRequest& request,
                            GovernanceToolResult& result) const {
        const auto targetRoot = resolveTargetRoot(request);

        if (descriptor.toolId == "forsetti.windows.manifest.validate") {
            const auto scriptPath = targetRoot / "scripts" / "check-mastercontrol-forsetti.ps1";
            if (!std::filesystem::exists(scriptPath)) {
                result.status = GovernanceToolStatus::Failed;
                result.summary = "Master Control Forsetti compliance script was not found at " + scriptPath.string() + ".";
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    "high",
                    "blocked",
                    "The repo-owned Forsetti compliance script is missing."));
                return;
            }

            const auto process = runPowerShellScript(scriptPath, targetRoot);
            result.rawOutput = process.stdoutText + process.stderrText;
            appendProcessFinding(result.findings, descriptor.toolId, descriptor.displayName, scriptPath.filename().string(), process);
            result.succeeded = process.launched && process.exitCode == 0;
            result.status = result.succeeded ? GovernanceToolStatus::Passed : GovernanceToolStatus::Failed;
            result.summary = result.succeeded
                ? "Repo-owned Forsetti compliance validation passed."
                : "Repo-owned Forsetti compliance validation failed.";
            return;
        }

        if (descriptor.toolId == "forsetti.windows.architecture.validate") {
            const auto scriptsRoot = findVendoredForsettiScriptsRoot(targetRoot);
            if (!scriptsRoot.has_value()) {
                result.status = GovernanceToolStatus::Failed;
                result.summary = "Vendored Forsetti architecture scripts were not found.";
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    "high",
                    "blocked",
                    "The vendored Forsetti Scripts directory could not be located for architecture validation."));
                return;
            }

            bool allPassed = true;
            std::ostringstream combinedOutput;
            for (const auto& scriptName : { "check-architecture.ps1", "check-dependencies.ps1" }) {
                const auto scriptPath = *scriptsRoot / scriptName;
                if (!std::filesystem::exists(scriptPath)) {
                    allPassed = false;
                    result.findings.push_back(makeFinding(
                        descriptor.toolId,
                        descriptor.displayName,
                        "high",
                        "blocked",
                        std::string(scriptName) + " is missing from the vendored Forsetti Scripts directory."));
                    continue;
                }

                const auto process = runPowerShellScript(scriptPath, *scriptsRoot);
                combinedOutput << process.stdoutText << process.stderrText;
                appendProcessFinding(result.findings, descriptor.toolId, descriptor.displayName, scriptName, process);
                allPassed = allPassed && process.launched && process.exitCode == 0;
            }

            result.rawOutput = combinedOutput.str();
            result.succeeded = allPassed;
            result.status = allPassed ? GovernanceToolStatus::Passed : GovernanceToolStatus::Failed;
            result.summary = allPassed
                ? "Vendored Forsetti architecture and dependency validation passed."
                : "Vendored Forsetti architecture and dependency validation reported violations.";
            return;
        }

        if (descriptor.toolId == "forsetti.windows.module-boundary.inspect") {
            const auto modulesCMake = targetRoot / "src" / "MasterControlModules" / "CMakeLists.txt";
            const auto appCMake = targetRoot / "src" / "MasterControlApp" / "CMakeLists.txt";
            const auto manifestsRoot = targetRoot / "src" / "MasterControlModules" / "Resources" / "ForsettiManifests";
            const auto modulesCMakeText = readTextFile(modulesCMake);
            const auto appCMakeText = readTextFile(appCMake);

            result.findings.push_back(makeFinding(
                descriptor.toolId,
                descriptor.displayName,
                std::filesystem::exists(modulesCMake) ? "low" : "high",
                std::filesystem::exists(modulesCMake) ? "pass" : "blocked",
                std::filesystem::exists(modulesCMake)
                    ? "MasterControlModules CMake target is present."
                    : "MasterControlModules CMake target is missing."));
            result.findings.push_back(makeFinding(
                descriptor.toolId,
                descriptor.displayName,
                modulesCMakeText.find("ForsettiPlatform") == std::string::npos ? "low" : "high",
                modulesCMakeText.find("ForsettiPlatform") == std::string::npos ? "pass" : "blocked",
                modulesCMakeText.find("ForsettiPlatform") == std::string::npos
                    ? "MasterControlModules stays Core-only and does not link ForsettiPlatform."
                    : "MasterControlModules links ForsettiPlatform, which violates the Core-only module boundary."));
            result.findings.push_back(makeFinding(
                descriptor.toolId,
                descriptor.displayName,
                appCMakeText.find("MasterControlModules.cpp") == std::string::npos ? "low" : "high",
                appCMakeText.find("MasterControlModules.cpp") == std::string::npos ? "pass" : "blocked",
                appCMakeText.find("MasterControlModules.cpp") == std::string::npos
                    ? "MasterControlApp does not compile MasterControlModules.cpp directly."
                    : "MasterControlApp compiles MasterControlModules.cpp directly, breaking the module boundary."));
            result.findings.push_back(makeFinding(
                descriptor.toolId,
                descriptor.displayName,
                std::filesystem::exists(manifestsRoot) ? "low" : "high",
                std::filesystem::exists(manifestsRoot) ? "pass" : "blocked",
                std::filesystem::exists(manifestsRoot)
                    ? "Forsetti manifests are staged from the module resource tree."
                    : "Forsetti manifests are missing from src/MasterControlModules/Resources/ForsettiManifests."));

            result.succeeded = std::all_of(
                result.findings.begin(),
                result.findings.end(),
                [](const GovernanceFinding& finding) { return finding.status == "pass"; });
            result.status = result.succeeded ? GovernanceToolStatus::Passed : GovernanceToolStatus::Failed;
            result.summary = result.succeeded
                ? "Module boundary inspection passed."
                : "Module boundary inspection found one or more Forsetti violations.";
            return;
        }

        if (descriptor.toolId == "forsetti.windows.package.validate") {
            const auto candidates = candidatePayloadRoots(targetRoot);
            const auto payloadIterator = std::find_if(candidates.begin(), candidates.end(), [](const auto& candidate) {
                return hasPayloadLayout(candidate);
            });

            if (payloadIterator == candidates.end()) {
                result.status = GovernanceToolStatus::Failed;
                result.summary = "No staged Master Control payload root was found for package validation.";
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    "high",
                    "blocked",
                    "Expected executables and staged web/manifest payloads were not found under the target path or its dist/* children."));
                return;
            }

            const auto& payloadRoot = *payloadIterator;
            const std::array<std::pair<std::filesystem::path, std::string>, 5> requiredArtifacts = {{
                { payloadRoot / "MasterControlServiceHost.exe", "Service host executable is staged." },
                { payloadRoot / "MasterControlShell.exe", "Shell executable is staged." },
                { payloadRoot / "MasterControlBootstrapper.exe", "Bootstrapper executable is staged." },
                { payloadRoot / "share" / "MasterControlProgram" / "web" / "index.html", "Browser payload is staged." },
                { payloadRoot / "share" / "MasterControlProgram" / "ForsettiManifests" / "DashboardUIModule.json", "Forsetti UI manifest is staged." }
            }};

            for (const auto& [artifactPath, message] : requiredArtifacts) {
                result.findings.push_back(makeFinding(
                    descriptor.toolId,
                    descriptor.displayName,
                    std::filesystem::exists(artifactPath) ? "low" : "high",
                    std::filesystem::exists(artifactPath) ? "pass" : "blocked",
                    std::filesystem::exists(artifactPath)
                        ? message
                        : artifactPath.string() + " is missing from the staged payload."));
            }

            result.succeeded = std::all_of(
                result.findings.begin(),
                result.findings.end(),
                [](const GovernanceFinding& finding) { return finding.status == "pass"; });
            result.status = result.succeeded ? GovernanceToolStatus::Passed : GovernanceToolStatus::Failed;
            result.summary = result.succeeded
                ? "Staged payload validation passed."
                : "Staged payload validation found missing deployment artifacts.";
            result.rawOutput = payloadRoot.string();
            return;
        }

        result.status = GovernanceToolStatus::Failed;
        result.summary = "Unknown Windows governance tool ID '" + descriptor.toolId + "'.";
    }

    AppPaths paths_;
    mutable std::mutex mutex_;
    std::map<std::string, GovernanceToolDescriptor> toolsByKey_;
    std::vector<GovernanceToolResult> recentExecutions_;
};

class BeaconService final : public IBeaconService {
public:
    BeaconService(std::shared_ptr<IConfigurationService> configurationService,
                  std::shared_ptr<ITelemetryService> telemetryService,
                  std::shared_ptr<IPlatformServiceCatalogService> platformServiceCatalogService)
        : configurationService_(std::move(configurationService))
        , telemetryService_(std::move(telemetryService))
        , platformServiceCatalogService_(std::move(platformServiceCatalogService)) {}

    BeaconAdvertisement currentAdvertisement() const override {
        const auto configuration = configurationService_->current();
        const auto snapshot = telemetryService_->captureSnapshot();
        return BeaconAdvertisement{
            configuration.instanceName,
            snapshot.hostName,
            snapshot.primaryIpAddress.empty() ? configuration.bindAddress : snapshot.primaryIpAddress,
            configuration.browserPort,
            configuration.browserPort,
            "online",
            platformServiceCatalogService_ ? platformServiceCatalogService_->listGateways() : std::vector<PlatformGatewayDescriptor>{}
        };
    }

    void start() override;
    void stop() override;
    ~BeaconService() override {
        stop();
    }

private:
    std::shared_ptr<IConfigurationService> configurationService_;
    std::shared_ptr<ITelemetryService> telemetryService_;
    std::shared_ptr<IPlatformServiceCatalogService> platformServiceCatalogService_;
    std::atomic<bool> running_{ false };
    std::thread worker_;
};

void BeaconService::start() {
    if (running_.exchange(true)) {
        return;
    }

    worker_ = std::thread([this]() {
        SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socketHandle == INVALID_SOCKET) {
            running_ = false;
            return;
        }

        BOOL broadcastEnabled = TRUE;
        setsockopt(socketHandle, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&broadcastEnabled), sizeof(broadcastEnabled));

        while (running_) {
            const auto configuration = configurationService_->current();
            if (!configuration.beaconEnabled) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(configuration.beaconPort);
            address.sin_addr.s_addr = INADDR_BROADCAST;

            const auto payload = nlohmann::json(currentAdvertisement()).dump();
            sendto(socketHandle, payload.c_str(), static_cast<int>(payload.size()), 0, reinterpret_cast<sockaddr*>(&address), sizeof(address));
            std::this_thread::sleep_for(std::chrono::seconds(configuration.beaconBroadcastIntervalSeconds));
        }

        closesocket(socketHandle);
    });
}

void BeaconService::stop() {
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

class AdminApiService final : public IAdminApiService {
public:
    AdminApiService(std::shared_ptr<ITelemetryService> telemetryService,
                    std::shared_ptr<IRuntimeInventoryService> inventoryService,
                    std::shared_ptr<IConfigurationService> configurationService,
                    std::shared_ptr<IPlatformServiceCatalogService> platformServiceCatalogService,
                    std::shared_ptr<IProviderRegistry> providerRegistry,
                    std::shared_ptr<IProviderCatalogService> providerCatalogService,
                    std::shared_ptr<IProviderCredentialStore> providerCredentialStore,
                    std::shared_ptr<IMcpServerCatalogService> mcpServerCatalogService,
                    std::shared_ptr<ISubAgentCatalogService> subAgentCatalogService,
                    std::shared_ptr<ISubAgentGroupService> subAgentGroupService,
                    std::shared_ptr<IProviderAssignmentService> providerAssignmentService,
                    std::shared_ptr<IProviderExecutionCatalogService> providerExecutionCatalogService,
                    std::shared_ptr<IProviderExecutionService> providerExecutionService,
                    std::shared_ptr<IInstallerOrchestrator> installerOrchestrator,
                    std::shared_ptr<IBootstrapRepoService> bootstrapRepoService,
                    std::shared_ptr<IZipBundleService> zipBundleService,
                    std::shared_ptr<IExportService> exportService,
                    std::shared_ptr<ICommandLogicUnitService> commandLogicUnitService,
                    std::shared_ptr<IForsettiSurfaceService> surfaceService)
        : telemetryService_(std::move(telemetryService))
        , inventoryService_(std::move(inventoryService))
        , configurationService_(std::move(configurationService))
        , platformServiceCatalogService_(std::move(platformServiceCatalogService))
        , providerRegistry_(std::move(providerRegistry))
        , providerCatalogService_(std::move(providerCatalogService))
        , providerCredentialStore_(std::move(providerCredentialStore))
        , mcpServerCatalogService_(std::move(mcpServerCatalogService))
        , subAgentCatalogService_(std::move(subAgentCatalogService))
        , subAgentGroupService_(std::move(subAgentGroupService))
        , providerAssignmentService_(std::move(providerAssignmentService))
        , providerExecutionCatalogService_(std::move(providerExecutionCatalogService))
        , providerExecutionService_(std::move(providerExecutionService))
        , installerOrchestrator_(std::move(installerOrchestrator))
        , bootstrapRepoService_(std::move(bootstrapRepoService))
        , zipBundleService_(std::move(zipBundleService))
        , exportService_(std::move(exportService))
        , commandLogicUnitService_(std::move(commandLogicUnitService))
        , surfaceService_(std::move(surfaceService)) {}

    DashboardSnapshot snapshot() override {
        inventoryService_->refresh();

        DashboardSnapshot snapshot;
        snapshot.telemetry = telemetryService_->captureSnapshot();
        snapshot.endpoints = inventoryService_->listEndpoints();
        snapshot.providers = providerRegistry_->listProviders();
        snapshot.providerCapabilities = providerCatalogService_->listCapabilities();
        snapshot.providerCredentialStatuses = providerCredentialStore_->listStatuses();
        snapshot.subAgentGroups = subAgentGroupService_->listGroups();
        snapshot.providerAssignmentTargets = providerAssignmentService_->listTargets();
        snapshot.providerAssignments = providerAssignmentService_->listAssignments();
        snapshot.providerExecutionRegistrations = providerExecutionCatalogService_->listRegistrations();
        snapshot.providerExecutionHistory = providerExecutionService_->history();
        for (auto& provider : snapshot.providers) {
            const auto statusIterator = std::find_if(
                snapshot.providerCredentialStatuses.begin(),
                snapshot.providerCredentialStatuses.end(),
                [&provider](const ProviderCredentialStatus& status) { return status.providerId == provider.id; });
            provider.credentialsConfigured = statusIterator != snapshot.providerCredentialStatuses.end() &&
                statusIterator->configured;
        }
        snapshot.installHistory = installerOrchestrator_->history();
        snapshot.exports = exportService_->generateExports();
        const auto configuration = configurationService_->current();
        snapshot.resourceAllocation = configuration.resourceAllocation;
        snapshot.security = configuration.security;
        snapshot.governance = commandLogicUnitService_->currentGovernance();
        snapshot.platformGateways = platformServiceCatalogService_
            ? platformServiceCatalogService_->listGateways()
            : std::vector<PlatformGatewayDescriptor>{};
        snapshot.governanceServers = platformServiceCatalogService_
            ? platformServiceCatalogService_->listGovernanceServers()
            : std::vector<GovernanceServerDescriptor>{};
        snapshot.surface = surfaceService_->currentSurface();
        for (const auto& gateway : snapshot.platformGateways) {
            snapshot.endpoints.push_back(RuntimeEndpoint{
                gateway.serviceId,
                gateway.displayName,
                EndpointKind::Gateway,
                gateway.ipAddress,
                gateway.port,
                "http",
                endpointStatusFromServiceState(gateway.status),
                "Forsetti platform gateway for " + platformKey(gateway.platform) + " clients.",
                timestampNowUtc(),
                gateway.gatewayPath,
                {},
                false
            });
        }
        for (const auto& governance : snapshot.governanceServers) {
            const auto gatewayIterator = std::find_if(
                snapshot.platformGateways.begin(),
                snapshot.platformGateways.end(),
                [&governance](const PlatformGatewayDescriptor& gateway) {
                    return gateway.serviceId == governance.gatewayServiceId;
                });
            snapshot.endpoints.push_back(RuntimeEndpoint{
                governance.serviceId,
                governance.displayName,
                EndpointKind::MCPServer,
                gatewayIterator != snapshot.platformGateways.end() ? gatewayIterator->ipAddress : std::string(),
                gatewayIterator != snapshot.platformGateways.end() ? gatewayIterator->port : configuration.browserPort,
                "http",
                endpointStatusFromServiceState(governance.status),
                "Governance MCP server lane for " + platformKey(governance.platform) + " enforcement.",
                timestampNowUtc(),
                governance.routePath,
                platformKey(governance.platform),
                false
            });
        }
        return snapshot;
    }

    GovernanceSnapshot governance() const override {
        return commandLogicUnitService_->currentGovernance();
    }

    GovernanceToolResult executeGovernanceToolJson(const std::string& requestBody) override {
        return commandLogicUnitService_->executeGovernanceTool(
            nlohmann::json::parse(requestBody).get<GovernanceToolRequest>());
    }

    OperationResult applyConfigurationJson(const std::string& requestBody,
                                           bool confirmUnsafeChanges) override {
        return configurationService_->update(nlohmann::json::parse(requestBody).get<AppConfiguration>(), confirmUnsafeChanges);
    }

    OperationResult upsertProviderJson(const std::string& requestBody) override {
        return providerRegistry_->upsertProvider(nlohmann::json::parse(requestBody).get<ProviderConnection>());
    }

    OperationResult upsertProviderCredentialsJson(const std::string& requestBody) override {
        return providerCredentialStore_->upsertCredentials(nlohmann::json::parse(requestBody).get<ProviderCredentialUpdate>());
    }

    OperationResult upsertMcpServerJson(const std::string& requestBody) override {
        return mcpServerCatalogService_->upsertMcpServer(nlohmann::json::parse(requestBody).get<RuntimeEndpoint>());
    }

    OperationResult removeMcpServerJson(const std::string& requestBody) override {
        return mcpServerCatalogService_->removeMcpServer(
            nlohmann::json::parse(requestBody).get<McpServerRemovalRequest>().mcpServerId);
    }

    OperationResult upsertSubAgentJson(const std::string& requestBody) override {
        return subAgentCatalogService_->upsertSubAgent(nlohmann::json::parse(requestBody).get<RuntimeEndpoint>());
    }

    OperationResult removeSubAgentJson(const std::string& requestBody) override {
        return subAgentCatalogService_->removeSubAgent(
            nlohmann::json::parse(requestBody).get<SubAgentRemovalRequest>().subAgentId);
    }

    OperationResult upsertSubAgentGroupJson(const std::string& requestBody) override {
        return subAgentGroupService_->upsertGroup(nlohmann::json::parse(requestBody).get<SubAgentGroupDefinition>());
    }

    OperationResult removeSubAgentGroupJson(const std::string& requestBody) override {
        return subAgentGroupService_->removeGroup(
            nlohmann::json::parse(requestBody).get<SubAgentGroupRemovalRequest>().groupId);
    }

    OperationResult upsertProviderAssignmentJson(const std::string& requestBody) override {
        return providerAssignmentService_->upsertAssignment(nlohmann::json::parse(requestBody).get<ProviderAssignment>());
    }

    ProviderExecutionRecord executeProviderTaskJson(const std::string& requestBody) override {
        return providerExecutionService_->execute(nlohmann::json::parse(requestBody).get<ProviderExecutionRequest>());
    }

    OperationResult installPackageJson(const std::string& requestBody) override {
        return installerOrchestrator_->installPackage(nlohmann::json::parse(requestBody).get<InstallerPackageSpec>());
    }

    OperationResult installRepoJson(const std::string& requestBody) override {
        return bootstrapRepoService_->installFromRepository(nlohmann::json::parse(requestBody).get<BootstrapRepoSpec>());
    }

    OperationResult installZipJson(const std::string& requestBody) override {
        return zipBundleService_->installFromZipBundle(nlohmann::json::parse(requestBody).get<ZipBundleSpec>());
    }

private:
    std::shared_ptr<ITelemetryService> telemetryService_;
    std::shared_ptr<IRuntimeInventoryService> inventoryService_;
    std::shared_ptr<IConfigurationService> configurationService_;
    std::shared_ptr<IPlatformServiceCatalogService> platformServiceCatalogService_;
    std::shared_ptr<IProviderRegistry> providerRegistry_;
    std::shared_ptr<IProviderCatalogService> providerCatalogService_;
    std::shared_ptr<IProviderCredentialStore> providerCredentialStore_;
    std::shared_ptr<IMcpServerCatalogService> mcpServerCatalogService_;
    std::shared_ptr<ISubAgentCatalogService> subAgentCatalogService_;
    std::shared_ptr<ISubAgentGroupService> subAgentGroupService_;
    std::shared_ptr<IProviderAssignmentService> providerAssignmentService_;
    std::shared_ptr<IProviderExecutionCatalogService> providerExecutionCatalogService_;
    std::shared_ptr<IProviderExecutionService> providerExecutionService_;
    std::shared_ptr<IInstallerOrchestrator> installerOrchestrator_;
    std::shared_ptr<IBootstrapRepoService> bootstrapRepoService_;
    std::shared_ptr<IZipBundleService> zipBundleService_;
    std::shared_ptr<IExportService> exportService_;
    std::shared_ptr<ICommandLogicUnitService> commandLogicUnitService_;
    std::shared_ptr<IForsettiSurfaceService> surfaceService_;
};

struct HttpRequest final {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse final {
    int statusCode = 200;
    std::string contentType = "application/json";
    std::string body;
};

class SimpleHttpServer final {
public:
    using RequestHandler = std::function<HttpResponse(const HttpRequest&)>;

    SimpleHttpServer(std::string bindAddress, uint16_t port, RequestHandler handler)
        : bindAddress_(std::move(bindAddress))
        , port_(port)
        , handler_(std::move(handler)) {}

    bool start();
    void stop();
    ~SimpleHttpServer() { stop(); }

private:
    static std::string reasonPhrase(int statusCode);
    static HttpRequest parseRequest(const std::string& rawRequest);
    static void sendResponse(SOCKET client, const HttpResponse& response);
    void run();
    void handleClient(SOCKET client);

    std::string bindAddress_;
    uint16_t port_;
    RequestHandler handler_;
    std::atomic<bool> running_{ false };
    std::thread worker_;
    SOCKET listenSocket_ = INVALID_SOCKET;
};

bool SimpleHttpServer::start() {
    if (running_.exchange(true)) {
        return true;
    }
    worker_ = std::thread([this]() { run(); });
    return true;
}

void SimpleHttpServer::stop() {
    running_ = false;
    if (listenSocket_ != INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::string SimpleHttpServer::reasonPhrase(const int statusCode) {
    switch (statusCode) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

HttpRequest SimpleHttpServer::parseRequest(const std::string& rawRequest) {
    HttpRequest request;
    std::istringstream stream(rawRequest);
    std::string requestLine;
    std::getline(stream, requestLine);
    if (!requestLine.empty() && requestLine.back() == '\r') {
        requestLine.pop_back();
    }

    std::istringstream requestLineStream(requestLine);
    requestLineStream >> request.method >> request.path;

    std::string headerLine;
    while (std::getline(stream, headerLine)) {
        if (headerLine == "\r" || headerLine.empty()) {
            break;
        }
        if (headerLine.back() == '\r') {
            headerLine.pop_back();
        }
        const auto separator = headerLine.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        auto name = headerLine.substr(0, separator);
        auto value = headerLine.substr(separator + 1);
        if (!value.empty() && value.front() == ' ') {
            value.erase(value.begin());
        }
        request.headers[name] = value;
    }

    std::ostringstream body;
    body << stream.rdbuf();
    request.body = body.str();
    return request;
}

void SimpleHttpServer::sendResponse(SOCKET client, const HttpResponse& response) {
    std::ostringstream stream;
    stream << "HTTP/1.1 " << response.statusCode << ' ' << reasonPhrase(response.statusCode) << "\r\n";
    stream << "Content-Type: " << response.contentType << "\r\n";
    stream << "Content-Length: " << response.body.size() << "\r\n";
    stream << "Connection: close\r\n\r\n";
    stream << response.body;

    const auto data = stream.str();
    send(client, data.c_str(), static_cast<int>(data.size()), 0);
}

void SimpleHttpServer::run() {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* results = nullptr;
    const auto portText = std::to_string(port_);
    if (getaddrinfo(bindAddress_.c_str(), portText.c_str(), &hints, &results) != 0) {
        running_ = false;
        return;
    }

    for (addrinfo* result = results; result != nullptr; result = result->ai_next) {
        listenSocket_ = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (listenSocket_ == INVALID_SOCKET) {
            continue;
        }

        const int reuse = 1;
        setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        if (bind(listenSocket_, result->ai_addr, static_cast<int>(result->ai_addrlen)) == 0 &&
            listen(listenSocket_, SOMAXCONN) == 0) {
            break;
        }

        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }

    freeaddrinfo(results);
    if (listenSocket_ == INVALID_SOCKET) {
        running_ = false;
        return;
    }

    while (running_) {
        SOCKET client = accept(listenSocket_, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            continue;
        }
        handleClient(client);
        closesocket(client);
    }
}

void SimpleHttpServer::handleClient(SOCKET client) {
    std::string requestBuffer;
    char chunk[4096]{};
    int bytesRead = 0;
    while ((bytesRead = recv(client, chunk, static_cast<int>(sizeof(chunk)), 0)) > 0) {
        requestBuffer.append(chunk, chunk + bytesRead);
        const auto headerEnd = requestBuffer.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            continue;
        }
        const auto contentLengthPosition = requestBuffer.find("Content-Length:");
        if (contentLengthPosition == std::string::npos) {
            break;
        }

        const auto lineEnd = requestBuffer.find("\r\n", contentLengthPosition);
        const auto value = requestBuffer.substr(contentLengthPosition + 15, lineEnd - (contentLengthPosition + 15));
        const auto contentLength = static_cast<size_t>(std::stoi(value));
        if (requestBuffer.size() >= headerEnd + 4 + contentLength) {
            break;
        }
    }

    const auto request = parseRequest(requestBuffer);
    const auto response = handler_(request);
    sendResponse(client, response);
}

std::string contentTypeForPath(const std::filesystem::path& path) {
    const auto extension = path.extension().string();
    if (extension == ".css") {
        return "text/css";
    }
    if (extension == ".js") {
        return "application/javascript";
    }
    if (extension == ".json") {
        return "application/json";
    }
    return "text/html; charset=utf-8";
}

} // namespace

class MasterControlApplication::Impl final {
public:
    Impl()
        : winsock_(std::make_unique<ScopedWinsock>())
        , paths_(resolveAppPaths())
        , state_(std::make_shared<SharedState>()) {}

    bool initialize();
    void shutdown();
    void requestStop() { stopRequested_ = true; }
    int runInteractive();
    std::string browserUrl() const;
    DashboardSnapshot snapshot() { return adminApiService_->snapshot(); }
    GovernanceToolResult executeGovernanceToolJson(const std::string& requestBody) { return adminApiService_->executeGovernanceToolJson(requestBody); }
    OperationResult applyConfigurationJson(const std::string& requestBody, bool confirmUnsafeChanges) { return adminApiService_->applyConfigurationJson(requestBody, confirmUnsafeChanges); }
    OperationResult upsertProviderJson(const std::string& requestBody) { return adminApiService_->upsertProviderJson(requestBody); }
    OperationResult upsertProviderCredentialsJson(const std::string& requestBody) { return adminApiService_->upsertProviderCredentialsJson(requestBody); }
    OperationResult upsertMcpServerJson(const std::string& requestBody) { return adminApiService_->upsertMcpServerJson(requestBody); }
    OperationResult removeMcpServerJson(const std::string& requestBody) { return adminApiService_->removeMcpServerJson(requestBody); }
    OperationResult upsertSubAgentJson(const std::string& requestBody) { return adminApiService_->upsertSubAgentJson(requestBody); }
    OperationResult removeSubAgentJson(const std::string& requestBody) { return adminApiService_->removeSubAgentJson(requestBody); }
    OperationResult upsertSubAgentGroupJson(const std::string& requestBody) { return adminApiService_->upsertSubAgentGroupJson(requestBody); }
    OperationResult removeSubAgentGroupJson(const std::string& requestBody) { return adminApiService_->removeSubAgentGroupJson(requestBody); }
    OperationResult upsertProviderAssignmentJson(const std::string& requestBody) { return adminApiService_->upsertProviderAssignmentJson(requestBody); }
    ProviderExecutionRecord executeProviderTaskJson(const std::string& requestBody) { return adminApiService_->executeProviderTaskJson(requestBody); }
    OperationResult installPackageJson(const std::string& requestBody) { return adminApiService_->installPackageJson(requestBody); }
    OperationResult installRepoJson(const std::string& requestBody) { return adminApiService_->installRepoJson(requestBody); }
    OperationResult installZipJson(const std::string& requestBody) { return adminApiService_->installZipJson(requestBody); }
    BeaconAdvertisement beaconAdvertisement() const { return beaconService_->currentAdvertisement(); }

private:
    void registerConfigurationDefaults();
    void createForsettiRuntime();
    void activateDefaultModules();
    HttpResponse handleHttpRequest(const HttpRequest& request);
    HttpResponse staticFileResponse(std::string path) const;

    template <typename T>
    static HttpResponse jsonResponse(const T& value, int statusCode = 200) {
        return HttpResponse{ statusCode, "application/json", nlohmann::json(value).dump(2) };
    }

    std::unique_ptr<ScopedWinsock> winsock_;
    AppPaths paths_;
    std::shared_ptr<SharedState> state_;
    std::shared_ptr<IConfigurationService> configurationService_;
    std::shared_ptr<IResourceAllocationService> resourceAllocationService_;
    std::shared_ptr<ITelemetryService> telemetryService_;
    std::shared_ptr<IRuntimeInventoryService> inventoryService_;
    std::shared_ptr<IPlatformServiceCatalogService> platformServiceCatalogService_;
    std::shared_ptr<IPackageTrustEvaluator> trustEvaluator_;
    std::shared_ptr<InstallerOrchestrator> installerOrchestrator_;
    std::shared_ptr<IProviderRegistry> providerRegistry_;
    std::shared_ptr<IProviderCatalogService> providerCatalogService_;
    std::shared_ptr<IProviderCredentialStore> providerCredentialStore_;
    std::shared_ptr<IMcpServerCatalogService> mcpServerCatalogService_;
    std::shared_ptr<ISubAgentCatalogService> subAgentCatalogService_;
    std::shared_ptr<ISubAgentGroupService> subAgentGroupService_;
    std::shared_ptr<IProviderAssignmentService> providerAssignmentService_;
    std::shared_ptr<IProviderExecutionCatalogService> providerExecutionCatalogService_;
    std::shared_ptr<IProviderExecutionService> providerExecutionService_;
    std::shared_ptr<IExportService> exportService_;
    std::shared_ptr<IPlatformGovernanceToolService> platformGovernanceToolService_;
    std::shared_ptr<ICommandLogicUnitService> commandLogicUnitService_;
    std::shared_ptr<IModuleControlSurfaceService> controlSurfaceService_;
    std::shared_ptr<IForsettiSurfaceService> surfaceService_;
    std::shared_ptr<IBeaconService> beaconService_;
    std::shared_ptr<IAdminApiService> adminApiService_;
    std::shared_ptr<Forsetti::UISurfaceManager> surfaceManager_;
    std::shared_ptr<Forsetti::IEntitlementProvider> entitlementProvider_;
    std::unique_ptr<Forsetti::ForsettiRuntime> runtime_;
    std::unique_ptr<SimpleHttpServer> httpServer_;
    std::atomic<bool> stopRequested_{ false };
    bool initialized_ = false;
};

bool MasterControlApplication::Impl::initialize() {
    configurationService_ = std::make_shared<FileBackedConfigurationService>(state_, paths_.configurationFile);
    resourceAllocationService_ = std::make_shared<ResourceAllocationService>(state_, paths_.configurationFile);
    telemetryService_ = std::make_shared<WindowsHostTelemetryService>();
    inventoryService_ = std::make_shared<RuntimeInventoryService>(state_);
    trustEvaluator_ = std::make_shared<PackageTrustEvaluator>(state_);
    installerOrchestrator_ = std::make_shared<InstallerOrchestrator>(state_, trustEvaluator_, paths_);
    providerCatalogService_ = std::make_shared<ProviderCatalogService>();
    providerRegistry_ = std::make_shared<ProviderRegistryService>(state_, paths_.configurationFile, providerCatalogService_);
    providerCredentialStore_ = std::make_shared<ProviderCredentialStore>(paths_.providerCredentialsFile, providerRegistry_, providerCatalogService_);
    mcpServerCatalogService_ = std::make_shared<McpServerCatalogService>(state_, paths_.configurationFile, inventoryService_);
    subAgentCatalogService_ = std::make_shared<SubAgentCatalogService>(state_, paths_.configurationFile, inventoryService_);
    subAgentGroupService_ = std::make_shared<SubAgentGroupService>(state_, paths_.configurationFile, inventoryService_);
    providerAssignmentService_ = std::make_shared<ProviderAssignmentService>(
        state_,
        paths_.configurationFile,
        inventoryService_,
        subAgentGroupService_,
        providerRegistry_);
    providerExecutionCatalogService_ = std::make_shared<ProviderExecutionCatalogService>();
    providerExecutionService_ = std::make_shared<ProviderExecutionService>(
        state_,
        providerRegistry_,
        providerCredentialStore_,
        providerAssignmentService_,
        inventoryService_,
        providerExecutionCatalogService_);
    exportService_ = std::make_shared<ExportService>(inventoryService_, configurationService_);
    platformServiceCatalogService_ = std::make_shared<PlatformServiceCatalogService>(configurationService_, telemetryService_);
    platformGovernanceToolService_ = std::make_shared<PlatformGovernanceToolService>(paths_);
    commandLogicUnitService_ = std::make_shared<CommandLogicUnitService>(
        paths_.cluProfileFile,
        configurationService_,
        providerRegistry_,
        providerAssignmentService_,
        installerOrchestrator_,
        exportService_,
        platformServiceCatalogService_,
        platformGovernanceToolService_);
    controlSurfaceService_ = std::make_shared<ModuleControlSurfaceService>();
    beaconService_ = std::make_shared<BeaconService>(configurationService_, telemetryService_, platformServiceCatalogService_);
    registerConfigurationDefaults();
    createForsettiRuntime();

    adminApiService_ = std::make_shared<AdminApiService>(
        telemetryService_,
        inventoryService_,
        configurationService_,
        platformServiceCatalogService_,
        providerRegistry_,
        providerCatalogService_,
        providerCredentialStore_,
        mcpServerCatalogService_,
        subAgentCatalogService_,
        subAgentGroupService_,
        providerAssignmentService_,
        providerExecutionCatalogService_,
        providerExecutionService_,
        installerOrchestrator_,
        installerOrchestrator_,
        installerOrchestrator_,
        exportService_,
        commandLogicUnitService_,
        surfaceService_);

    runtime_->boot();
    if (entitlementProvider_) {
        entitlementProvider_->onEntitlementsChanged([this]() {
            if (!runtime_ || !runtime_->isBooted()) {
                return;
            }
            activateDefaultModules();
        });
    }
    activateDefaultModules();

    if (configurationService_->current().beaconEnabled) {
        beaconService_->start();
    }

    const auto currentConfiguration = configurationService_->current();
    httpServer_ = std::make_unique<SimpleHttpServer>(
        currentConfiguration.bindAddress == "0.0.0.0" ? "0.0.0.0" : currentConfiguration.bindAddress,
        currentConfiguration.browserPort,
        [this](const HttpRequest& request) { return handleHttpRequest(request); });
    httpServer_->start();
    initialized_ = true;
    return true;
}

void MasterControlApplication::Impl::shutdown() {
    if (!initialized_) {
        return;
    }
    if (httpServer_) {
        httpServer_->stop();
    }
    if (beaconService_) {
        beaconService_->stop();
    }
    if (runtime_) {
        runtime_->shutdown();
    }
    initialized_ = false;
}

int MasterControlApplication::Impl::runInteractive() {
    while (!stopRequested_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return 0;
}

std::string MasterControlApplication::Impl::browserUrl() const {
    const auto configuration = configurationService_->current();
    const auto host = configuration.bindAddress == "0.0.0.0" ? "127.0.0.1" : configuration.bindAddress;
    return "http://" + host + ":" + std::to_string(configuration.browserPort) + "/";
}

void MasterControlApplication::Impl::registerConfigurationDefaults() {
    const auto currentConfiguration = configurationService_->current();
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->configuration = currentConfiguration;
    if (state_->configuration.providers.empty()) {
        state_->configuration.providers = buildDefaultProviders();
        writeJsonFile(paths_.configurationFile, state_->configuration);
    }
}

void MasterControlApplication::Impl::createForsettiRuntime() {
    auto services = std::make_shared<Forsetti::ServiceContainer>();
    Forsetti::DefaultForsettiPlatformServices::registerAll(*services);
    services->registerService<IConfigurationService>(configurationService_);
    services->registerService<IResourceAllocationService>(resourceAllocationService_);
    services->registerService<ITelemetryService>(telemetryService_);
    services->registerService<IRuntimeInventoryService>(inventoryService_);
    services->registerService<IPlatformServiceCatalogService>(platformServiceCatalogService_);
    services->registerService<IPackageTrustEvaluator>(trustEvaluator_);
    services->registerService<IInstallerOrchestrator>(installerOrchestrator_);
    services->registerService<IBootstrapRepoService>(installerOrchestrator_);
    services->registerService<IZipBundleService>(installerOrchestrator_);
    services->registerService<IProviderRegistry>(providerRegistry_);
    services->registerService<IProviderCatalogService>(providerCatalogService_);
    services->registerService<IProviderCredentialStore>(providerCredentialStore_);
    services->registerService<IMcpServerCatalogService>(mcpServerCatalogService_);
    services->registerService<ISubAgentCatalogService>(subAgentCatalogService_);
    services->registerService<ISubAgentGroupService>(subAgentGroupService_);
    services->registerService<IProviderAssignmentService>(providerAssignmentService_);
    services->registerService<IProviderExecutionCatalogService>(providerExecutionCatalogService_);
    services->registerService<IProviderExecutionService>(providerExecutionService_);
    services->registerService<IExportService>(exportService_);
    services->registerService<IPlatformGovernanceToolService>(platformGovernanceToolService_);
    services->registerService<ICommandLogicUnitService>(commandLogicUnitService_);
    services->registerService<IModuleControlSurfaceService>(controlSurfaceService_);
    services->registerService<IBeaconService>(beaconService_);

    auto eventBus = std::make_shared<Forsetti::InMemoryEventBus>();
    auto logger = std::make_shared<Forsetti::ConsoleLogger>();
    auto router = std::make_shared<Forsetti::NoopOverlayRouter>();
    auto guard = std::make_shared<Forsetti::DefaultModuleCommunicationGuard>();
    auto context = std::make_shared<Forsetti::ForsettiContext>(services, eventBus, logger, router, guard);
    surfaceManager_ = std::make_shared<Forsetti::UISurfaceManager>();
    surfaceService_ = std::make_shared<ForsettiSurfaceService>(surfaceManager_, controlSurfaceService_);
    services->registerService<IForsettiSurfaceService>(surfaceService_);
    auto compatibilityPolicy = std::make_shared<Forsetti::AllowAllCapabilityPolicy>();
    auto checker = std::make_shared<Forsetti::CompatibilityChecker>(Forsetti::SemVer{ 0, 1, 0 }, compatibilityPolicy);
    entitlementProvider_ = std::make_shared<FileBackedEntitlementProvider>(
        paths_.entitlementsFile,
        buildDefaultEntitlementStateDocument());
    auto activationStore = std::make_shared<JsonActivationStore>(paths_.dataDirectory / "state" / "activation-state.json");

    auto registry = Forsetti::ForsettiStaticModuleRegistry::buildRegistry([](Forsetti::ModuleRegistry& registry) {
        registerMasterControlModules(registry);
    });

    auto moduleManager = std::make_unique<Forsetti::ModuleManager>(
        std::move(registry),
        checker,
        entitlementProvider_,
        activationStore,
        surfaceManager_,
        context);

    runtime_ = std::make_unique<Forsetti::ForsettiRuntime>(
        std::move(moduleManager),
        entitlementProvider_,
        eventBus,
        paths_.manifestsDirectory.string());
}

void MasterControlApplication::Impl::activateDefaultModules() {
    static const std::array<const char*, 19> moduleIds = {
        "com.mastercontrol.environment-discovery",
        "com.mastercontrol.host-telemetry",
        "com.mastercontrol.runtime-inventory",
        "com.mastercontrol.configuration",
        "com.mastercontrol.installer-import",
        "com.mastercontrol.provider-integration",
        "com.mastercontrol.provider-codex",
        "com.mastercontrol.provider-claude-code",
        "com.mastercontrol.provider-xai",
        "com.mastercontrol.export",
        "com.mastercontrol.command-logic-unit",
        "com.mastercontrol.gateway-windows",
        "com.mastercontrol.gateway-macos",
        "com.mastercontrol.gateway-ios",
        "com.mastercontrol.governance-windows",
        "com.mastercontrol.governance-macos",
        "com.mastercontrol.governance-ios",
        "com.mastercontrol.beacon-gateway",
        "com.mastercontrol.dashboard-ui"
    };

    for (const auto* moduleId : moduleIds) {
        if (!runtime_->moduleManager().isModuleActive(moduleId)) {
            try {
                runtime_->activateModule(moduleId);
            } catch (const std::exception&) {
            }
        }
    }
}

HttpResponse MasterControlApplication::Impl::handleHttpRequest(const HttpRequest& request) {
    try {
        const auto gateways = platformServiceCatalogService_
            ? platformServiceCatalogService_->listGateways()
            : std::vector<PlatformGatewayDescriptor>{};
        const auto governanceServers = platformServiceCatalogService_
            ? platformServiceCatalogService_->listGovernanceServers()
            : std::vector<GovernanceServerDescriptor>{};
        const auto findGateway = [&gateways](const PlatformTarget platform) -> std::optional<PlatformGatewayDescriptor> {
            const auto iterator = std::find_if(
                gateways.begin(),
                gateways.end(),
                [platform](const PlatformGatewayDescriptor& descriptor) { return descriptor.platform == platform; });
            if (iterator == gateways.end()) {
                return std::nullopt;
            }
            return *iterator;
        };
        const auto findGovernanceServer =
            [&governanceServers](const PlatformTarget platform) -> std::optional<GovernanceServerDescriptor> {
            const auto iterator = std::find_if(
                governanceServers.begin(),
                governanceServers.end(),
                [platform](const GovernanceServerDescriptor& descriptor) { return descriptor.platform == platform; });
            if (iterator == governanceServers.end()) {
                return std::nullopt;
            }
            return *iterator;
        };
        const auto makeBaseUrl = [this]() {
            const auto configuration = configurationService_->current();
            const auto telemetry = telemetryService_->captureSnapshot();
            std::string host = configuration.bindAddress;
            if (host.empty() || host == "0.0.0.0") {
                host = telemetry.primaryIpAddress.empty() ? std::string("127.0.0.1") : telemetry.primaryIpAddress;
            }
            return std::string("http://") + host + ":" + std::to_string(configuration.browserPort);
        };
        const auto makePlatformConfigDocument = [&, this](const PlatformTarget platform) {
            const auto gateway = findGateway(platform);
            const auto governance = findGovernanceServer(platform);
            if (!gateway.has_value() || !governance.has_value()) {
                return nlohmann::json{};
            }

            const auto baseUrl = makeBaseUrl();
            const auto gatewayUrl = baseUrl + gateway->gatewayPath;
            const auto configUrl = baseUrl + gateway->configPath;
            const auto governanceUrl = baseUrl + governance->routePath;

            return nlohmann::json{
                { "platform", platformKey(platform) },
                { "serviceId", gateway->serviceId },
                { "displayName", gateway->displayName },
                { "serviceType", gateway->serviceType },
                { "browserUrl", browserUrl() },
                { "gatewayUrl", gatewayUrl },
                { "governanceUrl", governanceUrl },
                { "configUrl", configUrl },
                { "lanServiceDiscovery", {
                    { "mode", "dns-sd" },
                    { "serviceType", gateway->serviceType },
                    { "instanceLabel", gateway->instanceLabel },
                    { "hostName", gateway->hostName },
                    { "port", gateway->port }
                } },
                { "clientSelectionRule", "Clients should discover and connect to the service that matches their platform OS." },
                { "governanceTools", governance->toolIds },
                { "agentConfigurations", nlohmann::json::array({
                    {
                        { "provider", "codex" },
                        { "credentialFields", nlohmann::json::array({ "OPENAI_API_KEY" }) },
                        { "recommendedModel", "gpt-5.4" },
                        { "mcp", {
                            { "name", gateway->serviceId },
                            { "transport", "http" },
                            { "url", gatewayUrl }
                        } }
                    },
                    {
                        { "provider", "claude-code" },
                        { "credentialFields", nlohmann::json::array({ "ANTHROPIC_API_KEY", "ANTHROPIC_AUTH_TOKEN" }) },
                        { "mcp", {
                            { "mcpServers", {
                                { gateway->serviceId, {
                                    { "type", "http" },
                                    { "url", gatewayUrl }
                                } }
                            } }
                        } }
                    },
                    {
                        { "provider", "xai" },
                        { "credentialFields", nlohmann::json::array({ "XAI_API_KEY" }) },
                        { "recommendedModel", "grok-code-fast-1" },
                        { "mcp", {
                            { "gateway", gatewayUrl },
                            { "provider", "xai" },
                            { "recommendedModel", "grok-code-fast-1" }
                        } }
                    }
                }) }
            };
        };
        if (request.method == "GET" && request.path == "/api/health") {
            return jsonResponse(nlohmann::json{ { "status", "ok" }, { "time", timestampNowUtc() } });
        }
        if (request.method == "GET" && request.path == "/api/dashboard") {
            return jsonResponse(snapshot());
        }
        if (request.method == "GET" && request.path == "/api/config") {
            return jsonResponse(configurationService_->current());
        }
        if (request.method == "GET" && request.path == "/api/providers") {
            return jsonResponse(providerRegistry_->listProviders());
        }
        if (request.method == "GET" && request.path == "/api/exports") {
            return jsonResponse(exportService_->generateExports());
        }
        if (request.method == "GET" && request.path == "/api/clu") {
            return jsonResponse(commandLogicUnitService_->currentGovernance());
        }
        if (request.method == "GET" && request.path == "/api/clu/tools") {
            return jsonResponse(commandLogicUnitService_->currentGovernance().availableTools);
        }
        if (request.method == "GET" && request.path == "/api/forsetti/surface") {
            return jsonResponse(surfaceService_->currentSurface());
        }
        if (request.method == "GET" && request.path == "/api/install/history") {
            return jsonResponse(installerOrchestrator_->history());
        }
        if (request.method == "GET" && request.path == "/api/beacon") {
            return jsonResponse(beaconService_->currentAdvertisement());
        }
        if (request.method == "GET" && request.path == "/api/platform-services") {
            return jsonResponse(nlohmann::json{
                { "gateways", gateways },
                { "governanceServers", governanceServers }
            });
        }
        if (request.method == "GET" && request.path == "/api/platform-services/gateways") {
            return jsonResponse(gateways);
        }
        if (request.method == "GET" && request.path == "/api/platform-services/governance") {
            return jsonResponse(governanceServers);
        }
        if (request.method == "GET" && startsWith(request.path, "/api/platform-services/config/")) {
            const auto platform = platformFromKey(request.path.substr(std::string("/api/platform-services/config/").size()));
            const auto document = makePlatformConfigDocument(platform);
            if (platform == PlatformTarget::Unknown || document.empty()) {
                return jsonResponse(nlohmann::json{ { "message", "Platform service config not found." } }, 404);
            }
            return jsonResponse(document);
        }
        if (request.method == "GET" && startsWith(request.path, "/mcp/gateway/")) {
            const auto platform = platformFromKey(request.path.substr(std::string("/mcp/gateway/").size()));
            const auto gateway = findGateway(platform);
            const auto governance = findGovernanceServer(platform);
            if (!gateway.has_value() || !governance.has_value()) {
                return jsonResponse(nlohmann::json{ { "message", "Gateway service not found." } }, 404);
            }
            return jsonResponse(nlohmann::json{
                { "service", "platform-gateway" },
                { "platform", platformKey(platform) },
                { "gateway", *gateway },
                { "governanceServer", *governance },
                { "configuration", makePlatformConfigDocument(platform) }
            });
        }
        if (request.method == "GET" && startsWith(request.path, "/mcp/governance/")) {
            const auto platform = platformFromKey(request.path.substr(std::string("/mcp/governance/").size()));
            const auto governance = findGovernanceServer(platform);
            const auto gateway = findGateway(platform);
            if (!gateway.has_value() || !governance.has_value()) {
                return jsonResponse(nlohmann::json{ { "message", "Governance service not found." } }, 404);
            }
            const auto governanceSnapshot = commandLogicUnitService_->currentGovernance();
            nlohmann::json platformTools = nlohmann::json::array();
            for (const auto& tool : governanceSnapshot.availableTools) {
                if (tool.platform == platform) {
                    platformTools.push_back(tool);
                }
            }
            return jsonResponse(nlohmann::json{
                { "service", "governance-mcp-server" },
                { "platform", platformKey(platform) },
                { "gateway", *gateway },
                { "governanceServer", *governance },
                { "toolIds", governance->toolIds },
                { "tools", platformTools },
                { "requiresRemoteToolchain", governance->requiresRemoteToolchain }
            });
        }
        if (request.method == "POST" && request.path == "/api/config") {
            const bool confirmUnsafeChanges = request.headers.contains("X-Confirm-Unsafe") &&
                request.headers.at("X-Confirm-Unsafe") == "1";
            const auto result = adminApiService_->applyConfigurationJson(request.body, confirmUnsafeChanges);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/providers") {
            const auto result = adminApiService_->upsertProviderJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/providers/credentials") {
            const auto result = adminApiService_->upsertProviderCredentialsJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/runtime/mcp-servers") {
            const auto result = adminApiService_->upsertMcpServerJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/runtime/mcp-servers/remove") {
            const auto result = adminApiService_->removeMcpServerJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/runtime/subagents") {
            const auto result = adminApiService_->upsertSubAgentJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/runtime/subagents/remove") {
            const auto result = adminApiService_->removeSubAgentJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/providers/groups") {
            const auto result = adminApiService_->upsertSubAgentGroupJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/providers/groups/remove") {
            const auto result = adminApiService_->removeSubAgentGroupJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/providers/assignments") {
            const auto result = adminApiService_->upsertProviderAssignmentJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/providers/execute") {
            const auto result = adminApiService_->executeProviderTaskJson(request.body);
            return jsonResponse(result, result.status == ProviderExecutionStatus::Succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/clu/execute") {
            const auto result = adminApiService_->executeGovernanceToolJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/install/package") {
            const auto result = adminApiService_->installPackageJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/install/repo") {
            const auto result = adminApiService_->installRepoJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && request.path == "/api/install/zip") {
            const auto result = adminApiService_->installZipJson(request.body);
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }
        if (request.method == "POST" && startsWith(request.path, "/mcp/governance/")) {
            const auto routePlatform = platformFromKey(request.path.substr(std::string("/mcp/governance/").size()));
            auto payload = request.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(request.body);
            if (!payload.contains("platform") || platformFromKey(payload.value("platform", "")) == PlatformTarget::Unknown) {
                payload["platform"] = platformKey(routePlatform);
            }
            const auto result = commandLogicUnitService_->executeGovernanceTool(payload.get<GovernanceToolRequest>());
            return jsonResponse(result, result.succeeded ? 200 : 400);
        }

        return staticFileResponse(request.path);
    } catch (const std::exception& exception) {
        return jsonResponse(nlohmann::json{ { "succeeded", false }, { "message", exception.what() } }, 500);
    }
}

HttpResponse MasterControlApplication::Impl::staticFileResponse(std::string path) const {
    if (path.empty() || path == "/") {
        path = "/index.html";
    }
    if (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }

    const auto filePath = paths_.webRootDirectory / path;
    if (!std::filesystem::exists(filePath)) {
        return HttpResponse{ 404, "application/json", R"({"message":"Not found"})" };
    }

    return HttpResponse{ 200, contentTypeForPath(filePath), readTextFile(filePath) };
}

MasterControlApplication::MasterControlApplication()
    : impl_(std::make_unique<Impl>()) {}

MasterControlApplication::~MasterControlApplication() {
    shutdown();
}

bool MasterControlApplication::initialize() {
    return impl_->initialize();
}

void MasterControlApplication::shutdown() {
    impl_->shutdown();
}

void MasterControlApplication::requestStop() {
    impl_->requestStop();
}

int MasterControlApplication::runInteractive() {
    return impl_->runInteractive();
}

std::string MasterControlApplication::browserUrl() const {
    return impl_->browserUrl();
}

DashboardSnapshot MasterControlApplication::snapshot() {
    return impl_->snapshot();
}

GovernanceToolResult MasterControlApplication::executeGovernanceToolJson(const std::string& requestBody) {
    return impl_->executeGovernanceToolJson(requestBody);
}

OperationResult MasterControlApplication::applyConfigurationJson(const std::string& requestBody, bool confirmUnsafeChanges) {
    return impl_->applyConfigurationJson(requestBody, confirmUnsafeChanges);
}

OperationResult MasterControlApplication::upsertProviderJson(const std::string& requestBody) {
    return impl_->upsertProviderJson(requestBody);
}

OperationResult MasterControlApplication::upsertProviderCredentialsJson(const std::string& requestBody) {
    return impl_->upsertProviderCredentialsJson(requestBody);
}

OperationResult MasterControlApplication::upsertMcpServerJson(const std::string& requestBody) {
    return impl_->upsertMcpServerJson(requestBody);
}

OperationResult MasterControlApplication::removeMcpServerJson(const std::string& requestBody) {
    return impl_->removeMcpServerJson(requestBody);
}

OperationResult MasterControlApplication::upsertSubAgentJson(const std::string& requestBody) {
    return impl_->upsertSubAgentJson(requestBody);
}

OperationResult MasterControlApplication::removeSubAgentJson(const std::string& requestBody) {
    return impl_->removeSubAgentJson(requestBody);
}

OperationResult MasterControlApplication::upsertSubAgentGroupJson(const std::string& requestBody) {
    return impl_->upsertSubAgentGroupJson(requestBody);
}

OperationResult MasterControlApplication::removeSubAgentGroupJson(const std::string& requestBody) {
    return impl_->removeSubAgentGroupJson(requestBody);
}

OperationResult MasterControlApplication::upsertProviderAssignmentJson(const std::string& requestBody) {
    return impl_->upsertProviderAssignmentJson(requestBody);
}

ProviderExecutionRecord MasterControlApplication::executeProviderTaskJson(const std::string& requestBody) {
    return impl_->executeProviderTaskJson(requestBody);
}

OperationResult MasterControlApplication::installPackageJson(const std::string& requestBody) {
    return impl_->installPackageJson(requestBody);
}

OperationResult MasterControlApplication::installRepoJson(const std::string& requestBody) {
    return impl_->installRepoJson(requestBody);
}

OperationResult MasterControlApplication::installZipJson(const std::string& requestBody) {
    return impl_->installZipJson(requestBody);
}

BeaconAdvertisement MasterControlApplication::beaconAdvertisement() const {
    return impl_->beaconAdvertisement();
}

} // namespace MasterControl
