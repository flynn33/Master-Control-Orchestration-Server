const healthBadge = document.querySelector('#healthBadge');
const refreshButton = document.querySelector('#refreshButton');
const surfaceToolbar = document.querySelector('#surfaceToolbar');
const surfaceNavigation = document.querySelector('#surfaceNavigation');
const surfaceSummary = document.querySelector('#surfaceSummary');
const surfaceContentHost = document.querySelector('#surfaceContentHost');
const currentViewEyebrow = document.querySelector('#currentViewEyebrow');
const currentViewTitle = document.querySelector('#currentViewTitle');
const currentViewDescription = document.querySelector('#currentViewDescription');
const surfaceOverlayDialog = document.querySelector('#surfaceOverlayDialog');
const overlayEyebrow = document.querySelector('#overlayEyebrow');
const overlayTitle = document.querySelector('#overlayTitle');
const overlayDescription = document.querySelector('#overlayDescription');
const overlayCloseButton = document.querySelector('#overlayCloseButton');
const overlayWorkspaceButton = document.querySelector('#overlayWorkspaceButton');
const surfaceOverlayContent = document.querySelector('#surfaceOverlayContent');
const dangerDialog = document.querySelector('#dangerDialog');

function escapeHtml(value) {
  return String(value ?? '')
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;');
}

function checkedAttr(value) {
  return value ? ' checked' : '';
}

function selectedAttr(value) {
  return value ? ' selected' : '';
}

function safeArray(value) {
  return Array.isArray(value) ? value : [];
}

function safeNumber(value, fallback = 0) {
  return Number.isFinite(Number(value)) ? Number(value) : fallback;
}

function formatCount(value) {
  return safeNumber(value).toLocaleString();
}

function formatPercent(value) {
  return `${safeNumber(value).toFixed(1)}%`;
}

function formatTimestamp(date) {
  return new Intl.DateTimeFormat(undefined, {
    hour: 'numeric',
    minute: '2-digit',
    second: '2-digit'
  }).format(date);
}

function formatPreview(value, length = 120) {
  const text = String(value ?? '');
  return text.length > length ? `${text.slice(0, length)}...` : text;
}

function multilineHtml(value) {
  return escapeHtml(value ?? '').replaceAll('\n', '<br>');
}

function boolLabel(value) {
  return value ? 'Enabled' : 'Disabled';
}

function statusPill(status) {
  const safeStatus = String(status || 'unknown').toLowerCase();
  return `<span class="status-pill status-${escapeHtml(safeStatus)}">${escapeHtml(safeStatus)}</span>`;
}

function platformLabel(value) {
  switch (String(value || '').toLowerCase()) {
    case 'windows': return 'Windows';
    case 'macos': return 'macOS';
    case 'ios': return 'iOS';
    default: return String(value || 'Unknown');
  }
}

function transportLabel(value) {
  switch (String(value || '').toLowerCase()) {
    case 'companion_service': return 'Companion Service';
    case 'ssh': return 'SSH';
    default: return String(value || 'Unknown');
  }
}

function platformListLabel(values) {
  const labels = safeArray(values).map(platformLabel).filter(Boolean);
  return labels.length ? labels.join(', ') : 'Unassigned';
}

function appleHostAddressLabel(host) {
  const base = host?.address || host?.serviceBaseUrl || 'unconfigured';
  const port = safeNumber(host?.port, 0);
  if (port > 0 && !String(base).includes('://')) {
    return `${base}:${port}`;
  }
  return base;
}

function appleRequestOptionsSummary(operation) {
  const requestOptions = operation && typeof operation.requestOptions === 'object' && operation.requestOptions !== null
    ? operation.requestOptions
    : {};
  const redactedKeys = new Set(safeArray(operation?.redactedRequestOptionKeys).map((key) => String(key || '')));
  const visibleEntries = Object.entries(requestOptions)
    .filter(([key]) => !redactedKeys.has(String(key || '')))
    .map(([key, value]) => `${key}=${value}`);
  return visibleEntries.length ? visibleEntries.join(' | ') : '';
}

function renderAppleHostsMarkup(hosts) {
  if (!hosts.length) {
    return emptyState('No Apple remote hosts', 'Register remote Mac infrastructure to light up Mac and iOS governance lanes.');
  }

  return `
    <div class="history-list">
      ${hosts.map((host) => {
        const isSelected = state.appleHostDraft.hostId && state.appleHostDraft.hostId === host.hostId;
        const toolchain = host.toolchain || {};
        const signing = host.signing || {};
        const runtimeSummary = safeArray(toolchain.simulatorRuntimes).length
          ? `${safeArray(toolchain.simulatorRuntimes).length} simulator runtimes`
          : 'no simulator runtimes';
        const sdkSummary = `macOS SDK ${toolchain.macosSdkAvailable ? 'ready' : 'missing'} | iOS SDK ${toolchain.iosSdkAvailable ? 'ready' : 'missing'}`;
        const controlSummary = `simctl ${toolchain.simulatorControlAvailable ? 'ready' : 'blocked'} | devicectl ${toolchain.deviceControlAvailable ? 'ready' : 'blocked'}`;
        const teamsSummary = safeArray(signing.availableTeams).length
          ? `Teams: ${safeArray(signing.availableTeams).join(', ')}`
          : 'Teams: none published';
        const readinessIssues = safeArray(host.readinessIssues);
        return `
          <article class="history-item ${isSelected ? 'selected' : ''}" data-apple-host-id="${escapeHtml(host.hostId || '')}">
            <strong>${escapeHtml(host.displayName || host.hostId || 'Apple host')}</strong>
            <div>${escapeHtml(platformListLabel(host.platforms))} | ${escapeHtml(transportLabel(host.transport))}</div>
            <div>${escapeHtml(appleHostAddressLabel(host))}</div>
            <div>${statusPill(toolchain.status || 'unknown')} ${statusPill(signing.status || 'unknown')}</div>
            <div>${escapeHtml(toolchain.xcodeVersion ? `Xcode ${toolchain.xcodeVersion}` : 'Xcode pending')} | ${escapeHtml(runtimeSummary)}</div>
            <div>${escapeHtml(toolchain.developerDirectory ? `Developer dir: ${toolchain.developerDirectory}` : 'Developer dir pending')}</div>
            <div>${escapeHtml(sdkSummary)}</div>
            <div>${escapeHtml(controlSummary)}</div>
            <div>${escapeHtml(teamsSummary)}</div>
            <div>${escapeHtml(host.transportSummary || 'Transport summary pending')}</div>
            <div>${escapeHtml(host.credentialProfileSummary || 'No Apple distribution defaults configured')}</div>
            ${toolchain.message ? `<div>${escapeHtml(`Toolchain: ${toolchain.message}`)}</div>` : ''}
            ${signing.message ? `<div>${escapeHtml(`Signing: ${signing.message}`)}</div>` : ''}
            ${toolchain.checkedAtUtc ? `<div>${escapeHtml(`Checked: ${toolchain.checkedAtUtc}`)}</div>` : ''}
            ${readinessIssues.length ? `<div>${escapeHtml(`Readiness gaps: ${readinessIssues.join('; ')}`)}</div>` : ''}
          </article>
        `;
      }).join('')}
    </div>
  `;
}

function renderPlatformGatewaysMarkup(gateways) {
  if (!gateways.length) {
    return emptyState('No platform gateways', 'Gateway modules will publish LAN service lanes here when the service is online.');
  }

  return `
    <div class="history-list">
      ${gateways.map((gateway) => `
        <article class="history-item">
          <strong>${escapeHtml(gateway.displayName || gateway.serviceId || 'Gateway lane')}</strong>
          <div>${escapeHtml(platformLabel(gateway.platform))} | ${escapeHtml(gateway.serviceType || '_service._tcp.local')}</div>
          <div>${escapeHtml(gateway.instanceLabel || gateway.hostName || gateway.ipAddress || 'unpublished')}</div>
          <div>${escapeHtml(gateway.ipAddress || gateway.hostName || 'unknown host')}${safeNumber(gateway.port, 0) ? `:${escapeHtml(gateway.port)}` : ''}</div>
          <div>${statusPill(gateway.status || 'unknown')}</div>
        </article>
      `).join('')}
    </div>
  `;
}

function renderGovernanceServersMarkup(servers) {
  if (!servers.length) {
    return emptyState('No governance servers', 'Platform governance MCP lanes will appear here when their modules are active.');
  }

  return `
    <div class="history-list">
      ${servers.map((server) => `
        <article class="history-item">
          <strong>${escapeHtml(server.displayName || server.serviceId || 'Governance server')}</strong>
          <div>${escapeHtml(platformLabel(server.platform))} | ${escapeHtml(server.routePath || '/mcp/governance')}</div>
          <div>${escapeHtml(formatCount(safeArray(server.toolIds).length))} tools | remote toolchain ${server.requiresRemoteToolchain ? 'required' : 'optional'}</div>
          <div>${statusPill(server.status || 'unknown')}</div>
        </article>
      `).join('')}
    </div>
  `;
}

function renderAppleOperationsMarkup(operations) {
  if (!operations.length) {
    return emptyState('No Apple operations yet', 'Mac and iOS build, sign, notarize, and install runs will appear here.');
  }

  return `
    <div class="history-list">
      ${operations.map((operation) => {
        const requestOptionsSummary = appleRequestOptionsSummary(operation);
        return `
        <article class="history-item">
          <strong>${escapeHtml(operation.displayName || operation.toolId || 'Apple operation')}</strong>
          <div>${statusPill(operation.status || 'queued')}</div>
          <div>${escapeHtml(platformLabel(operation.platform))} | ${escapeHtml(operation.hostDisplayName || operation.hostId || 'unassigned host')} | ${escapeHtml(transportLabel(operation.transport))}</div>
          <div>${escapeHtml(operation.artifactPath || operation.summary || operation.errorMessage || 'No artifact published')}</div>
          ${operation.routeReason ? `<div>${escapeHtml(operation.routeReason)}</div>` : ''}
          ${operation.selectedDeveloperDirectory ? `<div>${escapeHtml(`Developer dir: ${operation.selectedDeveloperDirectory}`)}</div>` : ''}
          ${operation.credentialProfileSummary ? `<div>${escapeHtml(operation.credentialProfileSummary)}</div>` : ''}
          ${operation.workingDirectory ? `<div>${escapeHtml(`Working dir: ${operation.workingDirectory}`)}</div>` : ''}
          ${operation.targetPath ? `<div>${escapeHtml(`Target: ${operation.targetPath}`)}</div>` : ''}
          ${requestOptionsSummary ? `<div>${escapeHtml(`Request: ${requestOptionsSummary}`)}</div>` : ''}
          ${safeArray(operation.readinessIssues).length ? `<div>${escapeHtml(`Readiness gaps: ${safeArray(operation.readinessIssues).join('; ')}`)}</div>` : ''}
          ${operation.rerunReadinessMessage ? `<div>${escapeHtml(`Replay: ${operation.rerunReadinessMessage}`)}</div>` : ''}
          ${operation.diagnosticSummary ? `<div>${escapeHtml(operation.diagnosticSummary)}</div>` : ''}
          ${safeArray(operation.redactedRequestOptionKeys).length ? `<div>${escapeHtml('Sensitive request options were redacted from stored history. Rerun may require host defaults or fresh credentials.')}</div>` : ''}
          <div>${escapeHtml(`Queued: ${operation.queuedAtUtc || 'pending'} | Started: ${operation.startedAtUtc || 'pending'} | Completed: ${operation.completedAtUtc || 'pending'}`)}</div>
          <div class="card-actions">
            <button type="button" class="secondary-button" data-action="rerun-apple-operation" data-apple-operation-id="${escapeHtml(operation.operationId || '')}" ${operation.rerunReady ? '' : 'disabled'}>
              Rerun
            </button>
          </div>
        </article>
      `;
      }).join('')}
    </div>
  `;
}

function normalizeAppleOperationStatus(status) {
  const normalized = String(status || 'queued').toLowerCase();
  if (normalized === 'running' || normalized === 'in_progress') {
    return 'running';
  }
  if (normalized === 'failed' || normalized === 'error' || normalized === 'blocked') {
    return normalized === 'blocked' ? 'blocked' : 'failed';
  }
  if (normalized === 'completed' || normalized === 'success') {
    return 'succeeded';
  }
  return normalized;
}

function isAttentionAppleOperation(operation) {
  const normalized = normalizeAppleOperationStatus(operation?.status);
  return normalized === 'failed' || normalized === 'blocked';
}

function isActiveAppleOperation(operation) {
  const normalized = normalizeAppleOperationStatus(operation?.status);
  return normalized === 'queued' || normalized === 'running';
}

function appleOperationCounts(operations) {
  const counts = {
    queued: 0,
    running: 0,
    succeeded: 0,
    attention: 0,
    rerunnableAttention: 0
  };

  for (const operation of safeArray(operations)) {
    const normalized = normalizeAppleOperationStatus(operation?.status);
    if (normalized === 'queued') {
      counts.queued += 1;
    } else if (normalized === 'running') {
      counts.running += 1;
    } else if (normalized === 'succeeded') {
      counts.succeeded += 1;
    }

    if (isAttentionAppleOperation(operation)) {
      counts.attention += 1;
      if (operation?.rerunReady) {
        counts.rerunnableAttention += 1;
      }
    }
  }

  return counts;
}

function filterAppleOperationsByMode(operations, mode) {
  const normalizedMode = String(mode || 'attention').toLowerCase();
  if (normalizedMode === 'all') {
    return safeArray(operations);
  }
  if (normalizedMode === 'active') {
    return safeArray(operations).filter(isActiveAppleOperation);
  }
  if (normalizedMode === 'succeeded') {
    return safeArray(operations).filter((operation) => normalizeAppleOperationStatus(operation?.status) === 'succeeded');
  }
  return safeArray(operations).filter(isAttentionAppleOperation);
}

function renderGovernanceExecutionsMarkup(executions) {
  if (!executions.length) {
    return emptyState('No governance executions yet', 'CLU governance tool runs will appear here after platform checks execute.');
  }

  return `
    <div class="history-list">
      ${executions.map((execution) => `
        <article class="history-item">
          <strong>${escapeHtml(execution.displayName || execution.toolId || 'Governance tool')}</strong>
          <div>${statusPill(execution.status || 'unknown')}</div>
          <div>${escapeHtml(platformLabel(execution.platform))}</div>
          <div>${escapeHtml(execution.summary || formatPreview(execution.rawOutput || '', 180) || 'No summary published')}</div>
          <div>${escapeHtml(execution.completedAtUtc || execution.startedAtUtc || 'pending')}</div>
        </article>
      `).join('')}
    </div>
  `;
}

function metricCard(label, value, detail = '') {
  return `
    <article class="telemetry-card">
      <div class="card-label">${escapeHtml(label)}</div>
      <div class="metric">${escapeHtml(value)}</div>
      <div class="metric-detail">${escapeHtml(detail)}</div>
    </article>
  `;
}

function telemetryToneForPercent(value) {
  const percent = safeNumber(value, 0);
  if (percent >= 85) {
    return 'danger';
  }
  if (percent >= 65) {
    return 'warning';
  }
  return 'ok';
}

function telemetryMeterCard(label, value, percent, detail = '', tone = '') {
  const normalizedPercent = Math.max(0, Math.min(100, safeNumber(percent, 0)));
  const resolvedTone = tone || telemetryToneForPercent(normalizedPercent);
  return `
    <article class="telemetry-monitor-card" data-tone="${escapeHtml(resolvedTone)}">
      <div class="telemetry-monitor-header">
        <div class="card-label">${escapeHtml(label)}</div>
        <div class="telemetry-monitor-detail">${escapeHtml(detail)}</div>
      </div>
      <div class="telemetry-monitor-value">${escapeHtml(value)}</div>
      <div class="telemetry-meter"><span style="width:${normalizedPercent}%"></span></div>
    </article>
  `;
}

function telemetrySignalCard(label, value, detail = '') {
  return `
    <article class="telemetry-monitor-card telemetry-monitor-card--signal">
      <div class="telemetry-monitor-header">
        <div class="card-label">${escapeHtml(label)}</div>
      </div>
      <div class="telemetry-monitor-value">${escapeHtml(value)}</div>
      <div class="telemetry-monitor-detail">${escapeHtml(detail)}</div>
    </article>
  `;
}

function telemetryStatTable(rows) {
  return `
    <div class="telemetry-stat-list">
      ${rows.map((row) => `
        <div class="telemetry-stat-row">
          <div>
            <div class="card-label">${escapeHtml(row.label)}</div>
            <div class="telemetry-stat-value">${escapeHtml(row.value)}</div>
          </div>
          <div class="telemetry-stat-meta">${escapeHtml(row.detail || '')}</div>
        </div>
      `).join('')}
    </div>
  `;
}

function narrativePanel(label, title, body) {
  return `
    <article class="panel-block narrative-panel">
      <p class="eyebrow">${escapeHtml(label)}</p>
      <h3>${escapeHtml(title)}</h3>
      <p class="narrative-copy">${multilineHtml(body)}</p>
    </article>
  `;
}

function statusMessage(status) {
  return `
    <p class="status-message" data-tone="${escapeHtml(status.tone || 'info')}">
      ${escapeHtml(status.message || '')}
    </p>
  `;
}

function emptyState(title, body) {
  return `
    <article class="surface-placeholder">
      <h3>${escapeHtml(title)}</h3>
      <p>${escapeHtml(body)}</p>
    </article>
  `;
}

function iconToken(systemImageName) {
  switch (systemImageName) {
    case 'network': return '◎';
    case 'trackers': return '◉';
    case 'globe': return '◌';
    case 'shield': return '⬡';
    case 'arrow.down': return '↓';
    case 'share': return '↗';
    case 'gear': return '◇';
    default: return '▣';
  }
}

function defaultProviderDraft() {
  return {
    id: '',
    displayName: '',
    baseUrl: '',
    modelId: '',
    kind: 'generic',
    enabled: true,
    allowAutonomousControl: false
  };
}

function defaultProviderExecutionDraft() {
  return {
    targetId: '',
    prompt: '',
    allowToolAccess: true,
    maxTurns: 4
  };
}

function defaultSubAgentGroupDraft() {
  return {
    groupId: '',
    displayName: '',
    description: '',
    memberTargetIds: []
  };
}

function defaultSubAgentDraft() {
  return {
    id: '',
    displayName: '',
    specialization: '',
    host: '',
    port: 0,
    protocol: 'virtual',
    routePath: '',
    description: ''
  };
}

function defaultMcpServerDraft() {
  return {
    id: '',
    displayName: '',
    host: '',
    port: '',
    protocol: 'http',
    routePath: '/mcp',
    description: ''
  };
}

function defaultAppleHostDraft() {
  return {
    hostId: '',
    displayName: '',
    transport: 'companion_service',
    platforms: ['macos', 'ios'],
    address: '',
    port: '',
    username: '',
    serviceBaseUrl: '',
    companionHealthPath: '/healthz',
    companionExecutePath: '/execute',
    preferredDeveloperDirectory: '',
    defaultSigningIdentity: '',
    defaultNotaryKeychainProfile: '',
    defaultNotaryTeamId: '',
    enabled: true
  };
}

function makeStatus(message, tone = 'info') {
  return { message, tone };
}

function defaultGuidedWorkflowState() {
  return {
    id: '',
    providerCapabilityId: '',
    moduleCatalog: [],
    moduleId: '',
    moduleAction: 'install',
    runtimeMaintenanceKind: 'mcp',
    importMode: 'package',
    // WS2 — auto-connect progress steps and optional manual-fallback context.
    progress: null,
    fallback: null,
    status: makeStatus('Choose a guided workflow to start configuring the orchestration server.', 'info')
  };
}

function guidedFollowThroughText(destinationId) {
  if (destinationId === 'runtime') {
    return 'Review the Runtime surface to confirm the lane or host details, then use Assign Responsibility if that lane should own orchestration work.';
  }
  if (destinationId === 'providers') {
    return 'Review the Providers surface to confirm routing or ownership, then run Validate Provider Routing if you want an operator-safe execution check.';
  }
  if (destinationId === 'clu') {
    return 'Review CLU posture and module state to confirm the action matches the current governance plan.';
  }
  if (destinationId === 'imports') {
    return 'Review Imports for staging status and provenance, then continue deployment or validation once the intake is confirmed.';
  }
  if (destinationId === 'security') {
    return 'Review the Security surface to confirm the protection envelope and trusted-host posture.';
  }
  if (destinationId === 'settings') {
    return 'Review Settings to confirm ports, beacon behavior, and governed resource budgets.';
  }
  return 'Review the updated workspace section to confirm the guided change landed as expected.';
}

function guidedWorkflowDefinitions() {
  return {
    // -----------------------------------------------------------------------
    // Outcome-driven quick-connect workflows (pre-fill provider identity)
    // -----------------------------------------------------------------------
    'connect-chatgpt': {
      title: 'Connect ChatGPT',
      eyebrow: 'QUICK CONNECT',
      description: 'Set up ChatGPT as an orchestration provider. Enter your OpenAI API key and the runtime handles the rest.',
      destinationId: 'providers',
      // capabilityId IS the providerId; runtime resolves kind internally.
      presets: { capabilityId: 'chatgpt' }
    },
    'connect-codex': {
      title: 'Connect Codex',
      eyebrow: 'QUICK CONNECT',
      description: 'Set up Codex as a coding and architecture provider. Enter your OpenAI API key to get started.',
      destinationId: 'providers',
      presets: { capabilityId: 'codex' }
    },
    'connect-claude-code': {
      title: 'Connect Claude Code',
      eyebrow: 'QUICK CONNECT',
      description: 'Set up Claude Code for architecture and specialist coding execution. Enter your Anthropic API key or auth token.',
      destinationId: 'providers',
      presets: { capabilityId: 'claude-code' }
    },
    'connect-xai': {
      title: 'Connect xAI / Grok',
      eyebrow: 'QUICK CONNECT',
      description: 'Set up xAI Grok for orchestration. Enter your xAI API key to begin.',
      destinationId: 'providers',
      presets: { capabilityId: 'xai-grok' }
    },
    // -----------------------------------------------------------------------
    // Existing guided workflows
    // -----------------------------------------------------------------------
    'connect-model': {
      title: 'Connect AI Model',
      eyebrow: 'GUIDED SETUP',
      description: 'Choose the model connector, confirm route defaults, and optionally assign responsibility to planning, coding, review, or specialist lanes.',
      destinationId: 'providers'
    },
    'assign-responsibility': {
      title: 'Assign Responsibility',
      eyebrow: 'GUIDED SETUP',
      description: 'Map connected AI models to planner, coding, review, or specialist targets so CLU and execution routing agree.',
      destinationId: 'providers'
    },
    'guided-provider-execution': {
      title: 'Validate Provider Routing',
      eyebrow: 'GUIDED SETUP',
      description: 'Choose an orchestration lane, select a validation pattern, and dispatch a guided provider-owned task through the local admin API.',
      destinationId: 'providers'
    },
    'guided-runtime-maintenance': {
      title: 'Manage Runtime Lanes',
      eyebrow: 'GUIDED SETUP',
      description: 'Load an existing MCP server, sub-agent, or Apple host and update or remove it without working through the raw runtime editors.',
      destinationId: 'runtime'
    },
    'new-mcp': {
      title: 'New MCP Server',
      eyebrow: 'GUIDED SETUP',
      description: 'Publish a shared MCP lane with host, port, and route details so providers can reuse one tool surface.',
      destinationId: 'runtime'
    },
    'new-subagent': {
      title: 'New Sub-Agent',
      eyebrow: 'GUIDED SETUP',
      description: 'Create a specialist lane with a clear purpose so provider ownership can target exactly the work you want automated.',
      destinationId: 'runtime'
    },
    'new-subagent-group': {
      title: 'New Sub-Agent Group',
      eyebrow: 'GUIDED SETUP',
      description: 'Bundle specialist lanes into a reusable team so one provider can own a focused multi-agent squad.',
      destinationId: 'providers'
    },
    'new-apple-host': {
      title: 'New Apple Host',
      eyebrow: 'GUIDED SETUP',
      description: 'Register the remote Mac infrastructure that powers macOS and iOS build, sign, notarize, and install operations.',
      destinationId: 'runtime'
    },
    'manage-forsetti-modules': {
      title: 'Manage Forsetti Modules',
      eyebrow: 'GUIDED SETUP',
      description: 'Install, enable, update, or disable Forsetti modules without dropping into raw runtime state.',
      destinationId: 'clu'
    },
    'guided-security': {
      title: 'Guided Security Hardening',
      eyebrow: 'GUIDED SETUP',
      description: 'Choose the security posture, review trusted-host access, and publish the protection envelope without working through the raw security editor.',
      destinationId: 'security'
    },
    'guided-settings': {
      title: 'Guided Host Settings',
      eyebrow: 'GUIDED SETUP',
      description: 'Confirm bind ports, beacon behavior, and the governed resource envelope through a guided host configuration flow.',
      destinationId: 'settings'
    },
    'guided-import': {
      title: 'Guided Import',
      eyebrow: 'GUIDED SETUP',
      description: 'Walk through package, repository, or zip onboarding with the same browser control plane used by the desktop shell.',
      destinationId: 'imports'
    }
  };
}

function guidedWorkflowDefinition(workflowId) {
  return guidedWorkflowDefinitions()[workflowId] || null;
}

function recommendedForsettiAction(moduleRecord) {
  const recommended = String(moduleRecord?.recommendedAction || '').toLowerCase();
  if (recommended.includes('update') || recommended.includes('reload')) {
    return 'update';
  }
  if (recommended.includes('disable') || recommended.includes('remove')) {
    return 'disable';
  }
  return 'install';
}

function selectedGuidedProviderCapability() {
  const capabilityId = state.guidedWorkflow?.providerCapabilityId || '';
  const capabilities = dashboardSnapshot().providerCapabilities;
  if (!capabilityId) {
    return capabilities[0] || null;
  }
  return capabilities.find((capability) => capability.providerId === capabilityId) || capabilities[0] || null;
}

function selectedGuidedForsettiModule() {
  const moduleId = state.guidedWorkflow?.moduleId || '';
  const modules = safeArray(state.guidedWorkflow?.moduleCatalog);
  if (!moduleId) {
    return modules[0] || null;
  }
  return modules.find((moduleRecord) => moduleRecord.moduleId === moduleId) || modules[0] || null;
}

function runtimeMaintenanceCollections() {
  const snapshot = dashboardSnapshot();
  return {
    mcpServers: snapshot.endpoints.filter((endpoint) => String(endpoint.kind || '').toLowerCase() === 'mcp_server' && !!endpoint.userDefined),
    subAgents: snapshot.endpoints.filter((endpoint) => String(endpoint.kind || '').toLowerCase() === 'sub_agent' && !!endpoint.userDefined),
    appleHosts: safeArray(governanceSnapshot().appleRemoteHosts)
  };
}

function runtimeMaintenanceKind() {
  return state.guidedWorkflow?.runtimeMaintenanceKind || 'mcp';
}

function primeRuntimeMaintenanceDraft(kind = runtimeMaintenanceKind()) {
  const collections = runtimeMaintenanceCollections();
  if (kind === 'subagent') {
    applySubAgentDraft(collections.subAgents[0]?.id || '');
    return;
  }
  if (kind === 'apple') {
    applyAppleHostDraft(collections.appleHosts[0]?.hostId || '');
    return;
  }
  applyMcpServerDraft(collections.mcpServers[0]?.id || '');
}

function renderGuidedWorkflowLaunchers(workflowIds) {
  return `
    <div class="wizard-launcher-grid">
      ${workflowIds.map((workflowId) => {
        const definition = guidedWorkflowDefinition(workflowId);
        if (!definition) {
          return '';
        }
        return `
          <button type="button" class="wizard-launcher" data-action="open-guided-workflow" data-workflow-id="${escapeHtml(workflowId)}">
            <strong>${escapeHtml(definition.title)}</strong>
            <span>${escapeHtml(definition.description)}</span>
          </button>
        `;
      }).join('')}
    </div>
  `;
}

// WS2 — render the auto-connect progress list so the user sees real
// orchestration steps, not just a save-and-reload.
function renderAutoConnectProgress(progress) {
  if (!progress) { return ''; }
  const steps = Array.isArray(progress.steps) ? progress.steps : [];
  const stepsHtml = steps.map((step) => {
    const icon = step.succeeded ? '[OK]' : '[FAIL]';
    const latency = (typeof step.latencyMs === 'number' && step.latencyMs > 0)
      ? ` <span class="step-latency">(${step.latencyMs} ms)</span>` : '';
    return `<li class="auto-connect-step ${step.succeeded ? 'succeeded' : 'failed'}">${icon} <strong>${escapeHtml(step.stage || '')}</strong>${latency} — ${escapeHtml(step.message || '')}</li>`;
  }).join('');
  const header = progress.status === 'in-progress'
    ? 'Auto-connecting...'
    : (progress.status === 'done' ? 'Auto-connect complete.' : 'Auto-connect failed.');
  const note = progress.note ? `<p class="narrative-copy">${escapeHtml(progress.note)}</p>` : '';
  return `
    <article class="panel-block auto-connect-progress" data-status="${escapeHtml(progress.status || '')}">
      <p class="eyebrow">Orchestration Progress</p>
      <h4>${escapeHtml(header)}</h4>
      ${note}
      <ol class="auto-connect-steps">${stepsHtml}</ol>
    </article>
  `;
}

// WS2 — render the manual-fallback banner when auto-connect fails, offering
// the user a clear button to continue via the legacy manual flow.
function renderAutoConnectFallbackBanner(fallback) {
  if (!fallback || !fallback.active) { return ''; }
  return `
    <article class="panel-block auto-connect-fallback" data-status="fallback">
      <p class="eyebrow">Manual Setup Available</p>
      <h4>Auto-connect failed at ${escapeHtml(fallback.stage || 'orchestration')}</h4>
      <p class="narrative-copy">${escapeHtml(fallback.message || '')}</p>
      <p class="narrative-copy">You can continue with manual setup using the values you already entered.</p>
      <div class="button-row">
        <button type="button" class="route-button" data-action="continue-manual-fallback">Continue with manual setup</button>
        <button type="button" class="route-button" data-action="clear-manual-fallback">Try auto-connect again</button>
      </div>
    </article>
  `;
}

function renderGuidedWorkflowContent() {
  const workflow = state.guidedWorkflow || defaultGuidedWorkflowState();
  const definition = guidedWorkflowDefinition(workflow.id);
  if (!definition) {
    return renderUnavailableView('Guided workflow unavailable', 'The selected setup workflow could not be resolved.');
  }

  const snapshot = dashboardSnapshot();
  const capability = selectedGuidedProviderCapability();
  const credentialFields = safeArray(capability?.credentialFields);
  const providerAssignmentTargets = snapshot.providerAssignmentTargets;
  const providers = snapshot.providers;
  const subAgentTargets = providerAssignmentTargets.filter((target) => target.kind === 'sub_agent');
  const moduleCatalog = safeArray(workflow.moduleCatalog);
  const selectedModule = selectedGuidedForsettiModule();

  if (workflow.id === 'connect-model') {
    const capabilityOptions = snapshot.providerCapabilities.map((entry) => `
      <option value="${escapeHtml(entry.providerId)}"${selectedAttr(entry.providerId === (capability?.providerId || ''))}>
        ${escapeHtml(entry.displayName || entry.providerId)}
      </option>
    `).join('');
    const targetOptions = `
      <option value="">(Leave unassigned)</option>
      ${providerAssignmentTargets.map((target) => `
        <option value="${escapeHtml(target.targetId)}">
          ${escapeHtml(target.displayName)}${target.kind ? ` | ${escapeHtml(target.kind)}` : ''}
        </option>
      `).join('')}
    `;

    return `
      <section class="wizard-shell">
        ${statusMessage(workflow.status)}
        ${renderAutoConnectProgress(workflow.progress)}
        ${renderAutoConnectFallbackBanner(workflow.fallback)}
        <div class="wizard-step-list">
          <article class="wizard-step-card">
            <p class="eyebrow">Step 1</p>
            <h3>Choose AI model connector</h3>
            <p class="narrative-copy">Start with the provider module that represents the model family or service you want to route through the orchestration server.</p>
          </article>
          <article class="wizard-step-card">
            <p class="eyebrow">Step 2</p>
            <h3>Confirm route defaults</h3>
            <p class="narrative-copy">Review route identity, base URL, model defaults, and autonomy posture before enabling the lane.</p>
          </article>
          <article class="wizard-step-card">
            <p class="eyebrow">Step 3</p>
            <h3>Secure and assign it</h3>
            <p class="narrative-copy">Add credentials and optionally give that model ownership of planning, coding, review, or specialist work.</p>
          </article>
        </div>

        <form class="surface-form wizard-form" data-form-kind="guided-provider">
          <!-- Step 1: Select the AI model (if not pre-selected by a quick-connect card) -->
          <article class="wizard-summary-card">
            <p class="eyebrow">Step 1 — Choose Your AI Model</p>
            <h3>Which AI model do you want to connect?</h3>
            <select name="capabilityId" data-guided-field="providerCapabilityId" class="provider-select-large">
              ${capabilityOptions}
            </select>
          </article>

          <!-- Step 2: Authenticate -->
          <article class="wizard-summary-card">
            <p class="eyebrow">Step 2 — Sign In</p>
            <h3>Authenticate with ${escapeHtml(capability?.displayName || 'this provider')}</h3>

            ${capability?.supportsOAuth ? `
              <!-- OAuth flow (primary) — user clicks, browser redirects, token comes back -->
              <p class="narrative-copy">Click below to sign in with your ${escapeHtml(capability?.displayName || '')} account. MCOS will handle the rest automatically.</p>
              <div class="button-row" style="margin-bottom: 1rem;">
                <button type="button" class="oauth-signin-button" data-action="oauth-signin" data-provider-id="${escapeHtml(capability?.providerId || '')}">
                  Sign in with ${escapeHtml(capability?.displayName || 'Provider')}
                </button>
              </div>
              <details>
                <summary>Or enter an API key manually</summary>
            ` : `
              <p class="narrative-copy">Enter your API key below. You can get one from your ${escapeHtml(capability?.displayName || 'provider')} account dashboard.</p>
              <p class="form-help">${escapeHtml(credentialHelpUrl(capability))}</p>
            `}

            ${credentialFields.length ? credentialFields.slice(0, 2).map((field) => {
              const hint = environmentHintStatus(field);
              const badge = hint.hint
                ? `<span class="hint-badge ${hint.badgeClass}">${escapeHtml(hint.badgeText)}</span>`
                : '';
              const placeholder = hint.detected
                ? `Using environment variable ${hint.hint}`
                : (field.placeholder || 'Paste your key here');
              return `
              <label>${escapeHtml(field.label)} ${badge}
                <input
                  name="credential:${escapeHtml(field.fieldId)}"
                  type="password"
                  placeholder="${escapeHtml(placeholder)}">
              </label>
              `;
            }).join('') : '<p class="narrative-copy">No credentials required for this connector.</p>'}

            ${capability?.supportsOAuth ? '</details>' : ''}
          </article>

          <!-- Hidden fields: populated automatically from capability defaults -->
          <input type="hidden" name="id" value="${escapeHtml(capability?.providerId || '')}">
          <input type="hidden" name="displayName" value="${escapeHtml(capability?.displayName || '')}">
          <input type="hidden" name="baseUrl" value="${escapeHtml(capability?.defaultBaseUrl || '')}">
          <input type="hidden" name="modelId" value="${escapeHtml(capability?.recommendedModel || '')}">
          <input type="hidden" name="enabled" value="true">
          <input type="hidden" name="allowAutonomousControl" value="false">
          <input type="hidden" name="targetId" value="">

          <div class="button-row">
            <button type="submit">Connect ${escapeHtml(capability?.displayName || 'AI Model')}</button>
            <button type="button" class="route-button" data-action="close-guided-workflow">Cancel</button>
          </div>

          <p class="narrative-copy" style="margin-top: 1rem; opacity: 0.6;">After connecting, you can assign roles (planner, architect, coder) and configure autonomy from the Providers section.</p>
        </form>
      </section>
    `;
  }

  if (workflow.id === 'assign-responsibility') {
    const providerOptions = providers.map((provider) => `
      <option value="${escapeHtml(provider.id)}">${escapeHtml(provider.displayName || provider.id)}</option>
    `).join('');
    const targetOptions = providerAssignmentTargets.map((target) => `
      <option value="${escapeHtml(target.targetId)}" data-kind="${escapeHtml(target.kind || 'role')}">
        ${escapeHtml(target.displayName || target.targetId)}${target.kind ? ` | ${escapeHtml(target.kind)}` : ''}
      </option>
    `).join('');

    return `
      <section class="wizard-shell">
        ${statusMessage(workflow.status)}
        <div class="wizard-step-list">
          <article class="wizard-step-card"><p class="eyebrow">Step 1</p><h3>Choose connected AI model</h3><p class="narrative-copy">Pick the provider route that should own one orchestration lane.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 2</p><h3>Choose the responsibility lane</h3><p class="narrative-copy">Select planner, coding, review, specialist, or group ownership.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 3</p><h3>Save routing</h3><p class="narrative-copy">CLU and provider execution will use the same ownership map after you save.</p></article>
        </div>

        <form class="surface-form wizard-form" data-form-kind="guided-provider-assignment">
          <div class="wizard-field-grid">
            <label>Connected AI Model
              <select name="providerId"${providers.length ? '' : ' disabled'}>
                ${providerOptions || '<option value="">No providers available</option>'}
              </select>
            </label>
            <label>Responsibility Lane
              <select name="targetId"${providerAssignmentTargets.length ? '' : ' disabled'}>
                ${targetOptions || '<option value="">No lanes available</option>'}
              </select>
            </label>
          </div>
          <article class="wizard-summary-card">
            <p class="eyebrow">Responsibility Mapping</p>
            <h3>Model-to-Lane Ownership</h3>
            <p class="narrative-copy">Use this when you want one model planning, another coding, and others reviewing or handling specialist groups.</p>
          </article>
          <div class="button-row">
            <button type="submit"${providers.length && providerAssignmentTargets.length ? '' : ' disabled'}>Assign Responsibility</button>
            <button type="button" class="route-button" data-action="close-guided-workflow">Cancel</button>
          </div>
        </form>
      </section>
    `;
  }

  if (workflow.id === 'guided-provider-execution') {
    const executionDraft = state.providerExecutionDraft;
    const targetOptions = providerAssignmentTargets.map((target) => `
      <option value="${escapeHtml(target.targetId)}"${selectedAttr(target.targetId === executionDraft.targetId)}>
        ${escapeHtml(target.displayName || target.targetId)}${target.kind ? ` | ${escapeHtml(target.kind)}` : ''}
      </option>
    `).join('');
    const promptTemplates = [
      {
        label: 'Planning Check',
        prompt: 'Create a concise plan for this orchestration lane and explain the first action you would take.'
      },
      {
        label: 'Coding Check',
        prompt: 'Describe how you would implement the next coding task for this lane and note any MCP tools you would need.'
      },
      {
        label: 'Review Check',
        prompt: 'Review the current state of this lane, identify the highest-risk issue, and recommend the next operator action.'
      },
      {
        label: 'Specialist Coordination',
        prompt: 'Summarize how this lane would coordinate with related specialists and what inputs it needs before execution.'
      }
    ];
    const activeTemplatePrompt = executionDraft.prompt || promptTemplates[0].prompt;
    const templateOptions = promptTemplates.map((template) => `
      <option value="${escapeHtml(template.prompt)}"${selectedAttr(activeTemplatePrompt === template.prompt)}>${escapeHtml(template.label)}</option>
    `).join('');

    return `
      <section class="wizard-shell">
        ${statusMessage(workflow.status)}
        <div class="wizard-step-list">
          <article class="wizard-step-card"><p class="eyebrow">Step 1</p><h3>Choose the lane</h3><p class="narrative-copy">Pick the responsibility lane whose ownership and credentials you want to validate.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 2</p><h3>Choose the validation pattern</h3><p class="narrative-copy">Start from a planning, coding, review, or specialist coordination prompt instead of writing one from scratch.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 3</p><h3>Run the guided check</h3><p class="narrative-copy">Dispatch the provider-owned task through the local admin API and publish the result back into execution history.</p></article>
        </div>

        <form class="surface-form wizard-form" data-form-kind="guided-provider-execution">
          <div class="wizard-field-grid">
            <label>Execution Target
              <select name="targetId"${providerAssignmentTargets.length ? '' : ' disabled'}>
                ${targetOptions || '<option value="">No role or sub-agent lanes available</option>'}
              </select>
            </label>
            <label>Validation Pattern
              <select name="templatePrompt" data-guided-field="providerExecutionTemplatePrompt"${providerAssignmentTargets.length ? '' : ' disabled'}>
                ${templateOptions}
              </select>
            </label>
            <label>Max Turns
              <input name="maxTurns" type="number" min="1" max="12" value="${escapeHtml(executionDraft.maxTurns || 4)}">
            </label>
          </div>

          <label class="checkbox-field">
            <input name="allowToolAccess" type="checkbox"${checkedAttr(executionDraft.allowToolAccess)}>
            <span>Allow shared MCP tool access during validation.</span>
          </label>

          <label>Prompt
            <textarea name="prompt" rows="6" placeholder="Ask the assigned provider to work within its orchestration lane.">${escapeHtml(activeTemplatePrompt)}</textarea>
          </label>

          <article class="wizard-summary-card">
            <p class="eyebrow">Validation Result</p>
            <h3>Provider Routing Check</h3>
            <p class="narrative-copy">Use this to confirm responsibility ownership, credentials, and shared MCP access without dropping into the raw execution console first.</p>
          </article>

          <div class="button-row">
            <button type="submit"${providerAssignmentTargets.length ? '' : ' disabled'}>Run Guided Validation</button>
            <button type="button" class="route-button" data-action="close-guided-workflow">Cancel</button>
          </div>
        </form>
      </section>
    `;
  }

  if (workflow.id === 'guided-runtime-maintenance') {
    const collections = runtimeMaintenanceCollections();
    const maintenanceKind = runtimeMaintenanceKind();
    const activeDraft = maintenanceKind === 'subagent'
      ? state.subAgentDraft
      : (maintenanceKind === 'apple' ? state.appleHostDraft : state.mcpServerDraft);
    const options = maintenanceKind === 'subagent'
      ? collections.subAgents.map((endpoint) => ({
        value: endpoint.id,
        label: `${endpoint.displayName || endpoint.id}${endpoint.specialization ? ` | ${endpoint.specialization}` : ''}`
      }))
      : (maintenanceKind === 'apple'
        ? collections.appleHosts.map((host) => ({
          value: host.hostId,
          label: `${host.displayName || host.hostId}${host.transport ? ` | ${transportLabel(host.transport)}` : ''}`
        }))
        : collections.mcpServers.map((endpoint) => ({
          value: endpoint.id,
          label: `${endpoint.displayName || endpoint.id}${endpoint.routePath ? ` | ${endpoint.routePath}` : ''}`
        })));
    const selectedValue = maintenanceKind === 'apple' ? activeDraft.hostId : activeDraft.id;
    const selectorOptions = options.map((option) => `
      <option value="${escapeHtml(option.value)}"${selectedAttr(option.value === selectedValue)}>${escapeHtml(option.label)}</option>
    `).join('');

    return `
      <section class="wizard-shell">
        ${statusMessage(workflow.status)}
        <div class="wizard-step-list">
          <article class="wizard-step-card"><p class="eyebrow">Step 1</p><h3>Choose the lane type</h3><p class="narrative-copy">Switch between shared MCP servers, custom sub-agents, and Apple remote hosts.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 2</p><h3>Load a published lane</h3><p class="narrative-copy">Pick an existing lane instead of editing the runtime card directly.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 3</p><h3>Save or remove it</h3><p class="narrative-copy">Apply changes through the same local admin API used by the runtime view.</p></article>
        </div>

        <form class="surface-form wizard-form" data-form-kind="guided-runtime-maintenance">
          <div class="wizard-field-grid">
            <label>Runtime Lane Type
              <select name="maintenanceKind" data-guided-field="runtimeMaintenanceKind">
                <option value="mcp"${selectedAttr(maintenanceKind === 'mcp')}>Shared MCP Server</option>
                <option value="subagent"${selectedAttr(maintenanceKind === 'subagent')}>Custom Sub-Agent</option>
                <option value="apple"${selectedAttr(maintenanceKind === 'apple')}>Apple Remote Host</option>
              </select>
            </label>
            <label>Published Lane
              <select name="laneId" data-guided-field="runtimeMaintenanceTarget"${options.length ? '' : ' disabled'}>
                ${selectorOptions || '<option value="">No published lanes available</option>'}
              </select>
            </label>
          </div>

          ${maintenanceKind === 'mcp' ? `
            <div class="wizard-field-grid">
              <label>MCP Server ID<input name="id" value="${escapeHtml(state.mcpServerDraft.id)}" required></label>
              <label>Display Name<input name="displayName" value="${escapeHtml(state.mcpServerDraft.displayName)}" required></label>
              <label>Host<input name="host" value="${escapeHtml(state.mcpServerDraft.host)}"></label>
              <label>Port<input name="port" type="number" min="1" max="65535" value="${escapeHtml(state.mcpServerDraft.port)}" required></label>
              <label>Protocol<input name="protocol" value="${escapeHtml(state.mcpServerDraft.protocol || 'http')}"></label>
              <label>Route Path<input name="routePath" value="${escapeHtml(state.mcpServerDraft.routePath || '/mcp')}"></label>
            </div>
            <label>Notes
              <textarea name="description" rows="4">${escapeHtml(state.mcpServerDraft.description || '')}</textarea>
            </label>
          ` : ''}

          ${maintenanceKind === 'subagent' ? `
            <div class="wizard-field-grid">
              <label>Sub-Agent ID<input name="id" value="${escapeHtml(state.subAgentDraft.id)}" required></label>
              <label>Display Name<input name="displayName" value="${escapeHtml(state.subAgentDraft.displayName)}" required></label>
              <label>Specialization<input name="specialization" value="${escapeHtml(state.subAgentDraft.specialization || '')}"></label>
              <label>Host<input name="host" value="${escapeHtml(state.subAgentDraft.host || '')}"></label>
              <label>Port<input name="port" type="number" min="0" max="65535" value="${escapeHtml(state.subAgentDraft.port)}"></label>
              <label>Protocol<input name="protocol" value="${escapeHtml(state.subAgentDraft.protocol || 'virtual')}"></label>
              <label>Route Path<input name="routePath" value="${escapeHtml(state.subAgentDraft.routePath || '')}"></label>
            </div>
            <label>Notes
              <textarea name="description" rows="4">${escapeHtml(state.subAgentDraft.description || '')}</textarea>
            </label>
          ` : ''}

          ${maintenanceKind === 'apple' ? `
            <div class="wizard-field-grid">
              <label>Host ID<input name="hostId" value="${escapeHtml(state.appleHostDraft.hostId)}" required></label>
              <label>Display Name<input name="displayName" value="${escapeHtml(state.appleHostDraft.displayName)}" required></label>
              <label>Transport
                <select name="transport">
                  <option value="companion_service"${selectedAttr(state.appleHostDraft.transport === 'companion_service')}>Companion Service</option>
                  <option value="ssh"${selectedAttr(state.appleHostDraft.transport === 'ssh')}>SSH</option>
                </select>
              </label>
              <label>Address or Hostname<input name="address" value="${escapeHtml(state.appleHostDraft.address || '')}"></label>
              <label>Port<input name="port" type="number" min="0" max="65535" value="${escapeHtml(state.appleHostDraft.port)}"></label>
              <label>Username<input name="username" value="${escapeHtml(state.appleHostDraft.username || '')}"></label>
              <label>Companion Base URL<input name="serviceBaseUrl" value="${escapeHtml(state.appleHostDraft.serviceBaseUrl || '')}"></label>
              <label>Health Path<input name="companionHealthPath" value="${escapeHtml(state.appleHostDraft.companionHealthPath || '/healthz')}"></label>
              <label>Execute Path<input name="companionExecutePath" value="${escapeHtml(state.appleHostDraft.companionExecutePath || '/execute')}"></label>
              <label>Preferred Developer Directory<input name="preferredDeveloperDirectory" value="${escapeHtml(state.appleHostDraft.preferredDeveloperDirectory || '')}"></label>
              <label>Default Signing Identity<input name="defaultSigningIdentity" value="${escapeHtml(state.appleHostDraft.defaultSigningIdentity || '')}"></label>
              <label>Default Notary Profile<input name="defaultNotaryKeychainProfile" value="${escapeHtml(state.appleHostDraft.defaultNotaryKeychainProfile || '')}"></label>
              <label>Default Notary Team ID<input name="defaultNotaryTeamId" value="${escapeHtml(state.appleHostDraft.defaultNotaryTeamId || '')}"></label>
            </div>
            <div class="wizard-toggles">
              <label class="checkbox-field"><input type="checkbox" name="platform" value="macos"${checkedAttr(safeArray(state.appleHostDraft.platforms).includes('macos'))}><span>macOS lane enabled</span></label>
              <label class="checkbox-field"><input type="checkbox" name="platform" value="ios"${checkedAttr(safeArray(state.appleHostDraft.platforms).includes('ios'))}><span>iOS lane enabled</span></label>
              <label class="checkbox-field"><input type="checkbox" name="enabled"${checkedAttr(state.appleHostDraft.enabled ?? true)}><span>Host enabled</span></label>
            </div>
          ` : ''}

          <article class="wizard-summary-card">
            <p class="eyebrow">Maintenance Scope</p>
            <h3>${escapeHtml(options.length ? 'Published Runtime Lane' : 'No Published Lane')}</h3>
            <p class="narrative-copy">${escapeHtml(options.length
              ? 'Update the loaded lane here or remove it cleanly without dropping into the raw runtime editors.'
              : 'Use the creation wizards first, then come back here to maintain published lanes.')}</p>
          </article>

          <div class="button-row">
            <button type="submit"${options.length ? '' : ' disabled'}>Save Runtime Lane</button>
            <button type="button" class="route-button" data-action="guided-runtime-maintenance-remove"${options.length ? '' : ' disabled'}>Remove Runtime Lane</button>
            <button type="button" class="route-button" data-action="close-guided-workflow">Cancel</button>
          </div>
        </form>
      </section>
    `;
  }

  if (workflow.id === 'new-mcp') {
    return `
      <section class="wizard-shell">
        ${statusMessage(workflow.status)}
        <div class="wizard-step-list">
          <article class="wizard-step-card"><p class="eyebrow">Step 1</p><h3>Name the MCP lane</h3><p class="narrative-copy">Choose an ID and display name operators will recognize later.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 2</p><h3>Point it at the host</h3><p class="narrative-copy">Provide host, port, and protocol for the shared MCP surface.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 3</p><h3>Publish route details</h3><p class="narrative-copy">Set the route path and description so every provider can reuse it.</p></article>
        </div>

        <form class="surface-form wizard-form" data-form-kind="guided-mcp-server">
          <div class="wizard-field-grid">
            <label>MCP Server ID<input name="id" placeholder="swift-tools-mcp" required></label>
            <label>Display Name<input name="displayName" placeholder="Swift Tools MCP" required></label>
            <label>Host<input name="host" placeholder="127.0.0.1 or LAN host" required></label>
            <label>Port<input name="port" type="number" min="1" max="65535" placeholder="7305" required></label>
            <label>Protocol<input name="protocol" value="http" placeholder="http or https"></label>
            <label>Route Path<input name="routePath" value="/mcp" placeholder="/mcp"></label>
          </div>
          <label>Description
            <textarea name="description" rows="4" placeholder="What tools or capabilities does this shared MCP lane expose?"></textarea>
          </label>
          <div class="button-row">
            <button type="submit">Create MCP Server</button>
            <button type="button" class="route-button" data-action="close-guided-workflow">Cancel</button>
          </div>
        </form>
      </section>
    `;
  }

  if (workflow.id === 'new-subagent') {
    return `
      <section class="wizard-shell">
        ${statusMessage(workflow.status)}
        <div class="wizard-step-list">
          <article class="wizard-step-card"><p class="eyebrow">Step 1</p><h3>Name the specialist lane</h3><p class="narrative-copy">Create a sub-agent identity your team will recognize in routing and history.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 2</p><h3>Define its specialty</h3><p class="narrative-copy">Capture what this lane is responsible for so assignments stay obvious.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 3</p><h3>Publish optional endpoint details</h3><p class="narrative-copy">Add host, port, and route values only if this lane maps to a live endpoint.</p></article>
        </div>

        <form class="surface-form wizard-form" data-form-kind="guided-subagent">
          <div class="wizard-field-grid">
            <label>Sub-Agent ID<input name="id" placeholder="swift-specialist" required></label>
            <label>Display Name<input name="displayName" placeholder="Swift Specialist" required></label>
            <label>Specialization<input name="specialization" placeholder="Swift, C++, documentation, test automation..." required></label>
            <label>Host<input name="host" placeholder="Optional bind or LAN host"></label>
            <label>Port<input name="port" type="number" min="0" max="65535" value="0"></label>
            <label>Protocol<input name="protocol" value="virtual" placeholder="virtual or http"></label>
            <label>Route Path<input name="routePath" placeholder="/status or blank for logical lanes"></label>
          </div>
          <label>Description
            <textarea name="description" rows="4" placeholder="Optional notes about this specialist lane."></textarea>
          </label>
          <div class="button-row">
            <button type="submit">Create Sub-Agent</button>
            <button type="button" class="route-button" data-action="close-guided-workflow">Cancel</button>
          </div>
        </form>
      </section>
    `;
  }

  if (workflow.id === 'new-subagent-group') {
    return `
      <section class="wizard-shell">
        ${statusMessage(workflow.status)}
        <div class="wizard-step-list">
          <article class="wizard-step-card"><p class="eyebrow">Step 1</p><h3>Name the group</h3><p class="narrative-copy">Create a stable group identity for a specialist lane or squad.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 2</p><h3>Describe the responsibility</h3><p class="narrative-copy">Make the purpose obvious to operators and CLU routing decisions.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 3</p><h3>Select members</h3><p class="narrative-copy">Choose the sub-agents that should work together when this group is assigned a provider.</p></article>
        </div>

        <form class="surface-form wizard-form" data-form-kind="guided-subagent-group">
          <div class="wizard-field-grid">
            <label>Group ID<input name="groupId" placeholder="coding-squad" required></label>
            <label>Display Name<input name="displayName" placeholder="Coding Squad" required></label>
          </div>
          <label>Description
            <textarea name="description" rows="4" placeholder="Explain what this specialist group owns."></textarea>
          </label>
          <article class="wizard-summary-card">
            <p class="eyebrow">Sub-Agent Members</p>
            <h3>Specialist Squad</h3>
            ${subAgentTargets.length ? subAgentTargets.map((target) => `
              <label class="checkbox-field">
                <input name="memberTargetId" type="checkbox" value="${escapeHtml(target.targetId)}">
                <span>${escapeHtml(target.displayName || target.targetId)}</span>
              </label>
            `).join('') : '<p class="narrative-copy">Create at least one sub-agent before building a group.</p>'}
          </article>
          <div class="button-row">
            <button type="submit"${subAgentTargets.length ? '' : ' disabled'}>Create Sub-Agent Group</button>
            <button type="button" class="route-button" data-action="close-guided-workflow">Cancel</button>
          </div>
        </form>
      </section>
    `;
  }

  if (workflow.id === 'new-apple-host') {
    return `
      <section class="wizard-shell">
        ${statusMessage(workflow.status)}
        <div class="wizard-step-list">
          <article class="wizard-step-card"><p class="eyebrow">Step 1</p><h3>Name the Apple host</h3><p class="narrative-copy">Register a stable identity operators can route Mac and iOS work to.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 2</p><h3>Choose transport</h3><p class="narrative-copy">Use SSH or Companion Service and publish how the orchestration server reaches the host.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 3</p><h3>Set defaults</h3><p class="narrative-copy">Publish developer, signing, and notary defaults that CLU can reuse safely.</p></article>
        </div>

        <form class="surface-form wizard-form" data-form-kind="guided-apple-host">
          <div class="wizard-field-grid">
            <label>Host ID<input name="hostId" placeholder="apple-host-01" required></label>
            <label>Display Name<input name="displayName" placeholder="Primary Apple Build Host" required></label>
            <label>Transport
              <select name="transport">
                <option value="companion_service">Companion Service</option>
                <option value="ssh">SSH</option>
              </select>
            </label>
            <label>Address or Hostname<input name="address" placeholder="mac-builder.local"></label>
            <label>Port<input name="port" type="number" min="0" max="65535" placeholder="22"></label>
            <label>Username<input name="username" placeholder="builder"></label>
            <label>Companion Base URL<input name="serviceBaseUrl" placeholder="http://mac-builder.local:8081"></label>
            <label>Preferred Developer Directory<input name="preferredDeveloperDirectory" placeholder="/Applications/Xcode.app/Contents/Developer"></label>
            <label>Default Signing Identity<input name="defaultSigningIdentity" placeholder="Developer ID Application: Example Corp"></label>
            <label>Default Notary Profile<input name="defaultNotaryKeychainProfile" placeholder="mastercontrol-notary"></label>
            <label>Default Notary Team ID<input name="defaultNotaryTeamId" placeholder="ABCDE12345"></label>
          </div>
          <div class="wizard-toggles">
            <label class="checkbox-field"><input type="checkbox" name="platform" value="macos" checked><span>macOS lane enabled</span></label>
            <label class="checkbox-field"><input type="checkbox" name="platform" value="ios" checked><span>iOS lane enabled</span></label>
            <label class="checkbox-field"><input type="checkbox" name="enabled" checked><span>Host is enabled for routing</span></label>
          </div>
          <div class="button-row">
            <button type="submit">Create Apple Host</button>
            <button type="button" class="route-button" data-action="close-guided-workflow">Cancel</button>
          </div>
        </form>
      </section>
    `;
  }

  if (workflow.id === 'manage-forsetti-modules') {
    const moduleOptions = moduleCatalog.map((moduleRecord) => `
      <option value="${escapeHtml(moduleRecord.moduleId)}"${selectedAttr(moduleRecord.moduleId === (selectedModule?.moduleId || ''))}>
        ${escapeHtml(moduleRecord.displayName || moduleRecord.moduleId)}
      </option>
    `).join('');
    const moduleDetails = selectedModule ? [
      selectedModule.displayName || selectedModule.moduleId,
      selectedModule.version ? `Version: ${selectedModule.version}` : '',
      selectedModule.moduleType ? `Type: ${selectedModule.moduleType}` : '',
      `Status: ${selectedModule.statusSummary || (selectedModule.active ? 'active' : 'inactive')}`,
      `Unlocked: ${selectedModule.unlocked ? 'yes' : 'no'}`,
      `Protected: ${selectedModule.protectedModule ? 'yes' : 'no'}`,
      safeArray(selectedModule.supportedPlatforms).length ? `Platforms: ${safeArray(selectedModule.supportedPlatforms).join(', ')}` : '',
      safeArray(selectedModule.capabilitiesRequested).length ? `Capabilities: ${safeArray(selectedModule.capabilitiesRequested).join(', ')}` : '',
      selectedModule.recommendedAction ? `Recommended action: ${selectedModule.recommendedAction}` : ''
    ].filter(Boolean).join('\n') : 'No Forsetti module is selected.';

    return `
      <section class="wizard-shell">
        ${statusMessage(workflow.status)}
        <div class="wizard-step-list">
          <article class="wizard-step-card"><p class="eyebrow">Step 1</p><h3>Choose the module</h3><p class="narrative-copy">Select the Forsetti module you want to activate, refresh, or disable.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 2</p><h3>Review its status</h3><p class="narrative-copy">Check version, platforms, protections, and recommended action before changing it.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 3</p><h3>Apply module action</h3><p class="narrative-copy">Install, update, or disable the module through the local orchestration runtime.</p></article>
        </div>

        <form class="surface-form wizard-form" data-form-kind="guided-forsetti-module">
          <div class="wizard-field-grid">
            <label>Forsetti Module
              <select name="moduleId" data-guided-field="moduleId"${moduleCatalog.length ? '' : ' disabled'}>
                ${moduleOptions || '<option value="">No modules available</option>'}
              </select>
            </label>
            <label>Module Action
              <select name="action">
                <option value="install"${selectedAttr(workflow.moduleAction === 'install')}>Install or Enable</option>
                <option value="update"${selectedAttr(workflow.moduleAction === 'update')}>Update or Reload</option>
                <option value="disable"${selectedAttr(workflow.moduleAction === 'disable')}>Disable or Remove</option>
              </select>
            </label>
          </div>
          <article class="wizard-summary-card">
            <p class="eyebrow">Module Status</p>
            <h3>${escapeHtml(selectedModule?.displayName || 'Forsetti module status')}</h3>
            <p class="narrative-copy">${multilineHtml(moduleDetails)}</p>
          </article>
          <div class="button-row">
            <button type="button" class="route-button" data-action="refresh-guided-module-catalog">Refresh Catalog</button>
            <button type="submit"${moduleCatalog.length ? '' : ' disabled'}>Apply Module Action</button>
            <button type="button" class="route-button" data-action="close-guided-workflow">Cancel</button>
          </div>
        </form>
      </section>
    `;
  }

  if (workflow.id === 'guided-import') {
    const importMode = workflow.importMode || 'package';
    return `
      <section class="wizard-shell">
        ${statusMessage(workflow.status)}
        <div class="wizard-step-list">
          <article class="wizard-step-card"><p class="eyebrow">Step 1</p><h3>Choose input type</h3><p class="narrative-copy">Select package, repository, or zip import based on what you already have.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 2</p><h3>Provide source details</h3><p class="narrative-copy">Point the orchestration server at the source and any manifest or branch details it needs.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 3</p><h3>Launch onboarding</h3><p class="narrative-copy">Run the import through the local admin API and let the server stage the software for you.</p></article>
        </div>

        <div class="button-row">
          <button type="button" class="${importMode === 'package' ? 'route-button active' : 'route-button'}" data-action="set-guided-import-mode" data-guided-import-mode="package">Installer Package</button>
          <button type="button" class="${importMode === 'repo' ? 'route-button active' : 'route-button'}" data-action="set-guided-import-mode" data-guided-import-mode="repo">Bootstrap Repository</button>
          <button type="button" class="${importMode === 'zip' ? 'route-button active' : 'route-button'}" data-action="set-guided-import-mode" data-guided-import-mode="zip">Zip Bundle</button>
        </div>

        <form class="surface-form wizard-form ${importMode === 'package' ? '' : 'is-hidden'}" data-form-kind="guided-import-package">
          <div class="wizard-field-grid">
            <label>Source URL or Path<input name="source" required></label>
            <label>Kind
              <select name="kind">${packageKindOptions('exe')}</select>
            </label>
          </div>
          <label>Arguments<input name="arguments" placeholder="-EnvironmentName lab -Force"></label>
          <label class="checkbox-field">
            <input name="allowUntrustedExecution" type="checkbox">
            <span>Explicitly approve an untrusted source for this run.</span>
          </label>
          <div class="button-row">
            <button type="submit">Run Package Installer</button>
            <button type="button" class="route-button" data-action="close-guided-workflow">Cancel</button>
          </div>
        </form>

        <form class="surface-form wizard-form ${importMode === 'repo' ? '' : 'is-hidden'}" data-form-kind="guided-import-repo">
          <div class="wizard-field-grid">
            <label>Repository URL or Local Path<input name="repositoryUrl" required></label>
            <label>Branch<input name="branch" value="main"></label>
            <label>Manifest File<input name="manifestFile" value="mcp-bootstrap.json"></label>
          </div>
          <label class="checkbox-field">
            <input name="allowUntrustedExecution" type="checkbox">
            <span>Explicitly approve an untrusted repository source for this run.</span>
          </label>
          <div class="button-row">
            <button type="submit">Install Repository</button>
            <button type="button" class="route-button" data-action="close-guided-workflow">Cancel</button>
          </div>
        </form>

        <form class="surface-form wizard-form ${importMode === 'zip' ? '' : 'is-hidden'}" data-form-kind="guided-import-zip">
          <div class="wizard-field-grid">
            <label>Source URL or Path<input name="source" required></label>
            <label>Manifest File<input name="manifestFile" value="mcp-bootstrap.json"></label>
          </div>
          <label class="checkbox-field">
            <input name="allowUntrustedExecution" type="checkbox">
            <span>Explicitly approve an untrusted zip bundle for this run.</span>
          </label>
          <div class="button-row">
            <button type="submit">Install Zip Bundle</button>
            <button type="button" class="route-button" data-action="close-guided-workflow">Cancel</button>
          </div>
        </form>
      </section>
    `;
  }

  if (workflow.id === 'guided-security') {
    const security = currentConfig().security;
    return `
      <section class="wizard-shell">
        ${statusMessage(workflow.status)}
        <div class="wizard-step-list">
          <article class="wizard-step-card"><p class="eyebrow">Step 1</p><h3>Choose the posture</h3><p class="narrative-copy">Start from a balanced, restricted, or troubleshooting profile instead of flipping every toggle by hand.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 2</p><h3>Review access controls</h3><p class="narrative-copy">Confirm transport protection, authentication, troubleshooting bypass, and trusted hosts.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 3</p><h3>Publish the envelope</h3><p class="narrative-copy">Apply the protection posture through the same local admin API used by the desktop shell.</p></article>
        </div>

        <form class="surface-form wizard-form" data-form-kind="guided-security">
          <div class="wizard-field-grid">
            <label>Security Posture
              <select name="posture">
                <option value="balanced">Balanced Default</option>
                <option value="restricted">Restricted Operations</option>
                <option value="troubleshooting">Controlled Troubleshooting</option>
              </select>
            </label>
          </div>
          <div class="wizard-checklist">
            <label class="checkbox-field">
              <input name="securityProtocolsEnabled" type="checkbox"${checkedAttr(security.securityProtocolsEnabled)}>
              <span>Keep security protocols enabled for normal operation.</span>
            </label>
            <label class="checkbox-field">
              <input name="enableTls" type="checkbox"${checkedAttr(security.enableTls)}>
              <span>Require TLS wherever the service supports it.</span>
            </label>
            <label class="checkbox-field">
              <input name="enableAuthentication" type="checkbox"${checkedAttr(security.enableAuthentication)}>
              <span>Require authenticated access for protected operations.</span>
            </label>
            <label class="checkbox-field">
              <input name="allowTroubleshootingBypass" type="checkbox"${checkedAttr(security.allowTroubleshootingBypass)}>
              <span>Allow a tightly controlled troubleshooting bypass.</span>
            </label>
            <label class="checkbox-field">
              <input name="allowOpenLanAccess" type="checkbox"${checkedAttr(security.allowOpenLanAccess)}>
              <span>Expose the browser surface to the wider local LAN.</span>
            </label>
          </div>
          <label>Trusted Remote Hosts
            <textarea name="trustedRemoteHosts" rows="6" placeholder="one host per line or comma-separated">${escapeHtml(security.trustedRemoteHosts.join('\n'))}</textarea>
          </label>
          <div class="button-row">
            <button type="submit">Apply Security Hardening</button>
            <button type="button" class="route-button" data-action="close-guided-workflow">Cancel</button>
          </div>
        </form>
      </section>
    `;
  }

  if (workflow.id === 'guided-settings') {
    const config = currentConfig();
    return `
      <section class="wizard-shell">
        ${statusMessage(workflow.status)}
        <div class="wizard-step-list">
          <article class="wizard-step-card"><p class="eyebrow">Step 1</p><h3>Confirm host identity</h3><p class="narrative-copy">Set the instance name, bind address, and service ports that operators should rely on.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 2</p><h3>Choose discovery behavior</h3><p class="narrative-copy">Decide whether the LAN beacon should keep broadcasting runtime presence and metadata.</p></article>
          <article class="wizard-step-card"><p class="eyebrow">Step 3</p><h3>Shape the budget</h3><p class="narrative-copy">Tune the governed CPU, memory, bandwidth, and storage envelope for managed launches.</p></article>
        </div>

        <form class="surface-form wizard-form" data-form-kind="guided-settings">
          <div class="wizard-field-grid">
            <label>Instance Name<input name="instanceName" value="${escapeHtml(config.instanceName)}" required></label>
            <label>Bind Address<input name="bindAddress" value="${escapeHtml(config.bindAddress)}" required></label>
            <label>Browser Port<input name="browserPort" type="number" min="1" max="65535" value="${escapeHtml(config.browserPort)}"></label>
            <label>Beacon Port<input name="beaconPort" type="number" min="1" max="65535" value="${escapeHtml(config.beaconPort)}"></label>
            <label>CPU Allocation %<input name="cpuPercent" type="number" min="0" max="100" value="${escapeHtml(config.resourceAllocation.cpuPercent)}"></label>
            <label>Memory Allocation %<input name="memoryPercent" type="number" min="0" max="100" value="${escapeHtml(config.resourceAllocation.memoryPercent)}"></label>
            <label>Bandwidth Allocation %<input name="bandwidthPercent" type="number" min="0" max="100" value="${escapeHtml(config.resourceAllocation.bandwidthPercent)}"></label>
            <label>Storage Allocation %<input name="storagePercent" type="number" min="0" max="100" value="${escapeHtml(config.resourceAllocation.storagePercent)}"></label>
          </div>
          <label class="checkbox-field">
            <input name="beaconEnabled" type="checkbox"${checkedAttr(config.beaconEnabled)}>
            <span>Broadcast the LAN beacon and gateway metadata.</span>
          </label>
          <div class="button-row">
            <button type="submit">Apply Host Settings</button>
            <button type="button" class="route-button" data-action="close-guided-workflow">Cancel</button>
          </div>
        </form>
      </section>
    `;
  }

  return renderUnavailableView('Guided workflow unavailable', 'The selected setup workflow does not have browser content yet.');
}

function normalizeNavigationPointer(pointer) {
  return {
    id: pointer?.pointerID || pointer?.id || '',
    label: pointer?.label || '',
    destinationId: pointer?.baseDestinationID || pointer?.destinationID || pointer?.destinationId || ''
  };
}

function normalizeToolbarItem(item) {
  const action = item?.action || {};
  const actionKind = action?.type || item?.actionKind || 'unknown';
  let targetId = '';
  if (actionKind === 'navigate') {
    targetId = action?.destinationID || action?.destinationId || item?.targetId || '';
  } else if (actionKind === 'openOverlay') {
    targetId = action?.routeID || action?.routeId || item?.targetId || '';
  } else if (actionKind === 'publishEvent') {
    targetId = action?.eventType || item?.targetId || '';
  }

  return {
    id: item?.itemID || item?.id || '',
    title: item?.title || '',
    systemImageName: item?.systemImageName || '',
    actionKind,
    targetId
  };
}

function normalizeOverlayRoute(route) {
  const destination = route?.destination || {};
  const destinationType = destination?.type || 'base';
  return {
    id: route?.routeID || route?.id || '',
    label: route?.label || '',
    presentation: route?.presentation || 'sheet',
    targetsModuleView: destinationType === 'moduleOverlay',
    destinationId: destination?.destinationID || destination?.destinationId || '',
    moduleId: destination?.moduleID || destination?.moduleId || '',
    viewId: destination?.viewID || destination?.viewId || ''
  };
}

function normalizeViewInjection(injection) {
  return {
    id: injection?.injectionID || injection?.id || '',
    slotId: injection?.slotID || injection?.slotId || '',
    viewId: injection?.viewID || injection?.viewId || '',
    priority: safeNumber(injection?.priority, 0)
  };
}

function bootstrapNavigationPointers() {
  return [
    { id: 'overview-nav', label: 'Overview', destinationId: 'overview' },
    { id: 'telemetry-nav', label: 'Telemetry', destinationId: 'telemetry' },
    { id: 'runtime-nav', label: 'Runtime', destinationId: 'runtime' },
    { id: 'providers-nav', label: 'Providers', destinationId: 'providers' },
    { id: 'imports-nav', label: 'Imports', destinationId: 'imports' },
    { id: 'exports-nav', label: 'Exports', destinationId: 'exports' },
    { id: 'security-nav', label: 'Security', destinationId: 'security' },
    { id: 'settings-nav', label: 'Settings', destinationId: 'settings' }
  ];
}

function bootstrapToolbarItems() {
  return [
    { id: 'dashboard-home', title: 'Overview', systemImageName: 'network', actionKind: 'navigate', targetId: 'overview' },
    { id: 'dashboard-telemetry', title: 'Telemetry', systemImageName: 'trackers', actionKind: 'navigate', targetId: 'telemetry' },
    { id: 'dashboard-runtime', title: 'Runtime', systemImageName: 'globe', actionKind: 'navigate', targetId: 'runtime' },
    { id: 'dashboard-import', title: 'Imports', systemImageName: 'arrow.down', actionKind: 'openOverlay', targetId: 'imports-overlay' },
    { id: 'dashboard-export', title: 'Exports', systemImageName: 'share', actionKind: 'openOverlay', targetId: 'exports-overlay' },
    { id: 'dashboard-settings', title: 'Settings', systemImageName: 'gear', actionKind: 'openOverlay', targetId: 'settings-overlay' }
  ];
}

function bootstrapOverlayRoutes() {
  return [
    { id: 'settings-overlay', label: 'Settings', presentation: 'sheet', targetsModuleView: true, destinationId: '', moduleId: 'com.mastercontrol.dashboard-ui', viewId: 'SettingsSectionView' },
    { id: 'imports-overlay', label: 'Imports', presentation: 'sheet', targetsModuleView: true, destinationId: '', moduleId: 'com.mastercontrol.dashboard-ui', viewId: 'ImportsSectionView' },
    { id: 'exports-overlay', label: 'Exports', presentation: 'sheet', targetsModuleView: true, destinationId: '', moduleId: 'com.mastercontrol.dashboard-ui', viewId: 'ExportsSectionView' },
    { id: 'security-overlay', label: 'Security', presentation: 'sheet', targetsModuleView: true, destinationId: '', moduleId: 'com.mastercontrol.dashboard-ui', viewId: 'SecuritySectionView' }
  ];
}

function bootstrapViewInjectionsBySlot() {
  return {
    overview: [{ id: 'overview-surface', slotId: 'overview', viewId: 'OverviewSectionView', priority: 100 }],
    telemetry: [{ id: 'telemetry-surface', slotId: 'telemetry', viewId: 'TelemetrySectionView', priority: 100 }],
    runtime: [{ id: 'runtime-surface', slotId: 'runtime', viewId: 'RuntimeSectionView', priority: 100 }],
    providers: [{ id: 'providers-surface', slotId: 'providers', viewId: 'ProvidersSectionView', priority: 100 }],
    imports: [{ id: 'imports-surface', slotId: 'imports', viewId: 'ImportsSectionView', priority: 100 }],
    exports: [{ id: 'exports-surface', slotId: 'exports', viewId: 'ExportsSectionView', priority: 100 }],
    security: [{ id: 'security-surface', slotId: 'security', viewId: 'SecuritySectionView', priority: 100 }],
    settings: [{ id: 'settings-surface', slotId: 'settings', viewId: 'SettingsSectionView', priority: 100 }]
  };
}

function ensureBootstrapSurface(rawSurface) {
  const navigationPointers = safeArray(rawSurface?.overlaySchema?.navigationPointers)
    .map(normalizeNavigationPointer)
    .filter((pointer) => pointer.destinationId);
  const toolbarItems = safeArray(rawSurface?.toolbarItems)
    .map(normalizeToolbarItem)
    .filter((item) => item.id && item.title);
  const overlayRoutes = safeArray(rawSurface?.overlaySchema?.overlayRoutes)
    .map(normalizeOverlayRoute)
    .filter((route) => route.id);

  const viewInjectionsBySlot = {};
  const rawViewInjections = rawSurface?.viewInjectionsBySlot || {};
  for (const [slotId, injections] of Object.entries(rawViewInjections)) {
    viewInjectionsBySlot[slotId] = safeArray(injections)
      .map(normalizeViewInjection)
      .filter((injection) => injection.viewId)
      .sort((left, right) => right.priority - left.priority);
  }

  const bootstrapSlots = bootstrapViewInjectionsBySlot();
  for (const [slotId, injections] of Object.entries(bootstrapSlots)) {
    if (!viewInjectionsBySlot[slotId] || !viewInjectionsBySlot[slotId].length) {
      viewInjectionsBySlot[slotId] = injections;
    }
  }

  return {
    navigationPointers: navigationPointers.length ? navigationPointers : bootstrapNavigationPointers(),
    toolbarItems: toolbarItems.length ? toolbarItems : bootstrapToolbarItems(),
    overlayRoutes: overlayRoutes.length ? overlayRoutes : bootstrapOverlayRoutes(),
    viewInjectionsBySlot
  };
}

const state = {
  config: null,
  dashboard: null,
  surface: ensureBootstrapSurface(null),
  exports: [],
  selectedExportId: '',
  importMode: 'package',
  currentDestination: 'overview',
  overlayRouteId: '',
  overlayWorkspaceDestination: '',
  providerDraft: defaultProviderDraft(),
  providerExecutionDraft: defaultProviderExecutionDraft(),
  mcpServerDraft: defaultMcpServerDraft(),
  appleHostDraft: defaultAppleHostDraft(),
  cluAppleOperationFilter: 'attention',
  subAgentDraft: defaultSubAgentDraft(),
  subAgentGroupDraft: defaultSubAgentGroupDraft(),
  importStatus: makeStatus('Import operations are executed through the local admin API.', 'info'),
  exportStatus: makeStatus('Exports are generated by the service and downloaded directly through the browser.', 'info'),
  providerStatus: makeStatus('Provider changes are posted directly to the local admin API.', 'info'),
  runtimeStatus: makeStatus('Runtime lane changes are posted directly to the local admin API.', 'info'),
  cluStatus: makeStatus('CLU governance operations are executed through the local admin API.', 'info'),
  settingsStatus: makeStatus('Settings are authored locally and committed through the configuration API.', 'info'),
  securityStatus: makeStatus('Security changes require explicit confirmation before unsafe modes are applied.', 'info'),
  guidedWorkflow: defaultGuidedWorkflowState(),
  // WS3 — environment hint map (env var name -> bool) from /api/environment-hints
  environmentHints: {},
  // WS1 — runtime readiness snapshot from /api/readiness
  readiness: null,
  // WS1 — first-run wizard state; see defaultWizardState()
  wizard: defaultWizardState(),
  // WS4 — setup dependency detect/install state per dependency id
  setupDependencies: {},
  surfaceNotice: makeStatus('Browser host waiting for the first Forsetti surface snapshot.', 'info'),
  lastRefreshLabel: 'Pending'
};

// Shared user-facing copy — mirrors include/MasterControl/ReadinessCopy.h.
// WS8 test asserts byte-for-byte equality between this object and the header.
const READINESS_COPY = {
  hintDetected: 'Credential detected',
  hintNeeded: 'Additional information needed',
  hintNone: 'No configuration detected',
  readinessReady: 'Ready',
  readinessNeedsAttention: 'Needs Attention',
  readinessMissing: 'Missing',
  readinessFailed: 'Failed',
  nextConnectFirstProvider: 'connect-first-provider',
  nextAddMcp: 'add-mcp',
  nextCreateSpecialist: 'create-specialist',
  nextCreateStarterWorkflow: 'create-starter-workflow',
  nextReview: 'review',
  nextComplete: 'complete'
};

function defaultWizardState() {
  return {
    mode: null,  // 'guided' | 'manual' | 'import' | null
    stepIndex: 0,
    steps: ['preflight', 'discovery', 'providers', 'mcp', 'specialist', 'workflow', 'review'],
    stepState: {},
    dismissed: false,
    lastUpdatedUtc: null
  };
}

// WS1 — persistence helpers for wizard state, guarded against localStorage
// corruption, quota, or disabled storage.
const WIZARD_STATE_KEY = 'mco.wizard.state.v1';
function persistWizardState() {
  try {
    if (typeof localStorage === 'undefined') { return; }
    const payload = { ...state.wizard, lastUpdatedUtc: new Date().toISOString() };
    localStorage.setItem(WIZARD_STATE_KEY, JSON.stringify(payload));
  } catch (_) { /* quota or disabled — non-fatal */ }
}
function restoreWizardState() {
  try {
    if (typeof localStorage === 'undefined') { return; }
    const raw = localStorage.getItem(WIZARD_STATE_KEY);
    if (!raw) { return; }
    const parsed = JSON.parse(raw);
    if (parsed && typeof parsed === 'object' && Array.isArray(parsed.steps)) {
      state.wizard = { ...defaultWizardState(), ...parsed };
    }
  } catch (_) {
    // Corruption — reset to defaults.
    try { localStorage.removeItem(WIZARD_STATE_KEY); } catch (_) {}
  }
}
restoreWizardState();

// WS3 — fetch and cache environment hints.
async function loadEnvironmentHints() {
  try {
    const response = await fetch('/api/environment-hints');
    if (!response.ok) { return; }
    const json = await response.json();
    if (json && typeof json === 'object') {
      state.environmentHints = json;
    }
  } catch (_) { /* non-fatal */ }
}

// WS1 — mark setup complete and refresh.
function markSetupCompleteAndRefresh() {
  const skipped = [];
  fetch('/api/setup/complete', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ skippedSteps: skipped })
  })
    .then(() => {
      state.wizard.dismissed = true;
      persistWizardState();
      state.currentDestination = 'setup-readiness';
      return refreshDashboard({ preserveDynamicContent: false });
    })
    .catch(() => {});
}

// WS1 — fetch and cache readiness snapshot.
async function loadReadiness() {
  try {
    const response = await fetch('/api/readiness');
    if (!response.ok) { return; }
    state.readiness = await response.json();
  } catch (_) { /* non-fatal */ }
}

// WS4 — fetch and cache setup dependency catalog + detection.
async function loadSetupDependencies() {
  try {
    const response = await fetch('/api/setup/dependencies');
    if (!response.ok) { return; }
    const json = await response.json();
    const byId = {};
    for (const entry of (json?.dependencies || [])) {
      if (entry?.descriptor?.id) {
        byId[entry.descriptor.id] = entry;
      }
    }
    state.setupDependencies = byId;
  } catch (_) { /* non-fatal */ }
}

// Provider credential help URL by provider id.
function credentialHelpUrl(capability) {
  const id = capability?.providerId || '';
  switch (id) {
    case 'codex':
    case 'chatgpt':
      return 'Get your API key at https://platform.openai.com/api-keys';
    case 'claude-code':
      return 'Get your API key at https://console.anthropic.com/settings/keys';
    case 'xai-grok':
      return 'Get your API key at https://console.x.ai/team/api-keys';
    default:
      return 'Check your provider account dashboard for API credentials.';
  }
}

// WS3 — describe a credential field's detected/needed state from the hint cache.
function environmentHintStatus(field) {
  const hint = field?.environmentVariableHint || '';
  if (!hint) {
    return { hint: '', detected: false, badgeClass: 'hint-none', badgeText: READINESS_COPY.hintNone };
  }
  const detected = state.environmentHints[hint] === true;
  if (detected) {
    return {
      hint, detected: true,
      badgeClass: 'hint-detected',
      badgeText: `${READINESS_COPY.hintDetected} (${hint})`
    };
  }
  return {
    hint, detected: false,
    badgeClass: 'hint-needed',
    badgeText: `${READINESS_COPY.hintNeeded} (set ${hint} or paste below)`
  };
}

function currentConfig() {
  return {
    instanceName: state.config?.instanceName || 'Master Control Orchestration Server',
    bindAddress: state.config?.bindAddress || '0.0.0.0',
    browserPort: safeNumber(state.config?.browserPort, 7300),
    beaconPort: safeNumber(state.config?.beaconPort, 7301),
    beaconEnabled: state.config?.beaconEnabled ?? true,
    aiAutonomyEnabled: state.config?.aiAutonomyEnabled ?? false,
    advancedMode: state.config?.advancedMode ?? false,
    security: {
      enableTls: state.config?.security?.enableTls ?? false,
      enableAuthentication: state.config?.security?.enableAuthentication ?? false,
      allowTroubleshootingBypass: state.config?.security?.allowTroubleshootingBypass ?? false,
      allowOpenLanAccess: state.config?.security?.allowOpenLanAccess ?? true,
      securityProtocolsEnabled: state.config?.security?.securityProtocolsEnabled ?? true,
      trustedRemoteHosts: safeArray(state.config?.security?.trustedRemoteHosts)
    },
    resourceAllocation: {
      cpuPercent: safeNumber(state.config?.resourceAllocation?.cpuPercent, 50),
      memoryPercent: safeNumber(state.config?.resourceAllocation?.memoryPercent, 50),
      bandwidthPercent: safeNumber(state.config?.resourceAllocation?.bandwidthPercent, 50),
      storagePercent: safeNumber(state.config?.resourceAllocation?.storagePercent, 50)
    },
    activeProfile: {
      environmentName: state.config?.activeProfile?.environmentName || 'Unspecified',
      preferredBindAddress: state.config?.activeProfile?.preferredBindAddress || '0.0.0.0',
      macAddress: state.config?.activeProfile?.macAddress || 'n/a'
    }
  };
}

function dashboardSnapshot() {
  return {
    telemetry: state.dashboard?.telemetry || {},
    endpoints: safeArray(state.dashboard?.endpoints),
    providers: safeArray(state.dashboard?.providers),
    providerCapabilities: safeArray(state.dashboard?.providerCapabilities),
    providerCredentialStatuses: safeArray(state.dashboard?.providerCredentialStatuses),
    subAgentGroups: safeArray(state.dashboard?.subAgentGroups),
    providerAssignmentTargets: safeArray(state.dashboard?.providerAssignmentTargets),
    providerAssignments: safeArray(state.dashboard?.providerAssignments),
    providerExecutionRegistrations: safeArray(state.dashboard?.providerExecutionRegistrations),
    providerExecutionHistory: safeArray(state.dashboard?.providerExecutionHistory),
    installHistory: safeArray(state.dashboard?.installHistory),
    exports: safeArray(state.dashboard?.exports)
  };
}

function governanceSnapshot() {
  return state.dashboard?.governance || {};
}

function setHealthBadge(label, tone = 'info') {
  healthBadge.textContent = label;
  healthBadge.dataset.tone = tone;
}

function setSurfaceNotice(message, tone = 'info') {
  state.surfaceNotice = makeStatus(message, tone);
}

function syncStateSelections() {
  if (!state.surface.navigationPointers.some((pointer) => pointer.destinationId === state.currentDestination)) {
    state.currentDestination = state.surface.navigationPointers[0]?.destinationId || 'overview';
  }
  if (!state.exports.some((artifact) => artifact.id === state.selectedExportId)) {
    state.selectedExportId = state.exports[0]?.id || '';
  }
}

async function refreshGuidedForsettiModuleCatalog() {
  if (state.guidedWorkflow.id !== 'manage-forsetti-modules') {
    return;
  }

  state.guidedWorkflow.status = makeStatus('Loading the Forsetti module catalog from the local admin API.', 'info');
  renderGuidedWorkflowOverlay();

  try {
    const payload = await loadJson('/api/forsetti/modules');
    const modules = safeArray(payload.modules);
    const selectedModuleId = state.guidedWorkflow.moduleId;
    state.guidedWorkflow.moduleCatalog = modules;
    state.guidedWorkflow.moduleId = modules.some((moduleRecord) => moduleRecord.moduleId === selectedModuleId)
      ? selectedModuleId
      : (modules[0]?.moduleId || '');
    state.guidedWorkflow.moduleAction = recommendedForsettiAction(
      modules.find((moduleRecord) => moduleRecord.moduleId === state.guidedWorkflow.moduleId) || modules[0]
    );
    state.guidedWorkflow.status = makeStatus(payload.message || 'Forsetti module catalog loaded.', 'success');
  } catch (error) {
    state.guidedWorkflow.moduleCatalog = [];
    state.guidedWorkflow.moduleId = '';
    state.guidedWorkflow.status = makeStatus(error.message || 'Unable to load the Forsetti module catalog.', 'error');
  }

  renderGuidedWorkflowOverlay();
}

async function openGuidedWorkflow(workflowId) {
  const definition = guidedWorkflowDefinition(workflowId);
  if (!definition) {
    setSurfaceNotice(`Unknown guided workflow request: ${workflowId}`, 'warning');
    renderSurfaceSummary();
    return;
  }

  state.overlayRouteId = '';
  state.guidedWorkflow = defaultGuidedWorkflowState();
  state.guidedWorkflow.id = workflowId;
  // Quick-connect workflows pre-set the capability from definition presets.
  // Generic connect-model workflow defaults to the first available capability.
  const presets = definition.presets;
  if (presets?.capabilityId) {
    state.guidedWorkflow.providerCapabilityId = presets.capabilityId;
    // Route quick-connect workflows through the generic connect-model form
    state.guidedWorkflow.id = 'connect-model';
  } else {
    state.guidedWorkflow.providerCapabilityId = dashboardSnapshot().providerCapabilities[0]?.providerId || '';
  }
  state.guidedWorkflow.importMode = 'package';
  state.guidedWorkflow.runtimeMaintenanceKind = 'mcp';
  state.guidedWorkflow.status = makeStatus(definition.description, 'info');
  state.overlayWorkspaceDestination = definition.destinationId || '';

  if (workflowId === 'guided-runtime-maintenance') {
    primeRuntimeMaintenanceDraft('mcp');
  }

  renderGuidedWorkflowOverlay();

  if (workflowId === 'manage-forsetti-modules') {
    await refreshGuidedForsettiModuleCatalog();
  }
}

function labelForDestination(destinationId) {
  return state.surface.navigationPointers.find((pointer) => pointer.destinationId === destinationId)?.label
    || ({
      telemetry: 'Telemetry',
      runtime: 'Runtime',
      clu: 'Command Logic Unit',
      providers: 'Providers',
      imports: 'Imports',
      exports: 'Exports',
      security: 'Security',
      settings: 'Settings',
      setup: 'Setup',
      'setup-readiness': 'Setup Readiness'
    }[destinationId] || 'Overview');
}

function destinationForViewId(viewId) {
  return ({
    OverviewSectionView: 'overview',
    TelemetrySectionView: 'telemetry',
    RuntimeSectionView: 'runtime',
    CommandLogicUnitSectionView: 'clu',
    ProvidersSectionView: 'providers',
    ImportsSectionView: 'imports',
    ExportsSectionView: 'exports',
    SecuritySectionView: 'security',
    SettingsSectionView: 'settings'
  }[viewId] || 'overview');
}

function metadataForDestination(destinationId) {
  const title = labelForDestination(destinationId);
  if (destinationId === 'telemetry') {
    return {
      eyebrow: 'TELEMETRY',
      title,
      description: 'Use the dense monitoring deck to keep live host pressure, governed resource budgets, runtime activity, and environment discovery visible at a glance.'
    };
  }
  if (destinationId === 'runtime') {
    return {
      eyebrow: 'RUNTIME',
      title,
      description: 'Inspect MCP servers, sub-agents, platform gateway lanes, Apple remote hosts, and installation provenance from the shared runtime map.'
    };
  }
  if (destinationId === 'clu') {
    return {
      eyebrow: 'CLU',
      title,
      description: 'Review the Command Logic Unit governance profile, Apple production operations, and operator-visible enforcement doctrine.'
    };
  }
  if (destinationId === 'providers') {
    return {
      eyebrow: 'PROVIDERS',
      title,
      description: 'Manage provider adapters, AI autonomy posture, and the agent routing envelope from one Forsetti-managed control surface.'
    };
  }
  if (destinationId === 'imports') {
    return {
      eyebrow: 'IMPORTS',
      title,
      description: 'Run package, repository, and zip onboarding flows through the same browser control plane used by the desktop shell.'
    };
  }
  if (destinationId === 'exports') {
    return {
      eyebrow: 'EXPORTS',
      title,
      description: 'Preview and materialize agent export artifacts generated by the service for browser, desktop, and coding-agent handoff.'
    };
  }
  if (destinationId === 'security') {
    return {
      eyebrow: 'SECURITY',
      title,
      description: 'Control security protocols, trusted hosts, and LAN exposure with explicit confirmation for troubleshooting bypasses.'
    };
  }
  if (destinationId === 'settings') {
    return {
      eyebrow: 'SETTINGS',
      title,
      description: 'Adjust bind addresses, ports, beaconing, and resource allocation while keeping the service configuration authoritative.'
    };
  }
  return {
    eyebrow: 'OVERVIEW',
    title,
    description: 'Forsetti modules now drive navigation, toolbar actions, overlays, and section content instead of a hardcoded browser dashboard.'
  };
}

function resolvePrimaryViewForDestination(destinationId) {
  // WS1/WS6 — client-side routes that don't live in the Forsetti surface map.
  if (destinationId === 'setup') { return 'SetupWizardView'; }
  if (destinationId === 'setup-readiness') { return 'SetupReadinessView'; }
  const injections = state.surface.viewInjectionsBySlot[destinationId] || [];
  return injections[0]?.viewId || '';
}

function currentExport() {
  return state.exports.find((artifact) => artifact.id === state.selectedExportId) || null;
}

function providerKindOptions(selectedKind) {
  return [
    ['codex', 'Codex'],
    ['claude_code', 'Claude Code'],
    ['xai', 'xAI / Grok'],
    ['generic', 'Generic']
  ].map(([value, label]) => `
    <option value="${escapeHtml(value)}"${selectedAttr(value === selectedKind)}>${escapeHtml(label)}</option>
  `).join('');
}

function resolveProviderCapability(capabilities, providerId, kind) {
  // Resolve by providerId first (exact match, then prefix match for auto-connect ids),
  // fall back to kind-based match for backward compatibility.
  if (providerId) {
    const exact = capabilities.find((c) => c.providerId === providerId);
    if (exact) return exact;
    const prefix = capabilities.find((c) =>
      c.providerId && providerId.length > c.providerId.length
      && providerId[c.providerId.length] === '-'
      && providerId.startsWith(c.providerId));
    if (prefix) return prefix;
  }
  return capabilities.find((c) => c.kind === kind) || null;
}

function selectedProviderCapability() {
  const snapshot = dashboardSnapshot();
  const draft = state.providerDraft;
  const provider = snapshot.providers.find((candidate) => candidate.id === draft.id);
  const kind = provider?.kind || draft.kind;
  const providerId = provider?.id || draft.id;
  return resolveProviderCapability(snapshot.providerCapabilities, providerId, kind);
}

function selectedProviderCredentialStatus() {
  const draft = state.providerDraft;
  if (!draft.id) {
    return null;
  }
  return dashboardSnapshot().providerCredentialStatuses.find((status) => status.providerId === draft.id) || null;
}

function providerDisplayNameById(providerId) {
  return dashboardSnapshot().providers.find((provider) => provider.id === providerId)?.displayName || providerId;
}

function assignmentTargetLabel(targetId) {
  return dashboardSnapshot().providerAssignmentTargets.find((target) => target.targetId === targetId)?.displayName || targetId;
}

function packageKindOptions(selectedKind) {
  return [
    ['msi', 'MSI'],
    ['exe', 'EXE'],
    ['powershell', 'PowerShell']
  ].map(([value, label]) => `
    <option value="${escapeHtml(value)}"${selectedAttr(value === selectedKind)}>${escapeHtml(label)}</option>
  `).join('');
}

function renderSurfaceNavigation() {
  // WS6/WS7 — synthesize a nav set that:
  //   - prepends Setup Readiness so users can always reopen it
  //   - hides Exports unless advancedMode is on (WS7 demotion)
  const base = state.surface.navigationPointers.slice();
  const readinessPointer = { destinationId: 'setup-readiness', label: 'Setup Readiness' };
  const synthesized = [readinessPointer, ...base];
  const advanced = isAdvancedMode();
  const filtered = synthesized.filter((p) => {
    if (p.destinationId === 'exports' && !advanced) { return false; }
    return true;
  });
  surfaceNavigation.innerHTML = filtered.map((pointer) => `
    <button
      type="button"
      class="surface-nav-button ${pointer.destinationId === state.currentDestination ? 'is-active' : ''}"
      data-destination="${escapeHtml(pointer.destinationId)}">
      <span class="surface-nav-label">${escapeHtml(pointer.label || labelForDestination(pointer.destinationId))}</span>
      <span class="surface-nav-meta">${escapeHtml(pointer.destinationId)}</span>
    </button>
  `).join('');
}

function renderSurfaceToolbar() {
  surfaceToolbar.innerHTML = state.surface.toolbarItems.map((item) => `
    <button
      type="button"
      class="surface-toolbar-button"
      data-toolbar-id="${escapeHtml(item.id)}">
      <span class="toolbar-icon">${escapeHtml(iconToken(item.systemImageName))}</span>
      <span class="toolbar-copy">
        <strong>${escapeHtml(item.title)}</strong>
        <small>${escapeHtml(item.actionKind === 'openOverlay' ? 'overlay route' : item.actionKind)}</small>
      </span>
    </button>
  `).join('');
}

function isAdvancedMode() {
  return currentConfig().advancedMode;
}

function advancedOnly(html) {
  return isAdvancedMode() ? html : '';
}

function activeEndpoints(snapshot) {
  return snapshot.endpoints.filter((e) => !e.isTemplate);
}

function templateEndpoints(snapshot) {
  return snapshot.endpoints.filter((e) => e.isTemplate);
}

function activeProviders(snapshot) {
  return snapshot.providers.filter((p) => !p.isTemplate);
}

function templateProviders(snapshot) {
  return snapshot.providers.filter((p) => p.isTemplate);
}

function renderSurfaceSummary() {
  const snapshot = dashboardSnapshot();
  const config = currentConfig();
  const active = activeEndpoints(snapshot);
  const templates = templateEndpoints(snapshot);
  const onlineEndpoints = active.filter((endpoint) => String(endpoint.status || '').toLowerCase() === 'online').length;
  const enabledProviders = activeProviders(snapshot).filter((provider) => provider.enabled).length;
  const templateProviderCount = templateProviders(snapshot).length;
  const currentOverlay = state.guidedWorkflow.id
    ? guidedWorkflowDefinition(state.guidedWorkflow.id)?.title || 'Guided setup open'
    : state.overlayRouteId
      ? state.surface.overlayRoutes.find((route) => route.id === state.overlayRouteId)?.label || 'Overlay open'
      : 'No overlay open';

  surfaceSummary.innerHTML = `
    <div class="summary-grid">
      ${metricCard('Destination', labelForDestination(state.currentDestination), 'active view')}
      ${metricCard('Endpoints', formatCount(active.length), `${onlineEndpoints} online` + (templates.length ? ` · ${templates.length} templates` : ''))}
      ${metricCard('Providers', formatCount(activeProviders(snapshot).length), `${enabledProviders} enabled` + (templateProviderCount ? ` · ${templateProviderCount} templates` : ''))}
      ${metricCard('Exports', formatCount(state.exports.length), `${snapshot.installHistory.length} installs tracked`)}
    </div>
    <article class="panel-block summary-notice" data-tone="${escapeHtml(state.surfaceNotice.tone)}">
      <p class="eyebrow">Surface Sync</p>
      <p class="summary-copy">${escapeHtml(state.surfaceNotice.message)}</p>
      <p class="summary-copy">Last refresh: ${escapeHtml(state.lastRefreshLabel)} | Browser port: ${escapeHtml(config.browserPort)} | Overlay: ${escapeHtml(currentOverlay)}</p>
    </article>
  `;
}

function renderViewChrome() {
  const metadata = metadataForDestination(state.currentDestination);
  currentViewEyebrow.textContent = metadata.eyebrow;
  currentViewTitle.textContent = metadata.title;
  currentViewDescription.textContent = metadata.description;
}

function renderOverviewView() {
  const config = currentConfig();
  const snapshot = dashboardSnapshot();
  const telemetry = snapshot.telemetry;
  const active = activeEndpoints(snapshot);
  const templates = templateEndpoints(snapshot);
  const onlineEndpoints = active.filter((endpoint) => String(endpoint.status || '').toLowerCase() === 'online').length;
  const activeProviderList = activeProviders(snapshot);
  const enabledProviders = activeProviderList.filter((provider) => provider.enabled).length;
  const trustedHosts = safeArray(config.security.trustedRemoteHosts);

  const missionBrief = [
    `Instance: ${config.instanceName}`,
    `Bind address: ${config.bindAddress}:${config.browserPort}`,
    `AI autonomy: ${boolLabel(config.aiAutonomyEnabled)}`,
    `LAN browser access: ${boolLabel(config.security.allowOpenLanAccess)}`
  ].join('\n');

  const environmentProfile = [
    `Environment: ${config.activeProfile.environmentName}`,
    `Preferred IP: ${config.activeProfile.preferredBindAddress}`,
    `Primary MAC: ${config.activeProfile.macAddress}`,
    `Telemetry host: ${telemetry.hostName || 'n/a'}`
  ].join('\n');

  const securityPosture = [
    `Security protocols: ${boolLabel(config.security.securityProtocolsEnabled)}`,
    `TLS: ${boolLabel(config.security.enableTls)}`,
    `Authentication: ${boolLabel(config.security.enableAuthentication)}`,
    `Trusted hosts: ${trustedHosts.length ? trustedHosts.join(', ') : 'none'}`
  ].join('\n');

  return `
    <section class="section-shell">
      <div class="card-grid">
        ${metricCard('Active Endpoints', formatCount(active.length), `${onlineEndpoints} online` + (templates.length ? ` · ${templates.length} templates` : ''))}
        ${metricCard('Providers', formatCount(activeProviderList.length), `${enabledProviders} enabled` + (templateProviders(snapshot).length ? ` · ${templateProviders(snapshot).length} templates` : ''))}
        ${metricCard('Exports', formatCount(state.exports.length), `${snapshot.installHistory.length} recorded installs`)}
        ${metricCard('Host CPU', formatPercent(telemetry.cpuPercent || 0), telemetry.hostName || 'awaiting telemetry')}
      </div>

      <article class="panel-block">
        <p class="eyebrow">Get Started</p>
        <h3>Connect a Provider</h3>
        <p class="narrative-copy">Choose a provider to connect. Enter your API key and the runtime handles capability resolution, model discovery, credential encryption, and assignment.</p>
        ${renderGuidedWorkflowLaunchers([
          'connect-chatgpt',
          'connect-codex',
          'connect-claude-code',
          'connect-xai'
        ])}
      </article>

      <div class="section-actions">
        <button type="button" data-destination="runtime">Open Runtime Lanes</button>
        <button type="button" data-open-overlay="imports-overlay">Launch Import Overlay</button>
        <button type="button" data-open-overlay="exports-overlay">Preview Exports</button>
        <button type="button" data-destination="security">Inspect Security</button>
      </div>

      <article class="panel-block">
        <p class="eyebrow">More Setup Options</p>
        <h3>Advanced Workflows</h3>
        <p class="narrative-copy">Use walkthroughs for raw admin tasks: connect a generic model, publish MCP lanes, create sub-agents, or manage Forsetti modules.</p>
        ${renderGuidedWorkflowLaunchers([
          'connect-model',
          'new-mcp',
          'new-subagent',
          'new-subagent-group',
          'manage-forsetti-modules',
          'guided-import'
        ])}
      </article>

      <div class="split-grid">
        ${narrativePanel('Mission Brief', 'Control Deck', missionBrief)}
        ${narrativePanel('Environment Profile', 'Discovery Snapshot', environmentProfile)}
        ${narrativePanel('Security Posture', 'Trust Envelope', securityPosture)}
      </div>
    </section>
  `;
}

// ---------------------------------------------------------------------------
// WS1 — First-Run Setup Wizard (browser)
// ---------------------------------------------------------------------------

function renderFirstRunView() {
  // Entry screen: three equal cards (Guided / Manual / Import Existing).
  // Once a mode is chosen, subsequent renders dispatch to the step renderer.
  if (!state.wizard.mode) {
    return renderFirstRunEntryChoices();
  }
  if (state.wizard.mode === 'guided') {
    return renderGuidedWizardStep();
  }
  if (state.wizard.mode === 'import') {
    return renderImportConfigStep();
  }
  // Manual mode dismisses the wizard and lets the user use the operator surface.
  // The banner in renderManualSetupBanner() keeps them oriented.
  return renderUnavailableView('Manual Setup Active',
    'Use the main navigation to configure providers, MCP servers, and workflows. Open Setup Readiness when ready.');
}

function renderFirstRunEntryChoices() {
  return `
    <section class="section-shell">
      <article class="panel-block">
        <p class="eyebrow">Welcome</p>
        <h3>Start Here</h3>
        <p class="narrative-copy">Choose how you want to set up Master Control. All three paths lead to the same outcome — a configured, ready orchestration instance.</p>
        <div class="first-run-entry-grid">
          <button type="button" class="first-run-entry-card" data-action="first-run-choose-guided">
            <p class="eyebrow">GUIDED</p>
            <h4>Guided Setup</h4>
            <p class="narrative-copy">Step-by-step assistant. Preflight, discovery, connect providers, add MCP, create a specialist, pick a starter workflow, review.</p>
          </button>
          <button type="button" class="first-run-entry-card" data-action="first-run-choose-manual">
            <p class="eyebrow">MANUAL</p>
            <h4>Manual Setup</h4>
            <p class="narrative-copy">Go straight to the full operator surface and configure each section yourself. Open Setup Readiness when done to mark complete.</p>
          </button>
          <button type="button" class="first-run-entry-card" data-action="first-run-choose-import">
            <p class="eyebrow">IMPORT</p>
            <h4>Import Existing Configuration</h4>
            <p class="narrative-copy">Restore from an existing package, repo, or zip. We validate it, surface any gaps, and route you to fix them.</p>
          </button>
        </div>
      </article>
    </section>
  `;
}

// WS1 — guided wizard step host with a progress rail.
function renderGuidedWizardStep() {
  const wiz = state.wizard;
  const stepId = wiz.steps[wiz.stepIndex] || 'preflight';
  const railHtml = wiz.steps.map((id, idx) => {
    const cls = idx === wiz.stepIndex ? 'active' : (idx < wiz.stepIndex ? 'done' : '');
    return `<span class="wizard-step-rail-item ${cls}">${idx + 1}. ${escapeHtml(wizardStepLabel(id))}</span>`;
  }).join('');
  const body = renderWizardStepBody(stepId);
  const backDisabled = wiz.stepIndex === 0 ? 'disabled' : '';
  const isLast = wiz.stepIndex === wiz.steps.length - 1;
  const nextLabel = isLast ? 'Complete setup' : 'Next';
  return `
    <section class="section-shell">
      <article class="panel-block">
        <p class="eyebrow">Guided Setup — Step ${wiz.stepIndex + 1} of ${wiz.steps.length}</p>
        <h3>${escapeHtml(wizardStepLabel(stepId))}</h3>
        <div class="wizard-step-rail">${railHtml}</div>
        ${body}
        <div class="button-row" style="margin-top:1rem;">
          <button type="button" class="route-button" data-action="wizard-back" ${backDisabled}>Back</button>
          <button type="button" class="route-button" data-action="wizard-skip">Skip</button>
          <button type="button" data-action="wizard-next">${escapeHtml(nextLabel)}</button>
          <button type="button" class="route-button" data-action="wizard-exit-manual">Switch to Manual</button>
        </div>
      </article>
    </section>
  `;
}

function wizardStepLabel(stepId) {
  switch (stepId) {
    case 'preflight': return 'Preflight';
    case 'discovery': return 'Discovery';
    case 'providers': return 'Connect Providers';
    case 'mcp': return 'Add MCP Server';
    case 'specialist': return 'Create a Specialist';
    case 'workflow': return 'Starter Workflow';
    case 'review': return 'Readiness Review';
    default: return stepId;
  }
}

function renderWizardStepBody(stepId) {
  const readiness = state.readiness || {};
  switch (stepId) {
    case 'preflight':
      return `
        <p class="narrative-copy">Checking runtime basics. If you can see this view, the admin API is reachable.</p>
        <ul>
          <li>Admin API: <strong>Reachable</strong></li>
          <li>Configuration loaded: <strong>${state.config ? 'Yes' : 'No'}</strong></li>
          <li>Dashboard snapshot loaded: <strong>${state.dashboard ? 'Yes' : 'No'}</strong></li>
        </ul>
      `;
    case 'discovery': {
      const detected = Object.entries(state.environmentHints || {}).filter(([, v]) => v === true);
      const missing = Object.entries(state.environmentHints || {}).filter(([, v]) => v !== true);
      return `
        <p class="narrative-copy">We checked the environment for credentials so we can ask for less later.</p>
        <h4>Detected</h4>
        <ul>${detected.length
          ? detected.map(([k]) => `<li><span class="hint-badge hint-detected">${READINESS_COPY.hintDetected}</span> ${escapeHtml(k)}</li>`).join('')
          : '<li>No credentials detected in the environment yet.</li>'}
        </ul>
        <h4>Not detected</h4>
        <ul>${missing.length
          ? missing.map(([k]) => `<li><span class="hint-badge hint-needed">${READINESS_COPY.hintNeeded}</span> ${escapeHtml(k)}</li>`).join('')
          : '<li>All known credential slots have detected values.</li>'}
        </ul>
      `;
    }
    case 'providers':
      return `
        <p class="narrative-copy">Connect at least one provider. Each card opens the guided connect flow for that provider.</p>
        <div class="first-run-entry-grid">
          <button type="button" class="first-run-entry-card" data-action="open-guided-workflow" data-workflow-id="connect-chatgpt"><h4>Connect ChatGPT</h4><p class="narrative-copy">OpenAI API key.</p></button>
          <button type="button" class="first-run-entry-card" data-action="open-guided-workflow" data-workflow-id="connect-codex"><h4>Connect Codex</h4><p class="narrative-copy">OpenAI API key.</p></button>
          <button type="button" class="first-run-entry-card" data-action="open-guided-workflow" data-workflow-id="connect-claude-code"><h4>Connect Claude Code</h4><p class="narrative-copy">Anthropic API key + Claude Code CLI.</p></button>
          <button type="button" class="first-run-entry-card" data-action="open-guided-workflow" data-workflow-id="connect-xai"><h4>Connect xAI / Grok</h4><p class="narrative-copy">xAI API key.</p></button>
        </div>
        ${renderClaudeCodeDependencyCard()}
        <p class="narrative-copy">Providers currently ready: <strong>${readiness.providersReadyCount || 0}</strong></p>
      `;
    case 'mcp':
      return `
        <p class="narrative-copy">Add at least one MCP server so providers can share a tool lane. You can skip this step and add one later.</p>
        <button type="button" data-action="open-guided-workflow" data-workflow-id="new-mcp">Open MCP Server guided form</button>
        <p class="narrative-copy">MCP servers currently ready: <strong>${readiness.mcpReadyCount || 0}</strong></p>
      `;
    case 'specialist':
      return `
        <p class="narrative-copy">Create a specialist (sub-agent) so providers have something to execute. You can skip this step and return later.</p>
        <button type="button" data-action="open-guided-workflow" data-workflow-id="new-subagent">Open Sub-Agent guided form</button>
        <p class="narrative-copy">Specialists currently ready: <strong>${readiness.specialistsReadyCount || 0}</strong></p>
      `;
    case 'workflow':
      return renderStarterWorkflowStep();
    case 'review':
      return renderReadinessReviewStep();
    default:
      return '<p class="narrative-copy">Unknown step.</p>';
  }
}

// WS6 — starter workflow template picker.
let starterTemplatesCache = null;
async function loadStarterWorkflowTemplates() {
  try {
    const response = await fetch('/api/setup/workflow-templates');
    if (!response.ok) { return; }
    const json = await response.json();
    starterTemplatesCache = Array.isArray(json?.templates) ? json.templates : [];
    // Re-render so the list populates.
    if (state.currentDestination === 'setup' && state.wizard.mode === 'guided'
        && state.wizard.steps[state.wizard.stepIndex] === 'workflow') {
      renderCurrentContent();
    }
  } catch (_) { /* non-fatal */ }
}
function renderStarterWorkflowStep() {
  if (starterTemplatesCache === null) {
    loadStarterWorkflowTemplates();
    return '<p class="narrative-copy">Loading starter workflow templates...</p>';
  }
  const selected = state.wizard.stepState.workflow?.selectedTemplateId || '';
  const templates = starterTemplatesCache || [];
  const cards = templates.map((t) => `
    <label class="first-run-entry-card" data-action="select-starter-template" data-template-id="${escapeHtml(t.id)}">
      <input type="radio" name="starter-template" value="${escapeHtml(t.id)}" ${selected === t.id ? 'checked' : ''}>
      <h4>${escapeHtml(t.displayName)}</h4>
      <p class="narrative-copy">${escapeHtml(t.description)}</p>
      <p class="form-help">Requires ${t.requiresProviders} provider(s), ${t.requiresMcp} MCP, ${t.requiresSpecialists} specialist(s).</p>
    </label>
  `).join('');
  const lastResult = state.wizard.stepState.workflow?.lastResult;
  const resultHtml = lastResult
    ? `<p class="narrative-copy" style="color:${lastResult.succeeded ? 'rgb(60,230,120)' : 'rgb(255,140,150)'}">${escapeHtml(lastResult.message || '')}</p>`
    : '';
  return `
    <p class="narrative-copy">Pick a starter workflow to seed your first usable configuration. You can also skip and rely on anything you've already wired up manually.</p>
    <div class="first-run-entry-grid">${cards}</div>
    ${resultHtml}
    <div class="button-row" style="margin-top:0.75rem;">
      <button type="button" data-action="instantiate-starter-template" ${selected ? '' : 'disabled'}>Create this workflow</button>
    </div>
  `;
}

function renderReadinessReviewStep() {
  const readiness = state.readiness || {};
  const recommended = readiness.recommendedNextStep || '';
  return `
    <p class="narrative-copy">This is the same data the Setup Readiness dashboard uses. Complete setup when all required categories are ready.</p>
    ${renderReadinessGridInline(readiness)}
    ${recommended === 'complete' || recommended === 'review'
      ? '<p class="narrative-copy">You can mark setup complete now.</p>'
      : `<p class="narrative-copy">Recommended next step: <strong>${escapeHtml(recommended)}</strong></p>`}
    <div class="button-row" style="margin-top:0.75rem;">
      <button type="button" data-action="mark-setup-complete">Mark setup complete</button>
    </div>
  `;
}

function renderReadinessGridInline(readiness) {
  const tile = (label, ready, missing, fixAction) => {
    let state = 'needs-attention';
    if (ready > 0 && missing === 0) { state = 'ready'; }
    else if (ready === 0) { state = 'missing'; }
    const stateLabel = state === 'ready' ? READINESS_COPY.readinessReady
      : state === 'missing' ? READINESS_COPY.readinessMissing
      : READINESS_COPY.readinessNeedsAttention;
    return `
      <div class="readiness-tile" data-state="${state}">
        <p class="eyebrow">${escapeHtml(label)}</p>
        <h4>${ready} / ${ready + missing}</h4>
        <p class="narrative-copy">${escapeHtml(stateLabel)}</p>
        ${fixAction ? `<button type="button" class="route-button" data-action="${fixAction}">Fix now</button>` : ''}
      </div>
    `;
  };
  return `
    <div class="readiness-grid">
      ${tile('Providers', readiness.providersReadyCount || 0, readiness.providersMissingCount || 0, 'readiness-fix-providers')}
      ${tile('MCP Servers', readiness.mcpReadyCount || 0, readiness.mcpMissingCount || 0, 'readiness-fix-mcp')}
      ${tile('Specialists', readiness.specialistsReadyCount || 0, readiness.specialistsMissingCount || 0, 'readiness-fix-specialist')}
      ${tile('Workflows', readiness.workflowsReadyCount || 0, readiness.workflowsMissingCount || 0, 'readiness-fix-workflow')}
    </div>
  `;
}

// WS1 — Import Existing Configuration step (reuses existing import primitives).
function renderImportConfigStep() {
  return `
    <section class="section-shell">
      <article class="panel-block">
        <p class="eyebrow">Import Existing Configuration</p>
        <h3>Restore from a package, repo, or zip</h3>
        <p class="narrative-copy">Use any of the existing import flows to restore an existing configuration. We'll validate the imported state and route any gaps to Setup Readiness.</p>
        <div class="button-row">
          <button type="button" data-action="open-guided-workflow" data-workflow-id="guided-import">Open guided import</button>
          <button type="button" class="route-button" data-action="go-setup-readiness">Skip to Setup Readiness</button>
          <button type="button" class="route-button" data-action="wizard-exit-manual">Switch to Manual</button>
        </div>
      </article>
    </section>
  `;
}

// WS6 — Readiness dashboard (main view).
function renderReadinessView() {
  const readiness = state.readiness || {};
  const dependencyCard = renderClaudeCodeDependencyCard();
  const completeLabel = readiness.firstRunCompleted ? 'Setup complete' : 'Mark setup complete';
  const completeDisabled = readiness.firstRunCompleted ? 'disabled' : '';
  return `
    <section class="section-shell">
      <article class="panel-block">
        <p class="eyebrow">Setup Readiness</p>
        <h3>${escapeHtml(readiness.firstRunCompleted ? 'Setup complete' : 'Review and complete setup')}</h3>
        <p class="narrative-copy">Updated: ${escapeHtml(readiness.updatedAtUtc || 'never')}</p>
        ${renderReadinessGridInline(readiness)}
      </article>
      ${dependencyCard}
      <article class="panel-block">
        <p class="eyebrow">Complete</p>
        <div class="button-row">
          <button type="button" data-action="mark-setup-complete" ${completeDisabled}>${escapeHtml(completeLabel)}</button>
          <button type="button" class="route-button" data-action="setup-reset">Reset setup (testing)</button>
        </div>
      </article>
    </section>
  `;
}

// WS4 — Claude Code CLI dependency card (three-branch preflight).
function renderClaudeCodeDependencyCard() {
  const entry = state.setupDependencies?.['claude-code-cli'];
  if (!entry) {
    // Trigger a fetch if we haven't loaded dependencies yet.
    if (Object.keys(state.setupDependencies || {}).length === 0) {
      loadSetupDependencies().then(() => renderCurrentContent());
    }
    return '<article class="dependency-card" data-preflight=""><p class="narrative-copy">Checking Claude Code CLI...</p></article>';
  }
  const d = entry.detection || {};
  const descriptor = entry.descriptor || {};
  const preflight = d.preflight || 'unknown';
  const installState = state.setupDependencies['claude-code-cli']?.installState;
  const isInstalling = installState === 'installing';
  let body = '';
  if (preflight === 'ready') {
    body = `
      <h4>${escapeHtml(descriptor.displayName)} — <span style="color:rgb(60,230,120);">${escapeHtml(READINESS_COPY.readinessReady)}</span></h4>
      <p class="narrative-copy">${escapeHtml(d.detectedVersion || '')}</p>
    `;
  } else if (preflight === 'installable') {
    body = `
      <h4>${escapeHtml(descriptor.displayName)} — <span style="color:rgb(255,200,60);">${escapeHtml(READINESS_COPY.readinessMissing)}</span></h4>
      <p class="narrative-copy">${escapeHtml(d.detail || '')}</p>
      <div class="button-row">
        <button type="button" data-action="install-dependency" data-dependency-id="claude-code-cli" ${isInstalling ? 'disabled' : ''}>
          ${isInstalling ? 'Installing...' : 'Install Claude Code CLI'}
        </button>
      </div>
    `;
  } else if (preflight === 'prerequisite-missing') {
    body = `
      <h4>${escapeHtml(descriptor.displayName)} — <span style="color:rgb(255,140,150);">${escapeHtml(READINESS_COPY.readinessFailed)}</span></h4>
      <p class="narrative-copy">${escapeHtml(d.detail || '')}</p>
      <p class="narrative-copy">Install Node.js from <a href="https://nodejs.org" target="_blank" rel="noopener">nodejs.org</a>, then reload this view.</p>
    `;
  } else {
    body = `<h4>${escapeHtml(descriptor.displayName)}</h4><p class="narrative-copy">${escapeHtml(d.detail || 'Checking...')}</p>`;
  }
  const installResult = entry.lastInstallResult;
  const installResultHtml = installResult ? `
    <details style="margin-top:0.5rem;">
      <summary>${escapeHtml(installResult.summary || '')}</summary>
      ${installResult.stderrTail ? `<pre>${escapeHtml(installResult.stderrTail)}</pre>` : ''}
    </details>
  ` : '';
  return `
    <article class="dependency-card" data-preflight="${escapeHtml(preflight)}">
      ${body}
      ${installResultHtml}
    </article>
  `;
}

function renderTelemetryView() {
  const config = currentConfig();
  const snapshot = dashboardSnapshot();
  const governance = governanceSnapshot();
  const telemetry = snapshot.telemetry;
  const resourceAllocation = config.resourceAllocation;
  const appleHosts = safeArray(governance.appleRemoteHosts);
  const appleOperations = safeArray(governance.appleOperations);
  const gateways = safeArray(governance.platformGateways);
  const governanceServers = safeArray(governance.governanceServers);
  const findings = safeArray(governance.findings);
  const recentExecutions = safeArray(governance.recentExecutions);
  const providerExecutionHistory = safeArray(snapshot.providerExecutionHistory);
  const activeAppleOperationCount = appleOperations.filter(isActiveAppleOperation).length;
  const attentionAppleOperationCount = appleOperations.filter(isAttentionAppleOperation).length;
  const totalTraffic = safeNumber(telemetry.bytesSentPerSecond, 0) + safeNumber(telemetry.bytesReceivedPerSecond, 0);
  const telemetryHeadline = telemetry.hostName
    ? `Telemetry Grid · ${telemetry.hostName}`
    : 'Telemetry Grid';
  const telemetrySummary = [
    `CPU ${formatPercent(telemetry.cpuPercent || 0)}`,
    `RAM ${formatPercent(telemetry.memoryPercent || 0)}`,
    `Disk ${formatPercent(telemetry.diskPercent || 0)}`,
    `Traffic ${formatCount(totalTraffic)}/s`
  ].join('  |  ');
  const hostIdentityRows = [
    { label: 'Host', value: telemetry.hostName || 'pending', detail: telemetry.operatingSystem || 'Windows' },
    { label: 'Primary IP', value: telemetry.primaryIpAddress || 'pending', detail: `Browser :${config.browserPort}` },
    { label: 'Primary MAC', value: telemetry.primaryMacAddress || 'pending', detail: `Beacon ${boolLabel(config.beaconEnabled)}` },
    { label: 'Captured', value: telemetry.capturedAtUtc || state.lastRefreshLabel, detail: 'Latest telemetry sample' }
  ];
  const controlPlaneRows = [
    { label: 'Environment', value: config.activeProfile.environmentName || 'default', detail: `Bind ${config.bindAddress}:${config.browserPort}` },
    { label: 'Security', value: boolLabel(config.security.securityProtocolsEnabled), detail: `Open LAN ${boolLabel(config.security.allowOpenLanAccess)}` },
    { label: 'AI Autonomy', value: boolLabel(config.aiAutonomyEnabled), detail: `TLS ${boolLabel(config.security.enableTls)}` },
    { label: 'Runtime Paths', value: 'Live Payload', detail: telemetry.primaryIpAddress || config.bindAddress }
  ];
  const activityRows = [
    { label: 'Runtime Lanes', value: formatCount(snapshot.endpoints.length), detail: 'published service routes' },
    { label: 'Providers', value: formatCount(snapshot.providers.length), detail: 'connected model lanes' },
    { label: 'Execution History', value: formatCount(providerExecutionHistory.length), detail: 'recent provider runs' },
    { label: 'Governance Findings', value: formatCount(findings.length), detail: governance.lastEvaluatedUtc || 'awaiting evaluation' },
    { label: 'Apple Hosts', value: formatCount(appleHosts.length), detail: `${formatCount(appleOperations.length)} operations tracked` },
    { label: 'Apple Attention', value: formatCount(attentionAppleOperationCount), detail: `${formatCount(activeAppleOperationCount)} active now` },
    { label: 'Gateway Lanes', value: formatCount(gateways.length), detail: `${formatCount(governanceServers.length)} governance servers` },
    { label: 'Recent Executions', value: formatCount(recentExecutions.length), detail: 'platform governance runs' }
  ];
  const telemetryNarrative = [
    `Telemetry capture: ${telemetry.capturedAtUtc || 'pending'}`,
    `CPU load: ${formatPercent(telemetry.cpuPercent || 0)}`,
    `Memory pressure: ${formatPercent(telemetry.memoryPercent || 0)}`,
    `Disk occupancy: ${formatPercent(telemetry.diskPercent || 0)}`,
    `Outbound: ${formatCount(telemetry.bytesSentPerSecond || 0)} B/s`,
    `Inbound: ${formatCount(telemetry.bytesReceivedPerSecond || 0)} B/s`
  ].join('\n');
  const environmentNarrative = [
    `Environment: ${config.activeProfile.environmentName}`,
    `Bind address: ${config.bindAddress}:${config.browserPort}`,
    `Beacon port: ${config.beaconPort}`,
    `Beacon enabled: ${boolLabel(config.beaconEnabled)}`,
    `Preferred bind address: ${config.activeProfile.preferredBindAddress}`,
    `Telemetry MAC: ${telemetry.primaryMacAddress || 'pending'}`
  ].join('\n');
  const controlPlaneNarrative = [
    `Security protocols: ${boolLabel(config.security.securityProtocolsEnabled)}`,
    `TLS: ${boolLabel(config.security.enableTls)}`,
    `Authentication: ${boolLabel(config.security.enableAuthentication)}`,
    `AI autonomy: ${boolLabel(config.aiAutonomyEnabled)}`,
    `Open LAN access: ${boolLabel(config.security.allowOpenLanAccess)}`,
    `Trusted hosts: ${safeArray(config.security.trustedRemoteHosts).join(', ') || 'none'}`
  ].join('\n');
  return `
    <section class="section-shell telemetry-command-deck">
      <article class="panel-block telemetry-command-hero">
        <div class="telemetry-command-hero-grid">
          <div class="surface-stack">
            <p class="eyebrow">PRIMARY MONITORING SURFACE</p>
            <h3>${escapeHtml(telemetryHeadline)}</h3>
            <p class="summary-copy">${escapeHtml(telemetrySummary)}</p>
            <div class="telemetry-command-badges">
              <span class="badge">${escapeHtml(telemetry.capturedAtUtc || state.lastRefreshLabel)}</span>
              <span class="badge">${escapeHtml(config.activeProfile.environmentName || 'Environment pending')}</span>
              <span class="badge">${escapeHtml(`${config.bindAddress}:${config.browserPort}`)}</span>
            </div>
          </div>
          <div class="telemetry-panel">
            <p class="eyebrow">HOST IDENTITY</p>
            ${telemetryStatTable(hostIdentityRows)}
          </div>
          <div class="telemetry-panel">
            <p class="eyebrow">CONTROL PLANE</p>
            ${telemetryStatTable(controlPlaneRows)}
          </div>
        </div>
      </article>

      <div class="telemetry-monitor-grid">
        ${telemetryMeterCard('CPU Load', formatPercent(telemetry.cpuPercent || 0), telemetry.cpuPercent || 0, 'live host utilization')}
        ${telemetryMeterCard('Memory Pressure', formatPercent(telemetry.memoryPercent || 0), telemetry.memoryPercent || 0, 'resident pressure')}
        ${telemetryMeterCard('Disk Occupancy', formatPercent(telemetry.diskPercent || 0), telemetry.diskPercent || 0, 'storage occupancy')}
        ${telemetrySignalCard('TX / sec', formatCount(telemetry.bytesSentPerSecond || 0), 'outbound bytes per second')}
        ${telemetrySignalCard('RX / sec', formatCount(telemetry.bytesReceivedPerSecond || 0), 'inbound bytes per second')}
        ${telemetrySignalCard('Runtime Lanes', formatCount(snapshot.endpoints.length), 'published service routes')}
        ${telemetrySignalCard('Providers', formatCount(snapshot.providers.length), 'connected model lanes')}
        ${telemetrySignalCard('Apple Jobs', formatCount(appleOperations.length), `${formatCount(attentionAppleOperationCount)} attention / ${formatCount(activeAppleOperationCount)} active`)}
      </div>

      <div class="telemetry-command-grid">
        <div class="surface-stack">
          <article class="telemetry-panel">
            <p class="eyebrow">GOVERNED RESOURCE ENVELOPE</p>
            <div class="telemetry-cluster-grid">
              ${telemetryMeterCard('CPU Budget', `${safeNumber(resourceAllocation.cpuPercent, 0)}%`, resourceAllocation.cpuPercent, 'governed launch budget', safeNumber(resourceAllocation.cpuPercent, 0) <= 0 ? 'danger' : '')}
              ${telemetryMeterCard('RAM Budget', `${safeNumber(resourceAllocation.memoryPercent, 0)}%`, resourceAllocation.memoryPercent, 'managed memory ceiling', safeNumber(resourceAllocation.memoryPercent, 0) <= 0 ? 'danger' : '')}
              ${telemetryMeterCard('Bandwidth Budget', `${safeNumber(resourceAllocation.bandwidthPercent, 0)}%`, resourceAllocation.bandwidthPercent, 'provider and Apple traffic', safeNumber(resourceAllocation.bandwidthPercent, 0) <= 0 ? 'danger' : '')}
              ${telemetryMeterCard('Storage Budget', `${safeNumber(resourceAllocation.storagePercent, 0)}%`, resourceAllocation.storagePercent, 'exports and staging budget', safeNumber(resourceAllocation.storagePercent, 0) <= 0 ? 'danger' : '')}
            </div>
          </article>

          <article class="telemetry-panel">
            <p class="eyebrow">OPERATIONAL ACTIVITY</p>
            ${telemetryStatTable(activityRows)}
          </article>
        </div>

        <div class="surface-stack">
          ${narrativePanel('Host Identity', telemetry.hostName || 'Host pending', [
            `Primary IP: ${telemetry.primaryIpAddress || 'n/a'}`,
            `Primary MAC: ${telemetry.primaryMacAddress || 'n/a'}`,
            `Operating system: ${telemetry.operatingSystem || 'Windows'}`,
            `Captured: ${telemetry.capturedAtUtc || state.lastRefreshLabel}`
          ].join('\n'))}
          ${narrativePanel('Environment Profile', config.activeProfile.environmentName, environmentNarrative)}
          ${narrativePanel('Telemetry Narrative', 'Live Host Pressure', telemetryNarrative)}
          ${narrativePanel('Control Plane Narrative', 'Trust Envelope', controlPlaneNarrative)}
        </div>
      </div>
    </section>
  `;
}

function renderRuntimeView() {
  const snapshot = dashboardSnapshot();
  const governance = governanceSnapshot();
  const appleHosts = safeArray(governance.appleRemoteHosts);
  const gateways = safeArray(governance.platformGateways);
  const governanceServers = safeArray(governance.governanceServers);
  const customMcpServers = snapshot.endpoints.filter((endpoint) => String(endpoint.kind || '').toLowerCase() === 'mcp_server' && !!endpoint.userDefined);
  const customSubAgents = snapshot.endpoints.filter((endpoint) => String(endpoint.kind || '').toLowerCase() === 'sub_agent' && !!endpoint.userDefined);
  const mcpServerDraft = state.mcpServerDraft;
  const appleHostDraft = state.appleHostDraft;
  const subAgentDraft = state.subAgentDraft;
  const endpointRows = snapshot.endpoints.length ? `
    <div class="table-wrap">
      <table>
        <thead>
          <tr>
            <th>Name</th>
            <th>Kind</th>
            <th>Host</th>
            <th>Port</th>
            <th>Status</th>
            <th>Origin</th>
          </tr>
        </thead>
        <tbody>
          ${snapshot.endpoints.map((endpoint) => `
            <tr${endpoint.isTemplate ? ' class="template-row"' : ''}>
              <td>${escapeHtml(endpoint.displayName)}${endpoint.isTemplate ? ' <span class="badge badge-template">TEMPLATE</span>' : ''}</td>
              <td>${escapeHtml(endpoint.kind)}</td>
              <td>${escapeHtml(endpoint.host)}</td>
              <td>${escapeHtml(endpoint.port)}</td>
              <td>${endpoint.isTemplate ? statusPill('template') : statusPill(endpoint.status)}</td>
              <td>${endpoint.userDefined ? 'Custom' : endpoint.isTemplate ? 'Template' : 'Managed'}</td>
            </tr>
          `).join('')}
        </tbody>
      </table>
    </div>
  ` : emptyState('No runtime endpoints yet', 'Import or configure MCP servers and sub-agents to populate the runtime map.');

  const customMcpServerMarkup = customMcpServers.length ? `
    <div class="history-list">
      ${customMcpServers.map((endpoint) => `
        <article class="history-item ${mcpServerDraft.id === endpoint.id ? 'selected' : ''}" data-mcp-server-id="${escapeHtml(endpoint.id)}">
          <strong>${escapeHtml(endpoint.displayName || endpoint.id)}</strong>
          <div>${escapeHtml(endpoint.host || 'pending host')}${endpoint.port ? `:${escapeHtml(endpoint.port)}` : ''}</div>
          <div>${escapeHtml(endpoint.routePath || '/mcp')}</div>
          <div>${statusPill(endpoint.status || 'unknown')}</div>
        </article>
      `).join('')}
    </div>
  ` : emptyState('No custom MCP servers yet', 'Create shared MCP server lanes here so every provider can use the same tool surface.');

  const customSubAgentMarkup = customSubAgents.length ? `
    <div class="history-list">
      ${customSubAgents.map((endpoint) => `
        <article class="history-item ${subAgentDraft.id === endpoint.id ? 'selected' : ''}" data-subagent-id="${escapeHtml(endpoint.id)}">
          <strong>${escapeHtml(endpoint.displayName || endpoint.id)}</strong>
          <div>${escapeHtml(endpoint.specialization || endpoint.description || 'Custom specialist lane')}</div>
          <div>${escapeHtml(endpoint.host || 'logical lane')}${endpoint.port ? `:${escapeHtml(endpoint.port)}` : ''}</div>
          <div>${statusPill(endpoint.status || 'unknown')}</div>
        </article>
      `).join('')}
    </div>
  ` : emptyState('No custom sub-agents yet', 'Create specialist lanes here so provider ownership can target the exact agents you define.');

  const installHistory = snapshot.installHistory.length ? `
    <div class="history-list">
      ${snapshot.installHistory.map((entry) => `
        <article class="history-item">
          <strong>${escapeHtml(entry.kind)}</strong>
          <div>${escapeHtml(entry.source)}</div>
          <div>${escapeHtml(entry.installedAtUtc)}</div>
          <div>${escapeHtml(entry.executionSummary)}</div>
        </article>
      `).join('')}
    </div>
  ` : emptyState('No install provenance yet', 'Installer history will appear here after package, repo, or zip imports run successfully.');

  const gatewayMarkup = renderPlatformGatewaysMarkup(gateways);
  const appleHostMarkup = renderAppleHostsMarkup(appleHosts);
  const governanceServerMarkup = renderGovernanceServersMarkup(governanceServers);

  return `
    <section class="section-shell">
      <div class="card-grid">
        ${metricCard('Endpoints', formatCount(snapshot.endpoints.length), 'runtime inventory')}
        ${metricCard('Gateway Lanes', formatCount(gateways.length), 'LAN service broadcasts')}
        ${metricCard('Apple Hosts', formatCount(appleHosts.length), 'remote toolchains')}
        ${metricCard('Gov Servers', formatCount(governanceServers.length), 'platform enforcement')}
      </div>

      <article class="panel-block">
        <p class="eyebrow">Guided Runtime Setup</p>
        <h3>Publish Shared Lanes</h3>
        <p class="narrative-copy">Walk through creation and maintenance flows instead of filling out the runtime editors by hand.</p>
        ${renderGuidedWorkflowLaunchers([
          'guided-runtime-maintenance',
          'new-mcp',
          'new-subagent',
          'new-apple-host'
        ])}
      </article>

      <div class="split-grid">
        <article class="panel-block">
          <p class="eyebrow">Shared MCP</p>
          <h3>Custom MCP Servers</h3>
          ${statusMessage(state.runtimeStatus)}
          ${customMcpServerMarkup}
          <form class="surface-form panel-block" data-form-kind="mcp-server">
            <p class="eyebrow">MCP Server Editor</p>
            <h3>${escapeHtml(mcpServerDraft.displayName || 'Create a shared MCP lane')}</h3>
            <label>MCP Server ID
              <input name="id" value="${escapeHtml(mcpServerDraft.id)}" placeholder="swift-tools-mcp" />
            </label>
            <label>Display Name
              <input name="displayName" value="${escapeHtml(mcpServerDraft.displayName)}" placeholder="Swift Tools MCP" />
            </label>
            <div class="form-grid two-up">
              <label>Host
                <input name="host" value="${escapeHtml(mcpServerDraft.host)}" placeholder="127.0.0.1 or LAN host" />
              </label>
              <label>Port
                <input name="port" type="number" min="1" max="65535" value="${escapeHtml(mcpServerDraft.port || '')}" />
              </label>
            </div>
            <label>Protocol
              <input name="protocol" value="${escapeHtml(mcpServerDraft.protocol || 'http')}" placeholder="http or https" />
            </label>
            <label>Route Path
              <input name="routePath" value="${escapeHtml(mcpServerDraft.routePath || '/mcp')}" placeholder="/mcp or /status" />
            </label>
            <label>Description
              <textarea name="description" rows="4" placeholder="Optional notes about this MCP server lane.">${escapeHtml(mcpServerDraft.description)}</textarea>
            </label>
            <div class="action-row">
              <button type="submit">Save MCP Server</button>
              <button type="button" data-action="reset-mcp-server-draft">New MCP Server</button>
              <button type="button" data-action="remove-mcp-server">Remove MCP Server</button>
            </div>
          </form>
        </article>
        <article class="panel-block">
          <p class="eyebrow">Specialists</p>
          <h3>Custom Sub-Agents</h3>
          ${statusMessage(state.runtimeStatus)}
          ${customSubAgentMarkup}
          <form class="surface-form panel-block" data-form-kind="subagent">
            <p class="eyebrow">Sub-Agent Editor</p>
            <h3>${escapeHtml(subAgentDraft.displayName || 'Create a specialist lane')}</h3>
            <label>Sub-Agent ID
              <input name="id" value="${escapeHtml(subAgentDraft.id)}" placeholder="swift-specialist" />
            </label>
            <label>Display Name
              <input name="displayName" value="${escapeHtml(subAgentDraft.displayName)}" placeholder="Swift Specialist" />
            </label>
            <label>Specialization
              <input name="specialization" value="${escapeHtml(subAgentDraft.specialization)}" placeholder="Swift, C++, documentation, test automation..." />
            </label>
            <div class="form-grid two-up">
              <label>Host
                <input name="host" value="${escapeHtml(subAgentDraft.host)}" placeholder="Optional bind or LAN host" />
              </label>
              <label>Port
                <input name="port" type="number" min="0" max="65535" value="${escapeHtml(subAgentDraft.port || 0)}" />
              </label>
            </div>
            <label>Protocol
              <input name="protocol" value="${escapeHtml(subAgentDraft.protocol || 'virtual')}" placeholder="virtual or http" />
            </label>
            <label>Route Path
              <input name="routePath" value="${escapeHtml(subAgentDraft.routePath)}" placeholder="/status or blank for logical lanes" />
            </label>
            <label>Description
              <textarea name="description" rows="4" placeholder="Optional notes about this specialist lane.">${escapeHtml(subAgentDraft.description)}</textarea>
            </label>
            <div class="action-row">
              <button type="submit">Save Sub-Agent</button>
              <button type="button" data-action="reset-subagent-draft">New Sub-Agent</button>
              <button type="button" data-action="remove-subagent">Remove Sub-Agent</button>
            </div>
          </form>
        </article>
      </div>

      <div class="split-grid">
        <article class="panel-block">
          <p class="eyebrow">Platform Lanes</p>
          <h3>Gateway Services</h3>
          ${gatewayMarkup}
        </article>
        <article class="panel-block">
          <p class="eyebrow">Apple Fabric</p>
          <h3>Remote Hosts</h3>
          ${statusMessage(state.runtimeStatus)}
          ${appleHostMarkup}
          <form class="surface-form panel-block" data-form-kind="apple-host">
            <p class="eyebrow">Apple Host Editor</p>
            <h3>${escapeHtml(appleHostDraft.displayName || 'Register an Apple remote host')}</h3>
            <label>Host ID
              <input name="hostId" value="${escapeHtml(appleHostDraft.hostId)}" placeholder="apple-host-01" />
            </label>
            <label>Display Name
              <input name="displayName" value="${escapeHtml(appleHostDraft.displayName)}" placeholder="Primary Apple Build Host" />
            </label>
            <div class="form-grid three-up">
              <label>Transport
                <select name="transport">
                  <option value="companion_service"${selectedAttr((appleHostDraft.transport || 'companion_service') === 'companion_service')}>Companion Service</option>
                  <option value="ssh"${selectedAttr(appleHostDraft.transport === 'ssh')}>SSH</option>
                </select>
              </label>
              <label>Address or Hostname
                <input name="address" value="${escapeHtml(appleHostDraft.address)}" placeholder="mac-builder.local" />
              </label>
              <label>Port
                <input name="port" type="number" min="0" max="65535" value="${escapeHtml(appleHostDraft.port || '')}" />
              </label>
            </div>
            <div class="form-grid two-up">
              <label>Username
                <input name="username" value="${escapeHtml(appleHostDraft.username)}" placeholder="builder" />
              </label>
              <label>Companion Base URL
                <input name="serviceBaseUrl" value="${escapeHtml(appleHostDraft.serviceBaseUrl)}" placeholder="http://mac-builder.local:8081" />
              </label>
            </div>
            <div class="form-grid two-up">
              <label>Companion Health Path
                <input name="companionHealthPath" value="${escapeHtml(appleHostDraft.companionHealthPath || '/healthz')}" placeholder="/healthz" />
              </label>
              <label>Companion Execute Path
                <input name="companionExecutePath" value="${escapeHtml(appleHostDraft.companionExecutePath || '/execute')}" placeholder="/execute" />
              </label>
            </div>
            <label>Preferred Developer Directory
              <input name="preferredDeveloperDirectory" value="${escapeHtml(appleHostDraft.preferredDeveloperDirectory)}" placeholder="/Applications/Xcode.app/Contents/Developer" />
            </label>
            <div class="form-grid two-up">
              <label>Default Signing Identity
                <input name="defaultSigningIdentity" value="${escapeHtml(appleHostDraft.defaultSigningIdentity)}" placeholder="Developer ID Application: Example Corp" />
              </label>
              <label>Default Notary Profile
                <input name="defaultNotaryKeychainProfile" value="${escapeHtml(appleHostDraft.defaultNotaryKeychainProfile)}" placeholder="mastercontrol-notary" />
              </label>
            </div>
            <label>Default Notary Team ID
              <input name="defaultNotaryTeamId" value="${escapeHtml(appleHostDraft.defaultNotaryTeamId)}" placeholder="ABCDE12345" />
            </label>
            <div class="checkbox-stack">
              <label class="checkbox-row"><input type="checkbox" name="platform" value="macos"${checkedAttr(safeArray(appleHostDraft.platforms).includes('macos'))} /> macOS</label>
              <label class="checkbox-row"><input type="checkbox" name="platform" value="ios"${checkedAttr(safeArray(appleHostDraft.platforms).includes('ios'))} /> iOS</label>
              <label class="checkbox-row"><input type="checkbox" name="enabled"${checkedAttr(appleHostDraft.enabled ?? true)} /> Enabled</label>
            </div>
            <div class="action-row">
              <button type="submit">Save Apple Host</button>
              <button type="button" data-action="reset-apple-host-draft">New Apple Host</button>
              <button type="button" data-action="remove-apple-host">Remove Apple Host</button>
            </div>
          </form>
        </article>
      </div>

      <div class="split-grid">
        <article class="panel-block">
          <p class="eyebrow">Governance Lanes</p>
          <h3>Platform MCP Servers</h3>
          ${governanceServerMarkup}
        </article>
        <article class="panel-block">
          <p class="eyebrow">Runtime Map</p>
          <h3>Endpoints</h3>
          ${endpointRows}
        </article>
      </div>

      <article class="panel-block">
        <p class="eyebrow">Install Trail</p>
        <h3>Provenance</h3>
        ${installHistory}
      </article>
    </section>
  `;
}

function renderCluView() {
  const governance = governanceSnapshot();
  const resourceAllocation = currentConfig().resourceAllocation;
  const findings = safeArray(governance.findings);
  const roles = safeArray(governance.roles);
  const rules = safeArray(governance.rules);
  const documents = safeArray(governance.documents);
  const actions = safeArray(governance.recommendedActions);
  const operatorChecklist = safeArray(governance.operatorChecklist);
  const appleHosts = safeArray(governance.appleRemoteHosts);
  const gateways = safeArray(governance.platformGateways);
  const governanceServers = safeArray(governance.governanceServers);
  const recentExecutions = safeArray(governance.recentExecutions);
  const appleOperations = safeArray(governance.appleOperations);
  const appleOperationCountsSnapshot = appleOperationCounts(appleOperations);
  const appleOperationFilter = state.cluAppleOperationFilter || 'attention';
  const filteredAppleOperations = filterAppleOperationsByMode(appleOperations, appleOperationFilter);
  const attentionHosts = appleHosts.filter((host) => safeArray(host.readinessIssues).length || safeArray(host.toolchain?.readinessIssues).length);
  const appleQueueSummary = `${appleOperationCountsSnapshot.queued} queued | ${appleOperationCountsSnapshot.running} running | ${appleOperationCountsSnapshot.attention} attention`;
  const managedLaunchBlocked =
    safeNumber(resourceAllocation.cpuPercent, 0) <= 0 ||
    safeNumber(resourceAllocation.memoryPercent, 0) <= 0 ||
    safeNumber(resourceAllocation.bandwidthPercent, 0) <= 0 ||
    safeNumber(resourceAllocation.storagePercent, 0) <= 0;
  const resourceEnvelopeValue = `CPU ${safeNumber(resourceAllocation.cpuPercent, 0)}% | RAM ${safeNumber(resourceAllocation.memoryPercent, 0)}%`;
  const resourceEnvelopeDetail = managedLaunchBlocked
    ? 'one or more governed launch lanes are blocked'
    : `Bandwidth ${safeNumber(resourceAllocation.bandwidthPercent, 0)}% | Storage ${safeNumber(resourceAllocation.storagePercent, 0)}%`;
  const appleFilterDescription = ({
    attention: 'failed and blocked operations',
    active: 'queued and running operations',
    succeeded: 'completed Apple operations',
    all: 'the full Apple operation history'
  }[appleOperationFilter] || 'failed and blocked operations');

  const findingsMarkup = findings.length ? `
    <div class="history-list">
      ${findings.map((finding) => `
        <article class="history-item">
          <strong>${escapeHtml(finding.title || finding.ruleId || 'Governance Finding')}</strong>
          <div>${statusPill(finding.status || 'warning')}</div>
          <div>${escapeHtml(finding.message || '')}</div>
          <div>${escapeHtml(finding.severity || 'governance')}</div>
        </article>
      `).join('')}
    </div>
  ` : emptyState('No active findings', 'CLU currently reports no active governance findings.');

  const rolesMarkup = roles.length ? `
    <div class="history-list">
      ${roles.map((role) => `
        <article class="history-item">
          <strong>${escapeHtml(role.name || role.roleId || 'Role')}</strong>
          <div>${escapeHtml(role.authorityLevel || 'governance')}</div>
          <div>${escapeHtml(formatPreview(safeArray(role.responsibilities).join(' | '), 180) || 'No published responsibilities')}</div>
        </article>
      `).join('')}
    </div>
  ` : emptyState('No roles published', 'The CLU governance profile has not published any roles yet.');

  const rulesMarkup = rules.length ? `
    <div class="history-list">
      ${rules.map((rule) => `
        <article class="history-item">
          <strong>${escapeHtml(rule.ruleId || 'Rule')}</strong>
          <div>${escapeHtml(rule.title || 'Governance rule')}</div>
          <div>${escapeHtml(rule.severity || 'unspecified')} | ${escapeHtml(rule.failureConsequence || 'advisory')}</div>
        </article>
      `).join('')}
    </div>
  ` : emptyState('No rules published', 'The CLU governance profile has not published any rules yet.');

  const documentsMarkup = documents.length ? `
    <div class="history-list">
      ${documents.map((document) => `
        <article class="history-item">
          <strong>${escapeHtml(document.title || document.documentId || 'Document')}</strong>
          <div>${escapeHtml(document.category || 'governance')}</div>
          <div>${escapeHtml(formatPreview(document.summary || document.body || '', 180) || 'No summary provided')}</div>
        </article>
      `).join('')}
    </div>
  ` : emptyState('No documents published', 'The CLU governance profile has not published any reference documents yet.');

  const actionNarrative = actions.length ? actions.join('\n') : 'No recommended actions are currently required.';
  const checklistNarrative = operatorChecklist.length ? operatorChecklist.join('\n') : 'No operator checklist is published yet.';
  const appleHostsMarkup = renderAppleHostsMarkup(appleHosts);
  const appleOperationsMarkup = filteredAppleOperations.length
    ? renderAppleOperationsMarkup(filteredAppleOperations)
    : emptyState('No Apple operations match this filter', `Switch filters to review ${appleFilterDescription}.`);
  const gatewaysMarkup = renderPlatformGatewaysMarkup(gateways);
  const governanceServersMarkup = renderGovernanceServersMarkup(governanceServers);
  const governanceExecutionsMarkup = renderGovernanceExecutionsMarkup(recentExecutions);

  return `
    <section class="section-shell">
      <div class="card-grid">
        ${metricCard('Posture', String(governance.posture || 'pending').toUpperCase(), 'live governance evaluation')}
        ${metricCard('Findings', formatCount(findings.length), governance.lastEvaluatedUtc || 'awaiting evaluation')}
        ${metricCard('Roles', formatCount(roles.length), 'published authorities')}
        ${metricCard('Rules', formatCount(rules.length), `${documents.length} documents`)}
      </div>

      <div class="card-grid">
        ${metricCard('Apple Hosts', formatCount(appleHosts.length), 'remote toolchain lanes')}
        ${metricCard('Apple Ops', formatCount(appleOperations.length), appleQueueSummary)}
        ${metricCard('Apple Attention', formatCount(appleOperationCountsSnapshot.attention), `${appleOperationCountsSnapshot.rerunnableAttention} ready for replay`)}
        ${metricCard('Host Readiness', formatCount(attentionHosts.length), `${appleHosts.length - attentionHosts.length} hosts ready`)}
        ${metricCard('Resource Envelope', resourceEnvelopeValue, resourceEnvelopeDetail)}
        ${metricCard('Gateways', formatCount(gateways.length), 'platform broadcasts')}
        ${metricCard('Gov Servers', formatCount(governanceServers.length), `${recentExecutions.length} recent executions`)}
      </div>

      ${statusMessage(state.cluStatus)}

      <div class="split-grid">
        ${narrativePanel('Doctrine', governance.unitName || 'Command Logic Unit', governance.doctrine || 'Governance doctrine is waiting for the current CLU profile.')}
        ${narrativePanel('Recommended Actions', 'Operator Queue', actionNarrative)}
      </div>

      <article class="panel-block">
        <p class="eyebrow">Guided Setup</p>
        <h3>CLU Quick Actions</h3>
        <p class="narrative-copy">Launch the same setup workflows the desktop shell uses so operators can connect AI models, assign responsibilities, publish runtime lanes, manage Forsetti modules, and onboard software without hunting for raw editors.</p>
        ${renderGuidedWorkflowLaunchers([
          'connect-model',
          'assign-responsibility',
          'new-mcp',
          'new-subagent',
          'new-subagent-group',
          'new-apple-host',
          'manage-forsetti-modules',
          'guided-import'
        ])}
      </article>

      <div class="split-grid">
        <article class="panel-block">
          <p class="eyebrow">Operator Checklist</p>
          <h3>Published Guidance</h3>
          <p class="narrative-copy">${multilineHtml(checklistNarrative)}</p>
        </article>
        <article class="panel-block">
          <p class="eyebrow">Current Findings</p>
          <h3>Live Governance Posture</h3>
          ${findingsMarkup}
        </article>
      </div>

      <div class="split-grid">
        <article class="panel-block">
          <p class="eyebrow">Apple Fabric</p>
          <h3>Remote Hosts</h3>
          <p class="narrative-copy">${escapeHtml(attentionHosts.length ? `${attentionHosts.length} Apple host${attentionHosts.length === 1 ? '' : 's'} report readiness gaps that could block Mac or iOS jobs.` : 'All published Apple hosts currently report ready toolchains and signing posture.')}</p>
          ${appleHostsMarkup}
        </article>
        <article class="panel-block">
          <p class="eyebrow">Apple Operations</p>
          <h3>Production History</h3>
          <div class="button-row">
            <button type="button" class="${appleOperationFilter === 'attention' ? 'route-button active' : 'route-button'}" data-action="set-apple-operation-filter" data-apple-operation-filter="attention">Attention</button>
            <button type="button" class="${appleOperationFilter === 'active' ? 'route-button active' : 'route-button'}" data-action="set-apple-operation-filter" data-apple-operation-filter="active">Active</button>
            <button type="button" class="${appleOperationFilter === 'succeeded' ? 'route-button active' : 'route-button'}" data-action="set-apple-operation-filter" data-apple-operation-filter="succeeded">Succeeded</button>
            <button type="button" class="${appleOperationFilter === 'all' ? 'route-button active' : 'route-button'}" data-action="set-apple-operation-filter" data-apple-operation-filter="all">All</button>
            <button type="button" data-action="retry-apple-attention"${appleOperationCountsSnapshot.rerunnableAttention ? '' : ' disabled'}>Retry Attention Ops</button>
          </div>
          <p class="narrative-copy">${escapeHtml(`Showing ${appleFilterDescription}. ${filteredAppleOperations.length} operation${filteredAppleOperations.length === 1 ? '' : 's'} match the current filter.`)}</p>
          ${appleOperationsMarkup}
        </article>
      </div>

      <div class="split-grid">
        <article class="panel-block">
          <p class="eyebrow">Platform Gateways</p>
          <h3>LAN Broadcast Lanes</h3>
          ${gatewaysMarkup}
        </article>
        <article class="panel-block">
          <p class="eyebrow">Governance Servers</p>
          <h3>Enforcement Routes</h3>
          ${governanceServersMarkup}
        </article>
      </div>

      <div class="split-grid">
        <article class="panel-block">
          <p class="eyebrow">Roles</p>
          <h3>Published Authorities</h3>
          ${rolesMarkup}
        </article>
        <article class="panel-block">
          <p class="eyebrow">Rules</p>
          <h3>Measured Controls</h3>
          ${rulesMarkup}
        </article>
      </div>

      <div class="split-grid">
        <article class="panel-block">
          <p class="eyebrow">Executions</p>
          <h3>Recent Governance Runs</h3>
          ${governanceExecutionsMarkup}
        </article>
        <article class="panel-block">
          <p class="eyebrow">Documents</p>
          <h3>Governance Corpus</h3>
          ${documentsMarkup}
        </article>
      </div>
    </section>
  `;
}

function renderProvidersView() {
  const config = currentConfig();
  const snapshot = dashboardSnapshot();
  const draft = state.providerDraft;
  const executionDraft = state.providerExecutionDraft;
  const groupDraft = state.subAgentGroupDraft;
  const capability = selectedProviderCapability();
  const credentialStatus = selectedProviderCredentialStatus();
  const credentialFields = capability?.credentialFields || [];
  const subAgentTargets = snapshot.providerAssignmentTargets.filter((target) => target.kind === 'sub_agent');
  const providersMarkup = snapshot.providers.length ? `
    <div class="provider-list">
      ${snapshot.providers.map((provider) => `
        <button
          type="button"
          class="provider-card provider-card-button ${draft.id === provider.id ? 'is-selected' : ''}"
          data-provider-id="${escapeHtml(provider.id)}">
          <strong>${escapeHtml(provider.displayName)}${provider.isTemplate ? ' <span class="badge badge-template">TEMPLATE</span>' : ''}</strong>
          <div>${escapeHtml(provider.baseUrl)}</div>
          <div>${provider.isTemplate ? 'Not configured \u2014 click to set up' : `${escapeHtml(provider.kind)} | ${provider.enabled ? 'enabled' : 'disabled'} | autonomy ${provider.allowAutonomousControl ? 'on' : 'off'} | credentials ${provider.credentialsConfigured ? 'ready' : 'missing'}`}</div>
        </button>
      `).join('')}
    </div>
  ` : emptyState('No providers configured', 'Save a provider connection below to start routing AI services through the dashboard.');

  const capabilitiesMarkup = snapshot.providerCapabilities.length ? `
    <div class="history-list">
      ${snapshot.providerCapabilities.map((item) => `
        <article class="history-item">
          <strong>${escapeHtml(item.displayName)}</strong>
          <div>${escapeHtml(item.description || item.providerId)}</div>
          <div>${escapeHtml(item.recommendedModel || 'model optional')} | ${escapeHtml((item.supportedTargets || []).join(', ') || 'no targets')}</div>
        </article>
      `).join('')}
    </div>
  ` : emptyState('No provider modules', 'Provider capabilities will appear here when Forsetti provider modules are active.');

  const assignmentsMarkup = snapshot.providerAssignments.length ? `
    <div class="history-list">
      ${snapshot.providerAssignments.map((assignment) => `
        <article class="history-item">
          <strong>${escapeHtml(assignmentTargetLabel(assignment.targetId))}</strong>
          <div>${escapeHtml(providerDisplayNameById(assignment.providerId))}</div>
          <div>${escapeHtml(assignment.updatedAtUtc || 'pending')}</div>
        </article>
      `).join('')}
    </div>
  ` : emptyState('No provider ownership', 'Assign planner, architect, groups, or individual sub-agents to a provider owner.');

  const executionRegistrationsMarkup = snapshot.providerExecutionRegistrations.length ? `
    <div class="history-list">
      ${snapshot.providerExecutionRegistrations.map((registration) => `
        <article class="history-item">
          <strong>${escapeHtml(registration.displayName || registration.providerId)}</strong>
          <div>${escapeHtml(registration.transport || 'runtime transport')}</div>
          <div>${registration.supportsSharedMcpAccess ? 'shared MCP' : 'isolated'}${registration.supportsDirectMcpConfig ? ' | direct config' : ''}</div>
        </article>
      `).join('')}
    </div>
  ` : emptyState('No execution transports', 'Provider execution modules will appear here when their Forsetti registrations are active.');

  const executionHistoryMarkup = snapshot.providerExecutionHistory.length ? `
    <div class="history-list">
      ${snapshot.providerExecutionHistory.map((record) => `
        <article class="history-item">
          <strong>[${escapeHtml(record.status || 'pending')}] ${escapeHtml(record.targetDisplayName || record.targetId)}</strong>
          <div>${escapeHtml(record.providerDisplayName || record.providerId)}</div>
          <div>${escapeHtml(record.completedAtUtc || record.startedAtUtc || 'pending')}</div>
        </article>
      `).join('')}
    </div>
  ` : emptyState('No execution history', 'Run a provider task to capture routing and execution history.');

  const assignmentTargetOptions = snapshot.providerAssignmentTargets.map((target) => `
    <option value="${escapeHtml(target.targetId)}" data-kind="${escapeHtml(target.kind)}">
      ${escapeHtml(target.displayName)}${target.kind === 'sub_agent_group' ? ` (${target.memberTargetIds.length} members)` : ''}
    </option>
  `).join('');

  const executionTargetOptions = snapshot.providerAssignmentTargets.map((target) => `
    <option value="${escapeHtml(target.targetId)}"${target.targetId === executionDraft.targetId ? ' selected' : ''}>
      ${escapeHtml(target.displayName)}${target.kind === 'sub_agent_group' ? ` (${target.memberTargetIds.length} members)` : ''}
    </option>
  `).join('');

  const assignmentProviderOptions = `
    <option value="">(Unassigned)</option>
    ${snapshot.providers.map((provider) => `
      <option value="${escapeHtml(provider.id)}">${escapeHtml(provider.displayName)}</option>
    `).join('')}
  `;

  const subAgentGroupsMarkup = snapshot.subAgentGroups.length ? `
    <div class="provider-list">
      ${snapshot.subAgentGroups.map((group) => `
        <button
          type="button"
          class="provider-card provider-card-button ${groupDraft.groupId === group.groupId ? 'is-selected' : ''}"
          data-subagent-group-id="${escapeHtml(group.groupId)}">
          <strong>${escapeHtml(group.displayName)}</strong>
          <div>${escapeHtml(group.description || group.groupId)}</div>
          <div>${escapeHtml(group.memberTargetIds.length)} members | ${escapeHtml(group.updatedAtUtc || 'pending')}</div>
        </button>
      `).join('')}
    </div>
  ` : emptyState('No custom sub-agent groups', 'Create specialist groups here so one provider can own a focused lane of sub-agents.');

  const subAgentGroupMembersMarkup = subAgentTargets.length ? subAgentTargets.map((target) => `
    <label class="checkbox-field">
      <input
        name="memberTargetId"
        type="checkbox"
        value="${escapeHtml(target.targetId)}"${checkedAttr(groupDraft.memberTargetIds.includes(target.targetId))}>
      <span>${escapeHtml(target.displayName)}</span>
    </label>
  `).join('') : '<p class="narrative-copy">No sub-agent endpoints are currently published, so group membership cannot be configured yet.</p>';

  const credentialFormMarkup = draft.id ? `
    <form class="surface-form panel-block" data-form-kind="provider-credentials">
      <p class="eyebrow">Secure Credentials</p>
      <h3>${escapeHtml(draft.displayName || draft.id)}</h3>
      <p class="narrative-copy">${escapeHtml(credentialStatus?.message || 'No credentials are stored yet for this route.')}</p>
      ${credentialFields.length ? credentialFields.slice(0, 2).map((field) => {
        const hint = environmentHintStatus(field);
        const badge = hint.hint
          ? `<span class="hint-badge ${hint.badgeClass}">${escapeHtml(hint.badgeText)}</span>`
          : '';
        const placeholder = hint.detected
          ? `Using environment variable ${hint.hint} (type to override)`
          : (field.placeholder || 'Credential value');
        return `
        <label>${escapeHtml(field.label)} ${badge}
          <input
            name="credential:${escapeHtml(field.fieldId)}"
            type="password"
            placeholder="${escapeHtml(placeholder)}">
        </label>
        <p class="form-help">${escapeHtml(field.helpText || '')}</p>
        `;
      }).join('') : '<p class="narrative-copy">The selected provider kind has no published credential requirements.</p>'}
      <button type="submit"${credentialFields.length ? '' : ' disabled'}>Save Credentials</button>
    </form>
  ` : emptyState('Select a provider route', 'Save or select a provider route before entering credentials.');

  return `
    <section class="section-shell">
      <div class="card-grid">
        ${metricCard('Configured Providers', formatCount(snapshot.providers.length), 'service registry')}
        ${metricCard('Enabled Providers', formatCount(snapshot.providers.filter((provider) => provider.enabled).length), 'active routes')}
        ${metricCard('Credentialed Routes', formatCount(snapshot.providers.filter((provider) => provider.credentialsConfigured).length), 'secure store ready')}
        ${metricCard('Custom Groups', formatCount(snapshot.subAgentGroups.length), 'named specialist lanes')}
        ${metricCard('Owned Targets', formatCount(snapshot.providerAssignments.length), 'roles and sub-agents')}
        ${metricCard('Execution Transports', formatCount(snapshot.providerExecutionRegistrations.length), 'active provider modules')}
        ${metricCard('Execution History', formatCount(snapshot.providerExecutionHistory.length), 'recent provider runs')}
        ${metricCard('Global AI Autonomy', config.aiAutonomyEnabled ? 'On' : 'Off', 'instance-wide switch')}
      </div>

      <article class="panel-block">
        <p class="eyebrow">Guided Provider Setup</p>
        <h3>Connect And Assign Models</h3>
        <p class="narrative-copy">Use guided workflows when you want to bring new models online, define ownership, validate routing, or create specialist groups without working through the full provider editor stack.</p>
        ${renderGuidedWorkflowLaunchers([
          'connect-model',
          'assign-responsibility',
          'guided-provider-execution',
          'new-subagent-group'
        ])}
      </article>

      ${statusMessage(state.providerStatus)}

      <div class="split-grid">
        <article class="panel-block">
          <p class="eyebrow">Provider Fleet</p>
          <h3>Registered Adapters</h3>
          ${providersMarkup}
        </article>

        <div class="surface-stack">
          ${advancedOnly(`<form class="surface-form panel-block" data-form-kind="ai-autonomy">
            <p class="eyebrow">Autonomy Gate</p>
            <h3>Global AI Control</h3>
            <label class="checkbox-field">
              <input name="aiAutonomyEnabled" type="checkbox"${checkedAttr(config.aiAutonomyEnabled)}>
              <span>Allow AI full autonomy when the user explicitly enables it.</span>
            </label>
            <button type="submit">Save Autonomy Setting</button>
          </form>`)}

          <form class="surface-form panel-block" data-form-kind="provider">
            <p class="eyebrow">Provider Editor</p>
            <h3>${draft.id ? 'Edit Provider' : 'New Provider'}</h3>
            <div class="two-column">
              <label>Provider ID<input name="id" value="${escapeHtml(draft.id)}" required></label>
              <label>Display Name<input name="displayName" value="${escapeHtml(draft.displayName)}" required></label>
              <label>Base URL<input name="baseUrl" value="${escapeHtml(draft.baseUrl)}" required></label>
              <label>Model ID<input name="modelId" value="${escapeHtml(draft.modelId || '')}" placeholder="Recommended model or deployment name"></label>
              <label>Kind
                <select name="kind">${providerKindOptions(draft.kind)}</select>
              </label>
            </div>
            <label class="checkbox-field">
              <input name="enabled" type="checkbox"${checkedAttr(draft.enabled)}>
              <span>Provider is enabled for routing.</span>
            </label>
            <label class="checkbox-field">
              <input name="allowAutonomousControl" type="checkbox"${checkedAttr(draft.allowAutonomousControl)}>
              <span>Provider may configure the dashboard when autonomy is enabled.</span>
            </label>
            <div class="button-row">
              <button type="submit">Save Provider</button>
              <button type="button" class="route-button" data-action="reset-provider-draft">Clear Editor</button>
            </div>
          </form>

          ${credentialFormMarkup}

          ${advancedOnly(`
          <article class="panel-block">
            <p class="eyebrow">Sub-Agent Groups</p>
            <h3>Reusable Specialist Lanes</h3>
            ${subAgentGroupsMarkup}
          </article>

          <form class="surface-form panel-block" data-form-kind="subagent-group">
            <p class="eyebrow">Group Editor</p>
            <h3>${groupDraft.groupId ? 'Edit Group' : 'New Group'}</h3>
            <div class="two-column">
              <label>Group ID<input name="groupId" value="${escapeHtml(groupDraft.groupId)}" required></label>
              <label>Display Name<input name="displayName" value="${escapeHtml(groupDraft.displayName)}" required></label>
            </div>
            <label>Description
              <textarea name="description" rows="3" placeholder="Optional notes about the specialty lane.">${escapeHtml(groupDraft.description || '')}</textarea>
            </label>
            <div class="surface-stack">
              <p class="narrative-copy">Members</p>
              ${subAgentGroupMembersMarkup}
            </div>
            <div class="button-row">
              <button type="submit">Save Group</button>
              <button type="button" class="route-button" data-action="reset-subagent-group-draft">Clear Editor</button>
              <button type="button" class="route-button" data-action="remove-subagent-group"${groupDraft.groupId ? '' : ' disabled'}>Remove Group</button>
            </div>
          </form>

          <form class="surface-form panel-block" data-form-kind="provider-assignment">
            <p class="eyebrow">Ownership Routing</p>
            <h3>Role And Sub-Agent Ownership</h3>
            <label>Target
              <select name="targetId">${assignmentTargetOptions}</select>
            </label>
            <label>Owning Provider
              <select name="providerId">${assignmentProviderOptions}</select>
            </label>
            <button type="submit">Save Ownership</button>
          </form>

          <form class="surface-form panel-block" data-form-kind="provider-execution">
            <p class="eyebrow">Execution Console</p>
            <h3>Validate Provider Routing</h3>
            <p class="narrative-copy">Run a provider-owned task through the local admin API to validate credentials, role ownership, and shared MCP access.</p>
            <label>Execution Target
              <select name="targetId">${executionTargetOptions}</select>
            </label>
            <div class="two-column">
              <label>Max Turns<input name="maxTurns" type="number" min="1" max="12" value="${escapeHtml(executionDraft.maxTurns || 4)}"></label>
              <label class="checkbox-field">
                <input name="allowToolAccess" type="checkbox"${checkedAttr(executionDraft.allowToolAccess)}>
                <span>Allow shared MCP tool access during execution.</span>
              </label>
            </div>
            <label>Prompt
              <textarea name="prompt" rows="6" placeholder="Ask the assigned provider to work within its orchestration lane.">${escapeHtml(executionDraft.prompt || '')}</textarea>
            </label>
            <button type="submit">Run Provider Task</button>
          </form>
          `)}
        </div>
      </div>

      ${advancedOnly(`
      <div class="split-grid">
        <article class="panel-block">
          <p class="eyebrow">Provider Modules</p>
          <h3>Framework-Published Capabilities</h3>
          ${capabilitiesMarkup}
        </article>
        <article class="panel-block">
          <p class="eyebrow">Execution Modules</p>
          <h3>Active Runtime Transports</h3>
          ${executionRegistrationsMarkup}
        </article>
      </div>

      <div class="split-grid">
        <article class="panel-block">
          <p class="eyebrow">Current Ownership</p>
          <h3>Exclusive Control Map</h3>
          ${assignmentsMarkup}
        </article>
        <article class="panel-block">
          <p class="eyebrow">Execution History</p>
          <h3>Recent Provider Runs</h3>
          ${executionHistoryMarkup}
        </article>
      </div>
      `)}
    </section>
  `;
}

function renderImportsView() {
  return `
    <section class="section-shell">
      <div class="section-actions segmented-control" aria-label="Import mode">
        <button type="button" class="segment ${state.importMode === 'package' ? 'is-active' : ''}" data-import-mode="package">Package Installer</button>
        <button type="button" class="segment ${state.importMode === 'repo' ? 'is-active' : ''}" data-import-mode="repo">Bootstrap Repository</button>
        <button type="button" class="segment ${state.importMode === 'zip' ? 'is-active' : ''}" data-import-mode="zip">Zip Bundle</button>
      </div>

      ${statusMessage(state.importStatus)}

      <form class="surface-form import-form ${state.importMode === 'package' ? '' : 'is-hidden'}" data-form-kind="package-install">
        <p class="eyebrow">Managed Installer</p>
        <h3>MSI / EXE / PowerShell</h3>
        <div class="two-column">
          <label>Source URL or Path<input name="source" required></label>
          <label>Kind
            <select name="kind">${packageKindOptions('exe')}</select>
          </label>
        </div>
        <label>Arguments<input name="arguments" placeholder="-EnvironmentName lab -Force"></label>
        <label class="checkbox-field">
          <input name="allowUntrustedExecution" type="checkbox">
          <span>Explicitly approve an untrusted source for this run.</span>
        </label>
        <button type="submit">Run Package Installer</button>
      </form>

      <form class="surface-form import-form ${state.importMode === 'repo' ? '' : 'is-hidden'}" data-form-kind="repo-install">
        <p class="eyebrow">Git Bootstrap</p>
        <h3>Repository Import</h3>
        <div class="two-column">
          <label>Repository URL or Local Path<input name="repositoryUrl" required></label>
          <label>Branch<input name="branch" value="main"></label>
        </div>
        <label>Manifest File<input name="manifestFile" value="mcp-bootstrap.json"></label>
        <label class="checkbox-field">
          <input name="allowUntrustedExecution" type="checkbox">
          <span>Explicitly approve an untrusted repository source for this run.</span>
        </label>
        <button type="submit">Install Repository</button>
      </form>

      <form class="surface-form import-form ${state.importMode === 'zip' ? '' : 'is-hidden'}" data-form-kind="zip-install">
        <p class="eyebrow">Zip Bundle</p>
        <h3>Manifest-Driven Bundle</h3>
        <label>Source URL or Path<input name="source" required></label>
        <label>Manifest File<input name="manifestFile" value="mcp-bootstrap.json"></label>
        <label class="checkbox-field">
          <input name="allowUntrustedExecution" type="checkbox">
          <span>Explicitly approve an untrusted zip bundle for this run.</span>
        </label>
        <button type="submit">Install Zip Bundle</button>
      </form>
    </section>
  `;
}

function renderExportsView() {
  const selectedArtifact = currentExport();
  const selectorOptions = state.exports.map((artifact) => `
    <option value="${escapeHtml(artifact.id)}"${selectedAttr(artifact.id === state.selectedExportId)}>
      ${escapeHtml(artifact.fileName)} | ${escapeHtml(artifact.mediaType || 'text/plain')}
    </option>
  `).join('');

  return `
    <section class="section-shell">
      ${statusMessage(state.exportStatus)}

      <article class="panel-block">
        <p class="eyebrow">Artifact Control</p>
        <h3>Published Exports</h3>
        <div class="toolbar">
          <label class="compact-field">Artifact
            <select data-role="export-selector">
              ${selectorOptions}
            </select>
          </label>
          <div class="button-row">
            <button type="button" data-action="refresh-exports">Refresh Artifacts</button>
            <button type="button" data-action="download-selected-export"${selectedArtifact ? '' : ' disabled'}>Download Selected</button>
            <button type="button" data-action="download-all-exports"${state.exports.length ? '' : ' disabled'}>Download All</button>
          </div>
        </div>
      </article>

      <div class="card-grid">
        ${metricCard('File Name', selectedArtifact?.fileName || 'No artifact selected', 'current export')}
        ${metricCard('Media Type', selectedArtifact?.mediaType || 'n/a', 'content envelope')}
      </div>

      <label class="surface-form panel-block">Artifact Preview
        <textarea rows="14" readonly>${escapeHtml(selectedArtifact?.content || '')}</textarea>
      </label>

      ${state.exports.length ? `
        <div class="export-list">
          ${state.exports.map((artifact) => `
            <button
              type="button"
              class="export-card export-card-button ${artifact.id === state.selectedExportId ? 'is-selected' : ''}"
              data-export-id="${escapeHtml(artifact.id)}">
              <strong>${escapeHtml(artifact.fileName)}</strong>
              <div>${escapeHtml(artifact.mediaType || 'text/plain')}</div>
              <div>${escapeHtml(formatPreview(artifact.content || ''))}</div>
            </button>
          `).join('')}
        </div>
      ` : emptyState('No export artifacts yet', 'Exports will appear here after the service generates agent handoff material.')}
    </section>
  `;
}

function renderSecurityView() {
  const config = currentConfig();
  const security = config.security;
  return `
    <section class="section-shell">
      <div class="card-grid">
        ${metricCard('Security Protocols', boolLabel(security.securityProtocolsEnabled), 'primary gate')}
        ${metricCard('TLS', boolLabel(security.enableTls), 'transport posture')}
        ${metricCard('Authentication', boolLabel(security.enableAuthentication), 'identity posture')}
        ${metricCard('Trusted Hosts', formatCount(security.trustedRemoteHosts.length), 'allowlisted sources')}
      </div>

      ${statusMessage(state.securityStatus)}

      <article class="panel-block">
        <p class="eyebrow">Guided Security</p>
        <h3>Harden The Protection Envelope</h3>
        <p class="narrative-copy">Use the guided workflow when you want the orchestration server to walk you through security posture, LAN access, and trusted-host decisions instead of working directly in the raw control form.</p>
        ${renderGuidedWorkflowLaunchers(['guided-security'])}
      </article>

      <form class="surface-form panel-block" data-form-kind="security">
        <p class="eyebrow">Security Envelope</p>
        <h3>Operator Controls</h3>
        <label class="checkbox-field">
          <input name="securityProtocolsEnabled" type="checkbox"${checkedAttr(security.securityProtocolsEnabled)}>
          <span>Security protocols are enabled for normal operation.</span>
        </label>
        <label class="checkbox-field">
          <input name="enableTls" type="checkbox"${checkedAttr(security.enableTls)}>
          <span>Require TLS for service connectivity where supported.</span>
        </label>
        <label class="checkbox-field">
          <input name="enableAuthentication" type="checkbox"${checkedAttr(security.enableAuthentication)}>
          <span>Require authenticated access for protected operations.</span>
        </label>
        <label class="checkbox-field">
          <input name="allowTroubleshootingBypass" type="checkbox"${checkedAttr(security.allowTroubleshootingBypass)}>
          <span>Allow troubleshooting bypass when explicitly requested.</span>
        </label>
        <label class="checkbox-field">
          <input name="allowOpenLanAccess" type="checkbox"${checkedAttr(security.allowOpenLanAccess)}>
          <span>Expose the browser dashboard to the open local LAN.</span>
        </label>
        <label>Trusted Remote Hosts
          <textarea name="trustedRemoteHosts" rows="6" placeholder="one host per line or comma-separated">${escapeHtml(security.trustedRemoteHosts.join('\n'))}</textarea>
        </label>
        <button type="submit">Save Security Settings</button>
      </form>
    </section>
  `;
}

function renderSettingsView() {
  const config = currentConfig();
  return `
    <section class="section-shell">
      <div class="split-grid">
        <div class="surface-stack">
          <article class="panel-block">
            <p class="eyebrow">Guided Host Setup</p>
            <h3>Configure Identity And Capacity</h3>
            <p class="narrative-copy">Use the guided host-settings workflow when you want to tune bind ports, beacon behavior, and the governed resource envelope without working directly in the raw settings form.</p>
            ${renderGuidedWorkflowLaunchers(['guided-settings'])}
          </article>

          <form class="surface-form panel-block" data-form-kind="settings">
            <p class="eyebrow">Instance Settings</p>
            <h3>Host Configuration</h3>
            ${statusMessage(state.settingsStatus)}
            <div class="two-column">
              <label>Instance Name<input name="instanceName" value="${escapeHtml(config.instanceName)}" required></label>
              <label>Bind Address<input name="bindAddress" value="${escapeHtml(config.bindAddress)}" required></label>
              <label>Browser Port<input name="browserPort" type="number" min="1" max="65535" value="${escapeHtml(config.browserPort)}"></label>
              <label>Beacon Port<input name="beaconPort" type="number" min="1" max="65535" value="${escapeHtml(config.beaconPort)}"></label>
              <label>CPU Allocation %<input name="cpuPercent" type="number" min="0" max="100" value="${escapeHtml(config.resourceAllocation.cpuPercent)}"></label>
              <label>Memory Allocation %<input name="memoryPercent" type="number" min="0" max="100" value="${escapeHtml(config.resourceAllocation.memoryPercent)}"></label>
              <label>Bandwidth Allocation %<input name="bandwidthPercent" type="number" min="0" max="100" value="${escapeHtml(config.resourceAllocation.bandwidthPercent)}"></label>
              <label>Storage Allocation %<input name="storagePercent" type="number" min="0" max="100" value="${escapeHtml(config.resourceAllocation.storagePercent)}"></label>
            </div>
            <label class="checkbox-field">
              <input name="beaconEnabled" type="checkbox"${checkedAttr(config.beaconEnabled)}>
              <span>Broadcast the LAN beacon and gateway metadata.</span>
            </label>
            <label class="checkbox-field">
              <input name="advancedMode" type="checkbox"${checkedAttr(config.advancedMode)} data-action="toggle-advanced-mode">
              <span>Show advanced controls (sub-agent groups, assignment matrix, execution history, governance tools, import/export mechanics).</span>
            </label>
            <button type="submit">Save Settings</button>
          </form>
        </div>

        <div class="surface-stack">
          ${narrativePanel('Environment Profile', config.activeProfile.environmentName, [
            `Preferred bind address: ${config.activeProfile.preferredBindAddress}`,
            `Profile MAC: ${config.activeProfile.macAddress}`,
            `Beacon port: ${config.beaconPort}`,
            `Browser port: ${config.browserPort}`
          ].join('\n'))}
          ${narrativePanel('Resource Envelope', 'Capacity Budget', [
            `CPU: ${config.resourceAllocation.cpuPercent}%`,
            `Memory: ${config.resourceAllocation.memoryPercent}%`,
            `Bandwidth: ${config.resourceAllocation.bandwidthPercent}%`,
            `Storage: ${config.resourceAllocation.storagePercent}%`
          ].join('\n'))}
        </div>
      </div>
    </section>
  `;
}

function renderUnavailableView(title, body) {
  return emptyState(title, body);
}

function renderViewById(viewId) {
  switch (viewId) {
    case 'OverviewSectionView': return renderOverviewView();
    case 'TelemetrySectionView': return renderTelemetryView();
    case 'RuntimeSectionView': return renderRuntimeView();
    case 'CommandLogicUnitSectionView': return renderCluView();
    case 'ProvidersSectionView': return renderProvidersView();
    case 'ImportsSectionView': return renderImportsView();
    case 'ExportsSectionView': return renderExportsView();
    case 'SecuritySectionView': return renderSecurityView();
    case 'SettingsSectionView': return renderSettingsView();
    // WS1 — first-run wizard view; WS6 — readiness dashboard view.
    case 'SetupWizardView': return renderFirstRunView();
    case 'SetupReadinessView': return renderReadinessView();
    default:
      return renderUnavailableView('Unknown Forsetti View', `No browser renderer is registered for ${viewId || 'this destination'}.`);
  }
}

function renderCurrentContent() {
  // WS1 — first-run routing: if setup isn't complete and the user hasn't
  // explicitly chosen Manual or Import, short-circuit to the wizard.
  if (shouldForceFirstRun()) {
    surfaceContentHost.innerHTML = renderFirstRunView();
    return;
  }
  // Manual/Import dismissed-wizard users see the normal operator surface,
  // but we prepend a "Finish Setup" banner (WS1 Manual mode spec).
  const manualBanner = renderManualSetupBanner();
  const viewId = resolvePrimaryViewForDestination(state.currentDestination);
  const main = viewId
    ? renderViewById(viewId)
    : renderUnavailableView('Forsetti View Unavailable', 'The selected destination did not publish a usable view injection.');
  surfaceContentHost.innerHTML = manualBanner + main;
}

// WS1 — decide whether to force the first-run wizard to foreground.
function shouldForceFirstRun() {
  if (state.currentDestination === 'setup') { return true; }
  if (state.currentDestination === 'setup-readiness') { return false; }
  const readiness = state.readiness;
  if (readiness && readiness.firstRunCompleted) { return false; }
  if (state.wizard.dismissed) { return false; }
  if (state.wizard.mode === 'manual' || state.wizard.mode === 'import') { return false; }
  // Hide first-run on error pages so the user can still see diagnostics.
  return !!readiness; // only force when we have a readiness response
}

// WS1 — Manual mode banner. Rendered above every view until setup complete.
function renderManualSetupBanner() {
  const readiness = state.readiness;
  if (!readiness || readiness.firstRunCompleted) { return ''; }
  if (!state.wizard.dismissed) { return ''; }
  if (state.wizard.mode !== 'manual' && state.wizard.mode !== 'import') { return ''; }
  const modeLabel = state.wizard.mode === 'manual' ? 'Manual Setup' : 'Import Existing Configuration';
  return `
    <article class="manual-setup-banner" role="status">
      <div>
        <strong>You're in ${escapeHtml(modeLabel)} mode.</strong>
        Visit <em>Setup Readiness</em> when you're ready to confirm completion.
      </div>
      <div class="button-row">
        <button type="button" class="route-button" data-action="go-setup-readiness">Open Setup Readiness</button>
      </div>
    </article>
  `;
}

function renderOverlayRoute() {
  if (!state.overlayRouteId) {
    return;
  }

  const route = state.surface.overlayRoutes.find((candidate) => candidate.id === state.overlayRouteId);
  if (!route) {
    state.overlayRouteId = '';
    state.overlayWorkspaceDestination = '';
    if (surfaceOverlayDialog.open) {
      surfaceOverlayDialog.close('cancel');
    }
    return;
  }

  const workspaceDestination = route.destinationId || destinationForViewId(route.viewId);
  const viewId = route.targetsModuleView ? route.viewId : resolvePrimaryViewForDestination(workspaceDestination);

  state.overlayWorkspaceDestination = workspaceDestination;
  overlayEyebrow.textContent = `FORSETTI ${String(route.presentation || 'sheet').toUpperCase()}`;
  overlayTitle.textContent = route.label || labelForDestination(workspaceDestination);
  overlayDescription.textContent = route.targetsModuleView
    ? `Overlay content is hosted from module view ${route.viewId}.`
    : `Overlay content is routing to destination ${workspaceDestination}.`;
  overlayWorkspaceButton.hidden = !workspaceDestination;
  surfaceOverlayContent.innerHTML = viewId
    ? renderViewById(viewId)
    : renderUnavailableView('Overlay Unavailable', 'The selected overlay route did not resolve to a browser-hostable view.');

  if (!surfaceOverlayDialog.open) {
    surfaceOverlayDialog.showModal();
  }
}

function renderGuidedWorkflowOverlay() {
  const workflow = state.guidedWorkflow || defaultGuidedWorkflowState();
  const definition = guidedWorkflowDefinition(workflow.id);
  if (!definition) {
    return;
  }

  overlayEyebrow.textContent = definition.eyebrow;
  overlayTitle.textContent = definition.title;
  overlayDescription.textContent = definition.description;
  state.overlayWorkspaceDestination = definition.destinationId || '';
  overlayWorkspaceButton.hidden = !state.overlayWorkspaceDestination;
  surfaceOverlayContent.innerHTML = renderGuidedWorkflowContent();

  if (!surfaceOverlayDialog.open) {
    surfaceOverlayDialog.showModal();
  }
}

function renderShell(options = {}) {
  const preserveDynamicContent = !!options.preserveDynamicContent;
  syncStateSelections();
  // When preserving dynamic content (background refresh), skip re-rendering
  // chrome elements that cause visual flicker and reset scroll/toggle state.
  // Full chrome re-render happens only on explicit navigation or save.
  if (!preserveDynamicContent) {
    renderSurfaceNavigation();
    renderSurfaceToolbar();
    renderSurfaceSummary();
    renderViewChrome();
    renderCurrentContent();
    if (state.guidedWorkflow.id) {
      renderGuidedWorkflowOverlay();
    } else if (state.overlayRouteId) {
      renderOverlayRoute();
    }
  } else {
    // Background refresh: update only the summary badge (data-driven, no flicker).
    renderSurfaceSummary();
  }
}

function loadJson(url, options) {
  return fetch(url, options).then(async (response) => {
    let json = {};
    try {
      json = await response.json();
    } catch {
      json = {};
    }
    if (!response.ok) {
      throw new Error(json.message || 'Request failed');
    }
    return json;
  });
}

function cloneConfig() {
  return JSON.parse(JSON.stringify(state.config || currentConfig()));
}

function coerceInteger(value, fallback) {
  const parsed = Number.parseInt(String(value ?? ''), 10);
  return Number.isFinite(parsed) ? parsed : fallback;
}

function parseTrustedHosts(value) {
  return [...new Set(
    String(value ?? '')
      .split(/\r?\n|,/)
      .map((entry) => entry.trim())
      .filter(Boolean)
  )];
}

function shouldPreserveDynamicContent() {
  const activeElement = document.activeElement;
  if (!activeElement) {
    return false;
  }

  const tagName = activeElement.tagName;
  if (!['INPUT', 'TEXTAREA', 'SELECT'].includes(tagName)) {
    return false;
  }

  return !!(activeElement.closest('#surfaceContentHost') || activeElement.closest('#surfaceOverlayContent'));
}

function closeOverlayDialog(reason = 'cancel') {
  if (surfaceOverlayDialog.open) {
    surfaceOverlayDialog.close(reason);
  }
}

function confirmDangerousChange() {
  if (dangerDialog.open) {
    dangerDialog.close('cancel');
  }

  dangerDialog.showModal();
  return new Promise((resolve) => {
    dangerDialog.addEventListener('close', () => resolve(dangerDialog.returnValue === 'confirm'), { once: true });
  });
}

async function postConfiguration(nextConfig, confirmUnsafeChanges = false) {
  const response = await fetch('/api/config', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'X-Confirm-Unsafe': confirmUnsafeChanges ? '1' : '0'
    },
    body: JSON.stringify(nextConfig)
  });
  const result = await response.json();
  if (!response.ok) {
    throw new Error(result.message || 'Configuration update failed.');
  }
  return result;
}

async function refreshExportsFromApi(showStatus = false) {
  try {
    const artifacts = await loadJson('/api/exports');
    state.exports = safeArray(artifacts);
    if (!state.exports.some((artifact) => artifact.id === state.selectedExportId)) {
      state.selectedExportId = state.exports[0]?.id || '';
    }
    state.exportStatus = showStatus
      ? makeStatus(`Loaded ${state.exports.length} export artifact${state.exports.length === 1 ? '' : 's'} from the local admin API.`, 'success')
      : state.exportStatus;
    renderShell();
  } catch (error) {
    state.exportStatus = makeStatus(error.message, 'error');
    renderShell();
  }
}

function downloadArtifact(artifact) {
  const blob = new Blob([artifact.content], {
    type: artifact.mediaType || 'text/plain;charset=utf-8'
  });
  const objectUrl = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = objectUrl;
  anchor.download = artifact.fileName || `${artifact.id || 'artifact'}.txt`;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  setTimeout(() => URL.revokeObjectURL(objectUrl), 0);
}

function applyProviderDraft(providerId) {
  const provider = dashboardSnapshot().providers.find((candidate) => candidate.id === providerId);
  state.providerDraft = provider ? {
    id: provider.id,
    displayName: provider.displayName,
    baseUrl: provider.baseUrl,
    modelId: provider.modelId || '',
    kind: provider.kind,
    enabled: provider.enabled,
    allowAutonomousControl: provider.allowAutonomousControl
  } : defaultProviderDraft();
  renderShell();
}

function applySubAgentGroupDraft(groupId) {
  const group = dashboardSnapshot().subAgentGroups.find((candidate) => candidate.groupId === groupId);
  state.subAgentGroupDraft = group ? {
    groupId: group.groupId,
    displayName: group.displayName,
    description: group.description || '',
    memberTargetIds: safeArray(group.memberTargetIds)
  } : defaultSubAgentGroupDraft();
  renderShell();
}

function applySubAgentDraft(subAgentId) {
  const endpoint = dashboardSnapshot().endpoints.find((candidate) => candidate.id === subAgentId);
  state.subAgentDraft = endpoint ? {
    id: endpoint.id,
    displayName: endpoint.displayName || '',
    specialization: endpoint.specialization || '',
    host: endpoint.host || '',
    port: safeNumber(endpoint.port, 0),
    protocol: endpoint.protocol || 'virtual',
    routePath: endpoint.routePath || '',
    description: endpoint.description || ''
  } : defaultSubAgentDraft();
  renderShell();
}

function applyMcpServerDraft(mcpServerId) {
  const endpoint = dashboardSnapshot().endpoints.find((candidate) => candidate.id === mcpServerId);
  state.mcpServerDraft = endpoint ? {
    id: endpoint.id,
    displayName: endpoint.displayName || '',
    host: endpoint.host || '',
    port: safeNumber(endpoint.port, 0),
    protocol: endpoint.protocol || 'http',
    routePath: endpoint.routePath || '/mcp',
    description: endpoint.description || ''
  } : defaultMcpServerDraft();
  renderShell();
}

function applyAppleHostDraft(hostId) {
  const host = safeArray(governanceSnapshot().appleRemoteHosts).find((candidate) => candidate.hostId === hostId);
  state.appleHostDraft = host ? {
    hostId: host.hostId,
    displayName: host.displayName || '',
    transport: host.transport || 'companion_service',
    platforms: safeArray(host.platforms),
    address: host.address || '',
    port: safeNumber(host.port, 0),
    username: host.username || '',
    serviceBaseUrl: host.serviceBaseUrl || '',
    companionHealthPath: host.companionHealthPath || '/healthz',
    companionExecutePath: host.companionExecutePath || '/execute',
    preferredDeveloperDirectory: host.preferredDeveloperDirectory || '',
    defaultSigningIdentity: host.defaultSigningIdentity || '',
    defaultNotaryKeychainProfile: host.defaultNotaryKeychainProfile || '',
    defaultNotaryTeamId: host.defaultNotaryTeamId || '',
    enabled: host.enabled ?? true
  } : defaultAppleHostDraft();
  renderShell();
}

async function refreshDashboard(options = {}) {
  const preserveDynamicContent = options.preserveDynamicContent ?? shouldPreserveDynamicContent();
  setHealthBadge('Syncing', 'info');

  try {
    const [dashboard, config] = await Promise.all([
      loadJson('/api/dashboard'),
      loadJson('/api/config')
    ]);

    state.dashboard = dashboard;
    state.config = config;
    state.surface = ensureBootstrapSurface(dashboard.surface || {});
    state.exports = safeArray(dashboard.exports);
    // WS1/WS3 — readiness and environment hints are part of every refresh so
    // both the first-run dispatcher and credential forms see fresh data.
    await Promise.all([loadReadiness(), loadEnvironmentHints()]);
    state.lastRefreshLabel = formatTimestamp(new Date());
    setSurfaceNotice(`Forsetti browser surface synchronized at ${state.lastRefreshLabel}.`, 'success');
    setHealthBadge('Live', 'success');
    renderShell({ preserveDynamicContent });
  } catch (error) {
    console.error(error);
    state.lastRefreshLabel = formatTimestamp(new Date());
    setSurfaceNotice(error.message || 'The local admin API did not respond.', 'error');
    setHealthBadge('Error', 'error');
    renderShell({ preserveDynamicContent: false });
  }
}

async function handleImportSubmission(form, url, payloadBuilder) {
  const button = form.querySelector('button[type="submit"]');
  try {
    button.disabled = true;
    state.importStatus = makeStatus('Running import through the local admin API.', 'info');
    renderShell();
    const result = await loadJson(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payloadBuilder(form))
    });
    state.importStatus = makeStatus(result.message, 'success');
    form.reset();
    await refreshDashboard({ preserveDynamicContent: false });
  } catch (error) {
    state.importStatus = makeStatus(error.message, 'error');
    renderShell();
  } finally {
    button.disabled = false;
  }
}

async function submitSettingsForm(form) {
  try {
    const nextConfig = cloneConfig();
    nextConfig.instanceName = form.elements.instanceName.value;
    nextConfig.bindAddress = form.elements.bindAddress.value;
    nextConfig.browserPort = coerceInteger(form.elements.browserPort.value, currentConfig().browserPort);
    nextConfig.beaconPort = coerceInteger(form.elements.beaconPort.value, currentConfig().beaconPort);
    nextConfig.beaconEnabled = form.elements.beaconEnabled.checked;
    nextConfig.resourceAllocation = nextConfig.resourceAllocation || {};
    nextConfig.resourceAllocation.cpuPercent = coerceInteger(form.elements.cpuPercent.value, currentConfig().resourceAllocation.cpuPercent);
    nextConfig.resourceAllocation.memoryPercent = coerceInteger(form.elements.memoryPercent.value, currentConfig().resourceAllocation.memoryPercent);
    nextConfig.resourceAllocation.bandwidthPercent = coerceInteger(form.elements.bandwidthPercent.value, currentConfig().resourceAllocation.bandwidthPercent);
    nextConfig.resourceAllocation.storagePercent = coerceInteger(form.elements.storagePercent.value, currentConfig().resourceAllocation.storagePercent);

    const result = await postConfiguration(nextConfig, false);
    state.settingsStatus = makeStatus(result.message, 'success');
    await refreshDashboard({ preserveDynamicContent: false });
  } catch (error) {
    state.settingsStatus = makeStatus(error.message, 'error');
    renderShell();
  }
}

async function submitSecurityForm(form) {
  try {
    const nextConfig = cloneConfig();
    nextConfig.security = nextConfig.security || {};
    nextConfig.security.securityProtocolsEnabled = form.elements.securityProtocolsEnabled.checked;
    nextConfig.security.enableTls = form.elements.enableTls.checked;
    nextConfig.security.enableAuthentication = form.elements.enableAuthentication.checked;
    nextConfig.security.allowTroubleshootingBypass = form.elements.allowTroubleshootingBypass.checked;
    nextConfig.security.allowOpenLanAccess = form.elements.allowOpenLanAccess.checked;
    nextConfig.security.trustedRemoteHosts = parseTrustedHosts(form.elements.trustedRemoteHosts.value);

    const disablingProtocols = currentConfig().security.securityProtocolsEnabled && !nextConfig.security.securityProtocolsEnabled;
    const confirmed = disablingProtocols ? await confirmDangerousChange() : false;
    if (disablingProtocols && !confirmed) {
      state.securityStatus = makeStatus('Security protocol disable request was cancelled.', 'warning');
      renderShell();
      return;
    }

    const result = await postConfiguration(nextConfig, confirmed);
    state.securityStatus = makeStatus(result.message, 'success');
    await refreshDashboard({ preserveDynamicContent: false });
  } catch (error) {
    state.securityStatus = makeStatus(error.message, 'error');
    renderShell();
  }
}

async function submitAiAutonomyForm(form) {
  try {
    const nextConfig = cloneConfig();
    nextConfig.aiAutonomyEnabled = form.elements.aiAutonomyEnabled.checked;
    const result = await postConfiguration(nextConfig, false);
    state.providerStatus = makeStatus(result.message, 'success');
    await refreshDashboard({ preserveDynamicContent: false });
  } catch (error) {
    state.providerStatus = makeStatus(error.message, 'error');
    renderShell();
  }
}

async function submitProviderForm(form) {
  try {
    const result = await loadJson('/api/providers', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        id: form.elements.id.value,
        displayName: form.elements.displayName.value,
        baseUrl: form.elements.baseUrl.value,
        modelId: form.elements.modelId.value,
        kind: form.elements.kind.value,
        enabled: form.elements.enabled.checked,
        allowAutonomousControl: form.elements.allowAutonomousControl.checked
      })
    });
    state.providerStatus = makeStatus(result.message, 'success');
    state.providerDraft = defaultProviderDraft();
    await refreshDashboard({ preserveDynamicContent: false });
  } catch (error) {
    state.providerStatus = makeStatus(error.message, 'error');
    renderShell();
  }
}

async function submitProviderCredentialsForm(form) {
  try {
    const values = {};
    for (const element of Array.from(form.elements)) {
      if (!element?.name || !element.name.startsWith('credential:')) {
        continue;
      }
      values[element.name.slice('credential:'.length)] = element.value;
    }

    const result = await loadJson('/api/providers/credentials', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        providerId: state.providerDraft.id,
        values
      })
    });
    state.providerStatus = makeStatus(result.message, 'success');
    await refreshDashboard({ preserveDynamicContent: false });
  } catch (error) {
    state.providerStatus = makeStatus(error.message, 'error');
    renderShell();
  }
}

async function completeGuidedWorkflow({ destinationId, statusBucket, message }) {
  const nextStep = guidedFollowThroughText(destinationId);
  const completionMessage = nextStep ? `${message} Next: ${nextStep}` : message;
  if (statusBucket === 'provider') {
    state.providerStatus = makeStatus(completionMessage, 'success');
  } else if (statusBucket === 'runtime') {
    state.runtimeStatus = makeStatus(completionMessage, 'success');
  } else if (statusBucket === 'clu') {
    state.cluStatus = makeStatus(completionMessage, 'success');
  } else if (statusBucket === 'security') {
    state.securityStatus = makeStatus(completionMessage, 'success');
  } else if (statusBucket === 'settings') {
    state.settingsStatus = makeStatus(completionMessage, 'success');
  } else if (statusBucket === 'import') {
    state.importStatus = makeStatus(completionMessage, 'success');
  }

  if (destinationId) {
    state.currentDestination = destinationId;
  }

  closeOverlayDialog('cancel');
  await refreshDashboard({ preserveDynamicContent: false });
}

// WS2 — primary guided provider flow uses /api/providers/auto-connect (the richer
// orchestration path the shell already uses). The legacy three-call flow
// (/api/providers + /credentials + /assignments) is preserved as the manual
// fallback so Rule 1 (manual setup remains first-class) holds.
async function submitGuidedProviderForm(form) {
  const capability = selectedGuidedProviderCapability();
  if (!capability) {
    state.guidedWorkflow.status = makeStatus('Select a provider module before creating the route.', 'warning');
    renderGuidedWorkflowOverlay();
    return;
  }

  const id = form.elements.id.value.trim();
  const displayName = form.elements.displayName.value.trim();
  const baseUrl = form.elements.baseUrl.value.trim();
  if (!id || !displayName || !baseUrl) {
    state.guidedWorkflow.status = makeStatus('Provider route ID, display name, and base URL are all required.', 'warning');
    renderGuidedWorkflowOverlay();
    return;
  }

  // Collect credentials from credential:* form fields.
  const credentials = {};
  for (const element of Array.from(form.elements)) {
    if (element?.name && element.name.startsWith('credential:') && element.value) {
      credentials[element.name.slice('credential:'.length)] = element.value;
    }
  }
  const targetId = form.elements.targetId.value || '';

  // Read enabled/autonomy from either checkbox (.checked) or hidden field (.value)
  const enabledEl = form.elements.enabled;
  const autonomyEl = form.elements.allowAutonomousControl;
  const enabled = enabledEl?.type === 'checkbox' ? enabledEl.checked : (enabledEl?.value === 'true');
  const allowAutonomousControl = autonomyEl?.type === 'checkbox' ? autonomyEl.checked : (autonomyEl?.value === 'true');

  const payload = {
    providerId: id,  // providerId IS the canonical identity; runtime resolves kind
    credentials,
    displayNameOverride: displayName,
    baseUrlOverride: baseUrl,
    modelIdOverride: form.elements.modelId.value.trim(),
    allowAutonomousControl,
    discoverModels: true,
    assignmentTargetIds: targetId ? [targetId] : []
  };

  // Note template conversion so the user sees what's happening.
  const existingTemplate = dashboardSnapshot().providers.find(
    (p) => p.id === id && p.isTemplate
  );
  state.guidedWorkflow.progress = {
    steps: [],
    status: 'in-progress',
    note: existingTemplate ? 'Converting template into live provider...' : ''
  };
  renderGuidedWorkflowOverlay();

  try {
    const result = await loadJson('/api/providers/auto-connect', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });

    state.guidedWorkflow.progress = {
      steps: result.steps || [],
      status: result.succeeded ? 'done' : 'failed',
      summary: result.summary,
      error: result.errorMessage
    };
    renderGuidedWorkflowOverlay();

    if (!result.succeeded) {
      renderAutoConnectFallback(form, payload, result);
      return;
    }

    await completeGuidedWorkflow({
      destinationId: 'providers',
      statusBucket: 'provider',
      message: result.summary || `Connected '${result.displayName || displayName}'.`
    });
  } catch (error) {
    const friendlyMessage = friendlyProviderError(error.message, displayName);
    state.guidedWorkflow.status = makeStatus(friendlyMessage, 'error');
    renderGuidedWorkflowOverlay();
  }
}

// Auto-connect fallback: when the orchestrated path fails, offer the user
// the legacy manual three-call flow pre-filled with whatever they entered.
// Rule 3 — explain the blocker; Rule 1 — preserve manual mode.
function renderAutoConnectFallback(form, payload, result) {
  const failedStep = (result.steps || []).find((s) => !s.succeeded);
  const stageLabel = failedStep ? failedStep.stage : 'orchestration';
  const stageMessage = failedStep ? failedStep.message : (result.errorMessage || 'Unknown failure.');
  state.guidedWorkflow.fallback = {
    active: true,
    stage: stageLabel,
    message: stageMessage,
    payload
  };
  state.guidedWorkflow.status = makeStatus(
    `Auto-connect failed at ${stageLabel}: ${stageMessage}. You can continue with manual setup.`,
    'warning'
  );
  renderGuidedWorkflowOverlay();
}

// Invoked when the user clicks "Continue with manual setup" on the fallback
// banner. Replays the pre-auto-connect three-call flow against the legacy
// endpoints so manual mode is fully preserved.
async function submitGuidedProviderFormManualFallback(payload) {
  const id = payload.providerId;
  const displayName = payload.displayNameOverride;
  try {
    const providerResult = await loadJson('/api/providers', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        id,
        displayName,
        baseUrl: payload.baseUrlOverride,
        modelId: payload.modelIdOverride,
        // kind is optional; runtime resolves by providerId. Keep empty to avoid
        // regressing to the pre-remediation kind-based lookup path.
        enabled: true,
        allowAutonomousControl: !!payload.allowAutonomousControl
      })
    });
    if (payload.credentials && Object.keys(payload.credentials).length) {
      await loadJson('/api/providers/credentials', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ providerId: id, values: payload.credentials })
      });
    }
    const firstTargetId = (payload.assignmentTargetIds || [])[0];
    if (firstTargetId) {
      const target = dashboardSnapshot().providerAssignmentTargets.find(
        (candidate) => candidate.targetId === firstTargetId
      );
      await loadJson('/api/providers/assignments', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          targetId: firstTargetId,
          kind: target?.kind || 'role',
          providerId: id
        })
      });
    }
    state.guidedWorkflow.fallback = null;
    await completeGuidedWorkflow({
      destinationId: 'providers',
      statusBucket: 'provider',
      message: providerResult.message || `Connected AI model route '${displayName}' via manual setup.`
    });
  } catch (error) {
    state.guidedWorkflow.status = makeStatus(
      friendlyProviderError(error.message, displayName),
      'error'
    );
    renderGuidedWorkflowOverlay();
  }
}

function friendlyProviderError(rawMessage, providerName) {
  const raw = String(rawMessage || '').toLowerCase();
  if (raw.includes('not currently supported')) {
    return `Could not find a provider module for '${providerName}'. Check that the required Forsetti module is installed and enabled.`;
  }
  if (raw.includes('fetch') || raw.includes('network') || raw.includes('timeout')) {
    return `Could not reach the orchestration server. Check that the service is running and your network connection is available.`;
  }
  if (raw.includes('401') || raw.includes('unauthorized') || raw.includes('forbidden')) {
    return `Authentication failed for '${providerName}'. Double-check the API key or credentials you entered.`;
  }
  if (raw.includes('credential') || raw.includes('required')) {
    return rawMessage;
  }
  return rawMessage || `An unexpected error occurred while connecting '${providerName}'.`;
}

async function submitGuidedProviderAssignmentForm(form) {
  const targetSelect = form.elements.targetId;
  const selectedOption = targetSelect.options[targetSelect.selectedIndex];
  try {
    const result = await loadJson('/api/providers/assignments', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        targetId: targetSelect.value,
        kind: selectedOption?.dataset.kind || 'role',
        providerId: form.elements.providerId.value
      })
    });

    await completeGuidedWorkflow({
      destinationId: 'providers',
      statusBucket: 'provider',
      message: result.message || 'Saved provider responsibility mapping.'
    });
  } catch (error) {
    state.guidedWorkflow.status = makeStatus(error.message, 'error');
    renderGuidedWorkflowOverlay();
  }
}

async function submitGuidedMcpServerForm(form) {
  try {
    const result = await loadJson('/api/runtime/mcp-servers', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        id: form.elements.id.value,
        displayName: form.elements.displayName.value,
        kind: 'mcp_server',
        host: form.elements.host.value,
        port: coerceInteger(form.elements.port.value, 0),
        protocol: form.elements.protocol.value || 'http',
        description: form.elements.description.value,
        routePath: form.elements.routePath.value || '/mcp',
        specialization: '',
        userDefined: true
      })
    });

    await completeGuidedWorkflow({
      destinationId: 'runtime',
      statusBucket: 'runtime',
      message: result.message || `Published MCP server lane '${form.elements.displayName.value}'.`
    });
  } catch (error) {
    state.guidedWorkflow.status = makeStatus(error.message, 'error');
    renderGuidedWorkflowOverlay();
  }
}

async function submitGuidedSubAgentForm(form) {
  try {
    const result = await loadJson('/api/runtime/subagents', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        id: form.elements.id.value,
        displayName: form.elements.displayName.value,
        kind: 'sub_agent',
        host: form.elements.host.value,
        port: coerceInteger(form.elements.port.value, 0),
        protocol: form.elements.protocol.value || 'virtual',
        description: form.elements.description.value,
        routePath: form.elements.routePath.value,
        specialization: form.elements.specialization.value,
        userDefined: true
      })
    });

    await completeGuidedWorkflow({
      destinationId: 'runtime',
      statusBucket: 'runtime',
      message: result.message || `Created sub-agent lane '${form.elements.displayName.value}'.`
    });
  } catch (error) {
    state.guidedWorkflow.status = makeStatus(error.message, 'error');
    renderGuidedWorkflowOverlay();
  }
}

async function submitGuidedSubAgentGroupForm(form) {
  try {
    const memberTargetIds = Array.from(form.querySelectorAll('input[name="memberTargetId"]:checked')).map((input) => input.value);
    const result = await loadJson('/api/providers/groups', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        groupId: form.elements.groupId.value,
        displayName: form.elements.displayName.value,
        description: form.elements.description.value,
        memberTargetIds
      })
    });

    await completeGuidedWorkflow({
      destinationId: 'providers',
      statusBucket: 'provider',
      message: result.message || `Created sub-agent group '${form.elements.displayName.value}'.`
    });
  } catch (error) {
    state.guidedWorkflow.status = makeStatus(error.message, 'error');
    renderGuidedWorkflowOverlay();
  }
}

async function submitGuidedAppleHostForm(form) {
  try {
    const platforms = Array.from(form.querySelectorAll('input[name="platform"]:checked')).map((input) => input.value);
    const result = await loadJson('/api/platform-services/apple-hosts', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        hostId: form.elements.hostId.value,
        displayName: form.elements.displayName.value,
        transport: form.elements.transport.value,
        platforms,
        address: form.elements.address.value,
        port: coerceInteger(form.elements.port.value, 0),
        username: form.elements.username.value,
        serviceBaseUrl: form.elements.serviceBaseUrl.value,
        companionHealthPath: '/healthz',
        companionExecutePath: '/execute',
        preferredDeveloperDirectory: form.elements.preferredDeveloperDirectory.value,
        defaultSigningIdentity: form.elements.defaultSigningIdentity.value,
        defaultNotaryKeychainProfile: form.elements.defaultNotaryKeychainProfile.value,
        defaultNotaryTeamId: form.elements.defaultNotaryTeamId.value,
        enabled: form.elements.enabled.checked
      })
    });

    await completeGuidedWorkflow({
      destinationId: 'runtime',
      statusBucket: 'runtime',
      message: result.message || `Registered Apple host '${form.elements.displayName.value}'.`
    });
  } catch (error) {
    state.guidedWorkflow.status = makeStatus(error.message, 'error');
    renderGuidedWorkflowOverlay();
  }
}

async function submitGuidedForsettiModuleForm(form) {
  try {
    const result = await loadJson('/api/forsetti/modules/state', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        moduleId: form.elements.moduleId.value,
        action: form.elements.action.value
      })
    });

    await completeGuidedWorkflow({
      destinationId: 'clu',
      statusBucket: 'clu',
      message: result.message || 'Applied Forsetti module action.'
    });
  } catch (error) {
    state.guidedWorkflow.status = makeStatus(error.message, 'error');
    renderGuidedWorkflowOverlay();
  }
}

async function submitGuidedSecurityForm(form) {
  try {
    const posture = form.elements.posture.value || 'balanced';
    if (posture === 'restricted') {
      form.elements.securityProtocolsEnabled.checked = true;
      form.elements.enableTls.checked = true;
      form.elements.enableAuthentication.checked = true;
      form.elements.allowTroubleshootingBypass.checked = false;
      form.elements.allowOpenLanAccess.checked = false;
    } else if (posture === 'troubleshooting') {
      form.elements.securityProtocolsEnabled.checked = true;
      form.elements.enableTls.checked = false;
      form.elements.enableAuthentication.checked = false;
      form.elements.allowTroubleshootingBypass.checked = true;
      form.elements.allowOpenLanAccess.checked = false;
    }

    const nextConfig = cloneConfig();
    nextConfig.security = nextConfig.security || {};
    nextConfig.security.securityProtocolsEnabled = form.elements.securityProtocolsEnabled.checked;
    nextConfig.security.enableTls = form.elements.enableTls.checked;
    nextConfig.security.enableAuthentication = form.elements.enableAuthentication.checked;
    nextConfig.security.allowTroubleshootingBypass = form.elements.allowTroubleshootingBypass.checked;
    nextConfig.security.allowOpenLanAccess = form.elements.allowOpenLanAccess.checked;
    nextConfig.security.trustedRemoteHosts = parseTrustedHosts(form.elements.trustedRemoteHosts.value);

    const disablingProtocols = currentConfig().security.securityProtocolsEnabled && !nextConfig.security.securityProtocolsEnabled;
    const confirmed = disablingProtocols ? await confirmDangerousChange() : false;
    if (disablingProtocols && !confirmed) {
      state.guidedWorkflow.status = makeStatus('Security protocol disable request was cancelled.', 'warning');
      renderGuidedWorkflowOverlay();
      return;
    }

    const result = await postConfiguration(nextConfig, confirmed);
    await completeGuidedWorkflow({
      destinationId: 'security',
      statusBucket: 'security',
      message: result.message
    });
  } catch (error) {
    state.guidedWorkflow.status = makeStatus(error.message, 'error');
    renderGuidedWorkflowOverlay();
  }
}

async function submitGuidedSettingsForm(form) {
  try {
    const nextConfig = cloneConfig();
    nextConfig.instanceName = form.elements.instanceName.value;
    nextConfig.bindAddress = form.elements.bindAddress.value;
    nextConfig.browserPort = coerceInteger(form.elements.browserPort.value, currentConfig().browserPort);
    nextConfig.beaconPort = coerceInteger(form.elements.beaconPort.value, currentConfig().beaconPort);
    nextConfig.beaconEnabled = form.elements.beaconEnabled.checked;
    nextConfig.resourceAllocation = nextConfig.resourceAllocation || {};
    nextConfig.resourceAllocation.cpuPercent = coerceInteger(form.elements.cpuPercent.value, currentConfig().resourceAllocation.cpuPercent);
    nextConfig.resourceAllocation.memoryPercent = coerceInteger(form.elements.memoryPercent.value, currentConfig().resourceAllocation.memoryPercent);
    nextConfig.resourceAllocation.bandwidthPercent = coerceInteger(form.elements.bandwidthPercent.value, currentConfig().resourceAllocation.bandwidthPercent);
    nextConfig.resourceAllocation.storagePercent = coerceInteger(form.elements.storagePercent.value, currentConfig().resourceAllocation.storagePercent);

    const result = await postConfiguration(nextConfig, false);
    await completeGuidedWorkflow({
      destinationId: 'settings',
      statusBucket: 'settings',
      message: result.message
    });
  } catch (error) {
    state.guidedWorkflow.status = makeStatus(error.message, 'error');
    renderGuidedWorkflowOverlay();
  }
}

async function submitGuidedRuntimeMaintenanceForm(form) {
  const kind = runtimeMaintenanceKind();
  try {
    if (kind === 'subagent') {
      const result = await loadJson('/api/runtime/subagents', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          id: form.elements.id.value,
          displayName: form.elements.displayName.value,
          kind: 'sub_agent',
          host: form.elements.host.value,
          port: coerceInteger(form.elements.port.value, 0),
          protocol: form.elements.protocol.value || 'virtual',
          description: form.elements.description.value,
          routePath: form.elements.routePath.value,
          specialization: form.elements.specialization.value,
          userDefined: true
        })
      });
      await completeGuidedWorkflow({
        destinationId: 'runtime',
        statusBucket: 'runtime',
        message: result.message || `Updated sub-agent lane '${form.elements.displayName.value}'.`
      });
      return;
    }

    if (kind === 'apple') {
      const platforms = Array.from(form.querySelectorAll('input[name="platform"]:checked')).map((input) => input.value);
      const result = await loadJson('/api/platform-services/apple-hosts', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          hostId: form.elements.hostId.value,
          displayName: form.elements.displayName.value,
          transport: form.elements.transport.value,
          platforms,
          address: form.elements.address.value,
          port: coerceInteger(form.elements.port.value, 0),
          username: form.elements.username.value,
          serviceBaseUrl: form.elements.serviceBaseUrl.value,
          companionHealthPath: form.elements.companionHealthPath.value || '/healthz',
          companionExecutePath: form.elements.companionExecutePath.value || '/execute',
          preferredDeveloperDirectory: form.elements.preferredDeveloperDirectory.value,
          defaultSigningIdentity: form.elements.defaultSigningIdentity.value,
          defaultNotaryKeychainProfile: form.elements.defaultNotaryKeychainProfile.value,
          defaultNotaryTeamId: form.elements.defaultNotaryTeamId.value,
          enabled: form.elements.enabled.checked
        })
      });
      await completeGuidedWorkflow({
        destinationId: 'runtime',
        statusBucket: 'runtime',
        message: result.message || `Updated Apple host '${form.elements.displayName.value}'.`
      });
      return;
    }

    const result = await loadJson('/api/runtime/mcp-servers', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        id: form.elements.id.value,
        displayName: form.elements.displayName.value,
        kind: 'mcp_server',
        host: form.elements.host.value,
        port: coerceInteger(form.elements.port.value, 0),
        protocol: form.elements.protocol.value || 'http',
        description: form.elements.description.value,
        routePath: form.elements.routePath.value,
        specialization: '',
        userDefined: true
      })
    });
    await completeGuidedWorkflow({
      destinationId: 'runtime',
      statusBucket: 'runtime',
      message: result.message || `Updated MCP server lane '${form.elements.displayName.value}'.`
    });
  } catch (error) {
    state.guidedWorkflow.status = makeStatus(error.message, 'error');
    renderGuidedWorkflowOverlay();
  }
}

async function removeGuidedRuntimeMaintenance() {
  const kind = runtimeMaintenanceKind();
  try {
    if (kind === 'subagent') {
      if (!state.subAgentDraft.id) {
        throw new Error('Choose a custom sub-agent before removing it.');
      }
      const result = await loadJson('/api/runtime/subagents/remove', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ subAgentId: state.subAgentDraft.id })
      });
      state.subAgentDraft = defaultSubAgentDraft();
      await completeGuidedWorkflow({
        destinationId: 'runtime',
        statusBucket: 'runtime',
        message: result.message || 'Removed the selected sub-agent lane.'
      });
      return;
    }

    if (kind === 'apple') {
      if (!state.appleHostDraft.hostId) {
        throw new Error('Choose an Apple host before removing it.');
      }
      const result = await loadJson('/api/platform-services/apple-hosts/remove', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ hostId: state.appleHostDraft.hostId })
      });
      state.appleHostDraft = defaultAppleHostDraft();
      await completeGuidedWorkflow({
        destinationId: 'runtime',
        statusBucket: 'runtime',
        message: result.message || 'Removed the selected Apple host.'
      });
      return;
    }

    if (!state.mcpServerDraft.id) {
      throw new Error('Choose a custom MCP server before removing it.');
    }
    const result = await loadJson('/api/runtime/mcp-servers/remove', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ mcpServerId: state.mcpServerDraft.id })
    });
    state.mcpServerDraft = defaultMcpServerDraft();
    await completeGuidedWorkflow({
      destinationId: 'runtime',
      statusBucket: 'runtime',
      message: result.message || 'Removed the selected MCP server lane.'
    });
  } catch (error) {
    state.guidedWorkflow.status = makeStatus(error.message, 'error');
    renderGuidedWorkflowOverlay();
  }
}

async function submitGuidedImportForm(url, payload) {
  try {
    const result = await loadJson(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });

    await completeGuidedWorkflow({
      destinationId: 'imports',
      statusBucket: 'import',
      message: result.message || 'Import request submitted successfully.'
    });
  } catch (error) {
    state.guidedWorkflow.status = makeStatus(error.message, 'error');
    renderGuidedWorkflowOverlay();
  }
}

async function submitSubAgentForm(form) {
  try {
    const result = await loadJson('/api/runtime/subagents', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        id: form.elements.id.value,
        displayName: form.elements.displayName.value,
        kind: 'sub_agent',
        host: form.elements.host.value,
        port: coerceInteger(form.elements.port.value, 0),
        protocol: form.elements.protocol.value || 'virtual',
        description: form.elements.description.value,
        routePath: form.elements.routePath.value,
        specialization: form.elements.specialization.value,
        userDefined: true
      })
    });
    state.runtimeStatus = makeStatus(result.message, 'success');
    state.subAgentDraft = defaultSubAgentDraft();
    await refreshDashboard({ preserveDynamicContent: false });
  } catch (error) {
    state.runtimeStatus = makeStatus(error.message, 'error');
    renderShell();
  }
}

async function submitMcpServerForm(form) {
  try {
    const result = await loadJson('/api/runtime/mcp-servers', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        id: form.elements.id.value,
        displayName: form.elements.displayName.value,
        kind: 'mcp_server',
        host: form.elements.host.value,
        port: coerceInteger(form.elements.port.value, 0),
        protocol: form.elements.protocol.value || 'http',
        description: form.elements.description.value,
        routePath: form.elements.routePath.value,
        specialization: '',
        userDefined: true
      })
    });
    state.runtimeStatus = makeStatus(result.message, 'success');
    state.mcpServerDraft = defaultMcpServerDraft();
    await refreshDashboard({ preserveDynamicContent: false });
  } catch (error) {
    state.runtimeStatus = makeStatus(error.message, 'error');
    renderShell();
  }
}

async function submitAppleHostForm(form) {
  try {
    const platforms = Array.from(form.querySelectorAll('input[name="platform"]:checked')).map((input) => input.value);
    const result = await loadJson('/api/platform-services/apple-hosts', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        hostId: form.elements.hostId.value,
        displayName: form.elements.displayName.value,
        transport: form.elements.transport.value,
        platforms,
        address: form.elements.address.value,
        port: coerceInteger(form.elements.port.value, 0),
        username: form.elements.username.value,
        serviceBaseUrl: form.elements.serviceBaseUrl.value,
        companionHealthPath: form.elements.companionHealthPath.value || '/healthz',
        companionExecutePath: form.elements.companionExecutePath.value || '/execute',
        preferredDeveloperDirectory: form.elements.preferredDeveloperDirectory.value,
        defaultSigningIdentity: form.elements.defaultSigningIdentity.value,
        defaultNotaryKeychainProfile: form.elements.defaultNotaryKeychainProfile.value,
        defaultNotaryTeamId: form.elements.defaultNotaryTeamId.value,
        enabled: form.elements.enabled.checked
      })
    });
    state.runtimeStatus = makeStatus(result.message, 'success');
    state.appleHostDraft = defaultAppleHostDraft();
    await refreshDashboard({ preserveDynamicContent: false });
  } catch (error) {
    state.runtimeStatus = makeStatus(error.message, 'error');
    renderShell();
  }
}

async function rerunAppleOperation(operationId) {
  const operation = safeArray(governanceSnapshot().appleOperations).find((candidate) => candidate.operationId === operationId);
  if (!operation) {
    state.cluStatus = makeStatus('Select a recorded Apple operation before replaying it.', 'warning');
    renderShell();
    return;
  }
  if (!operation.rerunReady) {
    state.cluStatus = makeStatus(
      operation.rerunReadinessMessage || 'This Apple operation is not ready for a safe rerun yet.',
      'warning'
    );
    renderShell();
    return;
  }

  const options = { ...(operation.requestOptions || {}) };
  if (operation.hostId && !options.hostId) {
    options.hostId = operation.hostId;
  }

  try {
    const result = await loadJson('/api/clu/execute', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        platform: operation.platform,
        toolId: operation.toolId,
        targetPath: operation.targetPath || '',
        options
      })
    });
    state.cluStatus = makeStatus(result.summary || result.message || 'Apple operation replay completed.', 'success');
    await refreshDashboard({ preserveDynamicContent: false });
  } catch (error) {
    state.cluStatus = makeStatus(error.message, 'error');
    renderShell();
  }
}

async function rerunAppleOperationsBatch(operations) {
  const candidates = safeArray(operations).filter((operation) => operation?.rerunReady);
  if (!candidates.length) {
    state.cluStatus = makeStatus('No Apple operations are ready for a safe bulk replay.', 'warning');
    renderShell();
    return;
  }

  let completed = 0;
  for (const operation of candidates) {
    const options = { ...(operation.requestOptions || {}) };
    if (operation.hostId && !options.hostId) {
      options.hostId = operation.hostId;
    }

    try {
      await loadJson('/api/clu/execute', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          platform: operation.platform,
          toolId: operation.toolId,
          targetPath: operation.targetPath || '',
          options
        })
      });
      completed += 1;
    } catch (error) {
      state.cluStatus = makeStatus(`Bulk replay stopped on ${operation.displayName || operation.toolId || 'Apple operation'}: ${error.message}`, 'error');
      renderShell();
      return;
    }
  }

  state.cluStatus = makeStatus(`Replayed ${completed} Apple attention operation${completed === 1 ? '' : 's'}.`, 'success');
  await refreshDashboard({ preserveDynamicContent: false });
}

async function submitSubAgentGroupForm(form) {
  try {
    const memberTargetIds = Array.from(form.querySelectorAll('input[name="memberTargetId"]:checked')).map((input) => input.value);
    const result = await loadJson('/api/providers/groups', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        groupId: form.elements.groupId.value,
        displayName: form.elements.displayName.value,
        description: form.elements.description.value,
        memberTargetIds
      })
    });
    state.providerStatus = makeStatus(result.message, 'success');
    state.subAgentGroupDraft = defaultSubAgentGroupDraft();
    await refreshDashboard({ preserveDynamicContent: false });
  } catch (error) {
    state.providerStatus = makeStatus(error.message, 'error');
    renderShell();
  }
}

async function submitProviderAssignmentForm(form) {
  try {
    const targetSelect = form.elements.targetId;
    const selectedOption = targetSelect.options[targetSelect.selectedIndex];
    const result = await loadJson('/api/providers/assignments', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        targetId: targetSelect.value,
        kind: selectedOption?.dataset.kind || 'role',
        providerId: form.elements.providerId.value
      })
    });
    state.providerStatus = makeStatus(result.message, 'success');
    await refreshDashboard({ preserveDynamicContent: false });
  } catch (error) {
    state.providerStatus = makeStatus(error.message, 'error');
    renderShell();
  }
}

async function submitProviderExecutionForm(form) {
  state.providerExecutionDraft = {
    targetId: form.elements.targetId.value,
    prompt: form.elements.prompt.value,
    allowToolAccess: form.elements.allowToolAccess.checked,
    maxTurns: coerceInteger(form.elements.maxTurns.value, 4)
  };

  try {
    const response = await fetch('/api/providers/execute', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        targetId: state.providerExecutionDraft.targetId,
        prompt: state.providerExecutionDraft.prompt,
        allowToolAccess: state.providerExecutionDraft.allowToolAccess,
        maxTurns: state.providerExecutionDraft.maxTurns
      })
    });
    const record = await response.json().catch(() => ({}));
    if (!response.ok) {
      throw new Error(record.errorMessage || record.message || 'Provider execution failed.');
    }

    const summary = record.outputText || record.rawResponse || 'Provider task completed.';
    state.providerStatus = makeStatus(summary, 'success');
    await refreshDashboard({ preserveDynamicContent: false });
  } catch (error) {
    state.providerStatus = makeStatus(error.message, 'error');
    renderShell();
  }
}

async function submitGuidedProviderExecutionForm(form) {
  const prompt = form.elements.prompt.value.trim() || form.elements.templatePrompt.value.trim();
  state.providerExecutionDraft = {
    targetId: form.elements.targetId.value,
    prompt,
    allowToolAccess: form.elements.allowToolAccess.checked,
    maxTurns: coerceInteger(form.elements.maxTurns.value, 4)
  };

  if (!state.providerExecutionDraft.targetId || !state.providerExecutionDraft.prompt) {
    state.guidedWorkflow.status = makeStatus('Choose a lane and prompt before running guided provider validation.', 'warning');
    renderGuidedWorkflowOverlay();
    return;
  }

  try {
    const response = await fetch('/api/providers/execute', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        targetId: state.providerExecutionDraft.targetId,
        prompt: state.providerExecutionDraft.prompt,
        allowToolAccess: state.providerExecutionDraft.allowToolAccess,
        maxTurns: state.providerExecutionDraft.maxTurns
      })
    });
    const record = await response.json().catch(() => ({}));
    if (!response.ok) {
      throw new Error(record.errorMessage || record.message || 'Provider execution failed.');
    }

    const summary = record.outputText || record.rawResponse || 'Provider task completed.';
    await completeGuidedWorkflow({
      destinationId: 'providers',
      statusBucket: 'provider',
      message: summary
    });
  } catch (error) {
    state.guidedWorkflow.status = makeStatus(error.message, 'error');
    renderGuidedWorkflowOverlay();
  }
}

function handleSurfaceClick(event) {
  const destinationButton = event.target.closest('[data-destination]');
  if (destinationButton) {
    state.currentDestination = destinationButton.dataset.destination;
    closeOverlayDialog('cancel');
    renderShell();
    return;
  }

  const overlayButton = event.target.closest('[data-open-overlay]');
  if (overlayButton) {
    openOverlayRoute(overlayButton.dataset.openOverlay);
    return;
  }

  const workflowButton = event.target.closest('[data-workflow-id]');
  if (workflowButton && workflowButton.dataset.action === 'open-guided-workflow') {
    openGuidedWorkflow(workflowButton.dataset.workflowId);
    return;
  }

  const toolbarButton = event.target.closest('[data-toolbar-id]');
  if (toolbarButton) {
    const item = state.surface.toolbarItems.find((candidate) => candidate.id === toolbarButton.dataset.toolbarId);
    if (!item) {
      return;
    }

    if (item.actionKind === 'navigate') {
      state.currentDestination = item.targetId || 'overview';
      closeOverlayDialog('cancel');
      renderShell();
      return;
    }

    if (item.actionKind === 'openOverlay') {
      openOverlayRoute(item.targetId);
      return;
    }

    setSurfaceNotice(`Forsetti event publication is not yet exposed through the browser host: ${item.targetId}.`, 'warning');
    renderSurfaceSummary();
    return;
  }

  const importModeButton = event.target.closest('[data-import-mode]');
  if (importModeButton) {
    state.importMode = importModeButton.dataset.importMode;
    renderShell();
    return;
  }

  const exportCard = event.target.closest('[data-export-id]');
  if (exportCard) {
    state.selectedExportId = exportCard.dataset.exportId;
    renderShell();
    return;
  }

  const providerCard = event.target.closest('[data-provider-id]');
  if (providerCard) {
    applyProviderDraft(providerCard.dataset.providerId);
    return;
  }

  const subAgentGroupCard = event.target.closest('[data-subagent-group-id]');
  if (subAgentGroupCard) {
    applySubAgentGroupDraft(subAgentGroupCard.dataset.subagentGroupId);
    return;
  }

  const subAgentCard = event.target.closest('[data-subagent-id]');
  if (subAgentCard) {
    applySubAgentDraft(subAgentCard.dataset.subagentId);
    return;
  }

  const mcpServerCard = event.target.closest('[data-mcp-server-id]');
  if (mcpServerCard) {
    applyMcpServerDraft(mcpServerCard.dataset.mcpServerId);
    return;
  }

  const appleHostCard = event.target.closest('[data-apple-host-id]');
  if (appleHostCard) {
    applyAppleHostDraft(appleHostCard.dataset.appleHostId);
    return;
  }

  const actionButton = event.target.closest('[data-action]');
  if (!actionButton) {
    return;
  }

  const action = actionButton.dataset.action;
  if (action === 'open-guided-workflow') {
    openGuidedWorkflow(actionButton.dataset.workflowId);
    return;
  }
  if (action === 'close-guided-workflow') {
    closeOverlayDialog('cancel');
    return;
  }
  if (action === 'refresh-guided-module-catalog') {
    refreshGuidedForsettiModuleCatalog();
    return;
  }
  if (action === 'set-guided-import-mode') {
    state.guidedWorkflow.importMode = actionButton.dataset.guidedImportMode || 'package';
    renderGuidedWorkflowOverlay();
    return;
  }
  if (action === 'guided-runtime-maintenance-remove') {
    removeGuidedRuntimeMaintenance();
    return;
  }
  if (action === 'reset-provider-draft') {
    state.providerDraft = defaultProviderDraft();
    renderShell();
    return;
  }
  if (action === 'continue-manual-fallback') {
    const fallback = state.guidedWorkflow.fallback;
    if (fallback && fallback.payload) {
      submitGuidedProviderFormManualFallback(fallback.payload);
    }
    return;
  }
  if (action === 'clear-manual-fallback') {
    state.guidedWorkflow.fallback = null;
    state.guidedWorkflow.progress = null;
    renderGuidedWorkflowOverlay();
    return;
  }
  // WS1 — first-run mode selection
  if (action === 'first-run-choose-guided') {
    state.wizard = { ...defaultWizardState(), mode: 'guided', stepIndex: 0, dismissed: false };
    persistWizardState();
    fetch('/api/setup/start', { method: 'POST' }).catch(() => {});
    renderCurrentContent();
    return;
  }
  if (action === 'first-run-choose-manual') {
    state.wizard = { ...defaultWizardState(), mode: 'manual', dismissed: true };
    persistWizardState();
    state.currentDestination = 'overview';
    fetch('/api/setup/start', { method: 'POST' })
      .then(() => refreshDashboard({ preserveDynamicContent: false }))
      .catch(() => renderCurrentContent());
    return;
  }
  if (action === 'first-run-choose-import') {
    state.wizard = { ...defaultWizardState(), mode: 'import', dismissed: false };
    persistWizardState();
    fetch('/api/setup/start', { method: 'POST' }).catch(() => {});
    renderCurrentContent();
    return;
  }
  // WS1 — guided wizard navigation
  if (action === 'wizard-back') {
    if (state.wizard.stepIndex > 0) {
      state.wizard.stepIndex--;
      persistWizardState();
      renderCurrentContent();
    }
    return;
  }
  if (action === 'wizard-skip' || action === 'wizard-next') {
    const isLast = state.wizard.stepIndex === state.wizard.steps.length - 1;
    if (isLast && action === 'wizard-next') {
      // Complete setup on the last step's Next.
      markSetupCompleteAndRefresh();
      return;
    }
    state.wizard.stepIndex = Math.min(state.wizard.stepIndex + 1, state.wizard.steps.length - 1);
    persistWizardState();
    renderCurrentContent();
    return;
  }
  if (action === 'wizard-exit-manual') {
    state.wizard.mode = 'manual';
    state.wizard.dismissed = true;
    persistWizardState();
    state.currentDestination = 'overview';
    refreshDashboard({ preserveDynamicContent: false });
    return;
  }
  if (action === 'go-setup-readiness') {
    state.currentDestination = 'setup-readiness';
    renderShell();
    return;
  }
  if (action === 'mark-setup-complete') {
    markSetupCompleteAndRefresh();
    return;
  }
  if (action === 'setup-reset') {
    fetch('/api/setup/reset', { method: 'POST' })
      .then(() => {
        state.wizard = defaultWizardState();
        persistWizardState();
        refreshDashboard({ preserveDynamicContent: false });
      })
      .catch(() => {});
    return;
  }
  // WS6 — starter workflow actions
  if (action === 'select-starter-template') {
    const id = actionButton.dataset.templateId || '';
    state.wizard.stepState.workflow = { ...(state.wizard.stepState.workflow || {}), selectedTemplateId: id };
    persistWizardState();
    renderCurrentContent();
    return;
  }
  if (action === 'instantiate-starter-template') {
    const id = state.wizard.stepState.workflow?.selectedTemplateId;
    if (!id) { return; }
    fetch(`/api/setup/workflow-templates/${encodeURIComponent(id)}/instantiate`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ displayNameOverride: '' })
    })
      .then((r) => r.json())
      .then((result) => {
        state.wizard.stepState.workflow = { ...(state.wizard.stepState.workflow || {}), lastResult: result };
        persistWizardState();
        return refreshDashboard({ preserveDynamicContent: false });
      })
      .catch(() => {});
    return;
  }
  // WS6 — readiness "Fix now" buttons
  if (action === 'readiness-fix-providers') {
    state.wizard.mode = state.wizard.mode || 'guided';
    state.wizard.stepIndex = 2;  // providers
    persistWizardState();
    state.currentDestination = 'setup';
    renderShell();
    return;
  }
  if (action === 'readiness-fix-mcp') {
    state.wizard.mode = state.wizard.mode || 'guided';
    state.wizard.stepIndex = 3;
    persistWizardState();
    state.currentDestination = 'setup';
    renderShell();
    return;
  }
  if (action === 'readiness-fix-specialist') {
    state.wizard.mode = state.wizard.mode || 'guided';
    state.wizard.stepIndex = 4;
    persistWizardState();
    state.currentDestination = 'setup';
    renderShell();
    return;
  }
  if (action === 'readiness-fix-workflow') {
    state.wizard.mode = state.wizard.mode || 'guided';
    state.wizard.stepIndex = 5;
    persistWizardState();
    state.currentDestination = 'setup';
    renderShell();
    return;
  }
  // WS4 — dependency install trigger
  if (action === 'install-dependency') {
    const id = actionButton.dataset.dependencyId;
    if (!id) { return; }
    const entry = state.setupDependencies[id] || {};
    entry.installState = 'installing';
    state.setupDependencies[id] = entry;
    renderCurrentContent();
    fetch(`/api/setup/dependencies/${encodeURIComponent(id)}/install`, { method: 'POST' })
      .then((r) => r.json())
      .then((result) => {
        entry.installState = result.finalState || 'failed';
        entry.lastInstallResult = result;
        if (result.postInstallDetection) {
          entry.detection = result.postInstallDetection;
        }
        return loadSetupDependencies();
      })
      .then(() => refreshDashboard({ preserveDynamicContent: false }))
      .catch(() => {
        entry.installState = 'failed';
        entry.lastInstallResult = { summary: 'Install request failed (network or server error).' };
        renderCurrentContent();
      });
    return;
  }
  if (action === 'reset-mcp-server-draft') {
    state.mcpServerDraft = defaultMcpServerDraft();
    renderShell();
    return;
  }
  if (action === 'reset-apple-host-draft') {
    state.appleHostDraft = defaultAppleHostDraft();
    renderShell();
    return;
  }
  if (action === 'remove-mcp-server') {
    if (!state.mcpServerDraft.id) {
      state.runtimeStatus = makeStatus('Select a custom MCP server before removing it.', 'warning');
      renderShell();
      return;
    }
    loadJson('/api/runtime/mcp-servers/remove', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ mcpServerId: state.mcpServerDraft.id })
    }).then(async (result) => {
      state.runtimeStatus = makeStatus(result.message, 'success');
      state.mcpServerDraft = defaultMcpServerDraft();
      await refreshDashboard({ preserveDynamicContent: false });
    }).catch((error) => {
      state.runtimeStatus = makeStatus(error.message, 'error');
      renderShell();
    });
    return;
  }
  if (action === 'remove-apple-host') {
    if (!state.appleHostDraft.hostId) {
      state.runtimeStatus = makeStatus('Select an Apple host before removing it.', 'warning');
      renderShell();
      return;
    }
    loadJson('/api/platform-services/apple-hosts/remove', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ hostId: state.appleHostDraft.hostId })
    }).then(async (result) => {
      state.runtimeStatus = makeStatus(result.message, 'success');
      state.appleHostDraft = defaultAppleHostDraft();
      await refreshDashboard({ preserveDynamicContent: false });
    }).catch((error) => {
      state.runtimeStatus = makeStatus(error.message, 'error');
      renderShell();
    });
    return;
  }
  if (action === 'reset-subagent-draft') {
    state.subAgentDraft = defaultSubAgentDraft();
    renderShell();
    return;
  }
  if (action === 'remove-subagent') {
    if (!state.subAgentDraft.id) {
      state.runtimeStatus = makeStatus('Select a custom sub-agent before removing it.', 'warning');
      renderShell();
      return;
    }
    loadJson('/api/runtime/subagents/remove', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ subAgentId: state.subAgentDraft.id })
    }).then(async (result) => {
      state.runtimeStatus = makeStatus(result.message, 'success');
      state.subAgentDraft = defaultSubAgentDraft();
      await refreshDashboard({ preserveDynamicContent: false });
    }).catch((error) => {
      state.runtimeStatus = makeStatus(error.message, 'error');
      renderShell();
    });
    return;
  }
  if (action === 'reset-subagent-group-draft') {
    state.subAgentGroupDraft = defaultSubAgentGroupDraft();
    renderShell();
    return;
  }
  if (action === 'remove-subagent-group') {
    if (!state.subAgentGroupDraft.groupId) {
      state.providerStatus = makeStatus('Select a sub-agent group before removing it.', 'warning');
      renderShell();
      return;
    }
    loadJson('/api/providers/groups/remove', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ groupId: state.subAgentGroupDraft.groupId })
    }).then(async (result) => {
      state.providerStatus = makeStatus(result.message, 'success');
      state.subAgentGroupDraft = defaultSubAgentGroupDraft();
      await refreshDashboard({ preserveDynamicContent: false });
    }).catch((error) => {
      state.providerStatus = makeStatus(error.message, 'error');
      renderShell();
    });
    return;
  }
  if (action === 'refresh-exports') {
    refreshExportsFromApi(true);
    return;
  }
  if (action === 'download-selected-export') {
    const artifact = currentExport();
    if (!artifact) {
      state.exportStatus = makeStatus('Select an export artifact before downloading it.', 'warning');
      renderShell();
      return;
    }
    downloadArtifact(artifact);
    state.exportStatus = makeStatus(`Downloaded ${artifact.fileName}.`, 'success');
    renderShell();
    return;
  }
  if (action === 'download-all-exports') {
    if (!state.exports.length) {
      state.exportStatus = makeStatus('No export artifacts are available yet.', 'warning');
      renderShell();
      return;
    }
    for (const artifact of state.exports) {
      downloadArtifact(artifact);
    }
    state.exportStatus = makeStatus(`Downloaded ${state.exports.length} export artifact${state.exports.length === 1 ? '' : 's'}.`, 'success');
    renderShell();
    return;
  }
  if (action === 'set-apple-operation-filter') {
    state.cluAppleOperationFilter = actionButton.dataset.appleOperationFilter || 'attention';
    renderShell();
    return;
  }
  if (action === 'retry-apple-attention') {
    const appleOperations = safeArray(governanceSnapshot().appleOperations);
    const attentionOperations = appleOperations.filter((operation) => isAttentionAppleOperation(operation) && operation?.rerunReady);
    rerunAppleOperationsBatch(attentionOperations);
    return;
  }
  if (action === 'rerun-apple-operation') {
    rerunAppleOperation(actionButton.dataset.appleOperationId);
    return;
  }
}

function handleDynamicChange(event) {
  if (event.target.dataset?.action === 'toggle-advanced-mode') {
    const enabled = event.target.checked;
    loadJson('/api/settings/advanced-mode', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ enabled })
    }).then(() => {
      state.config.advancedMode = enabled;
      renderView();
    }).catch(() => {});
    return;
  }
  const guidedField = event.target.closest('[data-guided-field]');
  if (guidedField) {
    const field = guidedField.dataset.guidedField;
    if (field === 'providerCapabilityId') {
      state.guidedWorkflow.providerCapabilityId = guidedField.value || '';
      renderGuidedWorkflowOverlay();
      return;
    }
    if (field === 'moduleId') {
      state.guidedWorkflow.moduleId = guidedField.value || '';
      state.guidedWorkflow.moduleAction = recommendedForsettiAction(selectedGuidedForsettiModule());
      renderGuidedWorkflowOverlay();
      return;
    }
    if (field === 'providerExecutionTemplatePrompt') {
      state.providerExecutionDraft.prompt = guidedField.value || '';
      renderGuidedWorkflowOverlay();
      return;
    }
    if (field === 'runtimeMaintenanceKind') {
      state.guidedWorkflow.runtimeMaintenanceKind = guidedField.value || 'mcp';
      primeRuntimeMaintenanceDraft(state.guidedWorkflow.runtimeMaintenanceKind);
      renderGuidedWorkflowOverlay();
      return;
    }
    if (field === 'runtimeMaintenanceTarget') {
      const kind = runtimeMaintenanceKind();
      if (kind === 'subagent') {
        applySubAgentDraft(guidedField.value || '');
      } else if (kind === 'apple') {
        applyAppleHostDraft(guidedField.value || '');
      } else {
        applyMcpServerDraft(guidedField.value || '');
      }
      renderGuidedWorkflowOverlay();
      return;
    }
  }

  const exportSelector = event.target.closest('[data-role="export-selector"]');
  if (exportSelector) {
    state.selectedExportId = exportSelector.value;
    renderShell();
  }
}

async function handleFormSubmit(event) {
  const form = event.target.closest('form[data-form-kind]');
  if (!form) {
    return;
  }

  event.preventDefault();
  const kind = form.dataset.formKind;

  if (kind === 'settings') {
    await submitSettingsForm(form);
    return;
  }
  if (kind === 'security') {
    await submitSecurityForm(form);
    return;
  }
  if (kind === 'guided-security') {
    await submitGuidedSecurityForm(form);
    return;
  }
  if (kind === 'ai-autonomy') {
    await submitAiAutonomyForm(form);
    return;
  }
  if (kind === 'provider') {
    await submitProviderForm(form);
    return;
  }
  if (kind === 'provider-credentials') {
    await submitProviderCredentialsForm(form);
    return;
  }
  if (kind === 'guided-provider') {
    await submitGuidedProviderForm(form);
    return;
  }
  if (kind === 'guided-provider-assignment') {
    await submitGuidedProviderAssignmentForm(form);
    return;
  }
  if (kind === 'guided-provider-execution') {
    await submitGuidedProviderExecutionForm(form);
    return;
  }
  if (kind === 'guided-runtime-maintenance') {
    await submitGuidedRuntimeMaintenanceForm(form);
    return;
  }
  if (kind === 'mcp-server') {
    await submitMcpServerForm(form);
    return;
  }
  if (kind === 'guided-mcp-server') {
    await submitGuidedMcpServerForm(form);
    return;
  }
  if (kind === 'apple-host') {
    await submitAppleHostForm(form);
    return;
  }
  if (kind === 'guided-apple-host') {
    await submitGuidedAppleHostForm(form);
    return;
  }
  if (kind === 'subagent') {
    await submitSubAgentForm(form);
    return;
  }
  if (kind === 'guided-subagent') {
    await submitGuidedSubAgentForm(form);
    return;
  }
  if (kind === 'subagent-group') {
    await submitSubAgentGroupForm(form);
    return;
  }
  if (kind === 'guided-subagent-group') {
    await submitGuidedSubAgentGroupForm(form);
    return;
  }
  if (kind === 'provider-assignment') {
    await submitProviderAssignmentForm(form);
    return;
  }
  if (kind === 'guided-forsetti-module') {
    await submitGuidedForsettiModuleForm(form);
    return;
  }
  if (kind === 'guided-settings') {
    await submitGuidedSettingsForm(form);
    return;
  }
  if (kind === 'provider-execution') {
    await submitProviderExecutionForm(form);
    return;
  }
  if (kind === 'package-install') {
    await handleImportSubmission(form, '/api/install/package', (currentForm) => ({
      source: currentForm.elements.source.value,
      localPath: '',
      kind: currentForm.elements.kind.value,
      arguments: currentForm.elements.arguments.value,
      allowUntrustedExecution: currentForm.elements.allowUntrustedExecution.checked
    }));
    return;
  }
  if (kind === 'repo-install') {
    await handleImportSubmission(form, '/api/install/repo', (currentForm) => ({
      repositoryUrl: currentForm.elements.repositoryUrl.value,
      branch: currentForm.elements.branch.value || 'main',
      manifestFile: currentForm.elements.manifestFile.value || 'mcp-bootstrap.json',
      allowUntrustedExecution: currentForm.elements.allowUntrustedExecution.checked
    }));
    return;
  }
  if (kind === 'zip-install') {
    await handleImportSubmission(form, '/api/install/zip', (currentForm) => ({
      source: currentForm.elements.source.value,
      manifestFile: currentForm.elements.manifestFile.value || 'mcp-bootstrap.json',
      allowUntrustedExecution: currentForm.elements.allowUntrustedExecution.checked
    }));
    return;
  }
  if (kind === 'guided-import-package') {
    await submitGuidedImportForm('/api/install/package', {
      source: form.elements.source.value,
      localPath: '',
      kind: form.elements.kind.value,
      arguments: form.elements.arguments.value,
      allowUntrustedExecution: form.elements.allowUntrustedExecution.checked
    });
    return;
  }
  if (kind === 'guided-import-repo') {
    await submitGuidedImportForm('/api/install/repo', {
      repositoryUrl: form.elements.repositoryUrl.value,
      branch: form.elements.branch.value || 'main',
      manifestFile: form.elements.manifestFile.value || 'mcp-bootstrap.json',
      allowUntrustedExecution: form.elements.allowUntrustedExecution.checked
    });
    return;
  }
  if (kind === 'guided-import-zip') {
    await submitGuidedImportForm('/api/install/zip', {
      source: form.elements.source.value,
      manifestFile: form.elements.manifestFile.value || 'mcp-bootstrap.json',
      allowUntrustedExecution: form.elements.allowUntrustedExecution.checked
    });
    return;
  }
}

function openOverlayRoute(routeId) {
  state.guidedWorkflow = defaultGuidedWorkflowState();
  state.overlayRouteId = routeId;
  renderOverlayRoute();
  renderSurfaceSummary();
}

refreshButton.addEventListener('click', () => refreshDashboard({ preserveDynamicContent: false }));
surfaceNavigation.addEventListener('click', handleSurfaceClick);
surfaceToolbar.addEventListener('click', handleSurfaceClick);
surfaceContentHost.addEventListener('click', handleSurfaceClick);
surfaceOverlayContent.addEventListener('click', handleSurfaceClick);
surfaceContentHost.addEventListener('change', handleDynamicChange);
surfaceOverlayContent.addEventListener('change', handleDynamicChange);
document.addEventListener('submit', handleFormSubmit);

overlayCloseButton.addEventListener('click', () => closeOverlayDialog('cancel'));
overlayWorkspaceButton.addEventListener('click', () => closeOverlayDialog('workspace'));

surfaceOverlayDialog.addEventListener('close', () => {
  const openInWorkspace = surfaceOverlayDialog.returnValue === 'workspace' && state.overlayWorkspaceDestination;
  const destination = state.overlayWorkspaceDestination;
  state.overlayRouteId = '';
  state.guidedWorkflow = defaultGuidedWorkflowState();
  state.overlayWorkspaceDestination = '';
  if (openInWorkspace) {
    state.currentDestination = destination;
    renderShell();
    return;
  }
  renderSurfaceSummary();
});

renderShell();
refreshDashboard({ preserveDynamicContent: false });

// Refresh only when on views that benefit from live data (overview, telemetry,
// runtime). Static views like settings, providers (while editing), security,
// and the setup wizard do NOT need periodic refresh — it causes disruptive
// re-renders that reset UI state. The user triggers refresh explicitly by
// navigating or saving.
const LIVE_DATA_DESTINATIONS = new Set(['overview', 'telemetry', 'runtime']);
setInterval(() => {
  if (!LIVE_DATA_DESTINATIONS.has(state.currentDestination)) {
    return; // Skip refresh for static/editing views.
  }
  refreshDashboard({ preserveDynamicContent: true });
}, 15000); // 15 seconds instead of 5 — less disruptive even on live views.
