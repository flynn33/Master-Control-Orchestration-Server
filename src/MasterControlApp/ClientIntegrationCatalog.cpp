// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

// Model Parity (A3.12.0): provider-neutral Client Integration Catalog.
//
// This translation unit owns the concrete client integration providers, the
// deterministic catalog, and the remote-MCP compatibility analyzer. Everything
// concrete lives in an anonymous namespace and is reached only through the two
// factory functions declared in MasterControlContracts.h. MCOS stays a governed
// MCP gateway: providers emit provider-native config artifacts and report
// transport/auth compatibility honestly; they never execute a provider SDK.

#include "MasterControl/MasterControlContracts.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace MasterControl {
namespace {

// ---------------------------------------------------------------------------
// Small shared helpers.
// ---------------------------------------------------------------------------

bool contains(const std::vector<std::string>& values, std::string_view needle) {
    return std::find(values.begin(), values.end(), needle) != values.end();
}

std::string toLower(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const char ch : value) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lowered;
}

// A JSON-object onboarding snippet (mcpServers-style config).
OnboardingConfigSnippet jsonSnippet(std::string description,
                                    std::string filename,
                                    nlohmann::json content) {
    OnboardingConfigSnippet snippet;
    snippet.format = "json";
    snippet.description = std::move(description);
    snippet.filename = std::move(filename);
    snippet.content = std::move(content);
    return snippet;
}

// A text snippet (TOML / markdown / shell). content is stored as a JSON string
// so the OnboardingConfigSnippet model (content is nlohmann::json) carries the
// raw text verbatim; format tells clients how to interpret it.
OnboardingConfigSnippet textSnippet(std::string format,
                                    std::string description,
                                    std::string filename,
                                    std::string content) {
    OnboardingConfigSnippet snippet;
    snippet.format = std::move(format);
    snippet.description = std::move(description);
    snippet.filename = std::move(filename);
    snippet.content = std::move(content);
    return snippet;
}

ClientExportArtifact artifact(std::string fileName,
                              std::string mediaType,
                              std::string content,
                              std::string purpose) {
    ClientExportArtifact a;
    a.fileName = std::move(fileName);
    a.mediaType = std::move(mediaType);
    a.content = std::move(content);
    a.purpose = std::move(purpose);
    a.containsSecret = false; // MCOS never writes real secrets; placeholders only.
    return a;
}

GatewayRuntimeDescriptor gatewayDescriptorFrom(const ClientIntegrationContext& c) {
    GatewayRuntimeDescriptor g;
    g.mcpUrl = c.gatewayMcpUrl;
    g.tlsEnabled = c.gatewayTlsEnabled;
    g.lanOnly = c.lanOnly;
    g.sseAvailable = c.sseAvailable;
    g.streamableHttpPostOnly = c.streamableHttpPostOnly;
    g.destructiveToolsAvailable = c.destructiveToolsAvailable;
    g.authModel = "local-trust";
    return g;
}

// The URL a hosted/public client should target. When the live gateway is LAN
// only (or plain HTTP), we emit a public/edge placeholder rather than a dead
// LAN URL the hosted client could never reach.
std::string publicFacingUrl(const ClientIntegrationContext& c) {
    if (!c.lanOnly && c.gatewayTlsEnabled && !c.gatewayMcpUrl.empty()) {
        return c.gatewayMcpUrl;
    }
    return "https://<public-or-edge-mcos-host>/mcp";
}

// ---------------------------------------------------------------------------
// Remote-MCP compatibility analyzer.
// ---------------------------------------------------------------------------

class RemoteMcpCompatibilityAnalyzer final : public IRemoteMcpCompatibilityAnalyzer {
public:
    RemoteMcpCompatibilityResult analyze(const ClientIntegrationDescriptor& integration,
                                         const GatewayRuntimeDescriptor& gateway) const override {
        RemoteMcpCompatibilityResult result;
        result.integrationId = integration.id;
        bool blocking = false;

        const auto warn = [&](const char* code, const char* severity, std::string message) {
            result.warnings.push_back(ClientIntegrationWarning{ code, severity, std::move(message) });
        };

        // A hosted/public client cannot reach a LAN-only endpoint at all.
        if (integration.requiresPublicHttps && gateway.lanOnly) {
            blocking = true;
            result.requiresEdgeBridge = true;
            warn("endpoint_requires_public_https", "blocking",
                 integration.displayName + " cannot reach a LAN-only MCOS endpoint. "
                 "Configure an approved public HTTPS connector edge before using this profile.");
        } else if (integration.requiresPublicHttps && !gateway.tlsEnabled) {
            blocking = true;
            warn("endpoint_requires_https", "blocking",
                 integration.displayName + " requires an HTTPS MCP endpoint. The current gateway "
                 "URL is plain HTTP; terminate TLS at an approved edge or enable the gateway TLS dual-bind.");
        }

        // Be transparent about the POST-only Streamable HTTP subset.
        if (gateway.streamableHttpPostOnly) {
            warn("gateway_post_only_streamable_http", "info",
                 "The MCOS gateway serves the POST-only Streamable HTTP subset "
                 "(single JSON responses; GET /mcp returns 405 Allow: POST). "
                 "No server-initiated SSE stream is offered.");
        }

        // Clients that expect a Streaming HTTP / SSE transport.
        const bool wantsStreamingOrSse =
            contains(integration.supportedTransports, "sse")
            || contains(integration.requiredEndpointProperties, "streaming_http_or_sse");
        if (wantsStreamingOrSse && !gateway.sseAvailable) {
            warn("sse_not_offered_by_gateway", "advisory",
                 integration.displayName + " expects a Streaming HTTP / SSE transport. The gateway "
                 "is POST-only today; use an edge that upgrades the transport, or a client mode that "
                 "accepts the POST-only Streamable HTTP subset.");
        }

        // Tool-approval enforcement responsibility.
        if (!integration.supportsClientApprovalPolicy) {
            warn("mcos_enforced_tool_approval", "advisory",
                 "This client does not enforce per-tool approval itself. MCOS must gate mutating / "
                 "destructive tools via allow-lists, governance policy, or confirm guards.");
        }

        // Destructive tools reachable through the gateway.
        if (gateway.destructiveToolsAvailable) {
            warn("destructive_tools_require_guard", "advisory",
                 "Destructive tools are reachable through the gateway. Hide them or require operator "
                 "confirmation for this integration.");
        }

        result.compatible = !blocking;
        return result;
    }
};

// ---------------------------------------------------------------------------
// Provider base: holds the analyzer (composition) and shares validate() plus a
// base-profile builder. Concrete providers are `final` and only supply their
// id, descriptor, onboarding profile, and export artifacts.
// ---------------------------------------------------------------------------

class ProviderBase : public IClientIntegrationProvider {
public:
    explicit ProviderBase(std::shared_ptr<const IRemoteMcpCompatibilityAnalyzer> analyzer)
        : analyzer_(std::move(analyzer)) {}

    ClientIntegrationValidationResult validate(const ClientIntegrationContext& context) const override {
        const ClientIntegrationDescriptor desc = descriptor();
        ClientIntegrationValidationResult result;
        result.integrationId = desc.id;
        if (analyzer_) {
            const RemoteMcpCompatibilityResult analysis =
                analyzer_->analyze(desc, gatewayDescriptorFrom(context));
            result.compatible = analysis.compatible;
            result.warnings = analysis.warnings;
        } else {
            result.compatible = true;
        }
        return result;
    }

protected:
    // Populate the transport-neutral base fields every OnboardingProfile shares,
    // resolved from the live context (discovery + configuration).
    OnboardingProfile baseProfile(const ClientIntegrationContext& context,
                                  std::string displayName) const {
        OnboardingProfile profile;
        profile.clientType = id();
        profile.displayName = std::move(displayName);
        profile.gatewayMcpUrl = context.gatewayMcpUrl;
        profile.mcp.url = context.gatewayMcpUrl;
        profile.mcp.transport = "streamable_http";
        profile.mcp.protocolVersion = context.protocolVersion;
        profile.mcp.authRequired = false;
        profile.transport = "streamable_http";
        profile.authRequired = false;
        profile.trust = "lan";
        profile.governanceBundleUrl = context.governanceBundleUrl;
        profile.discoveryUrl = context.discoveryUrl;
        profile.instanceId = context.instanceId;
        return profile;
    }

    std::shared_ptr<const IRemoteMcpCompatibilityAnalyzer> analyzer_;
};

// ===========================================================================
// 1. Claude Code (Anthropic) -- existing first-class support, migrated behind
//    the catalog without regression.
// ===========================================================================
class ClaudeCodeIntegrationProvider final : public ProviderBase {
public:
    using ProviderBase::ProviderBase;

    std::string id() const override { return "claude-code"; }

    ClientIntegrationDescriptor descriptor() const override {
        ClientIntegrationDescriptor d;
        d.id = id();
        d.displayName = "Claude Code (Anthropic)";
        d.vendor = "Anthropic";
        d.products = { "Claude Code", "Claude Code Desktop" };
        d.aliases = { "claude", "claude_code", "claudecode" };
        d.supportedTransports = { "streamable_http" };
        d.requiredEndpointProperties = { "lan_http_reachable" };
        d.authModels = { "local-trust" };
        d.generatedArtifacts = { ".mcp.json", ".claude-plugin package" };
        d.verificationSteps = {
            "Restart Claude Code so it picks up the new MCP server entry.",
            "Run the MCOS tool listing to confirm gateway tool aggregation.",
            "Confirm GET /.well-known/mcos.json reports gateway.state=running."
        };
        d.caveats = {
            "Claude Code consumes Streamable HTTP MCP servers natively; no companion utility is required.",
            "Transport (alpha): POST-only Streamable HTTP subset; GET /mcp returns 405 Allow: POST."
        };
        d.requiresPublicHttps = false;
        d.supportsLanHttp = true;
        d.supportsMcpServerInstructions = true;
        d.supportsToolAllowList = true;
        d.supportsClientApprovalPolicy = true; // Claude Code has its own confirm/approval UX.
        return d;
    }

    OnboardingProfile createOnboardingProfile(const ClientIntegrationContext& context) const override {
        OnboardingProfile profile = baseProfile(context, "Claude Code (Anthropic)");
        profile.configSnippets.push_back(jsonSnippet(
            "Add MCOS as a Streamable HTTP MCP server in Claude Code's user settings. Drop this "
            "fragment into the mcpServers map of your config.",
            ".mcp.json",
            { { "mcpServers", { { "mcos", {
                { "url", context.gatewayMcpUrl },
                { "transport", "streamable_http" }
            } } } } }));
        profile.manualInstructions = {
            "Open Claude Code settings and add the MCOS MCP server using the JSON fragment above.",
            "No bearer token or app-layer login is required on the trusted LAN gateway.",
            "Load the CLU/Forsetti governance bundle from the URL above before granting Claude Code mutating access."
        };
        profile.verificationSteps = descriptor().verificationSteps;
        profile.caveats = descriptor().caveats;
        return profile;
    }

    std::vector<ClientExportArtifact> createExportArtifacts(const ClientIntegrationContext& context) const override {
        nlohmann::json claudeJson = {
            { "mcpServers", { { "mcos", {
                { "type", "http" },
                { "url", context.gatewayMcpUrl }
            } } } }
        };
        return {
            artifact(".mcp.json", "application/json", claudeJson.dump(2),
                     "Claude Code MCP server registration (Streamable HTTP).")
        };
    }
};

// ===========================================================================
// 2. Codex (OpenAI) -- MUST use current config.toml, not legacy JSON.
// ===========================================================================
class CodexIntegrationProvider final : public ProviderBase {
public:
    using ProviderBase::ProviderBase;

    std::string id() const override { return "codex"; }

    ClientIntegrationDescriptor descriptor() const override {
        ClientIntegrationDescriptor d;
        d.id = id();
        d.displayName = "Codex CLI / IDE (OpenAI)";
        d.vendor = "OpenAI";
        d.products = { "Codex CLI", "Codex IDE extension" };
        d.aliases = {};
        d.supportedTransports = { "streamable_http" };
        d.requiredEndpointProperties = { "lan_http_reachable" };
        d.authModels = { "local-trust", "bearer-env" };
        d.generatedArtifacts = { "~/.codex/config.toml", ".codex/config.toml" };
        d.verificationSteps = {
            "codex mcp list",
            "codex mcp --help",
            "Run a Codex session and confirm tool calls land at the MCOS gateway URL."
        };
        d.caveats = {
            "Codex MCP configuration is TOML (config.toml). Legacy JSON (codex.config.json / "
            "codex-mcp.json) is not the current primary format.",
            "User scope: ~/.codex/config.toml. Project scope: .codex/config.toml. CLI and IDE share config."
        };
        d.requiresPublicHttps = false;
        d.supportsLanHttp = true;
        d.supportsMcpServerInstructions = true;
        d.supportsToolAllowList = false;
        d.supportsClientApprovalPolicy = true; // Codex approves tool calls in-session.
        return d;
    }

    static std::string codexToml(const std::string& url) {
        std::ostringstream toml;
        toml << "[mcp_servers.mcos]\n"
             << "url = \"" << url << "\"\n"
             << "enabled = true\n"
             << "startup_timeout_sec = 10\n"
             << "tool_timeout_sec = 60\n";
        return toml.str();
    }

    OnboardingProfile createOnboardingProfile(const ClientIntegrationContext& context) const override {
        OnboardingProfile profile = baseProfile(context, "Codex CLI / IDE (OpenAI)");
        profile.configSnippets.push_back(textSnippet(
            "toml",
            "Codex reads MCP servers from config.toml. Add this table to ~/.codex/config.toml "
            "(user scope) or .codex/config.toml (project scope). If auth is configured, use an "
            "environment-variable placeholder rather than writing secrets.",
            "config.toml",
            codexToml(context.gatewayMcpUrl)));
        profile.manualInstructions = {
            "Add the [mcp_servers.mcos] table above to ~/.codex/config.toml (user) or .codex/config.toml (project).",
            "Restart Codex (CLI or IDE) so it re-reads config.toml.",
            "Authenticate Codex with OpenAI as usual; MCOS does not collect or proxy your OpenAI credentials."
        };
        profile.verificationSteps = descriptor().verificationSteps;
        profile.caveats = descriptor().caveats;
        return profile;
    }

    std::vector<ClientExportArtifact> createExportArtifacts(const ClientIntegrationContext& context) const override {
        return {
            artifact("config.toml", "application/toml", codexToml(context.gatewayMcpUrl),
                     "Codex MCP server table for ~/.codex/config.toml (user) or .codex/config.toml (project).")
        };
    }
};

// ===========================================================================
// 3. Codex as an external MCP server -- external process adapter profile.
// ===========================================================================
class CodexMcpServerIntegrationProvider final : public ProviderBase {
public:
    using ProviderBase::ProviderBase;

    std::string id() const override { return "codex-mcp-server"; }

    ClientIntegrationDescriptor descriptor() const override {
        ClientIntegrationDescriptor d;
        d.id = id();
        d.displayName = "Codex as external MCP server";
        d.vendor = "OpenAI";
        d.products = { "Codex as external MCP server" };
        d.supportedTransports = { "stdio" };
        d.requiredEndpointProperties = { "external_process_adapter" };
        d.authModels = { "local-trust" };
        d.generatedArtifacts = { "codex-mcp-server-notes.md" };
        d.verificationSteps = {
            "Run Codex as a subordinate external process behind an MCOS process adapter.",
            "Confirm the adapter speaks MCP over stdio and never executes inside the MCOS core runtime."
        };
        d.caveats = {
            "Codex execution stays out of the MCOS core runtime; it runs as a supervised external process adapter.",
            "This profile is future orchestration scaffolding, not an embedded provider executor."
        };
        d.supportsLanHttp = false;
        d.supportsClientApprovalPolicy = true;
        return d;
    }

    OnboardingProfile createOnboardingProfile(const ClientIntegrationContext& context) const override {
        OnboardingProfile profile = baseProfile(context, "Codex as external MCP server");
        profile.transport = "stdio_bridge";
        profile.mcp.transport = "stdio_bridge";
        profile.configSnippets.push_back(textSnippet(
            "markdown",
            "Codex-as-MCP-server runs behind an external process adapter. Keep execution outside the "
            "MCOS core runtime.",
            "codex-mcp-server-notes.md",
            codexServerNotes()));
        profile.manualInstructions = {
            "Configure an MCOS external-process adapter to launch Codex as a subordinate MCP server.",
            "Bridge the adapter's stdio JSON-RPC to the governed MCOS surface; do not embed Codex in the runtime."
        };
        profile.verificationSteps = descriptor().verificationSteps;
        profile.caveats = descriptor().caveats;
        return profile;
    }

    std::vector<ClientExportArtifact> createExportArtifacts(const ClientIntegrationContext&) const override {
        return {
            artifact("codex-mcp-server-notes.md", "text/markdown", codexServerNotes(),
                     "External-process adapter guidance for running Codex as a subordinate MCP server.")
        };
    }

private:
    static std::string codexServerNotes() {
        return "# Codex as an external MCP server\n\n"
               "MCOS can orchestrate Codex as a subordinate external process rather than embedding it.\n\n"
               "- Run Codex behind an MCOS external-process adapter.\n"
               "- The adapter speaks MCP over stdio (JSON-RPC on stdin/stdout).\n"
               "- Codex execution never runs inside the MCOS core runtime.\n"
               "- Tool calls remain governed by MCOS allow-lists and confirm guards.\n";
    }
};

// ===========================================================================
// 4. OpenAI Responses API (remote MCP) -- hosted; requires public HTTPS.
// ===========================================================================
class OpenAiResponsesIntegrationProvider final : public ProviderBase {
public:
    using ProviderBase::ProviderBase;

    std::string id() const override { return "openai-responses"; }

    ClientIntegrationDescriptor descriptor() const override {
        ClientIntegrationDescriptor d;
        d.id = id();
        d.displayName = "OpenAI Responses API (remote MCP)";
        d.vendor = "OpenAI";
        d.products = { "Responses API remote MCP" };
        d.aliases = { "openai" };
        d.supportedTransports = { "streamable_http", "sse" };
        d.requiredEndpointProperties = { "public_https" };
        d.authModels = { "bearer", "oauth" };
        d.generatedArtifacts = { "openai-responses.mcp.json" };
        d.verificationSteps = {
            "Deploy or bridge MCOS to a reachable public HTTPS endpoint.",
            "Issue a Responses API request with the mcp tool object and confirm tools/list resolves.",
            "Confirm destructive tools are excluded from allowed_tools or gated by require_approval."
        };
        d.caveats = {
            "Hosted OpenAI usage requires a reachable endpoint; a LAN-only gateway is not reachable.",
            "Public/edge deployments must use HTTPS.",
            "Set allowed_tools to the gateway's advertised read-only tools; use require_approval for anything mutating."
        };
        d.requiresPublicHttps = true;
        d.supportsLanHttp = false;
        d.supportsMcpServerInstructions = true;
        d.supportsToolAllowList = true;
        d.supportsClientApprovalPolicy = true; // Responses supports require_approval.
        return d;
    }

    static nlohmann::json responsesToolJson(const std::string& serverUrl) {
        return {
            { "model", "<operator-selected-model>" },
            { "tools", nlohmann::json::array({
                {
                    { "type", "mcp" },
                    { "server_label", "mcos" },
                    { "server_description", "MCOS governed orchestration gateway" },
                    { "server_url", serverUrl },
                    { "require_approval", "always" },
                    { "allowed_tools", nlohmann::json::array({ "mcos_read_status", "mcos_list_endpoints" }) }
                }
            }) },
            { "input", "Inspect MCOS orchestration health." }
        };
    }

    OnboardingProfile createOnboardingProfile(const ClientIntegrationContext& context) const override {
        OnboardingProfile profile = baseProfile(context, "OpenAI Responses API (remote MCP)");
        profile.configSnippets.push_back(jsonSnippet(
            "OpenAI Responses API request using an MCP tool. The model is operator-selected. server_url "
            "must be a reachable public/edge HTTPS endpoint; a LAN-only gateway will not work for hosted OpenAI.",
            "openai-responses.mcp.json",
            responsesToolJson(publicFacingUrl(context))));
        profile.manualInstructions = {
            "Expose MCOS at a reachable public HTTPS endpoint (edge bridge) before hosted OpenAI can call it.",
            "Send the Responses API request with the mcp tool object above; keep allowed_tools read-only.",
            "MCOS does not collect or proxy your OpenAI API key; use ${OPENAI_API_KEY} in your own client."
        };
        profile.verificationSteps = descriptor().verificationSteps;
        profile.caveats = descriptor().caveats;
        return profile;
    }

    std::vector<ClientExportArtifact> createExportArtifacts(const ClientIntegrationContext& context) const override {
        return {
            artifact("openai-responses.mcp.json", "application/json",
                     responsesToolJson(publicFacingUrl(context)).dump(2),
                     "OpenAI Responses API MCP tool example (type=mcp, server_label, server_url, approval/allow-list).")
        };
    }
};

// ===========================================================================
// 5. ChatGPT Apps / Connectors -- hosted; public HTTPS + OAuth 2.1.
// ===========================================================================
class ChatGptAppsIntegrationProvider final : public ProviderBase {
public:
    using ProviderBase::ProviderBase;

    std::string id() const override { return "chatgpt-apps"; }

    ClientIntegrationDescriptor descriptor() const override {
        ClientIntegrationDescriptor d;
        d.id = id();
        d.displayName = "ChatGPT Apps / Connectors";
        d.vendor = "OpenAI";
        d.products = { "ChatGPT Apps", "ChatGPT connectors" };
        d.aliases = { "chatgpt" };
        d.supportedTransports = { "streamable_http" };
        d.requiredEndpointProperties = { "public_https", "oauth2.1", "protected_resource_metadata", "client_identity" };
        d.authModels = { "oauth", "mtls" };
        d.generatedArtifacts = { "chatgpt-apps-connector.md" };
        d.verificationSteps = {
            "Publish MCOS behind an approved public HTTPS /mcp connector endpoint.",
            "Confirm OAuth 2.1 authorization-code + PKCE and protected resource metadata are served.",
            "Confirm read-only tools by default; destructive tools stay hidden without explicit policy."
        };
        d.caveats = {
            "Hosted ChatGPT runs in OpenAI's environment and cannot reach a LAN-only HTTP MCOS gateway directly.",
            "A public HTTPS /mcp connector/app endpoint is required.",
            "OAuth 2.1 (authorization-code + PKCE) and protected resource metadata are required for user-linked tools.",
            "Do not rely on custom API keys as the primary ChatGPT-facing auth story."
        };
        d.requiresPublicHttps = true;
        d.supportsLanHttp = false;
        d.supportsMcpServerInstructions = true;
        d.supportsToolAllowList = true;
        d.supportsClientApprovalPolicy = true; // via connector/app policies
        return d;
    }

    OnboardingProfile createOnboardingProfile(const ClientIntegrationContext& context) const override {
        OnboardingProfile profile = baseProfile(context, "ChatGPT Apps / Connectors");
        profile.configSnippets.push_back(textSnippet(
            "markdown",
            "ChatGPT Apps/connectors require an approved public HTTPS /mcp endpoint with OAuth 2.1 and "
            "protected resource metadata. Hosted ChatGPT cannot reach a LAN-only gateway directly.",
            "chatgpt-apps-connector.md",
            appsNotes()));
        profile.manualInstructions = {
            "Publish MCOS behind an approved public HTTPS /mcp connector/app endpoint.",
            "Serve OAuth 2.1 authorization-code + PKCE and protected resource metadata for user-linked tools.",
            "Identify ChatGPT connector traffic (OpenAI-managed mTLS where implemented); keep destructive tools hidden."
        };
        profile.verificationSteps = descriptor().verificationSteps;
        profile.caveats = descriptor().caveats;
        return profile;
    }

    std::vector<ClientExportArtifact> createExportArtifacts(const ClientIntegrationContext&) const override {
        return {
            artifact("chatgpt-apps-connector.md", "text/markdown", appsNotes(),
                     "ChatGPT Apps/connector requirements: public HTTPS /mcp, OAuth 2.1, protected resource metadata.")
        };
    }

private:
    static std::string appsNotes() {
        return "# ChatGPT Apps / Connectors\n\n"
               "Hosted ChatGPT cannot reach a LAN-only HTTP MCOS gateway directly.\n\n"
               "Requirements:\n"
               "- Public HTTPS `/mcp` endpoint (connector/app surface).\n"
               "- OAuth 2.1 / authorization-code + PKCE for user-linked tools.\n"
               "- Protected resource metadata.\n"
               "- Client identification for ChatGPT connector traffic (OpenAI-managed mTLS where implemented).\n"
               "- Read-only tools by default; destructive operations hidden unless explicit gateway policy,\n"
               "  identity verification, and confirm guards are implemented.\n";
    }
};

// ===========================================================================
// 6. ChatGPT connector edge bridge.
// ===========================================================================
class ChatGptConnectorEdgeIntegrationProvider final : public ProviderBase {
public:
    using ProviderBase::ProviderBase;

    std::string id() const override { return "chatgpt-connector-edge"; }

    ClientIntegrationDescriptor descriptor() const override {
        ClientIntegrationDescriptor d;
        d.id = id();
        d.displayName = "ChatGPT Connector Edge";
        d.vendor = "OpenAI";
        d.products = { "ChatGPT connector edge bridge" };
        d.supportedTransports = { "streamable_http" };
        d.requiredEndpointProperties = { "public_https", "lan_to_public_bridge" };
        d.authModels = { "oauth", "mtls", "connector-edge" };
        d.generatedArtifacts = { "chatgpt-connector-edge.md" };
        d.verificationSteps = {
            "Stand up a LAN-to-public edge bridge in front of MCOS.",
            "Confirm the edge forwards only approved MCP tool calls and never raw LAN admin surfaces.",
            "Confirm the edge enforces HTTPS + auth and preserves audit context."
        };
        d.caveats = {
            "A LAN-to-public bridge is required for hosted ChatGPT.",
            "The edge must not expose raw LAN admin surfaces.",
            "The edge must forward only approved MCP tool calls and preserve audit context.",
            "The edge must enforce HTTPS and auth where exposed publicly."
        };
        d.requiresPublicHttps = true;
        d.supportsLanHttp = false;
        d.supportsMcpServerInstructions = true;
        d.supportsToolAllowList = true;
        d.supportsClientApprovalPolicy = true; // edge-dependent
        return d;
    }

    OnboardingProfile createOnboardingProfile(const ClientIntegrationContext& context) const override {
        OnboardingProfile profile = baseProfile(context, "ChatGPT Connector Edge");
        profile.configSnippets.push_back(textSnippet(
            "markdown",
            "Connector-edge bridge notes: forward only approved MCP tool calls, never raw LAN admin surfaces.",
            "chatgpt-connector-edge.md",
            edgeNotes()));
        profile.manualInstructions = {
            "Deploy a LAN-to-public edge bridge that terminates HTTPS and enforces auth.",
            "Forward only approved MCP tool calls to the MCOS gateway; block raw LAN admin routes.",
            "Preserve audit context (client identity, tool, timestamp) across the bridge."
        };
        profile.verificationSteps = descriptor().verificationSteps;
        profile.caveats = descriptor().caveats;
        return profile;
    }

    std::vector<ClientExportArtifact> createExportArtifacts(const ClientIntegrationContext&) const override {
        return {
            artifact("chatgpt-connector-edge.md", "text/markdown", edgeNotes(),
                     "Connector-edge bridge policy: approved MCP forwarding, no raw LAN admin, HTTPS + auth + audit.")
        };
    }

private:
    static std::string edgeNotes() {
        return "# ChatGPT Connector Edge\n\n"
               "A LAN-to-public bridge is required for hosted ChatGPT to reach MCOS.\n\n"
               "Edge policy:\n"
               "- Expose only approved MCP tool calls, never raw LAN admin surfaces (`/api/*`).\n"
               "- Enforce HTTPS and authentication at the public boundary.\n"
               "- Preserve audit context (client identity, tool name, timestamp) end to end.\n"
               "- Keep the MCOS gateway itself LAN-trusted behind the bridge.\n";
    }
};

// ===========================================================================
// 7. xAI Responses API (remote MCP) -- hosted; Streaming HTTP/SSE; no
//    OpenAI-only require_approval / connector_id.
// ===========================================================================
class XaiResponsesIntegrationProvider final : public ProviderBase {
public:
    using ProviderBase::ProviderBase;

    std::string id() const override { return "xai-responses"; }

    ClientIntegrationDescriptor descriptor() const override {
        ClientIntegrationDescriptor d;
        d.id = id();
        d.displayName = "xAI Responses API (remote MCP)";
        d.vendor = "xAI";
        d.products = { "xAI Responses API", "Grok API remote MCP" };
        d.aliases = { "xai" };
        d.supportedTransports = { "streamable_http", "sse" };
        d.requiredEndpointProperties = { "public_https", "streaming_http_or_sse" };
        d.authModels = { "bearer" };
        d.generatedArtifacts = { "xai-responses.mcp.json" };
        d.verificationSteps = {
            "Expose MCOS at a reachable public HTTPS endpoint that supports Streaming HTTP / SSE.",
            "Issue an xAI remote MCP request with server_url + server_label and confirm tools/list resolves.",
            "Confirm tool safety is enforced by MCOS (allow-lists / governance / confirm guards)."
        };
        d.caveats = {
            "xAI remote MCP supports Streaming HTTP and SSE transports.",
            "xAI remote MCP does not support OpenAI-only require_approval or connector_id fields.",
            "Tool approval must be enforced by MCOS allow-lists, governance, edge policy, or confirm guards.",
            "Use grok-4.3 for general Grok API examples unless the operator configures another model."
        };
        d.requiresPublicHttps = true;
        d.supportsLanHttp = false;
        d.supportsMcpServerInstructions = true;
        d.supportsToolAllowList = true;
        d.supportsClientApprovalPolicy = false; // no require_approval; MCOS must enforce.
        return d;
    }

    static nlohmann::json xaiToolJson(const std::string& serverUrl) {
        // Deliberately omits require_approval / connector_id (unsupported by xAI).
        return {
            { "server_url", serverUrl },
            { "server_label", "mcos" },
            { "server_description", "MCOS governed orchestration gateway" },
            { "allowed_tools", nlohmann::json::array({ "mcos_read_status", "mcos_list_endpoints" }) }
        };
    }

    OnboardingProfile createOnboardingProfile(const ClientIntegrationContext& context) const override {
        OnboardingProfile profile = baseProfile(context, "xAI Responses API (remote MCP)");
        profile.configSnippets.push_back(jsonSnippet(
            "xAI remote MCP tool configuration. Uses server_url + server_label; does NOT include the "
            "OpenAI-only require_approval or connector_id fields. General model: grok-4.3.",
            "xai-responses.mcp.json",
            xaiToolJson(publicFacingUrl(context))));
        profile.manualInstructions = {
            "Expose MCOS at a reachable public HTTPS endpoint that supports Streaming HTTP / SSE.",
            "Send the xAI remote MCP request with the server_url + server_label above.",
            "MCOS does not collect or proxy your xAI API key; use ${XAI_API_KEY} in your own client."
        };
        profile.verificationSteps = descriptor().verificationSteps;
        profile.caveats = descriptor().caveats;
        return profile;
    }

    std::vector<ClientExportArtifact> createExportArtifacts(const ClientIntegrationContext& context) const override {
        return {
            artifact("xai-responses.mcp.json", "application/json",
                     xaiToolJson(publicFacingUrl(context)).dump(2),
                     "xAI remote MCP tool example (server_url + server_label; no require_approval/connector_id).")
        };
    }
};

// ===========================================================================
// 8. Grok Build (xAI CLI / headless) -- .grok/config.toml, grok-build-0.1.
// ===========================================================================
class GrokBuildIntegrationProvider final : public ProviderBase {
public:
    using ProviderBase::ProviderBase;

    std::string id() const override { return "grok-build"; }

    ClientIntegrationDescriptor descriptor() const override {
        ClientIntegrationDescriptor d;
        d.id = id();
        d.displayName = "Grok Build (xAI)";
        d.vendor = "xAI";
        d.products = { "Grok Build CLI", "Grok Build headless" };
        d.aliases = { "grok" };
        d.supportedTransports = { "streamable_http" };
        d.requiredEndpointProperties = { "lan_http_reachable" };
        d.authModels = { "local-trust", "bearer-env" };
        d.generatedArtifacts = { ".grok/config.toml" };
        d.verificationSteps = {
            "grok mcp list",
            "grok mcp doctor mcos --json",
            "grok inspect"
        };
        d.caveats = {
            "Grok Build reads project MCP config from .grok/config.toml; the coding model is grok-build-0.1.",
            "Only run verification commands supported by the installed Grok Build version; otherwise use manual steps.",
            "Headless: set ${XAI_API_KEY} in the environment. Never write the API key into project files."
        };
        d.requiresPublicHttps = false;
        d.supportsLanHttp = true;
        d.supportsMcpServerInstructions = true;
        d.supportsToolAllowList = false;
        d.supportsClientApprovalPolicy = true; // client-dependent
        return d;
    }

    static std::string grokToml(const std::string& url) {
        std::ostringstream toml;
        toml << "model = \"grok-build-0.1\"\n\n"
             << "[mcp_servers.mcos]\n"
             << "url = \"" << url << "\"\n"
             << "startup_timeout_sec = 30\n"
             << "tool_timeout_sec = 6000\n";
        return toml.str();
    }

    static std::string headlessNotes() {
        return "# Grok Build headless\n\n"
               "```powershell\n"
               "$env:XAI_API_KEY = \"<operator-managed-secret>\"\n"
               "grok --no-auto-update -p \"Inspect MCOS health through the mcos MCP server.\" "
               "--output-format streaming-json\n"
               "```\n\n"
               "Do not write the API key into project files.\n";
    }

    OnboardingProfile createOnboardingProfile(const ClientIntegrationContext& context) const override {
        OnboardingProfile profile = baseProfile(context, "Grok Build (xAI)");
        profile.configSnippets.push_back(textSnippet(
            "toml",
            "Grok Build project config. Add this to .grok/config.toml. If the active endpoint is HTTPS, "
            "the url above is HTTPS. If auth is required, use an environment-variable placeholder only.",
            ".grok/config.toml",
            grokToml(context.gatewayMcpUrl)));
        profile.configSnippets.push_back(textSnippet(
            "markdown",
            "Grok Build headless invocation pattern (streaming JSON). Uses ${XAI_API_KEY} from the environment.",
            "grok-build-headless.md",
            headlessNotes()));
        profile.manualInstructions = {
            "Create .grok/config.toml with the [mcp_servers.mcos] table above (model = grok-build-0.1).",
            "Verify with `grok mcp list` and `grok mcp doctor mcos --json` where supported by your Grok Build version.",
            "For headless runs, export ${XAI_API_KEY} in the environment; never write it into project files."
        };
        profile.verificationSteps = descriptor().verificationSteps;
        profile.caveats = descriptor().caveats;
        return profile;
    }

    std::vector<ClientExportArtifact> createExportArtifacts(const ClientIntegrationContext& context) const override {
        return {
            artifact(".grok/config.toml", "application/toml", grokToml(context.gatewayMcpUrl),
                     "Grok Build project MCP config (model = grok-build-0.1)."),
            artifact("grok-build-headless.md", "text/markdown", headlessNotes(),
                     "Grok Build headless invocation pattern; ${XAI_API_KEY} from the environment.")
        };
    }
};

// ===========================================================================
// 9. Grok Build ACP -- external process/client adapter over JSON-RPC stdio.
// ===========================================================================
class GrokBuildAcpIntegrationProvider final : public ProviderBase {
public:
    using ProviderBase::ProviderBase;

    std::string id() const override { return "grok-build-acp"; }

    ClientIntegrationDescriptor descriptor() const override {
        ClientIntegrationDescriptor d;
        d.id = id();
        d.displayName = "Grok Build ACP";
        d.vendor = "xAI";
        d.products = { "Grok Build ACP" };
        d.supportedTransports = { "stdio" };
        d.requiredEndpointProperties = { "external_process_adapter" };
        d.authModels = { "local-trust" };
        d.generatedArtifacts = { "grok-build-acp-notes.md" };
        d.verificationSteps = {
            "grok agent stdio",
            "Confirm ACP runs over JSON-RPC on stdin/stdout behind an MCOS external process adapter."
        };
        d.caveats = {
            "Grok Build ACP runs as an external process/client adapter, not embedded MCOS runtime execution.",
            "ACP is JSON-RPC over stdin/stdout (`grok agent stdio`)."
        };
        d.requiresPublicHttps = false;
        d.supportsLanHttp = false;
        d.supportsClientApprovalPolicy = true; // adapter-dependent
        return d;
    }

    OnboardingProfile createOnboardingProfile(const ClientIntegrationContext& context) const override {
        OnboardingProfile profile = baseProfile(context, "Grok Build ACP");
        profile.transport = "stdio_bridge";
        profile.mcp.transport = "stdio_bridge";
        profile.configSnippets.push_back(textSnippet(
            "markdown",
            "Grok Build ACP is JSON-RPC over stdin/stdout via `grok agent stdio`, driven behind an "
            "MCOS external process adapter.",
            "grok-build-acp-notes.md",
            acpNotes()));
        profile.manualInstructions = {
            "Launch Grok Build in ACP mode with `grok agent stdio`.",
            "Bridge the ACP JSON-RPC stdio stream through an MCOS external process adapter.",
            "Do not embed Grok Build execution in the MCOS core runtime."
        };
        profile.verificationSteps = descriptor().verificationSteps;
        profile.caveats = descriptor().caveats;
        return profile;
    }

    std::vector<ClientExportArtifact> createExportArtifacts(const ClientIntegrationContext&) const override {
        return {
            artifact("grok-build-acp-notes.md", "text/markdown", acpNotes(),
                     "Grok Build ACP guidance: `grok agent stdio`, JSON-RPC over stdin/stdout, external adapter.")
        };
    }

private:
    static std::string acpNotes() {
        return "# Grok Build ACP\n\n"
               "Grok Build ACP is an external process/client adapter.\n\n"
               "- Start with `grok agent stdio`.\n"
               "- ACP runs over JSON-RPC on stdin/stdout.\n"
               "- Drive it behind an MCOS external process adapter; never embed execution in the core runtime.\n";
    }
};

// ===========================================================================
// 10. Generic MCP -- honest report of the current gateway transport profile.
// ===========================================================================
class GenericMcpIntegrationProvider final : public ProviderBase {
public:
    using ProviderBase::ProviderBase;

    std::string id() const override { return "generic-mcp"; }

    ClientIntegrationDescriptor descriptor() const override {
        ClientIntegrationDescriptor d;
        d.id = id();
        d.displayName = "Generic MCP Client";
        d.vendor = "Generic";
        d.products = { "MCP-compatible clients" };
        d.aliases = { "generic" };
        d.supportedTransports = { "streamable_http" };
        d.requiredEndpointProperties = { "lan_http_reachable" };
        d.authModels = { "local-trust" };
        d.generatedArtifacts = { ".mcp.json" };
        d.verificationSteps = {
            "Resolve the MCOS discovery document (/.well-known/mcos.json).",
            "Connect to the gateway MCP URL and list tools.",
            "Confirm governance bundle retrieval before granting mutating access."
        };
        d.caveats = {
            "Transport (alpha): POST-only Streamable HTTP subset. Single JSON responses; no SSE stream "
            "is offered (GET /mcp returns 405 with Allow: POST).",
            "Auth: LAN trust (no app-layer bearer). Do not expose the gateway port to the public internet.",
            "Clients that require a full SSE stream should use the companion utility's stdio bridge."
        };
        d.requiresPublicHttps = false;
        d.supportsLanHttp = true;
        d.supportsMcpServerInstructions = true;
        d.supportsToolAllowList = false;
        d.supportsClientApprovalPolicy = false; // unknown clients -> MCOS enforces safety.
        return d;
    }

    OnboardingProfile createOnboardingProfile(const ClientIntegrationContext& context) const override {
        OnboardingProfile profile = baseProfile(context, "Generic MCP Client");
        profile.configSnippets.push_back(jsonSnippet(
            "Generic Streamable HTTP MCP server registration. Drop this into your client's MCP server config.",
            ".mcp.json",
            { { "mcpServers", { { "mcos", {
                { "url", context.gatewayMcpUrl }
            } } } } }));
        profile.manualInstructions = {
            "Add the MCOS gateway MCP URL to your MCP client.",
            "No bearer token or app-layer login is required on the trusted LAN surface.",
            "Load the CLU/Forsetti governance bundle for your platform."
        };
        profile.verificationSteps = descriptor().verificationSteps;
        profile.caveats = descriptor().caveats;
        return profile;
    }

    std::vector<ClientExportArtifact> createExportArtifacts(const ClientIntegrationContext& context) const override {
        nlohmann::json genericJson = {
            { "mcpServers", { { "mcos", { { "url", context.gatewayMcpUrl } } } } }
        };
        return {
            artifact(".mcp.json", "application/json", genericJson.dump(2),
                     "Generic MCP server registration honestly reporting the POST-only Streamable HTTP gateway.")
        };
    }
};

// ---------------------------------------------------------------------------
// Deterministic catalog. Providers are supplied at construction (no globals,
// no filesystem scanning). findById resolves canonical ids and compatibility
// aliases without collapsing product-specific behavior.
// ---------------------------------------------------------------------------

class ClientIntegrationCatalog final : public IClientIntegrationCatalog {
public:
    explicit ClientIntegrationCatalog(
        std::vector<std::shared_ptr<const IClientIntegrationProvider>> providers)
        : providers_(std::move(providers)) {
        for (const auto& provider : providers_) {
            if (!provider) {
                continue;
            }
            const std::string canonical = toLower(provider->id());
            index_.emplace(canonical, provider);
            for (const auto& alias : provider->descriptor().aliases) {
                // Canonical ids win over aliases if they ever collide.
                index_.emplace(toLower(alias), provider);
            }
        }
    }

    std::vector<ClientIntegrationDescriptor> list() const override {
        std::vector<ClientIntegrationDescriptor> descriptors;
        descriptors.reserve(providers_.size());
        for (const auto& provider : providers_) {
            if (provider) {
                descriptors.push_back(provider->descriptor());
            }
        }
        return descriptors;
    }

    std::shared_ptr<const IClientIntegrationProvider>
    findById(std::string_view id) const override {
        const auto it = index_.find(toLower(id));
        return it != index_.end() ? it->second : nullptr;
    }

private:
    std::vector<std::shared_ptr<const IClientIntegrationProvider>> providers_;
    std::unordered_map<std::string, std::shared_ptr<const IClientIntegrationProvider>> index_;
};

} // namespace

// ---------------------------------------------------------------------------
// Factory definitions (declared in MasterControlContracts.h).
// ---------------------------------------------------------------------------

std::shared_ptr<IRemoteMcpCompatibilityAnalyzer> createRemoteMcpCompatibilityAnalyzer() {
    return std::make_shared<RemoteMcpCompatibilityAnalyzer>();
}

std::shared_ptr<IClientIntegrationCatalog> createClientIntegrationCatalog(
    std::shared_ptr<const IRemoteMcpCompatibilityAnalyzer> analyzer) {
    if (!analyzer) {
        analyzer = createRemoteMcpCompatibilityAnalyzer();
    }
    // Canonical registration order (also the /api/client-integrations list order).
    std::vector<std::shared_ptr<const IClientIntegrationProvider>> providers = {
        std::make_shared<ClaudeCodeIntegrationProvider>(analyzer),
        std::make_shared<CodexIntegrationProvider>(analyzer),
        std::make_shared<CodexMcpServerIntegrationProvider>(analyzer),
        std::make_shared<OpenAiResponsesIntegrationProvider>(analyzer),
        std::make_shared<ChatGptAppsIntegrationProvider>(analyzer),
        std::make_shared<ChatGptConnectorEdgeIntegrationProvider>(analyzer),
        std::make_shared<XaiResponsesIntegrationProvider>(analyzer),
        std::make_shared<GrokBuildIntegrationProvider>(analyzer),
        std::make_shared<GrokBuildAcpIntegrationProvider>(analyzer),
        std::make_shared<GenericMcpIntegrationProvider>(analyzer)
    };
    return std::make_shared<ClientIntegrationCatalog>(std::move(providers));
}

} // namespace MasterControl
