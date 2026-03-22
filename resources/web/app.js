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
        const readinessIssues = safeArray(host.readinessIssues);
        return `
          <article class="history-item ${isSelected ? 'selected' : ''}" data-apple-host-id="${escapeHtml(host.hostId || '')}">
            <strong>${escapeHtml(host.displayName || host.hostId || 'Apple host')}</strong>
            <div>${escapeHtml(platformListLabel(host.platforms))} | ${escapeHtml(transportLabel(host.transport))}</div>
            <div>${escapeHtml(appleHostAddressLabel(host))}</div>
            <div>${statusPill(toolchain.status || 'unknown')} ${statusPill(signing.status || 'unknown')}</div>
            <div>${escapeHtml(toolchain.xcodeVersion ? `Xcode ${toolchain.xcodeVersion}` : 'Xcode pending')} | ${escapeHtml(runtimeSummary)}</div>
            <div>${escapeHtml(host.transportSummary || 'Transport summary pending')}</div>
            <div>${escapeHtml(host.credentialProfileSummary || 'No Apple distribution defaults configured')}</div>
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
      ${operations.map((operation) => `
        <article class="history-item">
          <strong>${escapeHtml(operation.displayName || operation.toolId || 'Apple operation')}</strong>
          <div>${statusPill(operation.status || 'queued')}</div>
          <div>${escapeHtml(platformLabel(operation.platform))} | ${escapeHtml(operation.hostDisplayName || operation.hostId || 'unassigned host')} | ${escapeHtml(transportLabel(operation.transport))}</div>
          <div>${escapeHtml(operation.artifactPath || operation.summary || operation.errorMessage || 'No artifact published')}</div>
          ${operation.routeReason ? `<div>${escapeHtml(operation.routeReason)}</div>` : ''}
          ${operation.selectedDeveloperDirectory ? `<div>${escapeHtml(`Developer dir: ${operation.selectedDeveloperDirectory}`)}</div>` : ''}
          ${operation.credentialProfileSummary ? `<div>${escapeHtml(operation.credentialProfileSummary)}</div>` : ''}
          ${safeArray(operation.readinessIssues).length ? `<div>${escapeHtml(`Readiness gaps: ${safeArray(operation.readinessIssues).join('; ')}`)}</div>` : ''}
          ${operation.rerunReadinessMessage ? `<div>${escapeHtml(`Replay: ${operation.rerunReadinessMessage}`)}</div>` : ''}
          ${operation.diagnosticSummary ? `<div>${escapeHtml(operation.diagnosticSummary)}</div>` : ''}
          ${safeArray(operation.redactedRequestOptionKeys).length ? `<div>${escapeHtml('Sensitive request options were redacted from stored history. Rerun may require host defaults or fresh credentials.')}</div>` : ''}
          <div>${escapeHtml(operation.completedAtUtc || operation.startedAtUtc || operation.queuedAtUtc || 'pending')}</div>
          <div class="card-actions">
            <button type="button" class="secondary-button" data-action="rerun-apple-operation" data-apple-operation-id="${escapeHtml(operation.operationId || '')}" ${operation.rerunReady ? '' : 'disabled'}>
              Rerun
            </button>
          </div>
        </article>
      `).join('')}
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
  surfaceNotice: makeStatus('Browser host waiting for the first Forsetti surface snapshot.', 'info'),
  lastRefreshLabel: 'Pending'
};

function currentConfig() {
  return {
    instanceName: state.config?.instanceName || 'Master Control Program',
    bindAddress: state.config?.bindAddress || '0.0.0.0',
    browserPort: safeNumber(state.config?.browserPort, 7300),
    beaconPort: safeNumber(state.config?.beaconPort, 7301),
    beaconEnabled: state.config?.beaconEnabled ?? true,
    aiAutonomyEnabled: state.config?.aiAutonomyEnabled ?? false,
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
      settings: 'Settings'
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
      description: 'Track live CPU, memory, disk, network, and environment discovery data published by the local Forsetti service host.'
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

function selectedProviderCapability() {
  const snapshot = dashboardSnapshot();
  const draft = state.providerDraft;
  const provider = snapshot.providers.find((candidate) => candidate.id === draft.id);
  const kind = provider?.kind || draft.kind;
  return snapshot.providerCapabilities.find((capability) => capability.kind === kind) || null;
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
  surfaceNavigation.innerHTML = state.surface.navigationPointers.map((pointer) => `
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

function renderSurfaceSummary() {
  const snapshot = dashboardSnapshot();
  const config = currentConfig();
  const onlineEndpoints = snapshot.endpoints.filter((endpoint) => String(endpoint.status || '').toLowerCase() === 'online').length;
  const enabledProviders = snapshot.providers.filter((provider) => provider.enabled).length;
  const currentOverlay = state.overlayRouteId
    ? state.surface.overlayRoutes.find((route) => route.id === state.overlayRouteId)?.label || 'Overlay open'
    : 'No overlay open';

  surfaceSummary.innerHTML = `
    <div class="summary-grid">
      ${metricCard('Destination', labelForDestination(state.currentDestination), 'active view')}
      ${metricCard('Endpoints', formatCount(snapshot.endpoints.length), `${onlineEndpoints} online`)}
      ${metricCard('Providers', formatCount(snapshot.providers.length), `${enabledProviders} enabled`)}
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
  const onlineEndpoints = snapshot.endpoints.filter((endpoint) => String(endpoint.status || '').toLowerCase() === 'online').length;
  const enabledProviders = snapshot.providers.filter((provider) => provider.enabled).length;
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
        ${metricCard('Live Endpoints', formatCount(snapshot.endpoints.length), `${onlineEndpoints} online now`)}
        ${metricCard('Providers', formatCount(snapshot.providers.length), `${enabledProviders} enabled`)}
        ${metricCard('Exports', formatCount(state.exports.length), `${snapshot.installHistory.length} recorded installs`)}
        ${metricCard('Host CPU', formatPercent(telemetry.cpuPercent || 0), telemetry.hostName || 'awaiting telemetry')}
      </div>

      <div class="section-actions">
        <button type="button" data-destination="runtime">Open Runtime Lanes</button>
        <button type="button" data-open-overlay="imports-overlay">Launch Import Overlay</button>
        <button type="button" data-open-overlay="exports-overlay">Preview Exports</button>
        <button type="button" data-destination="security">Inspect Security</button>
      </div>

      <div class="split-grid">
        ${narrativePanel('Mission Brief', 'Control Deck', missionBrief)}
        ${narrativePanel('Environment Profile', 'Discovery Snapshot', environmentProfile)}
        ${narrativePanel('Security Posture', 'Trust Envelope', securityPosture)}
      </div>
    </section>
  `;
}

function renderTelemetryView() {
  const config = currentConfig();
  const telemetry = dashboardSnapshot().telemetry;
  return `
    <section class="section-shell">
      <div class="card-grid">
        ${metricCard('CPU', formatPercent(telemetry.cpuPercent || 0), 'host utilization')}
        ${metricCard('Memory', formatPercent(telemetry.memoryPercent || 0), 'resident pressure')}
        ${metricCard('Disk', formatPercent(telemetry.diskPercent || 0), 'storage occupancy')}
        ${metricCard('TX/s', formatCount(telemetry.bytesSentPerSecond || 0), 'bytes per second')}
        ${metricCard('RX/s', formatCount(telemetry.bytesReceivedPerSecond || 0), 'bytes per second')}
        ${metricCard('Beacon MAC', telemetry.primaryMacAddress || 'n/a', telemetry.operatingSystem || 'Windows')}
      </div>

      <div class="split-grid">
        ${narrativePanel('Host Identity', telemetry.hostName || 'Host pending', [
          `Primary IP: ${telemetry.primaryIpAddress || 'n/a'}`,
          `Primary MAC: ${telemetry.primaryMacAddress || 'n/a'}`,
          `Operating system: ${telemetry.operatingSystem || 'Windows'}`,
          `Captured: ${telemetry.capturedAtUtc || 'pending'}`
        ].join('\n'))}
        ${narrativePanel('Environment Profile', config.activeProfile.environmentName, [
          `Preferred bind address: ${config.activeProfile.preferredBindAddress}`,
          `Configured bind address: ${config.bindAddress}`,
          `Browser port: ${config.browserPort}`,
          `Beacon port: ${config.beaconPort}`
        ].join('\n'))}
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
            <tr>
              <td>${escapeHtml(endpoint.displayName)}</td>
              <td>${escapeHtml(endpoint.kind)}</td>
              <td>${escapeHtml(endpoint.host)}</td>
              <td>${escapeHtml(endpoint.port)}</td>
              <td>${statusPill(endpoint.status)}</td>
              <td>${endpoint.userDefined ? 'Custom' : 'Managed'}</td>
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
        ${metricCard('Gateways', formatCount(gateways.length), 'platform broadcasts')}
        ${metricCard('Gov Servers', formatCount(governanceServers.length), `${recentExecutions.length} recent executions`)}
      </div>

      ${statusMessage(state.cluStatus)}

      <div class="split-grid">
        ${narrativePanel('Doctrine', governance.unitName || 'Command Logic Unit', governance.doctrine || 'Governance doctrine is waiting for the current CLU profile.')}
        ${narrativePanel('Recommended Actions', 'Operator Queue', actionNarrative)}
      </div>

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
          <strong>${escapeHtml(provider.displayName)}</strong>
          <div>${escapeHtml(provider.baseUrl)}</div>
          <div>${escapeHtml(provider.kind)} | ${provider.enabled ? 'enabled' : 'disabled'} | autonomy ${provider.allowAutonomousControl ? 'on' : 'off'} | credentials ${provider.credentialsConfigured ? 'ready' : 'missing'}</div>
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
      ${credentialFields.length ? credentialFields.slice(0, 2).map((field) => `
        <label>${escapeHtml(field.label)}
          <input
            name="credential:${escapeHtml(field.fieldId)}"
            type="password"
            placeholder="${escapeHtml(field.placeholder || 'Credential value')}">
        </label>
        <p class="form-help">${escapeHtml([field.helpText, field.environmentVariableHint ? `Env: ${field.environmentVariableHint}` : ''].filter(Boolean).join(' '))}</p>
      `).join('') : '<p class="narrative-copy">The selected provider kind has no published credential requirements.</p>'}
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

      ${statusMessage(state.providerStatus)}

      <div class="split-grid">
        <article class="panel-block">
          <p class="eyebrow">Provider Fleet</p>
          <h3>Registered Adapters</h3>
          ${providersMarkup}
        </article>

        <div class="surface-stack">
          <form class="surface-form panel-block" data-form-kind="ai-autonomy">
            <p class="eyebrow">Autonomy Gate</p>
            <h3>Global AI Control</h3>
            <label class="checkbox-field">
              <input name="aiAutonomyEnabled" type="checkbox"${checkedAttr(config.aiAutonomyEnabled)}>
              <span>Allow AI full autonomy when the user explicitly enables it.</span>
            </label>
            <button type="submit">Save Autonomy Setting</button>
          </form>

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
        </div>
      </div>

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
          <button type="submit">Save Settings</button>
        </form>

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
    default:
      return renderUnavailableView('Unknown Forsetti View', `No browser renderer is registered for ${viewId || 'this destination'}.`);
  }
}

function renderCurrentContent() {
  const viewId = resolvePrimaryViewForDestination(state.currentDestination);
  surfaceContentHost.innerHTML = viewId
    ? renderViewById(viewId)
    : renderUnavailableView('Forsetti View Unavailable', 'The selected destination did not publish a usable view injection.');
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

function renderShell(options = {}) {
  const preserveDynamicContent = !!options.preserveDynamicContent;
  syncStateSelections();
  renderSurfaceNavigation();
  renderSurfaceToolbar();
  renderSurfaceSummary();
  renderViewChrome();
  if (!preserveDynamicContent) {
    renderCurrentContent();
    if (state.overlayRouteId) {
      renderOverlayRoute();
    }
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
  if (action === 'reset-provider-draft') {
    state.providerDraft = defaultProviderDraft();
    renderShell();
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
  if (kind === 'mcp-server') {
    await submitMcpServerForm(form);
    return;
  }
  if (kind === 'apple-host') {
    await submitAppleHostForm(form);
    return;
  }
  if (kind === 'subagent') {
    await submitSubAgentForm(form);
    return;
  }
  if (kind === 'subagent-group') {
    await submitSubAgentGroupForm(form);
    return;
  }
  if (kind === 'provider-assignment') {
    await submitProviderAssignmentForm(form);
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
  }
}

function openOverlayRoute(routeId) {
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
setInterval(() => {
  refreshDashboard({ preserveDynamicContent: shouldPreserveDynamicContent() });
}, 5000);
