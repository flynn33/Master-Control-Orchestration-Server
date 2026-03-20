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
    kind: 'generic',
    enabled: true,
    allowAutonomousControl: false
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
  importStatus: makeStatus('Import operations are executed through the local admin API.', 'info'),
  exportStatus: makeStatus('Exports are generated by the service and downloaded directly through the browser.', 'info'),
  providerStatus: makeStatus('Provider changes are posted directly to the local admin API.', 'info'),
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
      description: 'Inspect MCP servers, sub-agents, browser gateway lanes, and installation provenance from the shared runtime map.'
    };
  }
  if (destinationId === 'clu') {
    return {
      eyebrow: 'CLU',
      title,
      description: 'Review the Command Logic Unit governance profile, live posture findings, and operator-visible control doctrine.'
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
    ['openai', 'OpenAI / ChatGPT'],
    ['xai', 'xAI / Grok'],
    ['generic', 'Generic']
  ].map(([value, label]) => `
    <option value="${escapeHtml(value)}"${selectedAttr(value === selectedKind)}>${escapeHtml(label)}</option>
  `).join('');
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
            </tr>
          `).join('')}
        </tbody>
      </table>
    </div>
  ` : emptyState('No runtime endpoints yet', 'Import or configure MCP servers and sub-agents to populate the runtime map.');

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

  return `
    <section class="section-shell">
      <div class="split-grid">
        <article class="panel-block">
          <p class="eyebrow">Runtime Map</p>
          <h3>Endpoints</h3>
          ${endpointRows}
        </article>
        <article class="panel-block">
          <p class="eyebrow">Install Trail</p>
          <h3>Provenance</h3>
          ${installHistory}
        </article>
      </div>
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
          <strong>${escapeHtml(role.displayName || role.roleId || 'Role')}</strong>
          <div>${escapeHtml(role.domain || 'governance')}</div>
          <div>${escapeHtml(formatPreview(safeArray(role.authorities).join(' | '), 180) || 'No published authorities')}</div>
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
          <div>${escapeHtml(rule.severity || 'unspecified')} | ${escapeHtml(rule.enforcement || 'advisory')}</div>
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

  return `
    <section class="section-shell">
      <div class="card-grid">
        ${metricCard('Posture', String(governance.posture || 'pending').toUpperCase(), 'live governance evaluation')}
        ${metricCard('Findings', formatCount(findings.length), governance.lastEvaluatedUtc || 'awaiting evaluation')}
        ${metricCard('Roles', formatCount(roles.length), 'published authorities')}
        ${metricCard('Rules', formatCount(rules.length), `${documents.length} documents`)}
      </div>

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

      <article class="panel-block">
        <p class="eyebrow">Documents</p>
        <h3>Governance Corpus</h3>
        ${documentsMarkup}
      </article>
    </section>
  `;
}

function renderProvidersView() {
  const config = currentConfig();
  const snapshot = dashboardSnapshot();
  const draft = state.providerDraft;
  const providersMarkup = snapshot.providers.length ? `
    <div class="provider-list">
      ${snapshot.providers.map((provider) => `
        <button
          type="button"
          class="provider-card provider-card-button ${draft.id === provider.id ? 'is-selected' : ''}"
          data-provider-id="${escapeHtml(provider.id)}">
          <strong>${escapeHtml(provider.displayName)}</strong>
          <div>${escapeHtml(provider.baseUrl)}</div>
          <div>${escapeHtml(provider.kind)} | ${provider.enabled ? 'enabled' : 'disabled'} | autonomy ${provider.allowAutonomousControl ? 'on' : 'off'}</div>
        </button>
      `).join('')}
    </div>
  ` : emptyState('No providers configured', 'Save a provider connection below to start routing AI services through the dashboard.');

  return `
    <section class="section-shell">
      <div class="card-grid">
        ${metricCard('Configured Providers', formatCount(snapshot.providers.length), 'service registry')}
        ${metricCard('Enabled Providers', formatCount(snapshot.providers.filter((provider) => provider.enabled).length), 'active routes')}
        ${metricCard('Autonomous Providers', formatCount(snapshot.providers.filter((provider) => provider.allowAutonomousControl).length), 'self-configurable')}
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
        </div>
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
    kind: provider.kind,
    enabled: provider.enabled,
    allowAutonomousControl: provider.allowAutonomousControl
  } : defaultProviderDraft();
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
