// Master Control Orchestration Server - browser surface
// Copyright (c) 2026 James Daley. All Rights Reserved.
//
// Tron operations surface for the LAN MCP Gateway, supervised worker pools,
// governance bundles, onboarding profiles, and honest telemetry.
//
// PHASE-09 (ADR-002 §9): the dashboard renders the -1.0 unavailable sentinel
// from PHASE-08 telemetry as the literal string "unavailable". Never as 0%
// or "idle". Use formatMetric() at every numeric render site.
//
// Legacy LAN-client / CLU-approvals / exports flows remain so ADR-001's
// operator surface is preserved. Manual setup paths (host/port/MCP URL for
// hand-pasting, exports for download) stay first-class.

(function() {
'use strict';

// ---------------------------------------------------------------------------
// State + helpers
// ---------------------------------------------------------------------------

const state = {
  destination: 'setup',
  dashboard: null,
  clients: [],
  approvals: [],
  exports: [],
  activity: { events: [], highWaterMarkId: '0' },
  selectedClientId: null,
  health: { status: 'unknown', time: '' },
  errorBanner: '',
  setupState: null,
  setupBypassThisSession: false,
  workflows: null,
  workflowTemplates: null,
  // PHASE-09 gateway-first state
  gatewayStatus: null,
  gatewayHealth: null,
  gatewayTools: [],
  pools: [],
  poolLeases: {},
  poolSaturation: {},
  selectedPoolId: null,
  discovery: null,
  onboardingClientType: 'claude-code',
  onboardingProfile: null,
  governanceBundlePlatform: 'windows',
  governanceBundle: null,
  telemetryEvents: { events: [], maxEvents: 0 },
  telemetryClients: [],
  telemetryGateway: null,
  // Claude Code plugin (mcos-control) registration toggle.
  // Backed by /api/claude-plugin/{status,toggle}. The plugin lives under
  // <install>\share\claude-plugins\mcos-control and is registered for the
  // active console user as a directory junction at
  // %USERPROFILE%\.claude\plugins\mcos-control.
  claudePlugin: null,
  claudePluginBusy: false
};

const destinations = [
  {
    id: 'overview',
    label: 'Overview',
    eyebrow: 'COMMAND DECK',
    title: 'Master Control',
    subtitle: 'Gateway, pools, clients, and governance posture at a glance.'
  },
  {
    id: 'setup',
    label: 'Start Here',
    eyebrow: 'SETUP',
    title: 'Start Here',
    subtitle: 'Guided, manual, or import setup backed by the shared readiness state machine.'
  },
  {
    id: 'gateway',
    label: 'Gateway',
    eyebrow: 'MCP GATEWAY',
    title: 'MCP Gateway',
    subtitle: 'One advertised LAN MCP endpoint. native HTTP.sys adapter health, tool surface, lifecycle.'
  },
  {
    id: 'pools',
    label: 'Pools',
    eyebrow: 'WORKERS',
    title: 'Managed Endpoint Pools',
    subtitle: 'MCP servers and sub-agents under one supervisor. Lifecycle, leases, saturation, scale-out.'
  },
  {
    id: 'telemetry-clients',
    label: 'Clients',
    eyebrow: 'PRESENCE',
    title: 'Connected Clients',
    subtitle: 'Heartbeat-driven roster from /api/telemetry/clients. Self-reported metrics; -1.0 renders as unavailable.'
  },
  {
    id: 'clients',
    label: 'LAN Identity',
    eyebrow: 'OPERATOR',
    title: 'LAN Client Identity (operator surface)',
    subtitle: 'ADR-001 operator-only LAN client management. Privileges, autonomous mode, config bundles.'
  },
  {
    id: 'governance',
    label: 'Governance',
    eyebrow: 'CLU',
    title: 'Governance',
    subtitle: 'CLU posture, governance bundle downloads, and the operator approval queue.'
  },
  {
    id: 'onboarding',
    label: 'Onboarding',
    eyebrow: 'SETUP',
    title: 'Client Onboarding',
    subtitle: 'Per-client profiles for claude-code, codex, grok, chatgpt, and generic MCP. Manual setup remains first-class.'
  },
  {
    id: 'discovery',
    label: 'Discovery',
    eyebrow: 'LAN DISCOVERY',
    title: 'Discovery Document',
    subtitle: 'What MCOS advertises on the LAN. DNS-SD service types, gateway URL, governance / onboarding URLs.'
  },
  {
    id: 'runtime',
    label: 'Shared Fabric',
    eyebrow: 'CATALOG',
    title: 'MCP Servers + Sub-Agents (catalog)',
    subtitle: 'Operator catalog of registered backends. PHASE-06 added pools above; this remains the operator inventory.'
  },
  {
    id: 'workflows',
    label: 'Workflows',
    eyebrow: 'SETUP',
    title: 'Workflows',
    subtitle: 'Manual, imported, and starter-template workflows that count toward setup readiness.'
  },
  {
    id: 'activity',
    label: 'Activity',
    eyebrow: 'STREAM',
    title: 'Activity Stream',
    subtitle: 'Telemetry event ring + legacy admin activity. Categories: system / gateway / worker / client / discovery / governance.'
  },
  {
    id: 'exports',
    label: 'Exports',
    eyebrow: 'ARTIFACTS',
    title: 'Exports',
    subtitle: 'Server-authored config bundles and gateway profiles. Manual download remains first-class.'
  }
];

function normalizeDestinationId(value, fallback) {
  const raw = String(value || '').toLowerCase();
  if (destinations.some((dest) => dest.id === raw)) return raw;
  if (raw.indexOf('workflow') !== -1) return 'workflows';
  if (raw.indexOf('client') !== -1) return 'clients';
  if (raw.indexOf('mcp') !== -1 || raw.indexOf('specialist') !== -1 || raw.indexOf('runtime') !== -1) return 'runtime';
  if (raw.indexOf('import') !== -1) return 'exports';
  return fallback || 'setup';
}

function $(selector) { return document.querySelector(selector); }

function escapeHtml(value) {
  if (value == null) return '';
  return String(value)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function shortDate(value) {
  if (!value) return '—';
  return String(value).replace('T', ' ').replace(/\..*Z?$/, '').replace(/Z$/, '');
}

function relativeAgo(value) {
  if (!value) return '—';
  const then = Date.parse(value);
  if (Number.isNaN(then)) return shortDate(value);
  const diffSec = Math.max(0, Math.floor((Date.now() - then) / 1000));
  if (diffSec < 60) return diffSec + 's ago';
  if (diffSec < 3600) return Math.floor(diffSec / 60) + 'm ago';
  if (diffSec < 86400) return Math.floor(diffSec / 3600) + 'h ago';
  return Math.floor(diffSec / 86400) + 'd ago';
}

// PHASE-09 (ADR-002 §9 honest telemetry):
// ClientHeartbeat / WorkerTelemetry use -1.0 as the "unavailable" sentinel.
// 0.0 is a genuine "idle" reading. Never collapse the two on screen.
// Every numeric render that consumes a self-reported metric MUST go through
// this helper. FORBIDDEN-CONTRACT §8.1 greps for direct .toFixed(...)+'%'.
function formatMetric(value, options) {
  options = options || {};
  if (value == null) return options.placeholder || 'unavailable';
  const num = Number(value);
  if (!Number.isFinite(num)) return options.placeholder || 'unavailable';
  if (num < 0) return 'unavailable';
  const digits = options.digits != null ? options.digits : 0;
  const suffix = options.suffix != null ? options.suffix : '';
  return num.toFixed(digits) + suffix;
}

function metricTone(value) {
  if (value == null) return 'muted';
  const num = Number(value);
  if (!Number.isFinite(num)) return 'muted';
  if (num < 0) return 'muted';
  if (num >= 90) return 'bad';
  if (num >= 70) return 'warn';
  return 'ok';
}

function formatBytes(value) {
  if (value == null) return '—';
  const num = Number(value);
  if (!Number.isFinite(num) || num < 0) return '—';
  if (num < 1024) return num.toFixed(0) + ' B';
  if (num < 1024 * 1024) return (num / 1024).toFixed(1) + ' KB';
  if (num < 1024 * 1024 * 1024) return (num / (1024 * 1024)).toFixed(1) + ' MB';
  return (num / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
}

async function loadJson(path, options) {
  const response = await fetch(path, options || {});
  const text = await response.text();
  let parsed = null;
  try { parsed = text ? JSON.parse(text) : null; } catch (_) { parsed = null; }
  if (!response.ok) {
    const err = new Error('HTTP ' + response.status);
    err.status = response.status;
    err.body = parsed || text;
    throw err;
  }
  return parsed;
}

function postJson(path, payload) {
  return loadJson(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload || {})
  });
}

function showBanner(message) {
  state.errorBanner = message;
  renderCurrent();
}

function clearBanner() {
  if (state.errorBanner) {
    state.errorBanner = '';
  }
}

// ---------------------------------------------------------------------------
// Privilege metadata
// ---------------------------------------------------------------------------

const PRIVILEGES = [
  { key: 'canCreateMcpServers',       label: 'Create MCP servers' },
  { key: 'canModifyMcpServers',       label: 'Modify MCP servers' },
  { key: 'canRemoveMcpServers',       label: 'Remove MCP servers' },
  { key: 'canCreateSubAgents',        label: 'Create sub-agents' },
  { key: 'canModifySubAgents',        label: 'Modify sub-agents' },
  { key: 'canRemoveSubAgents',        label: 'Remove sub-agents' },
  { key: 'canManageClients',          label: 'Manage LAN clients' },
  { key: 'canManageModules',          label: 'Manage Forsetti modules' },
  { key: 'canChangeGovernancePolicy', label: 'Change governance policy' }
];

// ---------------------------------------------------------------------------
// Network refresh
// ---------------------------------------------------------------------------

async function refreshAll() {
  try {
    const results = await Promise.all([
      // Legacy ADR-001 operator surface — preserved
      loadJson('/api/health').catch(() => ({ status: 'unreachable', time: '' })),
      loadJson('/api/dashboard').catch((err) => { console.warn('dashboard', err); return null; }),
      loadJson('/api/clients').catch(() => []),
      loadJson('/api/clu/approvals').catch(() => []),
      loadJson('/api/exports').catch(() => []),
      loadJson('/api/activity').catch(() => ({ events: [], highWaterMarkId: '0' })),
      loadJson('/api/setup/state').catch(() => null),
      loadJson('/api/workflows').catch(() => null),
      loadJson('/api/setup/workflow-templates').catch(() => null),
      // PHASE-02..08 gateway-first surface
      loadJson('/api/gateway/status').catch(() => null),
      loadJson('/api/gateway/health').catch(() => null),
      loadJson('/api/gateway/tools').catch(() => []),
      loadJson('/api/pools').catch(() => []),
      loadJson('/api/discovery').catch(() => null),
      loadJson('/api/telemetry/events?max=200').catch(() => ({ events: [], maxEvents: 0 })),
      loadJson('/api/telemetry/clients').catch(() => []),
      loadJson('/api/telemetry/gateway').catch(() => null),
      loadJson('/api/claude-plugin/status').catch(() => null),
      // v0.9.75: visible self-test surface alongside the existing
      // error-reporting frame so operators see PASS rows too, not
      // just FAIL ones.
      loadJson('/api/self-tests').catch(() => null),
      // v0.9.77: Supervisor Agent Assignment Wizard status for the
      // dashboard parity card (shell shipped in v0.9.76; this is the
      // browser-side mirror).
      loadJson('/api/supervisor/status').catch(() => null)
    ]);
    const [health, dashboard, clients, approvals, exportsList, activity, setupState, workflows, workflowTemplates,
           gwStatus, gwHealth, gwTools, pools, discovery,
           telEvents, telClients, telGateway, claudePlugin, selfTests,
           supervisorStatus] = results;
    state.health = health || { status: 'unreachable', time: '' };
    state.dashboard = dashboard;
    state.clients = Array.isArray(clients) ? clients : [];
    state.approvals = Array.isArray(approvals) ? approvals : [];
    state.exports = Array.isArray(exportsList) ? exportsList : [];
    state.activity = activity && Array.isArray(activity.events)
      ? activity
      : { events: [], highWaterMarkId: '0' };
    state.setupState = setupState;
    state.workflows = workflows;
    state.workflowTemplates = workflowTemplates;
    state.gatewayStatus = gwStatus;
    state.gatewayHealth = gwHealth;
    state.gatewayTools = Array.isArray(gwTools) ? gwTools : [];
    state.pools = Array.isArray(pools) ? pools : [];
    state.discovery = discovery;
    state.telemetryEvents = (telEvents && Array.isArray(telEvents.events))
      ? telEvents
      : { events: [], maxEvents: 0 };
    state.telemetryClients = Array.isArray(telClients) ? telClients : [];
    state.telemetryGateway = telGateway;
    state.claudePlugin = claudePlugin;
    state.selfTests = selfTests || null;
    state.supervisorStatus = supervisorStatus || null;
    const setupReadiness = state.setupState && state.setupState.readiness;
    const setupIncomplete = setupReadiness && setupReadiness.firstRunCompleted !== true;
    const criticalSetupBlockers = ((setupReadiness && setupReadiness.blockingIssues) || [])
      .some((issue) => issue && (issue.severity === 'critical' || issue.severity === 'blocking'));
    if (setupIncomplete
        && state.destination === 'overview'
        && !state.setupBypassThisSession
        && (criticalSetupBlockers || !state.setupState.dismissedAtUtc)) {
      state.destination = 'setup';
    }
    // Per-pool lease + saturation (best-effort; missing routes degrade silently)
    if (state.pools.length > 0) {
      const leaseFetches = state.pools.map((p) =>
        loadJson('/api/pools/' + encodeURIComponent(p.poolId) + '/leases').catch(() => [])
      );
      const satFetches = state.pools.map((p) =>
        loadJson('/api/pools/' + encodeURIComponent(p.poolId) + '/saturation').catch(() => null)
      );
      const [leasesAll, satAll] = await Promise.all([
        Promise.all(leaseFetches),
        Promise.all(satFetches)
      ]);
      state.pools.forEach((p, idx) => {
        state.poolLeases[p.poolId] = Array.isArray(leasesAll[idx]) ? leasesAll[idx] : [];
        state.poolSaturation[p.poolId] = satAll[idx] || null;
      });
    }
    // Onboarding profile lazily fetched when destination is 'onboarding'
    if (state.destination === 'onboarding') {
      try {
        state.onboardingProfile = await loadJson(
          '/api/onboarding/' + encodeURIComponent(state.onboardingClientType)
        );
      } catch (_) {
        state.onboardingProfile = null;
      }
    }
    // Governance bundle lazily fetched when destination is 'governance'
    if (state.destination === 'governance') {
      try {
        state.governanceBundle = await loadJson(
          '/api/governance/bundles/' + encodeURIComponent(state.governanceBundlePlatform)
        );
      } catch (_) {
        state.governanceBundle = null;
      }
    }
    clearBanner();
  } catch (err) {
    console.error('Refresh failed:', err);
    showBanner('Refresh failed: ' + (err.message || 'unknown error'));
  }
  renderCurrent();
}

// ---------------------------------------------------------------------------
// Top chrome
// ---------------------------------------------------------------------------

function renderHealth() {
  const badge = $('#healthBadge');
  if (!badge) return;
  const status = (state.health && state.health.status) || 'unknown';
  badge.textContent = status === 'ok' ? 'Online' : status;
  badge.dataset.tone = status === 'ok' ? 'success' : (status === 'unreachable' ? 'danger' : 'info');
}

function renderToolbar() {
  const toolbar = $('#surfaceToolbar');
  if (!toolbar) return;
  toolbar.innerHTML = `
    <div class="toolbar-group">
      <span class="toolbar-group-label">Start</span>
      <button type="button" data-action="open-setup" class="route-button accent">Start Here</button>
      <button type="button" data-action="open-onboarding" class="route-button accent">Onboard Client</button>
      <button type="button" data-action="open-workflows" class="route-button">Workflows</button>
    </div>
    <div class="toolbar-group">
      <span class="toolbar-group-label">Operate</span>
      <button type="button" data-action="open-gateway" class="route-button">Gateway</button>
      <button type="button" data-action="open-pools" class="route-button">Pools</button>
      <button type="button" data-action="open-governance" class="route-button">Governance</button>
    </div>
    <div class="toolbar-group">
      <span class="toolbar-group-label">Advanced</span>
      <button type="button" data-action="open-discovery" class="route-button">Discovery</button>
      <button type="button" data-action="register-client" class="route-button">LAN Identity</button>
    </div>
  `;
  const goto = (id) => () => { state.destination = id; renderCurrent(); };
  toolbar.querySelector('[data-action="open-setup"]').addEventListener('click', goto('setup'));
  toolbar.querySelector('[data-action="open-onboarding"]').addEventListener('click', goto('onboarding'));
  toolbar.querySelector('[data-action="open-gateway"]').addEventListener('click', goto('gateway'));
  toolbar.querySelector('[data-action="open-pools"]').addEventListener('click', goto('pools'));
  toolbar.querySelector('[data-action="open-workflows"]').addEventListener('click', goto('workflows'));
  toolbar.querySelector('[data-action="open-discovery"]').addEventListener('click', goto('discovery'));
  toolbar.querySelector('[data-action="open-governance"]').addEventListener('click', goto('governance'));
  toolbar.querySelector('[data-action="register-client"]').addEventListener('click', () => {
    state.destination = 'clients';
    state.selectedClientId = '__new__';
    renderCurrent();
  });
}

function renderNavigation() {
  const nav = $('#surfaceNavigation');
  if (!nav) return;
  const byId = {};
  destinations.forEach((dest) => { byId[dest.id] = dest; });
  const groups = [
    { label: 'Start', ids: ['setup', 'overview', 'onboarding', 'workflows'] },
    { label: 'Operate', ids: ['gateway', 'pools', 'telemetry-clients', 'governance'] },
    { label: 'Advanced', ids: ['runtime', 'discovery', 'clients', 'activity', 'exports'] }
  ];
  nav.innerHTML = groups.map((group) => `
    <div class="nav-group">
      <p class="nav-group-label">${escapeHtml(group.label)}</p>
      ${group.ids.map((id) => byId[id]).filter(Boolean).map((dest) => `
        <button type="button"
                class="nav-link${state.destination === dest.id ? ' active' : ''}"
                data-destination="${escapeHtml(dest.id)}">
          <span class="nav-link-label">${escapeHtml(dest.label)}</span>
          <span class="nav-link-eyebrow">${escapeHtml(dest.eyebrow)}</span>
        </button>
      `).join('')}
    </div>
  `).join('');
  nav.querySelectorAll('button[data-destination]').forEach((btn) => {
    btn.addEventListener('click', () => {
      state.destination = btn.dataset.destination;
      state.selectedClientId = null;
      renderCurrent();
    });
  });
}

function renderHeading() {
  const dest = destinations.find((d) => d.id === state.destination) || destinations[0];
  const eyebrowEl = $('#currentViewEyebrow');
  const titleEl = $('#currentViewTitle');
  const subEl = $('#currentViewDescription');
  if (eyebrowEl) eyebrowEl.textContent = dest.eyebrow;
  if (titleEl) titleEl.textContent = dest.title;
  if (subEl) subEl.textContent = dest.subtitle;
}

function renderSummary() {
  const target = $('#surfaceSummary');
  if (!target) return;
  const t = (state.dashboard && state.dashboard.telemetry) || {};
  // Host telemetry stays PDH-direct (0.0 = idle is genuine), so no formatMetric here.
  const cpu = t.cpuPercent != null ? t.cpuPercent.toFixed(0) + '%' : '—';
  const mem = t.memoryPercent != null ? t.memoryPercent.toFixed(0) + '%' : '—';
  // Gateway-first KPIs
  const gwState = state.gatewayStatus && state.gatewayStatus.state ? state.gatewayStatus.state : 'unknown';
  const gwHealth = state.gatewayHealth && state.gatewayHealth.status ? state.gatewayHealth.status : 'unknown';
  const poolCount = state.pools.length;
  const readyInstances = state.pools.reduce((acc, p) => {
    const sat = state.poolSaturation[p.poolId];
    return acc + ((sat && sat.readyInstanceCount) || 0);
  }, 0);
  const activeLeases = state.pools.reduce((acc, p) => {
    return acc + (state.poolLeases[p.poolId] || []).length;
  }, 0);
  const liveClients = state.telemetryClients.length;
  const pendingApprovals = state.approvals.filter((a) => a.status === 'pending').length;
  target.innerHTML = `
    <div class="summary-grid">
      <div class="summary-cell"><span class="summary-label">Host</span><span class="summary-value">${escapeHtml(t.hostName || '—')}</span></div>
      <div class="summary-cell"><span class="summary-label">CPU</span><span class="summary-value">${escapeHtml(cpu)}</span></div>
      <div class="summary-cell"><span class="summary-label">Memory</span><span class="summary-value">${escapeHtml(mem)}</span></div>
      <div class="summary-cell"><span class="summary-label">Gateway</span><span class="summary-value">${escapeHtml(gwState)} · ${escapeHtml(gwHealth)}</span></div>
      <div class="summary-cell"><span class="summary-label">Pools</span><span class="summary-value">${poolCount} (${readyInstances} ready)</span></div>
      <div class="summary-cell"><span class="summary-label">Active leases</span><span class="summary-value">${activeLeases}</span></div>
      <div class="summary-cell"><span class="summary-label">Live clients</span><span class="summary-value">${liveClients}</span></div>
      <div class="summary-cell"><span class="summary-label">Pending CLU</span><span class="summary-value">${pendingApprovals}</span></div>
    </div>
  `;
}

// ---------------------------------------------------------------------------
// Destinations
// ---------------------------------------------------------------------------

function renderCurrent() {
  renderHealth();
  renderHeading();
  renderSummary();
  renderNavigation();

  const host = $('#surfaceContentHost');
  if (!host) return;

  const banner = state.errorBanner
    ? `<div class="error-banner">${escapeHtml(state.errorBanner)}</div>`
    : '';

  switch (state.destination) {
    case 'setup':             host.innerHTML = banner + renderSetupPanel();       bindSetupHandlers();      break;
    case 'gateway':           host.innerHTML = banner + renderGatewayPanel();                                break;
    case 'pools':             host.innerHTML = banner + renderPoolsPanel();        bindPoolsHandlers();      break;
    case 'telemetry-clients': host.innerHTML = banner + renderTelemetryClients();                            break;
    case 'onboarding':        host.innerHTML = banner + renderOnboardingPanel();   bindOnboardingHandlers(); break;
    case 'discovery':         host.innerHTML = banner + renderDiscoveryPanel();    bindDiscoveryHandlers();  break;
    case 'clients':           host.innerHTML = banner + renderClients();           bindClientsHandlers();    break;
    case 'governance':        host.innerHTML = banner + renderGovernance();        bindGovernanceHandlers(); break;
    case 'runtime':           host.innerHTML = banner + renderRuntime();           bindRuntimeHandlers();    break;
    case 'workflows':         host.innerHTML = banner + renderWorkflowsPanel(); bindWorkflowHandlers();      break;
    case 'activity':          host.innerHTML = banner + renderActivity();                                    break;
    case 'exports':           host.innerHTML = banner + renderExports();           bindExportsHandlers();    break;
    case 'overview':
    default:                  host.innerHTML = banner + renderOverview();          bindOverviewHandlers();   break;
  }
}

// ---- Setup ----

function readinessTile(label, ready, missing, actionLabel, destination) {
  const total = Number(ready || 0) + Number(missing || 0);
  const tileState = ready > 0 && missing === 0 ? 'ready' : (ready === 0 ? 'missing' : 'needs-attention');
  return `
    <article class="readiness-tile" data-state="${escapeHtml(tileState)}">
      <p class="eyebrow">${escapeHtml(label)}</p>
      <h3>${escapeHtml(String(ready || 0))} / ${escapeHtml(String(total))}</h3>
      <p class="muted">${escapeHtml(tileState === 'ready' ? 'Ready' : (tileState === 'missing' ? 'Missing' : 'Needs attention'))}</p>
      ${destination ? `<button type="button" class="route-button" data-action="setup-goto" data-destination="${escapeHtml(destination)}">${escapeHtml(actionLabel || 'Fix now')}</button>` : ''}
    </article>
  `;
}

function renderSetupPanel() {
  const setup = state.setupState || {};
  const readiness = setup.readiness || {};
  const issues = Array.isArray(readiness.blockingIssues) ? readiness.blockingIssues : [];
  const steps = Array.isArray(setup.steps) ? setup.steps : [];
  const completed = readiness.firstRunCompleted === true;
  const mode = setup.mode || 'guided';
  const currentStep = setup.currentStep || 'welcome';
  const criticalIssues = issues.filter((issue) => issue && (issue.severity === 'critical' || issue.severity === 'blocking'));
  return `
    <article class="panel-block wide setup-state-panel">
      <p class="eyebrow">SETUP STATE</p>
      <h3>${completed ? 'Setup complete' : 'Start Here'}</h3>
      <p class="muted">Mode <code>${escapeHtml(mode)}</code> · current step <code>${escapeHtml(currentStep)}</code> · posture <code>${escapeHtml(setup.securityPosture || 'local-only')}</code></p>

      <div class="first-run-entry-grid">
        <button type="button" class="first-run-entry-card" data-action="setup-start" data-mode="guided">
          <p class="eyebrow">GUIDED</p>
          <h3>Guided Setup</h3>
          <p>Step through local preflight, security posture, runtime lanes, workflows, and readiness review.</p>
        </button>
        <button type="button" class="first-run-entry-card" data-action="setup-start" data-mode="manual">
          <p class="eyebrow">MANUAL</p>
          <h3>Manual Setup</h3>
          <p>Use the full operator console now, then return here for the shared readiness review.</p>
        </button>
        <button type="button" class="first-run-entry-card" data-action="setup-start" data-mode="import-existing">
          <p class="eyebrow">IMPORT</p>
          <h3>Import Existing Configuration</h3>
          <p>Bring in an existing package, repository, or zip bundle and validate it before completion.</p>
        </button>
      </div>

      <div class="wizard-step-rail">
        ${steps.map((step) => `
          <span class="wizard-step-rail-item ${step.state === 'ready' ? 'done' : (step.state === 'active' ? 'active' : '')}"
                title="${escapeHtml((step.blockers || []).join(', '))}">
            ${escapeHtml(step.label || step.id || 'step')}
          </span>
        `).join('')}
      </div>

      <div class="readiness-grid">
        ${readinessTile('MCP Servers', readiness.mcpReadyCount || 0, readiness.mcpMissingCount || 0, 'Open Runtime', 'runtime')}
        ${readinessTile('Specialists', readiness.specialistsReadyCount || 0, readiness.specialistsMissingCount || 0, 'Open Runtime', 'runtime')}
        ${readinessTile('Workflows', readiness.workflowsReadyCount || 0, readiness.workflowsMissingCount || 0, 'Open Workflows', 'workflows')}
      </div>

      ${issues.length === 0 ? '' : `
        <h3>Readiness Issues</h3>
        <table class="runtime-table">
          <thead><tr><th>Severity</th><th>Issue</th><th>Destination</th></tr></thead>
          <tbody>
            ${issues.map((issue) => {
              const destination = issue.remediationDestination || 'setup';
              return `<tr>
              <td>${escapeHtml(issue.severity || 'info')}</td>
              <td><strong>${escapeHtml(issue.title || issue.id || 'Issue')}</strong><br><span class="muted">${escapeHtml(issue.detail || '')}</span></td>
              <td><button type="button" class="route-button" data-action="setup-goto" data-destination="${escapeHtml(destination)}">${escapeHtml(issue.remediationLabel || 'Fix Now')}</button></td>
            </tr>`;
            }).join('')}
          </tbody>
        </table>
      `}

      <div class="manual-setup-banner">
        <div>
          <strong>${criticalIssues.length === 0 ? 'Ready for operator confirmation' : 'Critical blockers require attention'}</strong>
          <p class="muted">Completion requires <code>confirm: true</code>; critical blockers require explicit override reasons through the shared API.</p>
        </div>
        <div class="dialog-actions">
          ${completed ? '' : '<button type="button" class="route-button accent" data-action="setup-complete">Mark Setup Complete</button>'}
          <button type="button" class="route-button" data-action="setup-dismiss">Continue Setup Later</button>
        </div>
      </div>
    </article>
  `;
}

function bindSetupHandlers() {
  document.querySelectorAll('[data-action="setup-start"]').forEach((btn) => {
    btn.addEventListener('click', async () => {
      const mode = btn.dataset.mode || 'guided';
      try {
        const result = await postJson('/api/setup/start', { mode });
        state.setupState = result && result.setup ? result.setup : state.setupState;
        if (mode === 'manual') {
          await postJson('/api/setup/dismiss', {});
          state.setupBypassThisSession = true;
          state.destination = 'overview';
        } else if (mode === 'import-existing') {
          state.destination = 'exports';
        } else {
          state.destination = 'runtime';
        }
        await refreshAll();
      } catch (err) {
        const body = err.body || {};
        showBanner(body.message || body.errorMessage || ('Setup start failed: ' + (err.message || 'unknown')));
      }
    });
  });
  document.querySelectorAll('[data-action="setup-goto"]').forEach((btn) => {
    btn.addEventListener('click', () => {
      state.destination = normalizeDestinationId(btn.dataset.destination, 'setup');
      renderCurrent();
    });
  });
  const completeBtn = document.querySelector('[data-action="setup-complete"]');
  if (completeBtn) {
    completeBtn.addEventListener('click', async () => {
      try {
        const result = await postJson('/api/setup/complete', { confirm: true });
        state.setupState = result && result.setup ? result.setup : state.setupState;
        clearBanner();
        await refreshAll();
      } catch (err) {
        const body = err.body || {};
        showBanner(body.errorMessage || body.message || ('Setup completion failed: ' + (err.message || 'unknown')));
      }
    });
  }
  const dismissBtn = document.querySelector('[data-action="setup-dismiss"]');
  if (dismissBtn) {
    dismissBtn.addEventListener('click', async () => {
      try {
        const result = await postJson('/api/setup/dismiss', {});
        state.setupState = result && result.setup ? result.setup : state.setupState;
        state.setupBypassThisSession = true;
        state.destination = 'overview';
        await refreshAll();
      } catch (err) {
        const body = err.body || {};
        showBanner(body.message || body.errorMessage || ('Setup dismiss failed: ' + (err.message || 'unknown')));
      }
    });
  }
}

// ---- Workflows ----

function riskTone(value) {
  const risk = String(value || '').toLowerCase();
  if (risk === 'critical' || risk === 'high') return 'critical';
  if (risk === 'medium' || risk === 'warning') return 'warn';
  if (risk === 'low') return 'good';
  return 'info';
}

function capabilityChips(capabilities) {
  const caps = Array.isArray(capabilities) ? capabilities : [];
  if (caps.length === 0) {
    return '<span class="capability-chip muted">no extra capability</span>';
  }
  return caps.map((cap) => `<span class="capability-chip">${escapeHtml(cap)}</span>`).join('');
}

function renderStarterWorkflowTemplates(templates) {
  if (!templates || templates.length === 0) {
    return '';
  }
  return `
    <section class="template-card-section">
      <div class="section-split-heading">
        <div>
          <p class="eyebrow">GUIDED TASK CARDS</p>
          <h3>Starter workflow templates</h3>
        </div>
        <span class="muted">Direct instantiate, then validate readiness.</span>
      </div>
      <div class="template-card-grid">
        ${templates.map((tpl) => {
          const caps = Array.isArray(tpl.requiredCapabilities) ? tpl.requiredCapabilities : [];
          const requirements = [
            `${tpl.requiresClients || 0} client${tpl.requiresClients === 1 ? '' : 's'}`,
            `${tpl.requiresMcp || 0} MCP`,
            `${tpl.requiresSpecialists || 0} specialist${tpl.requiresSpecialists === 1 ? '' : 's'}`
          ].join(' · ');
          return `
            <article class="template-card" data-risk="${caps.length > 0 ? 'warn' : 'info'}">
              <div class="template-card-head">
                <span class="badge-template">TEMPLATE</span>
                <span class="risk-pill ${caps.length > 0 ? 'warn' : 'info'}">${caps.length > 0 ? 'capability gated' : 'standard'}</span>
              </div>
              <h3>${escapeHtml(tpl.displayName || tpl.id || 'Workflow template')}</h3>
              <p class="muted">${escapeHtml(tpl.description || '')}</p>
              <p class="muted">Requires ${escapeHtml(requirements)}</p>
              <div class="capability-chip-row">${capabilityChips(caps)}</div>
              <button type="button" class="route-button accent" data-action="workflow-template-instantiate" data-template-id="${escapeHtml(tpl.id || '')}">Instantiate</button>
            </article>
          `;
        }).join('')}
      </div>
    </section>
  `;
}

function renderWorkflowsPanel() {
  const doc = state.workflows || {};
  const workflows = Array.isArray(doc.workflows) ? doc.workflows : [];
  const templateDoc = state.workflowTemplates || {};
  const templates = Array.isArray(templateDoc.templates) ? templateDoc.templates : [];
  return `
    <article class="panel-block wide">
      <div class="manual-setup-banner">
        <div>
          <strong>${escapeHtml(String(doc.readyCount || 0))} ready workflow${doc.readyCount === 1 ? '' : 's'}</strong>
          <p class="muted">${escapeHtml(String(doc.invalidCount || 0))} invalid · ${escapeHtml(String(doc.disabledCount || 0))} disabled · ${escapeHtml(String(doc.missingCount || 0))} missing</p>
        </div>
        <button type="button" class="route-button accent" data-action="workflow-create-demo">Create Manual Workflow</button>
      </div>
      ${renderStarterWorkflowTemplates(templates)}
      ${workflows.length === 0 ? '<p class="muted">No workflows have been created yet. Manual, imported, and starter-template workflows all satisfy readiness when enabled and valid.</p>' : `
        <table class="runtime-table">
          <thead><tr><th>Workflow</th><th>Source</th><th>State</th><th>Steps</th><th>Actions</th></tr></thead>
          <tbody>
            ${workflows.map((wf) => {
              const issues = Array.isArray(wf.validationIssues) ? wf.validationIssues : [];
              return `<tr>
                <td><strong>${escapeHtml(wf.displayName || wf.workflowId || 'Workflow')}</strong><br><code>${escapeHtml(wf.workflowId || '')}</code>${issues.length === 0 ? '' : `<br><span class="muted">${escapeHtml(issues.map((issue) => issue.id || issue.message || 'issue').join(', '))}</span>`}</td>
                <td>${escapeHtml(wf.source || 'manual')}</td>
                <td>${wf.enabled === false ? 'disabled' : (wf.ready ? 'ready' : 'invalid')}</td>
                <td>${escapeHtml(String((wf.steps || []).length))}</td>
                <td>
                  <button type="button" class="route-button" data-action="workflow-toggle" data-workflow-id="${escapeHtml(wf.workflowId || '')}" data-enabled="${wf.enabled === false ? 'false' : 'true'}">${wf.enabled === false ? 'Enable' : 'Disable'}</button>
                  <button type="button" class="route-button danger" data-action="workflow-delete" data-workflow-id="${escapeHtml(wf.workflowId || '')}">Delete</button>
                </td>
              </tr>`;
            }).join('')}
          </tbody>
        </table>
      `}
    </article>
  `;
}

function workflowDemoPayload(source) {
  const stamp = String(Date.now()).slice(-6);
  return {
    workflowId: source + '-workflow-' + stamp,
    displayName: source === 'imported' ? 'Imported Workflow ' + stamp : 'Manual Workflow ' + stamp,
    description: 'Operator-created workflow for setup readiness.',
    source,
    enabled: true,
    steps: [
      {
        stepId: 'operator-review',
        kind: 'approval',
        target: 'operator.readiness-review',
        arguments: {},
        requiresApproval: true
      }
    ]
  };
}

function bindWorkflowHandlers() {
  const createBtn = document.querySelector('[data-action="workflow-create-demo"]');
  if (createBtn) {
    createBtn.addEventListener('click', async () => {
      try {
        await postJson('/api/workflows', workflowDemoPayload('manual'));
        await refreshAll();
      } catch (err) {
        const body = err.body || {};
        showBanner(body.message || body.errorMessage || 'Workflow create failed.');
      }
    });
  }
  document.querySelectorAll('[data-action="workflow-template-instantiate"]').forEach((btn) => {
    btn.addEventListener('click', async () => {
      const templateId = btn.dataset.templateId || '';
      if (!templateId) return;
      try {
        const result = await postJson('/api/setup/workflow-templates/' + encodeURIComponent(templateId) + '/instantiate', {});
        if (result && result.succeeded === false) {
          showBanner(result.message || 'Starter workflow requirements are not met.');
          return;
        }
        await refreshAll();
      } catch (err) {
        const body = err.body || {};
        showBanner(body.message || body.errorMessage || ('Starter workflow instantiate failed: ' + (err.message || 'unknown')));
      }
    });
  });
  document.querySelectorAll('[data-action="workflow-toggle"]').forEach((btn) => {
    btn.addEventListener('click', async () => {
      const id = btn.dataset.workflowId || '';
      const action = btn.dataset.enabled === 'false' ? 'enable' : 'disable';
      try {
        await postJson('/api/workflows/' + encodeURIComponent(id) + '/' + action, {});
        await refreshAll();
      } catch (err) {
        const body = err.body || {};
        showBanner(body.message || body.errorMessage || 'Workflow update failed.');
      }
    });
  });
  document.querySelectorAll('[data-action="workflow-delete"]').forEach((btn) => {
    btn.addEventListener('click', async () => {
      const id = btn.dataset.workflowId || '';
      try {
        await loadJson('/api/workflows/' + encodeURIComponent(id), { method: 'DELETE' });
        await refreshAll();
      } catch (err) {
        const body = err.body || {};
        showBanner(body.message || body.errorMessage || 'Workflow delete failed.');
      }
    });
  });
}

// ---- Overview ----

// v0.7.7: helper that resolves wildcard hosts (0.0.0.0, ::, empty) in
// URLs into the primary LAN IP so the operator sees a usable address
// instead of the literal listen-on-all-interfaces sentinel. Returns the
// original URL unchanged when the host is already an explicit IP/name or
// when the LAN IP is not yet known. IPv6 literals get RFC 3986 brackets.
function resolveDisplayUrl(url, lanIp) {
  if (!url || typeof url !== 'string') return url;
  if (!lanIp) return url;
  // The wildcard hosts we substitute. Match the URL prefix so the rest of
  // the URL (port + path) flows through unchanged.
  const wildcardHosts = ['0.0.0.0', '[::]', '[::1]'];
  for (const wc of wildcardHosts) {
    const needle = '://' + wc;
    const idx = url.indexOf(needle);
    if (idx >= 0) {
      // IPv6 literal needs brackets in the URL even though the snapshot
      // stores it bare.
      const isIPv6 = lanIp.indexOf(':') >= 0;
      const rendered = isIPv6 ? '[' + lanIp + ']' : lanIp;
      return url.slice(0, idx + 3) + rendered + url.slice(idx + needle.length);
    }
  }
  return url;
}

function renderOverview() {
  const t = (state.dashboard && state.dashboard.telemetry) || {};
  const cluPosture = state.dashboard && state.dashboard.governance
    ? state.dashboard.governance.posture
    : 'unknown';
  const pendingApprovals = state.approvals.filter((a) => a.status === 'pending');
  const gwState = state.gatewayStatus && state.gatewayStatus.state ? state.gatewayStatus.state : 'unknown';
  const gwHealth = state.gatewayHealth && state.gatewayHealth.status ? state.gatewayHealth.status : 'unknown';
  const gwMcpUrlRaw = (state.gatewayStatus && state.gatewayStatus.mcpUrl)
    || (state.discovery && state.discovery.gateway && state.discovery.gateway.mcpUrl)
    || '';
  const lanIp = t.primaryIpAddress || (state.discovery && state.discovery.serverIpAddress) || '';
  // v0.7.7: substitute 0.0.0.0 / ::/ empty wildcard with the primary LAN IP
  // for display only. Without this the operator-facing URL reads
  // "http://0.0.0.0:8080/mcp" -- technically the configured listen value
  // but useless for any client trying to actually connect.
  const gwMcpUrl = resolveDisplayUrl(gwMcpUrlRaw, lanIp) || '—';
  const gwMcpUrlListen = gwMcpUrlRaw && gwMcpUrl !== gwMcpUrlRaw ? gwMcpUrlRaw : '';
  const poolCount = state.pools.length;
  const readyInstances = state.pools.reduce((acc, p) => {
    const sat = state.poolSaturation[p.poolId];
    return acc + ((sat && sat.readyInstanceCount) || 0);
  }, 0);
  const activeLeases = state.pools.reduce((acc, p) => acc + (state.poolLeases[p.poolId] || []).length, 0);
  const liveClients = state.telemetryClients.length;
  const recentEvents = (state.telemetryEvents.events || []).slice(-8).reverse();

  return `
    <div class="overview-grid">
      <article class="panel-block">
        <h3>MCP Gateway</h3>
        <p class="big-stat ${gwState === 'running' ? 'good' : (gwState === 'failed' ? 'bad' : 'warn')}">${escapeHtml(gwState)}</p>
        <p class="muted">Health: <strong>${escapeHtml(gwHealth)}</strong></p>
        <p class="muted">LAN clients: <code>${escapeHtml(gwMcpUrl)}</code></p>
        ${gwMcpUrlListen ? `<p class="muted">Listen: <code>${escapeHtml(gwMcpUrlListen)}</code></p>` : ''}
        <button type="button" data-action="goto-gateway" class="route-button">Open Gateway</button>
      </article>
      <article class="panel-block">
        <h3>Worker Pools</h3>
        <p class="big-stat">${poolCount}<span class="big-stat-suffix"> pool${poolCount === 1 ? '' : 's'}</span></p>
        <p class="muted">${readyInstances} ready instance${readyInstances === 1 ? '' : 's'} · ${activeLeases} active lease${activeLeases === 1 ? '' : 's'}</p>
        <button type="button" data-action="goto-pools" class="route-button">Open Pools</button>
      </article>
      <article class="panel-block">
        <h3>Connected Clients</h3>
        <p class="big-stat">${liveClients}</p>
        <p class="muted">Heartbeat-driven roster from /api/telemetry/clients.</p>
        <button type="button" data-action="goto-clients" class="route-button">Open Roster</button>
      </article>
      <article class="panel-block">
        <h3>Governance Posture</h3>
        <p class="big-stat ${cluPosture === 'pass' ? 'good' : (cluPosture === 'blocked' ? 'bad' : 'warn')}">${escapeHtml(cluPosture)}</p>
        <p class="muted">CLU runtime posture · ${pendingApprovals.length} pending approval${pendingApprovals.length === 1 ? '' : 's'}.</p>
        <button type="button" data-action="goto-governance" class="route-button">Open Governance</button>
      </article>
      <article class="panel-block">
        <h3>Host Telemetry</h3>
        <ul class="kv-list">
          <li><span>Host</span><strong>${escapeHtml(t.hostName || '—')}</strong></li>
          <li><span>IP</span><strong>${escapeHtml(t.primaryIpAddress || '—')}</strong></li>
          <li><span>CPU</span><strong>${t.cpuPercent != null ? t.cpuPercent.toFixed(0) + '%' : '—'}</strong></li>
          <li><span>Memory</span><strong>${t.memoryPercent != null ? t.memoryPercent.toFixed(0) + '%' : '—'}</strong></li>
          <li><span>Disk</span><strong>${t.diskPercent != null ? t.diskPercent.toFixed(0) + '%' : '—'}</strong></li>
        </ul>
        <p class="muted">Host metrics are PDH-direct; <code>0%</code> means idle.</p>
      </article>
      ${renderClaudePluginCard()}
      ${renderSelfTestPanel()}
      ${renderSupervisorPanel()}
      ${renderSubAgentUtilizationPanel({ deck: 'overview' })}
      <article class="panel-block wide">
        <h3>Recent Telemetry Events</h3>
        ${renderTelemetryEventRows(recentEvents, { compact: true })}
      </article>
    </div>
  `;
}

// v0.9.75: visible self-test panel on the Overview deck. Mirrors the
// shell's Self-Test card (state.selfTests is the same /api/self-tests
// payload the shell consumes). Re-run button POSTs /api/self-tests/run
// and replaces state.selfTests with the freshly-computed snapshot so
// the panel updates immediately, without waiting for the next 8s
// safety-net poll or the next SSE dashboard event.
function renderSelfTestPanel() {
  const s = state.selfTests || null;
  const hasResults = !!(s && Array.isArray(s.results));
  const passed = s && typeof s.passedCount === 'number' ? s.passedCount : 0;
  const failed = s && typeof s.failedCount === 'number' ? s.failedCount : 0;
  const total  = s && typeof s.totalCount === 'number' ? s.totalCount : 0;
  const pending = !s || s.pending === true;

  let tone = 'info';
  let headline = '';
  if (!s) {
    tone = 'warn'; headline = 'Self-tests endpoint unavailable';
  } else if (pending) {
    tone = 'warn';
    headline = 'Self-tests pending — first sweep runs ~3s after service start';
  } else if (failed > 0) {
    tone = 'bad';
    headline = failed + ' of ' + total + ' probe' + (total === 1 ? '' : 's') + ' FAILED';
  } else {
    tone = 'good';
    headline = 'All ' + passed + ' probe' + (passed === 1 ? '' : 's') + ' passed';
  }

  const sweep = s && (s.startedAtUtc || s.finishedAtUtc)
    ? 'Last sweep: ' + escapeHtml(s.startedAtUtc || '') +
      (s.finishedAtUtc && s.finishedAtUtc !== s.startedAtUtc ? ' → ' + escapeHtml(s.finishedAtUtc) : '')
    : 'Probes: admin port + gateway state + every supervised pool + activity ring + telemetry sampler + worker exe presence.';

  const rows = (hasResults ? s.results : [])
    // failures float up; alphabetical within each bucket
    .slice()
    .sort((a, b) => (a.ok === b.ok ? (a.name < b.name ? -1 : 1) : (a.ok ? 1 : -1)))
    .map((r) => {
      const cls = r.ok ? 'self-test-row pass' : 'self-test-row fail';
      const tag = r.ok ? 'PASS' : 'FAIL';
      return '<div class="' + cls + '">'
           + '<span class="self-test-tag">' + tag + '</span>'
           + '<span class="self-test-name">' + escapeHtml(r.name || '') + '</span>'
           + '<span class="self-test-dur">' + (r.durationMs || 0) + ' ms</span>'
           + '<div class="self-test-msg">' + escapeHtml(r.message || '') + '</div>'
           + '</div>';
    }).join('');

  return `
    <article class="panel-block wide self-test-panel">
      <div class="self-test-head">
        <div class="self-test-summary">
          <span class="self-test-eyebrow">SELF-TESTS</span>
          <span class="self-test-headline ${tone}">${escapeHtml(headline)}</span>
          <span class="muted">${sweep}</span>
        </div>
        <button type="button" class="route-button" data-action="rerun-selftests">Re-run</button>
      </div>
      <div class="self-test-list">
        ${hasResults && rows
          ? rows
          : '<p class="muted">No probe results yet. Sweep finishes ~3s after service start.</p>'}
      </div>
    </article>
  `;
}

// v0.9.75: re-run handler — POSTs /api/self-tests/run, swaps state.selfTests,
// re-renders the current view. Same one-shot pattern as the shell button.
async function rerunSelfTests() {
  try {
    const fresh = await fetch('/api/self-tests/run', { method: 'POST' })
      .then((r) => r.ok ? r.json() : null);
    if (fresh) state.selfTests = fresh;
    renderCurrent();
  } catch (e) {
    console.warn('rerun-selftests failed', e);
  }
}

// v0.9.91: relative-time formatter for the supervisor heartbeat.
// Mirrors the WinUI Shell's formatRelativeUtcTime helper (shipped in
// v0.9.90). Parses an ISO-8601 UTC stamp, computes delta vs now,
// renders in operator-friendly buckets (Xs / Xm Ys / Xh Ym / Xd Yh).
// Returns empty string on parse failure so callers can fall back to
// the raw stamp. Future stamps (clock skew) render as 'just now'.
function formatRelativeUtcTime(isoStampUtc) {
  if (!isoStampUtc || typeof isoStampUtc !== 'string') return '';
  const parsed = Date.parse(isoStampUtc);
  if (Number.isNaN(parsed)) return '';
  const deltaMs = Date.now() - parsed;
  if (deltaMs < 0) return 'just now';
  const sec = Math.floor(deltaMs / 1000);
  if (sec < 60) return sec + 's ago';
  if (sec < 3600) {
    const m = Math.floor(sec / 60);
    const s = sec % 60;
    return s === 0 ? (m + 'm ago') : (m + 'm ' + s + 's ago');
  }
  if (sec < 86400) {
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    return m === 0 ? (h + 'h ago') : (h + 'h ' + m + 'm ago');
  }
  const d = Math.floor(sec / 86400);
  const h = Math.floor((sec % 86400) / 3600);
  return h === 0 ? (d + 'd ago') : (d + 'd ' + h + 'h ago');
}

// v0.9.95: future-direction relative-time formatter. Symmetric with
// formatRelativeUtcTime; mirrors the WinUI Shell's formatFutureUtcTime
// (shipped v0.9.94). Renders 'in Xs' / 'in Xm Ys' / 'in Xh Ym' / 'in
// Xd Yh' for stamps in the future, 'expired Xs ago' (via the past
// helper) for stamps already in the past, '' on parse failure.
function formatFutureUtcTime(isoStampUtc) {
  if (!isoStampUtc || typeof isoStampUtc !== 'string') return '';
  const parsed = Date.parse(isoStampUtc);
  if (Number.isNaN(parsed)) return '';
  const deltaMs = parsed - Date.now();
  if (deltaMs <= 0) {
    const since = formatRelativeUtcTime(isoStampUtc);
    return since ? ('expired ' + since) : 'expired';
  }
  const sec = Math.floor(deltaMs / 1000);
  if (sec < 60) return 'in ' + sec + 's';
  if (sec < 3600) {
    const m = Math.floor(sec / 60);
    const s = sec % 60;
    return s === 0 ? ('in ' + m + 'm') : ('in ' + m + 'm ' + s + 's');
  }
  if (sec < 86400) {
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    return m === 0 ? ('in ' + h + 'h') : ('in ' + h + 'h ' + m + 'm');
  }
  const d = Math.floor(sec / 86400);
  const h = Math.floor((sec % 86400) / 3600);
  return h === 0 ? ('in ' + d + 'd') : ('in ' + d + 'd ' + h + 'h');
}

// v0.9.77: Supervisor Agent Assignment Wizard parity for the browser
// dashboard. Mirrors the shell Overview card shipped in v0.9.76:
// three single-selection radio inputs (chatgpt / claude / grok),
// Generate Config & Save (POSTs /api/supervisor/config/generate then
// triggers a Blob download with the model-specific filename), and a
// Revoke Active button. Status row reflects state.supervisorStatus
// on every refresh so the operator sees the same lifecycle the shell
// surface shows.
function renderSupervisorPanel() {
  const s = state.supervisorStatus || null;
  const provider = (s && s.activeProviderId) || '';
  const mode = (s && s.mode) || 'autonomous_supervisor';
  const stateLabel = (s && s.state) || 'off';
  const displayName = (s && s.providerDisplayName) || '';
  const active = !!(s && s.active);

  let tone = 'info';
  let headline = 'No supervisor assigned.';
  let statusLine = 'Status: off. Pick exactly one provider and click Generate Config & Save.';
  if (stateLabel === 'connected') {
    tone = 'good';
    headline = 'Active supervisor: ' + escapeHtml(displayName || provider);
    // v0.9.91: relative-time formatting on the heartbeat. Parity with
    // the WinUI Shell's v0.9.90 ApplySupervisorCard.
    let heartbeatPhrase = '';
    if (!s || !s.lastHeartbeatUtc) {
      heartbeatPhrase = ' · Last heartbeat: (none yet)';
    } else {
      const relative = formatRelativeUtcTime(s.lastHeartbeatUtc);
      heartbeatPhrase = relative
        ? (' · Last heartbeat: ' + escapeHtml(relative))
        : (' · Last heartbeat (UTC): ' + escapeHtml(s.lastHeartbeatUtc));
    }
    statusLine = 'Mode: ' + escapeHtml(mode) + ' · Status: connected' + heartbeatPhrase;
  } else if (stateLabel === 'pending_connection' || stateLabel === 'config_generated') {
    tone = 'warn';
    headline = escapeHtml(displayName || provider) + ' supervisor pending connection';
    // v0.9.95: future-relative expiry rendering. Pre-v0.9.95 the
    // expiry was a raw UTC stamp; now it shows 'Expires: in 2h 14m
    // (2026-05-10T...)' with the raw stamp in parens for cross-check.
    let expiryPhrase = '';
    if (s && s.expiresAtUtc) {
      const expRelative = formatFutureUtcTime(s.expiresAtUtc);
      expiryPhrase = expRelative
        ? (' · Expires: ' + escapeHtml(expRelative) + ' (' + escapeHtml(s.expiresAtUtc) + ')')
        : (' · Expires (UTC): ' + escapeHtml(s.expiresAtUtc));
    }
    statusLine = 'Status: ' + escapeHtml(stateLabel) + expiryPhrase
      + '. Move the saved config to the LAN client and import it.';
  } else if (stateLabel === 'error') {
    tone = 'bad';
    headline = escapeHtml(displayName || provider) + ' supervisor error';
    statusLine = (s && s.lastErrorMessage)
      ? 'Error: ' + escapeHtml(s.lastErrorMessage)
      : 'Status: error.';
  } else if (stateLabel === 'revoked') {
    tone = 'warn';
    headline = escapeHtml(displayName || provider) + ' supervisor revoked';
    statusLine = 'Status: revoked. Select a provider and Generate Config to assign a new supervisor.';
  } else if (stateLabel === 'disconnected') {
    tone = 'warn';
    headline = escapeHtml(displayName || provider) + ' supervisor disconnected';
    statusLine = 'Status: disconnected. The remote client has not heartbeated recently.';
  }

  const row = (id, label, desc) => {
    const checked = (provider === id) ? ' checked' : '';
    return `
      <label class="supervisor-row">
        <input type="radio" name="supervisor-provider" value="${id}"${checked} data-action="supervisor-select" />
        <div class="supervisor-row-body">
          <div class="supervisor-row-name">${label}</div>
          <div class="supervisor-row-desc muted">${desc}</div>
        </div>
      </label>
    `;
  };

  return `
    <article class="panel-block wide supervisor-panel">
      <div class="self-test-head">
        <div class="self-test-summary">
          <span class="self-test-eyebrow">SUPERVISOR AGENT</span>
          <span class="self-test-headline ${tone}">${headline}</span>
          <span class="muted">Choose one AI model to supervise coding agents and make project decisions through MCOS. Generate Config will download a provider-specific JSON; move it to the LAN client running that model and import it there.</span>
        </div>
      </div>
      <div class="supervisor-rows">
        ${row('chatgpt', 'ChatGPT', 'Recommended for design-authority supervision and project-level decision review.')}
        ${row('claude',  'Claude',  'Recommended for Claude Code coordination and local coding-agent workflows.')}
        ${row('grok',    'Grok',    'Recommended for alternate review or independent validation.')}
      </div>
      <div class="supervisor-actions">
        <button type="button" class="route-button accent" data-action="supervisor-generate"${provider ? '' : ' disabled'}>Generate Config &amp; Save</button>
        <button type="button" class="route-button" data-action="supervisor-revoke"${active ? '' : ' disabled'}>Revoke Active</button>
      </div>
      <p class="supervisor-status muted">${statusLine}</p>
    </article>
  `;
}

// v0.9.77: Generate the supervisor config via the runtime, then trigger
// a browser-side download of the embedded config object with the
// suggested filename. Same lifecycle the shell wizard drives, just
// without a native FileSavePicker -- the browser save-as dialog opens
// automatically on Blob URL navigation with the download attribute.
async function generateSupervisorConfig(providerId) {
  if (!providerId) return;
  let response;
  try {
    response = await fetch('/api/supervisor/config/generate', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ providerId: providerId, mode: 'autonomous_supervisor', exclusive: true })
    });
  } catch (e) {
    console.warn('supervisor generate transport error', e);
    return;
  }
  if (!response.ok) {
    let detail = '';
    try { detail = JSON.stringify(await response.json()); } catch (e2) { detail = String(response.status); }
    console.warn('supervisor generate failed', detail);
    await refreshAll();
    return;
  }
  let payload;
  try { payload = await response.json(); } catch (e3) { return; }
  if (!payload || !payload.ok || !payload.config) {
    await refreshAll();
    return;
  }
  // Save the config JSON to disk via a Blob URL + anchor click. Default
  // filename comes from the server-side issuance result; falls back to
  // the spec's canonical name when missing.
  const fileName = payload.fileName || ('mcos-supervisor-' + providerId + '.config.json');
  const blob = new Blob([JSON.stringify(payload.config, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = fileName;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  setTimeout(() => URL.revokeObjectURL(url), 1000);
  await refreshAll();
}

async function revokeSupervisor() {
  try {
    await fetch('/api/supervisor/assignment/revoke', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ reason: 'Browser dashboard Revoke Active button.' })
    });
  } catch (e) {
    console.warn('supervisor revoke transport error', e);
  }
  await refreshAll();
}

// v0.7.1: per-sub-agent live utilization panel.
// Reads state.dashboard.subAgentRuntimeStats. For each sub-agent the
// runtime knows about, renders a card showing the live utilization bar
// (active leases / capacity * 100), the spawned-instance count, and the
// list of LAN clients currently holding leases. utilizationPercent =
// -1.0 means "no managed pool wraps this sub-agent" -- ADR-002 §9 says
// don't fabricate, so we render an honest-unavailable state with a
// pointer at the docs.
function renderSubAgentUtilizationPanel(options) {
  options = options || {};
  const stats = (state.dashboard && state.dashboard.subAgentRuntimeStats) || [];
  if (stats.length === 0) {
    if (options.deck === 'overview') {
      // On the Overview deck, hide the panel entirely when there are no
      // sub-agents. Operators don't need a "you have 0 sub-agents" tile
      // crowding the overview.
      return '';
    }
    return `
      <article class="panel-block wide">
        <h3>Sub-Agents</h3>
        <p class="muted">No sub-agents registered. Use <code>POST /api/runtime/subagents</code> to register one. To enable autoscale + utilization tracking, also register a managed pool with the same id via <code>POST /api/pools</code>.</p>
      </article>
    `;
  }
  const cards = stats.map(renderSubAgentCard).join('');
  const summaryBanner = renderRuntimeStatSummaryBanner(stats, 'sub-agent');
  return `
    <article class="panel-block wide subagent-panel">
      <h3>Sub-Agents (${stats.length})</h3>
      <p class="muted">Live utilization. Per-sub-agent leases routed through the gateway, capacity = readyInstances * maxLeasesPerInstance. Autoscale fires when every Ready instance is at capacity and the pool isn't yet at <code>scalePolicy.maxInstances</code>.</p>
      ${summaryBanner}
      <div class="subagent-card-grid">
        ${cards}
      </div>
    </article>
  `;
}

function renderSubAgentCard(stat) {
  // v0.7.6: utilization is always a real number now (0% when idle, never
  // -1). Tone scales good/warn/critical for the bar fill; 0% still gets
  // the good tone so the card doesn't pulse alarming.
  const utilization = (typeof stat.utilizationPercent === 'number')
    ? Math.max(0, stat.utilizationPercent)
    : 0;
  const hasPool = !!(stat.poolId && stat.poolId.length);
  const utilizationLabel = `${utilization.toFixed(0)}%`;
  const utilizationTone = utilization >= 95 ? 'critical'
                        : utilization >= 75 ? 'warn'
                        : 'good';
  const barWidth = Math.max(0, Math.min(100, utilization));

  // Reachability dot: green if probe succeeded, red if not, gray if no
  // endpoint to probe (sub-agent registration with no host:port).
  const hasEndpoint = !!(stat.endpointHostPort && stat.endpointHostPort.length);
  const reachableTone = !hasEndpoint ? 'unknown'
                       : (stat.reachable ? 'good' : 'bad');
  const reachableLabel = !hasEndpoint ? 'no endpoint'
                        : (stat.reachable ? 'reachable' : 'unreachable');

  // Inventory status passthrough (online / offline / degraded / unknown
  // from the seeded RuntimeEndpoint.status, which may differ from live
  // reachability if the inventory hasn't refreshed since startup).
  const invStatus = (stat.status || 'unknown').toLowerCase();

  const clients = stat.activeClients || [];
  const clientRows = clients.length === 0
    ? '<p class="muted">No active clients leasing this sub-agent.</p>'
    : `<ul class="subagent-client-list">${clients.map((c) => {
        const ip = escapeHtml(c.ipAddress || 'unknown');
        const ct = escapeHtml(c.clientType || 'unknown-client');
        return `<li><code>${ip}</code> <span class="subagent-client-type">${ct}</span></li>`;
      }).join('')}</ul>`;

  const endpointLine = hasEndpoint
    ? `<p class="subagent-endpoint-line"><span class="subagent-reach-dot ${reachableTone}" title="${escapeHtml(reachableLabel)}"></span><code>${escapeHtml(stat.endpointHostPort)}</code> <span class="muted">${escapeHtml(invStatus)}</span></p>`
    : `<p class="muted"><span class="subagent-reach-dot unknown"></span>No host:port registered</p>`;

  const poolLine = hasPool
    ? `<p class="muted">Pool <code>${escapeHtml(stat.poolId)}</code> · ${stat.readyInstanceCount}/${stat.totalInstanceCount} Ready · max ${stat.maxInstancesAllowed} · autoscale ${stat.autoscaleEnabled ? 'on' : 'off'}</p>`
    : '<p class="muted">No managed pool. POST <code>/api/pools</code> with matching id to enable autoscale + per-instance telemetry.</p>';

  const probeLine = stat.lastProbedAtUtc
    ? `<p class="muted subagent-probe-line">Last probed: ${escapeHtml(relativeAgo(stat.lastProbedAtUtc))}</p>`
    : '';

  // v0.9.57: surface the v0.9.56 honest-unavailable diagnostic block.
  // Pre-v0.9.57 the card showed only a red dot for unreachable entries
  // and operators had to query /api/diagnostics/runtime-stats to see
  // why. Now the card carries installState, unavailableReason,
  // lastErrorMessage, and installHint so the operator can read
  // "what does this entry need from me?" without leaving the page.
  const diagnosticBlock = renderRuntimeStatDiagnostic(stat);

  return `
    <div class="subagent-card" data-sub-agent-id="${escapeHtml(stat.subAgentId)}">
      <div class="subagent-card-head">
        <h4>${escapeHtml(stat.displayName || stat.subAgentId)}</h4>
        <span class="subagent-spec">${escapeHtml(stat.specialization || '—')}</span>
      </div>
      <div class="subagent-utilization-row">
        <span class="subagent-utilization-label ${utilizationTone}">${utilizationLabel}</span>
        <div class="subagent-utilization-bar-track">
          <div class="subagent-utilization-bar-fill ${utilizationTone}" style="width: ${barWidth}%;"></div>
        </div>
        <span class="subagent-utilization-counts">${stat.activeLeaseCount} / ${stat.leaseCapacity}</span>
      </div>
      ${endpointLine}
      ${poolLine}
      ${probeLine}
      ${diagnosticBlock}
      <div class="subagent-clients">
        <h5>Active clients (${clients.length})</h5>
        ${clientRows}
      </div>
    </div>
  `;
}

// v0.9.57: render the v0.9.56 honest-unavailable diagnostic fields
// (installState / unavailableReason / lastErrorMessage / lastErrorAtUtc
// / installHint) into a small block on each sub-agent + MCP-server
// card. Returns an empty string when no diagnostic info is available
// (older runtime) so the card layout stays compact.
function renderRuntimeStatDiagnostic(stat) {
  const installState   = (stat && stat.installState)   ? String(stat.installState)   : '';
  const unavailable    = (stat && stat.unavailableReason) ? String(stat.unavailableReason) : '';
  const errorMsg       = (stat && stat.lastErrorMessage)  ? String(stat.lastErrorMessage)  : '';
  const errorAtUtc     = (stat && stat.lastErrorAtUtc)    ? String(stat.lastErrorAtUtc)    : '';
  const hint           = (stat && stat.installHint)       ? String(stat.installHint)       : '';
  if (!installState && !unavailable && !errorMsg && !hint) return '';

  // Map installState to a tone for the pill chip.
  let pillTone = 'info';
  let pillLabel = installState || 'unknown';
  if (installState === 'installed_and_supervised') {
    pillTone = 'good';
    pillLabel = 'supervised';
  } else if (installState === 'online_via_admin_port' || installState === 'online_via_external_listener') {
    pillTone = 'warn';
    pillLabel = installState === 'online_via_admin_port' ? 'online (admin port)' : 'online (external)';
  } else if (installState === 'supervised_pool_not_ready') {
    pillTone = 'critical';
    pillLabel = 'pool stuck';
  } else if (installState === 'awaiting_pool_registration') {
    pillTone = 'critical';
    pillLabel = 'no pool registered';
  } else if (installState === 'unknown' || installState === '') {
    pillTone = 'info';
    pillLabel = 'unclassified';
  }

  // Only render the error / hint lines when there's content; healthy
  // entries get just the pill chip.
  const reasonChip = unavailable
    ? `<span class="subagent-diag-reason">${escapeHtml(unavailable)}</span>`
    : '';
  const errorLine = errorMsg
    ? `<p class="subagent-diag-error muted"><strong>Last probe:</strong> ${escapeHtml(errorMsg)}${errorAtUtc ? ` <span class="muted">(${escapeHtml(relativeAgo(errorAtUtc))})</span>` : ''}</p>`
    : '';
  const hintLine = hint
    ? `<p class="subagent-diag-hint"><strong>Next step:</strong> ${escapeHtml(hint)}</p>`
    : '';

  // v0.9.58: surface the optional installCommand as a copy-paste-able
  // code block so operators can install a known canonical npm package
  // for the entry without having to guess the vendor. Only renders
  // when the runtime emits a non-empty installCommand for the entry
  // id (currently: chrome-devtools and the four already-pooled MCPs).
  // v0.10.0: playwright removed from the catalog and from this list.
  // v0.9.60: when the runtime has detected the canonical package
  // already exists on disk, swap the "Install:" line for an
  // "Already installed:" line so operators know they can skip
  // straight to pool registration.
  const installCmd = (stat && stat.installCommand) ? String(stat.installCommand) : '';
  const packageDetected = !!(stat && stat.installPackageDetected);
  let installCmdLine = '';
  if (installCmd) {
    if (packageDetected) {
      installCmdLine = `<p class="subagent-diag-install-cmd"><strong>Already installed:</strong> <code>${escapeHtml(installCmd.replace(/^npm install -g /, ''))}</code> <span class="subagent-diag-detected-pill">detected</span></p>`;
    } else {
      installCmdLine = `<p class="subagent-diag-install-cmd"><strong>Install:</strong> <code>${escapeHtml(installCmd)}</code></p>`;
    }
  }

  return `
    <div class="subagent-diagnostic-block">
      <div class="subagent-diag-pill-row">
        <span class="subagent-diag-pill ${pillTone}">${escapeHtml(pillLabel)}</span>
        ${reasonChip}
      </div>
      ${errorLine}
      ${hintLine}
      ${installCmdLine}
    </div>
  `;
}

function renderClaudePluginCard() {
  const cp = state.claudePlugin;
  const busy = state.claudePluginBusy;
  if (!cp) {
    return `
      <article class="panel-block">
        <h3>Claude Code Control</h3>
        <p class="big-stat warn">unknown</p>
        <p class="muted">Plugin status surface unavailable. The runtime may be older than 0.6.1.</p>
      </article>
    `;
  }
  const registered = cp.registered === true;
  const userResolved = cp.activeUserResolved === true;
  const headline = busy
    ? 'Working…'
    : (registered
        ? `Connected as ${cp.userName || '—'}`
        : (userResolved
            ? `Disconnected${cp.userName ? ' (' + cp.userName + ')' : ''}`
            : 'No interactive Windows user resolved'));
  const tone = registered ? 'good' : (userResolved ? 'warn' : 'bad');
  // Render the toggle switch as an HTML <input type="checkbox"> wrapped in
  // a <label.toggle-switch>. The checked state mirrors `registered`. The
  // toggle is always interactive (not disabled) so the operator can see
  // immediate feedback even when the runtime would refuse — refusal is
  // shown in a status line instead.
  const checkedAttr = registered ? ' checked' : '';
  const detailLine = userResolved
    ? `<p class="muted">Drops <code>${escapeHtml(cp.target || '%USERPROFILE%\\.claude\\plugins\\mcos-control')}</code> as a junction onto the install directory's bundled plugin source.</p>`
    : `<p class="muted">Sign in to Windows on the host first.${cp.lastError ? ' ' + escapeHtml(cp.lastError) : ''}</p>`;
  const errorLine = (cp.lastError && userResolved && !registered)
    ? `<p class="muted">${escapeHtml(cp.lastError)}</p>`
    : '';
  return `
    <article class="panel-block">
      <h3>Claude Code Control</h3>
      <p class="big-stat ${tone}">${escapeHtml(headline)}</p>
      <label class="toggle-switch" title="Toggle to register / unregister the mcos-control Claude Code plugin for the active Windows user.">
        <input type="checkbox" data-action="toggle-claude-plugin"${checkedAttr}>
        <span class="toggle-track"></span>
        <span class="toggle-label">${registered ? 'Connected' : 'Disconnected'}</span>
      </label>
      ${detailLine}
      ${errorLine}
    </article>
  `;
}

async function toggleClaudePlugin(event) {
  if (state.claudePluginBusy) {
    if (event && event.target && event.target.checked !== undefined) {
      // Bounce the checkbox back to whatever the cached state says.
      event.target.checked = !!(state.claudePlugin && state.claudePlugin.registered);
    }
    return;
  }
  // The HTML toggle's click flips `checked` synchronously before the change
  // event fires. We don't read it — the runtime always interprets POST
  // /api/claude-plugin/toggle as "flip" relative to its own ground truth.
  state.claudePluginBusy = true;
  renderCurrent();
  try {
    const result = await loadJson('/api/claude-plugin/toggle', { method: 'POST' });
    state.claudePlugin = result;
    if (result && result.ok === false && result.lastError) {
      showBanner('Claude Code toggle: ' + result.lastError);
    } else {
      clearBanner();
    }
  } catch (err) {
    showBanner('Claude Code toggle failed: ' + (err && err.message ? err.message : 'unknown'));
  } finally {
    state.claudePluginBusy = false;
    renderCurrent();
  }
}

function bindOverviewHandlers() {
  const navMap = {
    'goto-gateway': 'gateway',
    'goto-pools': 'pools',
    'goto-clients': 'telemetry-clients',
    'goto-governance': 'governance'
  };
  Object.keys(navMap).forEach((action) => {
    const btn = document.querySelector('[data-action="' + action + '"]');
    if (btn) {
      btn.addEventListener('click', () => {
        state.destination = navMap[action];
        renderCurrent();
      });
    }
  });
  const toggleSwitch = document.querySelector('[data-action="toggle-claude-plugin"]');
  if (toggleSwitch) {
    // Use 'change' instead of 'click' so keyboard activation (space bar
    // when the checkbox is focused) also drives the toggle.
    toggleSwitch.addEventListener('change', toggleClaudePlugin);
  }
  // v0.9.75: re-run self-tests button. POSTs /api/self-tests/run and
  // re-renders the panel with the freshly-computed snapshot.
  const rerunBtn = document.querySelector('[data-action="rerun-selftests"]');
  if (rerunBtn) {
    rerunBtn.addEventListener('click', rerunSelfTests);
  }
  // v0.9.77: Supervisor Agent radio selection + Generate / Revoke.
  document.querySelectorAll('[data-action="supervisor-select"]').forEach((radio) => {
    radio.addEventListener('change', () => {
      const btn = document.querySelector('[data-action="supervisor-generate"]');
      if (btn) btn.disabled = !radio.checked;
    });
  });
  const supGen = document.querySelector('[data-action="supervisor-generate"]');
  if (supGen) {
    supGen.addEventListener('click', () => {
      const checked = document.querySelector('input[name="supervisor-provider"]:checked');
      if (!checked) return;
      generateSupervisorConfig(checked.value);
    });
  }
  const supRev = document.querySelector('[data-action="supervisor-revoke"]');
  if (supRev) {
    supRev.addEventListener('click', revokeSupervisor);
  }
}

// ---- LAN Clients ----

function renderClients() {
  const selected = state.selectedClientId
    ? (state.selectedClientId === '__new__'
        ? 'new'
        : state.clients.find((c) => c.clientId === state.selectedClientId))
    : null;

  return `
    <div class="clients-shell">
      <section class="clients-list-pane">
        <div class="pane-toolbar">
          <button type="button" data-action="new-client" class="route-button accent">+ Register</button>
          <button type="button" data-action="refresh" class="route-button">Refresh</button>
        </div>
        ${renderClientsTable()}
      </section>
      <section class="clients-detail-pane">
        ${selected === 'new'
          ? renderClientRegisterForm()
          : (selected ? renderClientDrawer(selected) : renderClientEmptyState())}
      </section>
    </div>
  `;
}

function renderClientsTable() {
  if (state.clients.length === 0) {
    return `<p class="muted">No LAN clients registered yet. Use <strong>+ Register</strong> to add one.</p>`;
  }
  const rows = state.clients.map((client) => {
    const privCount = PRIVILEGES.filter((p) => client.privileges && client.privileges[p.key]).length;
    const isActive = state.selectedClientId === client.clientId;
    return `
      <tr class="clients-row${isActive ? ' active' : ''}" data-client-id="${escapeHtml(client.clientId)}">
        <td>
          <div class="cell-primary">${escapeHtml(client.displayName || client.clientId)}</div>
          <div class="cell-secondary"><code>${escapeHtml(client.clientId)}</code></div>
        </td>
        <td>${escapeHtml(client.clientType || '—')}</td>
        <td>${client.enabled ? '<span class="badge" data-tone="success">Enabled</span>' : '<span class="badge" data-tone="danger">Disabled</span>'}</td>
        <td>${client.autonomousMode ? '<span class="badge" data-tone="warn">Autonomous</span>' : '<span class="badge" data-tone="info">Standard</span>'}</td>
        <td>${privCount}/${PRIVILEGES.length}</td>
        <td>${escapeHtml(relativeAgo(client.lastSeenUtc))}</td>
      </tr>
    `;
  }).join('');
  return `
    <table class="clients-table">
      <thead>
        <tr><th>Client</th><th>Type</th><th>Status</th><th>Mode</th><th>Privileges</th><th>Last seen</th></tr>
      </thead>
      <tbody>${rows}</tbody>
    </table>
  `;
}

function renderClientEmptyState() {
  return `
    <div class="empty-detail">
      <h3>Select a LAN client</h3>
      <p class="muted">Pick a client from the list, or register a new one.</p>
    </div>
  `;
}

function renderClientRegisterForm() {
  return `
    <form class="client-form" data-form="register-client">
      <h3>Register LAN client</h3>
      <p class="muted">Identity is by clientId on a trusted LAN. The registered client uses the X-MCOS-Client-Id header to identify itself; no tokens are exchanged.</p>
      <label>Client id (slug)
        <input type="text" name="clientId" required placeholder="claude-code-jdaley-wks" />
      </label>
      <label>Display name
        <input type="text" name="displayName" required placeholder="Claude Code on Jdaley workstation" />
      </label>
      <label>Client type
        <input type="text" name="clientType" placeholder="claude_code | codex | grok | other" />
      </label>
      <label>Host name
        <input type="text" name="hostName" placeholder="PC-GAMING-R7-58" />
      </label>
      <div class="form-actions">
        <button type="submit" class="route-button accent">Register</button>
        <button type="button" data-action="cancel-register" class="route-button">Cancel</button>
      </div>
      <div class="form-status" data-role="status"></div>
    </form>
  `;
}

function renderClientDrawer(client) {
  const privileges = client.privileges || {};
  const privilegeRows = PRIVILEGES.map((p) => `
    <label class="privilege-row">
      <input type="checkbox" data-priv="${escapeHtml(p.key)}" ${privileges[p.key] ? 'checked' : ''} />
      <span>${escapeHtml(p.label)}</span>
      <code class="privilege-key">${escapeHtml(p.key)}</code>
    </label>
  `).join('');

  return `
    <div class="client-drawer" data-client-id="${escapeHtml(client.clientId)}">
      <header class="drawer-heading">
        <div>
          <h3>${escapeHtml(client.displayName || client.clientId)}</h3>
          <p class="muted"><code>${escapeHtml(client.clientId)}</code> · ${escapeHtml(client.clientType || 'unspecified')}</p>
        </div>
        <div class="drawer-status-badges">
          ${client.enabled ? '<span class="badge" data-tone="success">Enabled</span>' : '<span class="badge" data-tone="danger">Disabled</span>'}
          ${client.autonomousMode ? '<span class="badge" data-tone="warn">Autonomous</span>' : ''}
        </div>
      </header>

      <section class="drawer-section">
        <h4>Lifecycle</h4>
        <div class="action-row">
          <button type="button" data-action="config-download" class="route-button accent">Download config bundle</button>
          ${client.enabled
            ? '<button type="button" data-action="disable" class="route-button">Disable</button>'
            : '<button type="button" data-action="enable" class="route-button">Enable</button>'}
          <button type="button" data-action="remove" class="route-button danger">Remove</button>
        </div>
        <ul class="kv-list">
          <li><span>Created</span><strong>${escapeHtml(shortDate(client.createdAtUtc))}</strong></li>
          <li><span>Last seen</span><strong>${escapeHtml(relativeAgo(client.lastSeenUtc))}</strong></li>
          <li><span>Disabled at</span><strong>${escapeHtml(client.disabledAtUtc ? shortDate(client.disabledAtUtc) : '—')}</strong></li>
          <li><span>Network</span><strong>${escapeHtml(client.networkAddress || '—')}</strong></li>
        </ul>
      </section>

      <section class="drawer-section">
        <h4>Privileges</h4>
        <p class="muted">Use is universal across the shared fabric. Only mutations are gated.</p>
        <div class="privilege-list">${privilegeRows}</div>
        <div class="action-row">
          <button type="button" data-action="save-privileges" class="route-button accent">Save privileges</button>
          <button type="button" data-action="reset-privileges" class="route-button">Reset</button>
        </div>
      </section>

      <section class="drawer-section">
        <h4>Autonomous mode</h4>
        <p class="muted">When enabled, this client may create MCP servers and sub-agents without holding the matching create privilege. Other actions stay privilege-gated. Requires global aiAutonomyEnabled to be true (CLU-C009).</p>
        <div class="action-row">
          ${client.autonomousMode
            ? '<button type="button" data-action="autonomous-off" class="route-button">Disable autonomous</button>'
            : '<button type="button" data-action="autonomous-on" class="route-button accent">Enable autonomous</button>'}
        </div>
      </section>

      <div class="drawer-status" data-role="drawer-status"></div>
    </div>
  `;
}

function bindClientsHandlers() {
  const newBtn = document.querySelector('[data-action="new-client"]');
  if (newBtn) newBtn.addEventListener('click', () => { state.selectedClientId = '__new__'; renderCurrent(); });

  const refreshBtn = document.querySelector('[data-action="refresh"]');
  if (refreshBtn) refreshBtn.addEventListener('click', () => { refreshAll(); });

  document.querySelectorAll('.clients-row').forEach((row) => {
    row.addEventListener('click', () => {
      state.selectedClientId = row.dataset.clientId;
      renderCurrent();
    });
  });

  const cancel = document.querySelector('[data-action="cancel-register"]');
  if (cancel) cancel.addEventListener('click', () => { state.selectedClientId = null; renderCurrent(); });

  const form = document.querySelector('form[data-form="register-client"]');
  if (form) form.addEventListener('submit', handleClientRegisterSubmit);

  const drawer = document.querySelector('.client-drawer');
  if (drawer) bindDrawerHandlers(drawer);
}

async function handleClientRegisterSubmit(event) {
  event.preventDefault();
  const form = event.currentTarget;
  const data = {
    clientId: form.clientId.value.trim(),
    displayName: form.displayName.value.trim(),
    clientType: form.clientType.value.trim(),
    hostName: form.hostName.value.trim()
  };
  const status = form.querySelector('[data-role="status"]');
  status.textContent = 'Registering…';
  try {
    const result = await loadJson('/api/clients', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(data)
    });
    if (result && result.succeeded) {
      status.textContent = 'Registered.';
      state.selectedClientId = data.clientId.toLowerCase();
      await refreshAll();
    } else {
      status.textContent = (result && result.message) || 'Registration failed.';
    }
  } catch (err) {
    const body = err.body || {};
    status.textContent = (body && (body.message || body.errorMessage)) || ('Registration failed: ' + err.message);
  }
}

function bindDrawerHandlers(drawer) {
  const clientId = drawer.dataset.clientId;
  const status = drawer.querySelector('[data-role="drawer-status"]');

  function setStatus(text, tone) {
    status.textContent = text;
    status.dataset.tone = tone || 'info';
  }

  drawer.querySelector('[data-action="config-download"]').addEventListener('click', async () => {
    setStatus('Issuing config bundle…');
    try {
      const bundle = await loadJson('/api/clients/' + encodeURIComponent(clientId) + '/config');
      const blob = new Blob([JSON.stringify(bundle, null, 2)], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const link = document.createElement('a');
      link.href = url;
      link.download = 'lan-client-' + clientId + '.json';
      link.click();
      URL.revokeObjectURL(url);
      setStatus('Bundle downloaded.', 'success');
    } catch (err) {
      setStatus('Download failed: ' + (err.message || ''), 'danger');
    }
  });

  const disableBtn = drawer.querySelector('[data-action="disable"]');
  if (disableBtn) disableBtn.addEventListener('click', () => postClient(clientId, 'disable', 'Disabled.', setStatus));

  const enableBtn = drawer.querySelector('[data-action="enable"]');
  if (enableBtn) enableBtn.addEventListener('click', () => postClient(clientId, 'enable', 'Enabled.', setStatus));

  drawer.querySelector('[data-action="remove"]').addEventListener('click', async () => {
    if (!confirm('Remove LAN client ' + clientId + '? This cannot be undone.')) return;
    setStatus('Removing…');
    try {
      await loadJson('/api/clients/' + encodeURIComponent(clientId), { method: 'DELETE' });
      state.selectedClientId = null;
      await refreshAll();
    } catch (err) {
      setStatus('Remove failed: ' + (err.message || ''), 'danger');
    }
  });

  drawer.querySelector('[data-action="save-privileges"]').addEventListener('click', async () => {
    const payload = {};
    PRIVILEGES.forEach((p) => {
      const cb = drawer.querySelector('input[data-priv="' + p.key + '"]');
      payload[p.key] = !!(cb && cb.checked);
    });
    setStatus('Saving privileges…');
    try {
      const result = await loadJson('/api/clients/' + encodeURIComponent(clientId) + '/privileges', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      });
      setStatus(result && result.succeeded ? 'Privileges saved.' : (result && result.message) || 'Save failed.', 'success');
      await refreshAll();
    } catch (err) {
      const body = err.body || {};
      setStatus((body && (body.message || body.errorMessage)) || 'Save failed: ' + err.message, 'danger');
    }
  });

  drawer.querySelector('[data-action="reset-privileges"]').addEventListener('click', () => {
    const client = state.clients.find((c) => c.clientId === clientId);
    if (!client) return;
    PRIVILEGES.forEach((p) => {
      const cb = drawer.querySelector('input[data-priv="' + p.key + '"]');
      if (cb) cb.checked = !!(client.privileges && client.privileges[p.key]);
    });
    setStatus('Reset to last saved.', 'info');
  });

  const onAuto = drawer.querySelector('[data-action="autonomous-on"]');
  if (onAuto) onAuto.addEventListener('click', () => setAutonomous(clientId, true, setStatus));
  const offAuto = drawer.querySelector('[data-action="autonomous-off"]');
  if (offAuto) offAuto.addEventListener('click', () => setAutonomous(clientId, false, setStatus));
}

async function postClient(clientId, action, successMessage, setStatus) {
  setStatus(action === 'enable' ? 'Enabling…' : 'Disabling…');
  try {
    const result = await loadJson('/api/clients/' + encodeURIComponent(clientId) + '/' + action, { method: 'POST' });
    setStatus((result && result.message) || successMessage, 'success');
    await refreshAll();
  } catch (err) {
    const body = err.body || {};
    setStatus((body && (body.message || body.errorMessage)) || (action + ' failed: ' + err.message), 'danger');
  }
}

async function setAutonomous(clientId, enabled, setStatus) {
  setStatus(enabled ? 'Enabling autonomous…' : 'Disabling autonomous…');
  try {
    const response = await fetch('/api/clients/' + encodeURIComponent(clientId) + '/autonomous-mode', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ enabled: enabled })
    });
    const text = await response.text();
    let parsed = null;
    try { parsed = JSON.parse(text); } catch (_) {}
    if (response.ok) {
      setStatus((parsed && parsed.message) || (enabled ? 'Autonomous enabled.' : 'Autonomous disabled.'), 'success');
      await refreshAll();
    } else {
      const reason = parsed && (parsed.errorMessage || parsed.message)
        ? (parsed.errorMessage || parsed.message)
        : ('Request failed (' + response.status + ').');
      setStatus(reason, 'danger');
    }
  } catch (err) {
    setStatus('Request failed: ' + (err.message || ''), 'danger');
  }
}

// ---- Governance ----

function renderGovernance() {
  const gov = (state.dashboard && state.dashboard.governance) || {};
  const pending = state.approvals.filter((a) => a.status === 'pending');
  const decided = state.approvals.filter((a) => a.status !== 'pending');
  const platforms = ['windows', 'macos', 'ios'];
  const bundle = state.governanceBundle || {};

  return `
    <div class="governance-shell">
      <article class="panel-block">
        <h3>Posture</h3>
        <p class="big-stat ${gov.posture === 'pass' ? 'good' : (gov.posture === 'blocked' ? 'bad' : 'warn')}">${escapeHtml(gov.posture || 'unknown')}</p>
        <p class="muted">Authority: ${escapeHtml(gov.unitName || 'Command Logic Unit')}</p>
        <p class="muted">Last evaluated: ${escapeHtml(shortDate(gov.lastEvaluatedUtc))}</p>
      </article>
      <article class="panel-block wide">
        <h3>Governance bundles</h3>
        <p class="muted">Distributed via <code>/api/governance/bundles/{platform}</code> (PHASE-05). Forsetti Framework + Forsetti Framework for Agentic Coding instructions, rules JSON, decision policy, and a sha256 checksum.</p>
        <div class="onboarding-tabs">
          ${platforms.map((p) => `
            <button type="button"
                    class="route-button ${p === state.governanceBundlePlatform ? 'accent' : ''}"
                    data-action="governance-tab" data-platform="${escapeHtml(p)}">
              ${escapeHtml(p)}
            </button>
          `).join('')}
        </div>
        <ul class="kv-list">
          <li><span>Platform</span><strong>${escapeHtml(bundle.platform || state.governanceBundlePlatform)}</strong></li>
          <li><span>Forsetti framework</span><strong>${escapeHtml(bundle.forsettiFrameworkVersion || '—')}</strong></li>
          <li><span>Agentic coding</span><strong>${escapeHtml(bundle.agenticCodingFrameworkVersion || '—')}</strong></li>
          <li><span>CLU schema</span><strong>${escapeHtml(bundle.cluSchemaVersion || '—')}</strong></li>
          <li><span>Generated</span><strong>${escapeHtml(shortDate(bundle.generatedAt))}</strong></li>
          <li><span>Checksum</span><strong><code>${escapeHtml(bundle.checksum || '—')}</code></strong></li>
        </ul>
        <a class="route-button" href="/api/governance/bundles/${escapeHtml(state.governanceBundlePlatform)}" download>Download bundle JSON</a>
      </article>
      <article class="panel-block wide">
        <h3>Pending approvals (${pending.length})</h3>
        ${pending.length === 0
          ? '<p class="muted">No deferred actions awaiting approval.</p>'
          : pending.map(renderApprovalRow).join('')}
      </article>
      <article class="panel-block wide">
        <h3>Decisions</h3>
        ${decided.length === 0
          ? '<p class="muted">No decisions on record yet.</p>'
          : decided.slice().reverse().slice(0, 20).map(renderDecisionRow).join('')}
      </article>
      <article class="panel-block wide">
        <h3>Rules</h3>
        ${(gov.rules || []).map((r) => `
          <div class="rule-row">
            <code>${escapeHtml(r.ruleId || '')}</code>
            <strong>${escapeHtml(r.title || '')}</strong>
            <span class="rule-severity" data-tone="${escapeHtml((r.severity || 'medium').toLowerCase())}">${escapeHtml(r.severity || '')}</span>
            <p class="muted">${escapeHtml(r.description || '')}</p>
          </div>
        `).join('')}
      </article>
    </div>
  `;
}

function renderApprovalRow(action) {
  return `
    <div class="approval-row" data-deferred-id="${escapeHtml(action.id)}">
      <div class="approval-meta">
        <code>${escapeHtml(action.id)}</code>
        <span>${escapeHtml(action.action || '')}</span>
        <span>actor: <strong>${escapeHtml(action.actor || '—')}</strong></span>
        <span>${escapeHtml(relativeAgo(action.createdAtUtc))}</span>
      </div>
      <div class="approval-detail muted">${escapeHtml(action.reason || '')}</div>
      ${action.targetId ? `<div class="approval-detail">Target: <code>${escapeHtml(action.targetId)}</code></div>` : ''}
      <div class="action-row">
        <button type="button" data-action="approve" class="route-button accent">Approve</button>
        <button type="button" data-action="reject" class="route-button danger">Reject</button>
      </div>
    </div>
  `;
}

function renderDecisionRow(action) {
  return `
    <div class="decision-row" data-tone="${action.status === 'approved' ? 'success' : 'danger'}">
      <code>${escapeHtml(action.id)}</code>
      <span>${escapeHtml(action.action || '')}</span>
      <span class="decision-status">${escapeHtml(action.status)}</span>
      <span>by ${escapeHtml(action.decidedBy || 'operator')}</span>
      <span>${escapeHtml(relativeAgo(action.decidedAtUtc))}</span>
      ${action.reason ? `<span class="muted">${escapeHtml(action.reason)}</span>` : ''}
    </div>
  `;
}

function bindGovernanceHandlers() {
  document.querySelectorAll('.approval-row').forEach((row) => {
    const id = row.dataset.deferredId;
    const approve = row.querySelector('[data-action="approve"]');
    const reject = row.querySelector('[data-action="reject"]');
    if (approve) approve.addEventListener('click', () => approveDeferred(id));
    if (reject) reject.addEventListener('click', () => rejectDeferred(id));
  });
  document.querySelectorAll('[data-action="governance-tab"]').forEach((btn) => {
    btn.addEventListener('click', async () => {
      state.governanceBundlePlatform = btn.dataset.platform;
      try {
        state.governanceBundle = await loadJson(
          '/api/governance/bundles/' + encodeURIComponent(state.governanceBundlePlatform)
        );
      } catch (_) {
        state.governanceBundle = null;
      }
      renderCurrent();
    });
  });
}

async function approveDeferred(id) {
  try {
    await loadJson('/api/clu/approvals/' + encodeURIComponent(id) + '/approve', { method: 'POST' });
    await refreshAll();
  } catch (err) {
    const body = err.body || {};
    showBanner((body && (body.message || body.errorMessage)) || 'Approve failed: ' + err.message);
  }
}

async function rejectDeferred(id) {
  const reason = prompt('Reason for rejecting ' + id + '?');
  if (reason === null) return;
  try {
    await loadJson('/api/clu/approvals/' + encodeURIComponent(id) + '/reject', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ reason: reason })
    });
    await refreshAll();
  } catch (err) {
    const body = err.body || {};
    showBanner((body && (body.message || body.errorMessage)) || 'Reject failed: ' + err.message);
  }
}

// ---- Shared fabric ----

function renderEndpointTemplateCards(templates, label, setupDestination) {
  if (!templates || templates.length === 0) {
    return '';
  }
  return `
    <article class="panel-block wide template-card-section">
      <div class="section-split-heading">
        <div>
          <p class="eyebrow">GUIDED TASK CARDS</p>
          <h3>${escapeHtml(label)} templates (${templates.length})</h3>
        </div>
        <span class="muted">Templates are setup tasks, not live inventory.</span>
      </div>
      <div class="template-card-grid">
        ${templates.map((endpoint) => {
          const risk = endpoint.risk || (endpoint.highRisk ? 'high' : 'standard');
          const tone = riskTone(risk);
          return `
            <article class="template-card" data-risk="${escapeHtml(tone)}">
              <div class="template-card-head">
                <span class="badge-template">TEMPLATE</span>
                <span class="risk-pill ${escapeHtml(tone)}">${escapeHtml(risk || 'standard')}</span>
              </div>
              <h3>${escapeHtml(endpoint.displayName || endpoint.id || 'Template')}</h3>
              <p class="muted">${escapeHtml(endpoint.description || endpoint.specialization || '')}</p>
              <div class="capability-chip-row">${capabilityChips(endpoint.requiredCapabilities)}</div>
              <button type="button" class="route-button" data-action="template-open-setup" data-destination="${escapeHtml(setupDestination || 'setup')}">Configure</button>
            </article>
          `;
        }).join('')}
      </div>
    </article>
  `;
}

function renderRuntime() {
  const endpoints = (state.dashboard && state.dashboard.endpoints) || [];
  const mcp = endpoints.filter((e) => e.kind === 'mcp_server' && !e.isTemplate);
  const subs = endpoints.filter((e) => e.kind === 'sub_agent' && !e.isTemplate);
  const mcpTemplates = endpoints.filter((e) => e.kind === 'mcp_server' && e.isTemplate);
  const subTemplates = endpoints.filter((e) => e.kind === 'sub_agent' && e.isTemplate);
  return `
    <div class="runtime-grid">
      ${renderEndpointTemplateCards(mcpTemplates, 'MCP server', 'setup')}
      ${renderEndpointTemplateCards(subTemplates, 'Specialist', 'setup')}
      ${renderMcpServerUtilizationPanel({ deck: 'runtime' })}
      <article class="panel-block wide">
        <h3>MCP Servers (${mcp.length}) — configured inventory</h3>
        <p class="muted">Only configured or live MCP servers appear here. Template candidates stay in guided task cards above.</p>
        ${endpointTable(mcp)}
      </article>
      ${renderSubAgentUtilizationPanel({ deck: 'runtime' })}
      <article class="panel-block wide">
        <h3>Sub-Agents (${subs.length}) — configured inventory</h3>
        <p class="muted">Only configured or live specialist lanes appear here. Setup templates are visually separated from active inventory.</p>
        ${subAgentTable(subs)}
      </article>
    </div>
  `;
}

function bindRuntimeHandlers() {
  document.querySelectorAll('[data-action="template-open-setup"]').forEach((btn) => {
    btn.addEventListener('click', () => {
      state.destination = normalizeDestinationId(btn.dataset.destination, 'setup');
      renderCurrent();
    });
  });
}


// v0.8.3: per-MCP-server live utilization panel. Mirrors the
// sub-agent panel (v0.7.1+) so both endpoint kinds share the same
// card surface: utilization bar, reachability dot, host:port,
// active-client list. state.dashboard.mcpServerRuntimeStats carries
// the runtime-side telemetry; runtime fills the same shape as for
// sub-agents.
function renderMcpServerUtilizationPanel(options) {
  options = options || {};
  const stats = (state.dashboard && state.dashboard.mcpServerRuntimeStats) || [];
  if (stats.length === 0) {
    return `
      <article class="panel-block wide">
        <h3>MCP Servers</h3>
        <p class="muted">No MCP servers registered. Use <code>POST /api/runtime/mcp-servers</code> (or seed via the inventory) to register one. Adding a managed pool with the same id via <code>POST /api/pools</code> enables autoscale + utilization tracking.</p>
      </article>
    `;
  }
  const cards = stats.map(renderMcpServerCard).join('');
  const summaryBanner = renderRuntimeStatSummaryBanner(stats, 'MCP server');
  return `
    <article class="panel-block wide subagent-panel">
      <h3>MCP Servers (${stats.length})</h3>
      <p class="muted">Live utilization. Probes reachability per server (TCP connect, 200 ms timeout). Active leases show which LAN clients are using each server right now. Same telemetry shape as sub-agent cards.</p>
      ${summaryBanner}
      <div class="subagent-card-grid">
        ${cards}
      </div>
    </article>
  `;
}

// v0.9.59: at-a-glance summary banner above each card grid. Pre-v0.9.59
// the grid header read "MCP Servers (28)" -- operators saw a count but
// no breakdown of how many were actually live. v0.9.59 inserts a
// summary line right under the heading: total / reachable / unreachable
// + an installState bucket pill row (supervised / online / pool stuck /
// no pool / unclassified). The pills are clickable jumpers in a future
// iteration; for now they are read-only chips. Reads runtime stats
// already in state.dashboard so no extra HTTP call.
function renderRuntimeStatSummaryBanner(stats, kindLabel) {
  if (!stats || stats.length === 0) return '';
  const total = stats.length;
  let reachable = 0;
  const buckets = {
    installed_and_supervised: 0,
    online_via_admin_port: 0,
    online_via_external_listener: 0,
    supervised_pool_not_ready: 0,
    awaiting_pool_registration: 0,
    unknown: 0
  };
  let withInstallCommand = 0;
  stats.forEach((s) => {
    if (s.reachable) reachable += 1;
    const key = (s.installState && buckets.hasOwnProperty(s.installState))
      ? s.installState
      : 'unknown';
    buckets[key] += 1;
    if (s.installCommand && s.installCommand.length) withInstallCommand += 1;
  });
  const unreachable = total - reachable;
  const reachTone = reachable === total ? 'good'
                  : reachable > 0       ? 'warn'
                  : 'critical';

  // Build the bucket pill row. Suppress zero-count buckets so the
  // banner stays compact; an operator on a clean install with all
  // entries supervised sees only the green "supervised: N" pill.
  const pillSpec = [
    { key: 'installed_and_supervised',     label: 'supervised',           tone: 'good'     },
    { key: 'online_via_admin_port',        label: 'online (admin port)',  tone: 'warn'     },
    { key: 'online_via_external_listener', label: 'online (external)',    tone: 'warn'     },
    { key: 'supervised_pool_not_ready',    label: 'pool stuck',           tone: 'critical' },
    { key: 'awaiting_pool_registration',   label: 'no pool registered',   tone: 'critical' },
    { key: 'unknown',                      label: 'unclassified',         tone: 'info'     }
  ];
  const pills = pillSpec
    .filter((p) => buckets[p.key] > 0)
    .map((p) => `<span class="subagent-summary-pill ${p.tone}">${escapeHtml(p.label)}: ${buckets[p.key]}</span>`)
    .join('');

  const installLine = withInstallCommand > 0
    ? `<p class="subagent-summary-install muted">${withInstallCommand} of the placeholders carry a known canonical install command — copy it from the entry's diagnostic block.</p>`
    : '';

  return `
    <div class="subagent-summary-banner">
      <div class="subagent-summary-headline">
        <span class="subagent-summary-count ${reachTone}">${reachable} / ${total} reachable</span>
        <span class="subagent-summary-sep">·</span>
        <span class="muted">${unreachable} ${kindLabel}${unreachable === 1 ? '' : 's'} need attention</span>
      </div>
      <div class="subagent-summary-pill-row">
        ${pills}
      </div>
      ${installLine}
    </div>
  `;
}

function renderMcpServerCard(stat) {
  // v0.8.3: reuses renderSubAgentCard's exact visual structure -- the
  // shape of the data is identical, only the field name for the id
  // differs (mcpServerId vs subAgentId), so we synthesize a
  // sub-agent-shaped object and delegate.
  const synthetic = Object.assign({}, stat, {
    subAgentId: stat.mcpServerId || stat.subAgentId,
  });
  return renderSubAgentCard(synthetic);
}

// v0.7.1: extended sub-agents table with live utilization + active-client
// columns sourced from state.dashboard.subAgentRuntimeStats. The base
// inventory columns (id / name / host / port / status / specialization)
// stay; the new columns light up only when a managed pool wraps the
// sub-agent (operator wires this with `POST /api/pools` using poolId
// equal to the sub-agent's id). For sub-agents without a managed pool,
// the new columns render `unavailable` honestly per ADR-002 §9.
function subAgentTable(endpoints) {
  if (endpoints.length === 0) {
    return '<p class="muted">No entries registered yet.</p>';
  }
  const stats = (state.dashboard && state.dashboard.subAgentRuntimeStats) || [];
  const statById = {};
  stats.forEach((s) => { statById[s.subAgentId] = s; });
  return `
    <table class="runtime-table">
      <thead><tr>
        <th>Id</th><th>Display name</th><th>Host</th><th>Port</th><th>Status</th>
        <th>Specialization</th><th>Utilization</th><th>Instances</th><th>Active clients</th>
      </tr></thead>
      <tbody>
        ${endpoints.map((e) => {
          const stat = statById[e.id];
          const utilization = stat && typeof stat.utilizationPercent === 'number' ? stat.utilizationPercent : -1;
          const utilLabel = utilization < 0 ? 'unavailable' : `${utilization.toFixed(0)}%`;
          const instances = stat ? `${stat.readyInstanceCount}/${stat.totalInstanceCount}` : '—';
          const clientList = stat && stat.activeClients && stat.activeClients.length > 0
            ? stat.activeClients.map((c) => `${c.ipAddress || 'unknown'} (${c.clientType || 'unknown-client'})`).join(', ')
            : (stat ? '—' : 'unavailable');
          return `
            <tr>
              <td><code>${escapeHtml(e.id)}</code></td>
              <td>${escapeHtml(e.displayName || '')}</td>
              <td>${escapeHtml(e.host || '')}</td>
              <td>${escapeHtml(String(e.port || ''))}</td>
              <td>${escapeHtml(e.status || '')}</td>
              <td>${escapeHtml(e.specialization || '')}</td>
              <td>${escapeHtml(utilLabel)}</td>
              <td>${escapeHtml(instances)}</td>
              <td>${escapeHtml(clientList)}</td>
            </tr>
          `;
        }).join('')}
      </tbody>
    </table>
  `;
}

function endpointTable(endpoints) {
  if (endpoints.length === 0) {
    return '<p class="muted">No entries registered yet.</p>';
  }
  return `
    <table class="runtime-table">
      <thead><tr><th>Id</th><th>Display name</th><th>Host</th><th>Port</th><th>Status</th><th>Specialization</th></tr></thead>
      <tbody>
        ${endpoints.map((e) => `
          <tr>
            <td><code>${escapeHtml(e.id)}</code></td>
            <td>${escapeHtml(e.displayName || '')}</td>
            <td>${escapeHtml(e.host || '')}</td>
            <td>${escapeHtml(String(e.port || ''))}</td>
            <td>${escapeHtml(e.status || '')}</td>
            <td>${escapeHtml(e.specialization || '')}</td>
          </tr>
        `).join('')}
      </tbody>
    </table>
  `;
}

// ---- Activity ----

function renderActivity() {
  const telEvents = (state.telemetryEvents.events || []).slice().reverse();
  const legacy = (state.activity.events || []).slice().reverse();
  return `
    <div class="activity-grid">
      <article class="panel-block wide">
        <h3>Telemetry events (${telEvents.length} of ${state.telemetryEvents.maxEvents || '—'})</h3>
        <p class="muted">PHASE-08 ring buffer. Categories: system / gateway / worker / client / discovery / governance.</p>
        ${renderTelemetryEventRows(telEvents)}
      </article>
      <article class="panel-block wide">
        <h3>Admin activity (high water mark ${escapeHtml(state.activity.highWaterMarkId || '0')})</h3>
        <p class="muted">Legacy ADR-001 admin ring. Operator surface activity (clients, governance, exports).</p>
        ${renderActivityRows(legacy)}
      </article>
    </div>
  `;
}

function renderActivityRows(events, options) {
  options = options || {};
  if (!events || events.length === 0) {
    return '<p class="muted">No events captured yet.</p>';
  }
  return `
    <ol class="activity-list ${options.compact ? 'compact' : ''}">
      ${events.map((e) => `
        <li class="activity-row" data-kind="${escapeHtml(e.kind)}">
          <span class="activity-time">${escapeHtml(relativeAgo(e.timestampUtc))}</span>
          <span class="activity-kind">${escapeHtml(e.kind || '')}</span>
          <span class="activity-actor">${escapeHtml(e.actor || '')}</span>
          <span class="activity-message">${escapeHtml(e.message || '')}</span>
        </li>
      `).join('')}
    </ol>
  `;
}

// ---- Exports ----

function renderExports() {
  if (state.exports.length === 0) {
    return '<p class="muted">No exports available.</p>';
  }
  const rows = state.exports.map((artifact) => `
    <tr data-artifact-id="${escapeHtml(artifact.id)}">
      <td><code>${escapeHtml(artifact.id)}</code></td>
      <td>${escapeHtml(artifact.fileName || '')}</td>
      <td>${escapeHtml(artifact.mediaType || '')}</td>
      <td><button type="button" data-action="download" class="route-button">Download</button></td>
    </tr>
  `).join('');
  return `
    <article class="panel-block wide">
      <h3>Server-authored artifacts (${state.exports.length})</h3>
      <p class="muted">LAN client config bundles appear here once the matching client is registered and enabled.</p>
      <table class="runtime-table">
        <thead><tr><th>Id</th><th>File</th><th>Media</th><th></th></tr></thead>
        <tbody>${rows}</tbody>
      </table>
    </article>
  `;
}

function bindExportsHandlers() {
  document.querySelectorAll('[data-artifact-id] [data-action="download"]').forEach((btn) => {
    btn.addEventListener('click', () => {
      const tr = btn.closest('[data-artifact-id]');
      const id = tr.dataset.artifactId;
      const artifact = state.exports.find((a) => a.id === id);
      if (!artifact) return;
      const blob = new Blob([artifact.content || ''], { type: artifact.mediaType || 'application/octet-stream' });
      const url = URL.createObjectURL(blob);
      const link = document.createElement('a');
      link.href = url;
      link.download = artifact.fileName || (id + '.txt');
      link.click();
      URL.revokeObjectURL(url);
    });
  });
}

// ---------------------------------------------------------------------------
// PHASE-09 gateway-first panels
// ---------------------------------------------------------------------------

// ---- Gateway ----

function renderGatewayPanel() {
  const status = state.gatewayStatus || {};
  const health = state.gatewayHealth || {};
  const tools = state.gatewayTools || [];
  const stateLower = (status.state || 'unknown').toString().toLowerCase();
  const healthLower = (health.status || 'unknown').toString().toLowerCase();
  const stateTone = stateLower === 'running' ? 'good' : (stateLower === 'failed' ? 'bad' : 'warn');
  const healthTone = healthLower === 'healthy' ? 'good' : (healthLower === 'unhealthy' ? 'bad' : 'warn');
  // v0.9.74: route every gateway-URL render through resolveDisplayUrl so a
  // wildcard bindAddress (0.0.0.0 / [::]) doesn't render as the literal
  // listen-on-all-interfaces sentinel on this card while the Overview
  // card 60 lines above shows the resolved LAN IP. Single source of
  // truth for "what does an actual client type into their config?"
  const mcpUrlRaw = status.mcpUrl || (state.discovery && state.discovery.gateway && state.discovery.gateway.mcpUrl) || '—';
  const t = (state.dashboard && state.dashboard.telemetry) || {};
  const lanIp = t.primaryIpAddress || (state.discovery && state.discovery.serverIpAddress) || '';
  const mcpUrl = resolveDisplayUrl(mcpUrlRaw, lanIp);
  const mcpUrlListen = mcpUrlRaw && mcpUrl !== mcpUrlRaw ? mcpUrlRaw : '';
  const adapter = status.adapterType || (state.discovery && state.discovery.gateway && state.discovery.gateway.type) || 'unknown';
  return `
    <div class="gateway-grid">
      <article class="panel-block">
        <h3>Adapter</h3>
        <p class="big-stat">${escapeHtml(adapter)}</p>
        <p class="muted">Replaceable behind <code>IMcpGateway</code> (PHASE-02).</p>
      </article>
      <article class="panel-block">
        <h3>State</h3>
        <p class="big-stat ${stateTone}">${escapeHtml(stateLower)}</p>
        <p class="muted">Lifecycle: configured → starting → running → stopping → stopped.</p>
      </article>
      <article class="panel-block">
        <h3>Health</h3>
        <p class="big-stat ${healthTone}">${escapeHtml(healthLower)}</p>
        <p class="muted">${escapeHtml((health.message || '').toString() || 'Probe message unavailable.')}</p>
      </article>
      <article class="panel-block wide">
        <h3>Advertised MCP URL</h3>
        <p><code>${escapeHtml(mcpUrl)}</code></p>
        ${mcpUrlListen
          ? `<p class="muted">Listening on <code>${escapeHtml(mcpUrlListen)}</code> (wildcard bind; LAN clients connect to the resolved IP above).</p>`
          : ''}
        <p class="muted">Clients on the trusted LAN connect here. <code>auth=none</code>; <code>trust=lan</code> per ADR-002.</p>
      </article>
      <article class="panel-block wide">
        <h3>Registered tools (${tools.length})</h3>
        ${tools.length === 0
          ? '<p class="muted">No tools registered. PHASE-06 worker pools advertise themselves through the gateway as a single logical server, never as autoscaled clones.</p>'
          : `<table class="runtime-table">
              <thead><tr><th>Name</th><th>Logical server</th><th>Description</th></tr></thead>
              <tbody>${tools.map((tool) => `
                <tr>
                  <td><code>${escapeHtml(tool.name || '')}</code></td>
                  <td>${escapeHtml(tool.serverId || tool.logicalServer || '')}</td>
                  <td>${escapeHtml(tool.description || '')}</td>
                </tr>`).join('')}
              </tbody>
            </table>`}
      </article>
    </div>
  `;
}

// ---- Pools ----

function renderPoolsPanel() {
  if (state.pools.length === 0) {
    return `
      <article class="panel-block wide">
        <h3>No managed pools registered</h3>
        <p class="muted">Pools are registered via <code>POST /api/pools</code> (PHASE-06). Each pool is a managed group of MCP-server or sub-agent instances under a single supervisor with a sticky-session lease router.</p>
        <p class="muted">Default policy is safe by ADR-002 §9: <code>minInstances=0</code> means MCOS does not auto-spawn workers. Operators must register a pool before any backend runs.</p>
      </article>
    `;
  }
  return `
    <div class="pools-grid">
      ${state.pools.map((p) => renderPoolCard(p)).join('')}
    </div>
  `;
}

function renderPoolCard(pool) {
  const sat = state.poolSaturation[pool.poolId] || {};
  const leases = state.poolLeases[pool.poolId] || [];
  const policy = pool.scalePolicy || {};
  const minI = policy.minInstances != null ? policy.minInstances : 0;
  const maxI = policy.maxInstances != null ? policy.maxInstances : 0;
  const maxL = policy.maxActiveLeasesPerInstance != null ? policy.maxActiveLeasesPerInstance : 0;
  const ready = sat.readyInstanceCount || 0;
  const draining = sat.drainingInstanceCount || 0;
  const totalInstances = sat.instanceCount || (pool.instances ? pool.instances.length : 0);
  const maxTotalLeases = (maxI > 0 && maxL > 0) ? (maxI * maxL) : 0;
  const utilPct = maxTotalLeases > 0 ? Math.min(100, Math.round((leases.length / maxTotalLeases) * 100)) : 0;
  const tone = sat.atMaxInstances ? 'bad' : (sat.atSaturation ? 'warn' : 'ok');
  return `
    <article class="panel-block pool-card">
      <header class="pool-card-header">
        <div>
          <p class="eyebrow">${escapeHtml((pool.kind || '').toString())}</p>
          <h3>${escapeHtml(pool.poolId || '')}</h3>
          <p class="muted">${escapeHtml(pool.logicalMcpUrl || '')}</p>
        </div>
        <div class="pool-card-policy">
          <span class="pool-policy-cell"><span>min</span><strong>${minI}</strong></span>
          <span class="pool-policy-cell"><span>max</span><strong>${maxI}</strong></span>
          <span class="pool-policy-cell"><span>max-leases/inst</span><strong>${maxL}</strong></span>
        </div>
      </header>
      <div class="pool-card-meter">
        <div class="meter-bar"><div class="meter-fill ${tone}" style="width:${utilPct}%"></div></div>
        <p class="muted">${leases.length} active lease${leases.length === 1 ? '' : 's'} / ${maxTotalLeases || '—'} headroom · ${utilPct}% utilization</p>
      </div>
      <ul class="kv-list">
        <li><span>Instances</span><strong>${totalInstances}</strong></li>
        <li><span>Ready</span><strong>${ready}</strong></li>
        <li><span>Draining</span><strong>${draining}</strong></li>
        <li><span>At saturation</span><strong>${sat.atSaturation ? 'yes' : 'no'}</strong></li>
        <li><span>Scale-out triggered</span><strong>${sat.scaleOutTriggered ? 'yes' : 'no'}</strong></li>
        <li><span>At maxInstances</span><strong>${sat.atMaxInstances ? 'yes' : 'no'}</strong></li>
        <li><span>Queue depth</span><strong>${sat.queueDepth != null ? sat.queueDepth : '—'}</strong></li>
      </ul>
      ${renderPoolInstances(pool)}
      ${renderPoolLeases(pool, leases)}
      <div class="pool-card-actions">
        <button type="button" class="route-button" data-action="pool-scale" data-pool-id="${escapeHtml(pool.poolId)}">Scale to min</button>
        <button type="button" class="route-button" data-action="pool-drain" data-pool-id="${escapeHtml(pool.poolId)}">Drain</button>
      </div>
    </article>
  `;
}

// In-memory time-series buffer for per-instance telemetry sparklines.
// state.instanceHistory[instanceId] = { cpu: [...], mem: [...], lastTs: '' }
// Each refresh appends one sample per known instance, capped at 60 points
// (~2 minutes of history at the 2 s polling cadence). The DOM render
// turns these into <canvas>-drawn sparklines that update in place
// without rebuilding the row.
const TELEMETRY_HISTORY_LIMIT = 60;
function recordInstanceTelemetry(pool) {
  if (!state.instanceHistory) state.instanceHistory = {};
  for (const inst of (pool.instances || [])) {
    const id = inst.instanceId;
    if (!id) continue;
    const tel = inst.telemetry || {};
    const buf = state.instanceHistory[id] || { cpu: [], mem: [], lastTs: '' };
    if (tel.lastProbedAtUtc && tel.lastProbedAtUtc !== buf.lastTs) {
      buf.lastTs = tel.lastProbedAtUtc;
      const cpu = Number.isFinite(tel.cpuPercent) && tel.cpuPercent >= 0 ? tel.cpuPercent : null;
      const mem = Number.isFinite(tel.memoryMbytes) && tel.memoryMbytes >= 0 ? tel.memoryMbytes : null;
      buf.cpu.push(cpu);
      buf.mem.push(mem);
      while (buf.cpu.length > TELEMETRY_HISTORY_LIMIT) buf.cpu.shift();
      while (buf.mem.length > TELEMETRY_HISTORY_LIMIT) buf.mem.shift();
      state.instanceHistory[id] = buf;
    }
  }
}

function drawSparkline(canvas, samples, options) {
  if (!canvas || !canvas.getContext) return;
  const ctx = canvas.getContext('2d');
  const w = canvas.width;
  const h = canvas.height;
  ctx.clearRect(0, 0, w, h);
  // Subtle Tron grid: faint center line and accent fill area.
  const accent = options.accent || 'rgba(28, 242, 193, 0.95)';
  const fill = options.fill || 'rgba(28, 242, 193, 0.18)';
  const numericSamples = samples.filter((v) => Number.isFinite(v));
  if (numericSamples.length === 0) {
    ctx.fillStyle = 'rgba(232, 243, 247, 0.4)';
    ctx.font = '10px Cascadia Code, Consolas, monospace';
    ctx.textBaseline = 'middle';
    ctx.fillText('idle', 6, h / 2);
    return;
  }
  const maxFloor = Number.isFinite(options.maxFloor) ? options.maxFloor : 0;
  const explicitMax = Number.isFinite(options.fixedMax) ? options.fixedMax : null;
  const seriesMax = explicitMax !== null ? explicitMax : Math.max(maxFloor, ...numericSamples);
  const range = Math.max(seriesMax, 1);
  ctx.strokeStyle = 'rgba(118, 224, 255, 0.18)';
  ctx.beginPath();
  ctx.moveTo(0, h - 0.5);
  ctx.lineTo(w, h - 0.5);
  ctx.stroke();
  // Build the line.
  const stepX = w / Math.max(samples.length - 1, 1);
  ctx.beginPath();
  let firstDrawn = true;
  for (let i = 0; i < samples.length; i++) {
    const v = samples[i];
    if (!Number.isFinite(v)) continue;
    const x = i * stepX;
    const y = h - (v / range) * (h - 4) - 2;
    if (firstDrawn) {
      ctx.moveTo(x, y);
      firstDrawn = false;
    } else {
      ctx.lineTo(x, y);
    }
  }
  ctx.lineWidth = 1.4;
  ctx.strokeStyle = accent;
  ctx.stroke();
  // Fill below the line.
  ctx.lineTo(w, h);
  ctx.lineTo(0, h);
  ctx.closePath();
  ctx.fillStyle = fill;
  ctx.fill();
  // Latest value badge.
  const latest = numericSamples[numericSamples.length - 1];
  const label = (options.formatLatest || ((v) => v.toFixed(0)))(latest);
  ctx.fillStyle = accent;
  ctx.font = 'bold 10px Cascadia Code, Consolas, monospace';
  ctx.textBaseline = 'top';
  ctx.textAlign = 'right';
  ctx.fillText(label, w - 4, 2);
}

function renderPoolInstances(pool) {
  const instances = (pool.instances || []);
  if (instances.length === 0) {
    return '<p class="muted">No instances yet. Scale to min spawns workers via Job Object–contained <code>CreateProcessW</code>.</p>';
  }
  // Capture the latest sample BEFORE rendering so the sparkline draws
  // include the freshest reading from the current poll cycle.
  recordInstanceTelemetry(pool);
  return `
    <table class="runtime-table instance-telemetry-table">
      <thead><tr><th>Instance</th><th>State</th><th>Supervised</th><th>CPU %</th><th>Memory MB</th><th>Last probe</th></tr></thead>
      <tbody>
        ${instances.map((inst) => {
          const tel = inst.telemetry || {};
          return `<tr>
            <td><code>${escapeHtml(inst.instanceId || '')}</code></td>
            <td>${escapeHtml((inst.state || '').toString())}</td>
            <td>${inst.supervised ? 'yes' : 'no'}</td>
            <td>
              <div class="instance-spark" data-spark-instance="${escapeHtml(inst.instanceId || '')}" data-spark-metric="cpu">
                <canvas width="120" height="32"></canvas>
                <span class="muted">${escapeHtml(formatMetric(tel.cpuPercent, { suffix: '%' }))}</span>
              </div>
            </td>
            <td>
              <div class="instance-spark" data-spark-instance="${escapeHtml(inst.instanceId || '')}" data-spark-metric="mem">
                <canvas width="120" height="32"></canvas>
                <span class="muted">${escapeHtml(formatMetric(tel.memoryMbytes, { digits: 0 }))}</span>
              </div>
            </td>
            <td>${escapeHtml(relativeAgo(tel.lastProbedAtUtc))}</td>
          </tr>`;
        }).join('')}
      </tbody>
    </table>
  `;
}

// Walk every .instance-spark canvas in the DOM and draw the latest
// time-series. Called from bindPoolsHandlers (which runs after
// renderPoolsPanel injects new HTML on each refresh).
function paintInstanceSparklines() {
  if (!state.instanceHistory) return;
  for (const node of document.querySelectorAll('.instance-spark')) {
    const id = node.getAttribute('data-spark-instance');
    const metric = node.getAttribute('data-spark-metric');
    const buf = state.instanceHistory[id];
    if (!buf) continue;
    const canvas = node.querySelector('canvas');
    if (!canvas) continue;
    if (metric === 'cpu') {
      drawSparkline(canvas, buf.cpu, {
        accent: 'rgba(28, 242, 193, 0.95)',
        fill: 'rgba(28, 242, 193, 0.18)',
        fixedMax: 100,
        formatLatest: (v) => v.toFixed(1) + '%'
      });
    } else if (metric === 'mem') {
      drawSparkline(canvas, buf.mem, {
        accent: 'rgba(0, 246, 255, 0.95)',
        fill: 'rgba(0, 246, 255, 0.16)',
        maxFloor: 16,
        formatLatest: (v) => v.toFixed(0) + ' MB'
      });
    }
  }
}

function renderPoolLeases(pool, leases) {
  if (!leases || leases.length === 0) {
    return '<p class="muted">No active leases. Acquire via <code>POST /api/pools/' + escapeHtml(pool.poolId) + '/leases</code>.</p>';
  }
  return `
    <details class="pool-leases-details">
      <summary>${leases.length} active lease${leases.length === 1 ? '' : 's'}</summary>
      <table class="runtime-table">
        <thead><tr><th>Lease</th><th>Session</th><th>Instance</th><th>State</th><th>Acquired</th></tr></thead>
        <tbody>
          ${leases.map((l) => `<tr>
            <td><code>${escapeHtml(l.leaseId || '')}</code></td>
            <td>${escapeHtml(l.sessionId || '—')}</td>
            <td><code>${escapeHtml(l.instanceId || '')}</code></td>
            <td>${escapeHtml((l.state || '').toString())}</td>
            <td>${escapeHtml(relativeAgo(l.acquiredAtUtc))}</td>
          </tr>`).join('')}
        </tbody>
      </table>
    </details>
  `;
}

function bindPoolsHandlers() {
  document.querySelectorAll('[data-action="pool-scale"]').forEach((btn) => {
    btn.addEventListener('click', async () => {
      const poolId = btn.dataset.poolId;
      try {
        await loadJson('/api/pools/' + encodeURIComponent(poolId) + '/scale', { method: 'POST' });
        await refreshAll();
      } catch (err) {
        showBanner('Pool scale failed: ' + (err.message || 'unknown error'));
      }
    });
  });
  document.querySelectorAll('[data-action="pool-drain"]').forEach((btn) => {
    btn.addEventListener('click', async () => {
      const poolId = btn.dataset.poolId;
      try {
        await loadJson('/api/pools/' + encodeURIComponent(poolId) + '/drain', { method: 'POST' });
        await refreshAll();
      } catch (err) {
        showBanner('Pool drain failed: ' + (err.message || 'unknown error'));
      }
    });
  });
  // v0.6.8: per-instance live sparklines. Painted on every renderCurrent
  // tick so the lines slide left as new samples come in. The browser
  // GPU-composites the canvas automatically; rendering cost is one path
  // + one fill rectangle per chart on each 2 s refresh.
  paintInstanceSparklines();
}

// ---- Telemetry clients (presence roster) ----

function renderTelemetryClients() {
  const clients = state.telemetryClients || [];
  // v0.7.1: the Telemetry destination now also surfaces the per-sub-agent
  // utilization panel so operators looking at "what's hot right now"
  // don't have to bounce to the Overview deck. Same data, same cards.
  // v0.9.70: MCP server card grid is now also surfaced on the Telemetry
  // deck per operator directive — each MCP server gets its own small
  // card with status indicator, usage bar, and connected-client list,
  // identical visual shape to the sub-agent cards above. Reads
  // state.dashboard.mcpServerRuntimeStats which the runtime populates
  // every snapshot tick.
  const mcpServerPanel = renderMcpServerUtilizationPanel({ deck: 'telemetry' });
  const subAgentPanel = renderSubAgentUtilizationPanel({ deck: 'telemetry' });
  if (clients.length === 0) {
    return `
      <article class="panel-block wide">
        <h3>No connected clients</h3>
        <p class="muted">No client has POSTed <code>/api/telemetry/heartbeat</code> yet. Clients self-report; metrics they don't supply render as "unavailable" rather than "0%" (ADR-002 §9).</p>
      </article>
      ${mcpServerPanel}
      ${subAgentPanel}
    `;
  }
  return `
    <article class="panel-block wide">
      <h3>Connected clients (${clients.length})</h3>
      <table class="runtime-table">
        <thead><tr>
          <th>Client</th><th>Type</th><th>IP</th>
          <th>CPU</th><th>Memory</th><th>GPU</th><th>GPU MB</th>
          <th>Sent/s</th><th>Recv/s</th>
          <th>Last seen</th>
        </tr></thead>
        <tbody>
          ${clients.map((c) => {
            const hb = c.lastHeartbeat || {};
            return `<tr>
              <td><code>${escapeHtml(c.clientId || '')}</code></td>
              <td>${escapeHtml(c.clientType || '—')}</td>
              <td>${escapeHtml(c.ipAddress || '—')}</td>
              <td>${escapeHtml(formatMetric(hb.cpuPercent, { suffix: '%' }))}</td>
              <td>${escapeHtml(formatMetric(hb.memoryPercent, { suffix: '%' }))}</td>
              <td>${escapeHtml(formatMetric(hb.gpuPercent, { suffix: '%' }))}</td>
              <td>${escapeHtml(formatMetric(hb.gpuMemoryMb, { digits: 0 }))}</td>
              <td>${formatBytes(hb.bytesSentPerSecond)}</td>
              <td>${formatBytes(hb.bytesReceivedPerSecond)}</td>
              <td>${escapeHtml(relativeAgo(c.lastSeenUtc))}</td>
            </tr>`;
          }).join('')}
        </tbody>
      </table>
      <p class="muted">Cells reading <em>unavailable</em> mean the client did not supply that metric in its heartbeat. They are <strong>never</strong> rendered as <code>0%</code>.</p>
    </article>
    ${mcpServerPanel}
    ${subAgentPanel}
  `;
}

// ---- Onboarding ----

function renderOnboardingPanel() {
  const types = ['claude-code', 'codex', 'grok', 'chatgpt', 'generic-mcp'];
  const profile = state.onboardingProfile || {};
  const snippets = profile.configSnippets || [];
  const manualInstructions = profile.manualInstructions || [];
  const verification = profile.verificationSteps || [];
  const caveats = profile.caveats || [];
  // v0.9.74: same wildcard-substitution as the gateway and overview
  // panels so onboarding always advertises the IP a remote client can
  // actually connect to, never the literal listen-on-all-interfaces
  // sentinel.
  const gatewayUrlRaw = profile.gatewayMcpUrl
    || (state.discovery && state.discovery.gateway && state.discovery.gateway.mcpUrl)
    || '—';
  const onboardLanIp = ((state.dashboard && state.dashboard.telemetry) || {}).primaryIpAddress
    || (state.discovery && state.discovery.serverIpAddress) || '';
  const gatewayUrl = resolveDisplayUrl(gatewayUrlRaw, onboardLanIp);
  return `
    <article class="panel-block wide onboarding-panel">
      <div class="onboarding-tabs">
        ${types.map((t) => `
          <button type="button"
                  class="route-button ${t === state.onboardingClientType ? 'accent' : ''}"
                  data-action="onboarding-tab" data-client-type="${escapeHtml(t)}">
            ${escapeHtml(t)}
          </button>
        `).join('')}
      </div>
      <div class="onboarding-summary">
        <p class="muted">Profile for <strong>${escapeHtml(state.onboardingClientType)}</strong>. Trust = <code>${escapeHtml(profile.trust || 'lan')}</code> · Auth required = <code>${profile.authRequired === true ? 'true' : 'false'}</code>.</p>
        <p>Advertised MCP URL: <code>${escapeHtml(gatewayUrl)}</code></p>
      </div>
      <h3>Manual setup (always first-class)</h3>
      ${manualInstructions.length === 0
        ? '<p class="muted">No manual instructions in this profile.</p>'
        : `<ol class="onboarding-instructions">${manualInstructions.map((s) => `<li>${escapeHtml(s)}</li>`).join('')}</ol>`}
      <h3>Config snippets</h3>
      ${snippets.length === 0
        ? '<p class="muted">No snippets in this profile.</p>'
        : snippets.map((sn) => `
            <details class="onboarding-snippet">
              <summary>${escapeHtml(sn.label || sn.fileName || 'snippet')}</summary>
              <p class="muted">${escapeHtml(sn.description || '')}</p>
              ${sn.fileName ? `<p class="muted">File: <code>${escapeHtml(sn.fileName)}</code></p>` : ''}
              <pre class="onboarding-code">${escapeHtml(sn.content || '')}</pre>
              <button type="button" class="route-button" data-action="copy-snippet" data-content="${escapeHtml(sn.content || '')}">Copy</button>
            </details>
          `).join('')}
      <h3>Verification</h3>
      ${verification.length === 0
        ? '<p class="muted">No verification steps in this profile.</p>'
        : `<ol class="onboarding-instructions">${verification.map((s) => `<li>${escapeHtml(s)}</li>`).join('')}</ol>`}
      ${caveats.length === 0 ? '' : `
        <h3>Caveats</h3>
        <ul class="onboarding-caveats">${caveats.map((c) => `<li>${escapeHtml(c)}</li>`).join('')}</ul>
      `}
    </article>
  `;
}

function bindOnboardingHandlers() {
  document.querySelectorAll('[data-action="onboarding-tab"]').forEach((btn) => {
    btn.addEventListener('click', async () => {
      state.onboardingClientType = btn.dataset.clientType;
      state.onboardingProfile = null;
      try {
        state.onboardingProfile = await loadJson(
          '/api/onboarding/' + encodeURIComponent(state.onboardingClientType)
        );
      } catch (_) {
        state.onboardingProfile = null;
      }
      renderCurrent();
    });
  });
  document.querySelectorAll('[data-action="copy-snippet"]').forEach((btn) => {
    btn.addEventListener('click', async () => {
      const content = btn.dataset.content || '';
      try {
        await navigator.clipboard.writeText(content);
        btn.textContent = 'Copied';
        setTimeout(() => { btn.textContent = 'Copy'; }, 1500);
      } catch (_) {
        // Clipboard may be unavailable on some browsers; silently no-op.
      }
    });
  });
}

// ---- Discovery document ----

function renderDiscoveryPanel() {
  const doc = state.discovery || {};
  const gw = doc.gateway || {};
  const onboardingPaths = (doc.onboarding && doc.onboarding.paths) || [];
  const governance = doc.governance || {};
  // v0.9.74: substitute wildcard listen address with the resolved LAN
  // IP for display only -- the discovery doc itself carries the
  // configured value verbatim for archival use, but the operator
  // looking at this card wants to know "what URL do I tell my
  // teammates to type". Ship both: resolved URL prominently, raw
  // listen string in muted text below for transparency.
  const discLanIp = ((state.dashboard && state.dashboard.telemetry) || {}).primaryIpAddress
    || doc.serverIpAddress || '';
  const gwMcpUrlRender = resolveDisplayUrl(gw.mcpUrl || '', discLanIp) || '—';
  const gwHealthUrlRender = resolveDisplayUrl(gw.healthUrl || '', discLanIp) || '—';
  const gwMcpUrlListen = (gw.mcpUrl && gwMcpUrlRender !== gw.mcpUrl) ? gw.mcpUrl : '';
  return `
    <article class="panel-block wide discovery-doc">
      <h3>Discovery document</h3>
      <p class="muted">What MCOS advertises to the LAN via DNS-SD plus <code>/.well-known/mcos.json</code>.</p>
      <ul class="kv-list">
        <li><span>Instance ID</span><strong><code>${escapeHtml(doc.instanceId || '—')}</code></strong></li>
        <li><span>Product</span><strong>${escapeHtml(doc.product || '—')}</strong></li>
        <li><span>Auth</span><strong>${escapeHtml(doc.auth || 'none')}</strong></li>
        <li><span>Trust</span><strong>${escapeHtml(doc.trust || 'lan')}</strong></li>
        <li><span>Protocol versions</span><strong>${escapeHtml((doc.protocolVersions || []).join(', '))}</strong></li>
        <li><span>Server IP</span><strong><code>${escapeHtml(doc.serverIpAddress || '—')}</code></strong></li>
      </ul>
      <h3>Gateway</h3>
      <ul class="kv-list">
        <li><span>Type</span><strong>${escapeHtml(gw.type || '—')}</strong></li>
        <li><span>State</span><strong>${escapeHtml(gw.state || '—')}</strong></li>
        <li><span>MCP URL</span><strong><code>${escapeHtml(gwMcpUrlRender)}</code></strong></li>
        <li><span>Health URL</span><strong><code>${escapeHtml(gwHealthUrlRender)}</code></strong></li>
        ${gwMcpUrlListen ? `<li><span>Bound on</span><strong><code>${escapeHtml(gwMcpUrlListen)}</code></strong> <span class="muted">(wildcard listener; the resolved URL above is what clients connect to)</span></li>` : ''}
      </ul>
      <h3>Governance</h3>
      <ul class="kv-list">
        <li><span>Bundles URL</span><strong><code>${escapeHtml(governance.bundlesUrl || '—')}</code></strong></li>
        <li><span>Profile URL</span><strong><code>${escapeHtml(governance.profileUrl || '—')}</code></strong></li>
      </ul>
      <h3>Onboarding</h3>
      ${onboardingPaths.length === 0
        ? '<p class="muted">No onboarding paths advertised.</p>'
        : `<ul class="kv-list">${onboardingPaths.map((p) => `<li><span>${escapeHtml(p.clientType || '')}</span><strong><code>${escapeHtml(p.url || '')}</code></strong></li>`).join('')}</ul>`}
    </article>
    ${renderFirewallGuidanceCard()}
  `;
}

// PHASE-09 follow-on: surface the Windows Firewall + LAN advertising
// snippets the operator needs to apply once. ADR-002 §10 / docs/wiki/
// Operations/Windows-Firewall-LAN-Mode.md is the canonical reference.
// This card surfaces the same snippets with port values templated from
// the live discovery document so copy-paste lands an accurate rule.
function renderFirewallGuidanceCard() {
  // Pull live values where available; fall back to documented defaults.
  const browserPort = inferBrowserPortFromDiscovery() || 7300;
  const beaconPort = (state.discovery && state.discovery.beacon && state.discovery.beacon.port) || 7301;
  const gatewayPort = inferGatewayPortFromDiscovery() || 8080;
  const mDnsPort = 5353;
  const exePath = 'C:\\\\Program Files\\\\Master Control Orchestration Server\\\\MasterControlServiceHost.exe';

  const rules = [
    {
      label: '1 of 4 — MCP Gateway (TCP, AI-client surface)',
      content: buildFirewallRule('MCOS — MCP Gateway (LAN)', 'TCP', gatewayPort, exePath)
    },
    {
      label: '2 of 4 — Operator surface (TCP, dashboard + admin API)',
      content: buildFirewallRule('MCOS — Operator Surface (LAN)', 'TCP', browserPort, exePath)
    },
    {
      label: '3 of 4 — DNS-SD / mDNS (UDP 5353, the Bonjour advertising path)',
      content: buildFirewallRule('MCOS — DNS-SD/mDNS (LAN)', 'UDP', mDnsPort, exePath)
    },
    {
      label: '4 of 4 — Discovery beacon (UDP, legacy JSON broadcast)',
      content: buildFirewallRule('MCOS — Discovery Beacon (LAN)', 'UDP', beaconPort, exePath)
    }
  ];

  return `
    <article class="panel-block wide discovery-doc firewall-guidance">
      <h3>LAN advertising and Windows Firewall</h3>
      <p class="muted">MCOS advertises itself on the LAN via Win32 DNS-SD (Bonjour-compatible), a UDP JSON beacon, and the discovery document at <code>/.well-known/mcos.json</code>. Advertising fires whenever this service is running, but the host firewall must allow the inbound ports below or LAN clients will see nothing. <strong>Run each snippet from an elevated PowerShell.</strong> Admin rights are required.</p>
      <ul class="kv-list">
        <li><span>Operator surface (TCP)</span><strong>${browserPort}</strong></li>
        <li><span>MCP Gateway (TCP)</span><strong>${gatewayPort}</strong></li>
        <li><span>DNS-SD / mDNS (UDP)</span><strong>${mDnsPort}</strong></li>
        <li><span>Beacon (UDP)</span><strong>${beaconPort}</strong></li>
      </ul>
      ${rules.map((r) => `
        <details class="onboarding-snippet" open>
          <summary>${escapeHtml(r.label)}</summary>
          <pre class="onboarding-code">${escapeHtml(r.content)}</pre>
          <button type="button" class="route-button" data-action="copy-snippet" data-content="${escapeHtml(r.content)}">Copy</button>
        </details>
      `).join('')}
      <p class="muted"><code>Profile=Private,Domain</code> on every rule keeps MCOS off the Public profile. If your LAN is currently on the Public profile, reclassify the network in Windows Settings before applying these rules. To remove the rules later: <code>Get-NetFirewallRule -DisplayName 'MCOS *' | Remove-NetFirewallRule</code>.</p>
      <p class="muted">Verification: from another LAN host, <code>Test-NetConnection -ComputerName &lt;this-host-ip&gt; -Port ${gatewayPort}</code>. From a Public-profile network the same call should fail. mDNS browsing requires a Bonjour-aware tool such as <code>dns-sd -B _mcos._tcp</code> on macOS or <code>avahi-browse _mcos._tcp</code> on Linux. Full operations runbook: <code>docs/wiki/Operations/Windows-Firewall-LAN-Mode.md</code>.</p>
    </article>
  `;
}

function buildFirewallRule(displayName, protocol, port, exePath) {
  return [
    'New-NetFirewallRule `',
    '  -DisplayName "' + displayName + '" `',
    '  -Direction Inbound `',
    '  -Action Allow `',
    '  -Protocol ' + protocol + ' `',
    '  -LocalPort ' + port + ' `',
    '  -Profile Private,Domain `',
    '  -Program "' + exePath + '"'
  ].join('\n');
}

function inferBrowserPortFromDiscovery() {
  // Discovery document does not currently include the operator browser
  // port directly. Best-effort: parse it from the governance bundles URL
  // (which lives on the same operator surface). Returns null if absent.
  const url = (state.discovery && state.discovery.governance && state.discovery.governance.bundlesUrl) || '';
  const match = url.match(/:(\d+)\b/);
  return match ? parseInt(match[1], 10) : null;
}

function inferGatewayPortFromDiscovery() {
  const url = (state.discovery && state.discovery.gateway && state.discovery.gateway.mcpUrl) || '';
  const match = url.match(/:(\d+)\b/);
  return match ? parseInt(match[1], 10) : null;
}

function bindDiscoveryHandlers() {
  // Reuse the same copy-snippet behavior the Onboarding panel uses, so a
  // single click puts the New-NetFirewallRule snippet on the clipboard.
  document.querySelectorAll('[data-action="copy-snippet"]').forEach((btn) => {
    if (btn.dataset.discoveryBound === '1') return;
    btn.dataset.discoveryBound = '1';
    btn.addEventListener('click', async () => {
      const content = btn.dataset.content || '';
      try {
        await navigator.clipboard.writeText(content);
        const original = btn.textContent;
        btn.textContent = 'Copied — run from elevated PowerShell';
        setTimeout(() => { btn.textContent = original; }, 2200);
      } catch (_) {
        // Clipboard may be unavailable on some browsers; silently no-op.
      }
    });
  });
}

// ---- Telemetry event row helper (used by overview + activity) ----

function renderTelemetryEventRows(events, options) {
  options = options || {};
  if (!events || events.length === 0) {
    return '<p class="muted">No telemetry events captured yet.</p>';
  }
  return `
    <ol class="activity-list ${options.compact ? 'compact' : ''}">
      ${events.map((e) => `
        <li class="activity-row" data-kind="${escapeHtml(e.category || '')}" data-tone="${escapeHtml((e.severity || 'info').toString())}">
          <span class="activity-time">${escapeHtml(relativeAgo(e.timestamp))}</span>
          <span class="activity-kind">${escapeHtml(e.category || '')}/${escapeHtml(e.severity || '')}</span>
          <span class="activity-actor">${escapeHtml(e.source || '')}</span>
          <span class="activity-message">${escapeHtml(e.message || '')}</span>
        </li>
      `).join('')}
    </ol>
  `;
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

function boot() {
  const refreshBtn = $('#refreshButton');
  if (refreshBtn) refreshBtn.addEventListener('click', () => refreshAll());

  renderToolbar();
  renderNavigation();
  renderHeading();
  renderCurrent();
  refreshAll();
  // v0.9.71: real-time push via Server-Sent Events. Open a stream on
  // /api/events; server emits 'dashboard' events every snapshot tick
  // (~1s, only on actual change) and 'activity' events as the
  // activity ring appends. Polling continues at a much slower 8s
  // cadence as a safety net if the SSE connection dies for some
  // reason (proxy, network blip) and EventSource hasn't reconnected
  // yet. The poll-cycle was 2s pre-v0.9.71; reducing it to 8s under
  // SSE means the dashboard stays "live" without redundant polling.
  startRealtimeStream();
  setInterval(refreshAll, 8000);
}

let realtimeStream = null;
let realtimeLastSnapshotAt = 0;
function startRealtimeStream() {
  if (typeof EventSource === 'undefined') {
    console.warn('EventSource not supported; sticking with polling.');
    return;
  }
  try { if (realtimeStream) realtimeStream.close(); } catch (e) {}
  const stream = new EventSource('/api/events');
  realtimeStream = stream;

  stream.addEventListener('open', () => {
    if (typeof state === 'object') {
      state.realtimeConnected = true;
    }
    updateRealtimeChip(true);
  });

  stream.addEventListener('dashboard', (evt) => {
    try {
      const snap = JSON.parse(evt.data);
      state.dashboard = snap;
      realtimeLastSnapshotAt = Date.now();
      // Re-render the active view using the freshly-pushed snapshot.
      renderCurrent();
      updateRealtimeChip(true);
    } catch (e) {
      console.warn('dashboard SSE parse failed', e);
    }
  });

  stream.addEventListener('activity', (evt) => {
    try {
      const newEvent = JSON.parse(evt.data);
      if (!state.activity) state.activity = { events: [], highWaterMarkId: '0' };
      state.activity.events = (state.activity.events || []).concat([newEvent]);
      // Bound the local cache at 500 most-recent events to keep the
      // browser memory profile sane on a long-lived stream.
      if (state.activity.events.length > 500) {
        state.activity.events = state.activity.events.slice(-500);
      }
      state.activity.highWaterMarkId = newEvent.id || state.activity.highWaterMarkId;
      // Only re-render activity-bearing decks so we don't churn
      // unrelated views on every event.
      const dest = state.destination || 'overview';
      if (dest === 'overview' || dest === 'activity' || dest === 'telemetry') {
        renderCurrent();
      }
    } catch (e) {
      console.warn('activity SSE parse failed', e);
    }
  });

  stream.addEventListener('error', () => {
    state.realtimeConnected = false;
    updateRealtimeChip(false);
    // EventSource auto-reconnects unless we close it. Let it try.
  });
}

function updateRealtimeChip(connected) {
  const chip = document.getElementById('realtimeChip');
  if (!chip) return;
  if (connected) {
    chip.dataset.tone = 'success';
    chip.textContent = 'LIVE';
  } else {
    chip.dataset.tone = 'warn';
    chip.textContent = 'POLLING';
  }
}

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', boot);
} else {
  boot();
}

})();
