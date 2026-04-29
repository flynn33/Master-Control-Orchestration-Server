// Master Control Orchestration Server - browser surface
// Copyright (c) 2026 James Daley. All Rights Reserved.
//
// Phase 8 of ADR-001 LAN Client Control Plane. Replaces the provider-era
// dashboard with a focused surface built around the LAN client identity
// model, the shared MCP/sub-agent fabric, and CLU governance with the
// operator approval queue.

(function() {
'use strict';

// ---------------------------------------------------------------------------
// State + helpers
// ---------------------------------------------------------------------------

const state = {
  destination: 'overview',
  dashboard: null,
  clients: [],
  approvals: [],
  exports: [],
  activity: { events: [], highWaterMarkId: '0' },
  selectedClientId: null,
  health: { status: 'unknown', time: '' },
  errorBanner: ''
};

const destinations = [
  {
    id: 'overview',
    label: 'Overview',
    eyebrow: 'COMMAND DECK',
    title: 'Master Control',
    subtitle: 'Live posture across the LAN client control plane.'
  },
  {
    id: 'clients',
    label: 'LAN Clients',
    eyebrow: 'IDENTITY',
    title: 'LAN Clients',
    subtitle: 'Per-client identity, privileges, autonomous mode, and configuration bundles.'
  },
  {
    id: 'governance',
    label: 'Governance',
    eyebrow: 'CLU',
    title: 'Governance',
    subtitle: 'CLU posture, Forsetti-aligned rules, and the operator approval queue.'
  },
  {
    id: 'runtime',
    label: 'Shared Fabric',
    eyebrow: 'CATALOG',
    title: 'MCP Servers + Sub-Agents',
    subtitle: 'Universal-use catalog. Every authenticated LAN client may invoke every entry.'
  },
  {
    id: 'activity',
    label: 'Activity',
    eyebrow: 'STREAM',
    title: 'Activity Stream',
    subtitle: 'Live event ring across admin API requests, governance decisions, and lifecycle events.'
  },
  {
    id: 'exports',
    label: 'Exports',
    eyebrow: 'ARTIFACTS',
    title: 'Exports',
    subtitle: 'Server-authored config bundles and gateway profiles.'
  }
];

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
      loadJson('/api/health').catch(() => ({ status: 'unreachable', time: '' })),
      loadJson('/api/dashboard').catch((err) => { console.warn('dashboard', err); return null; }),
      loadJson('/api/clients').catch(() => []),
      loadJson('/api/clu/approvals').catch(() => []),
      loadJson('/api/exports').catch(() => []),
      loadJson('/api/activity').catch(() => ({ events: [], highWaterMarkId: '0' }))
    ]);
    const [health, dashboard, clients, approvals, exportsList, activity] = results;
    state.health = health || { status: 'unreachable', time: '' };
    state.dashboard = dashboard;
    state.clients = Array.isArray(clients) ? clients : [];
    state.approvals = Array.isArray(approvals) ? approvals : [];
    state.exports = Array.isArray(exportsList) ? exportsList : [];
    state.activity = activity && Array.isArray(activity.events)
      ? activity
      : { events: [], highWaterMarkId: '0' };
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
    <button type="button" data-action="register-client" class="route-button accent">
      Register LAN Client
    </button>
    <button type="button" data-action="open-clients" class="route-button">
      Open Clients
    </button>
    <button type="button" data-action="open-governance" class="route-button">
      Governance Approvals
    </button>
  `;
  toolbar.querySelector('[data-action="register-client"]').addEventListener('click', () => {
    state.destination = 'clients';
    state.selectedClientId = '__new__';
    renderCurrent();
  });
  toolbar.querySelector('[data-action="open-clients"]').addEventListener('click', () => {
    state.destination = 'clients';
    state.selectedClientId = null;
    renderCurrent();
  });
  toolbar.querySelector('[data-action="open-governance"]').addEventListener('click', () => {
    state.destination = 'governance';
    renderCurrent();
  });
}

function renderNavigation() {
  const nav = $('#surfaceNavigation');
  if (!nav) return;
  nav.innerHTML = destinations.map((dest) => `
    <button type="button"
            class="nav-link${state.destination === dest.id ? ' active' : ''}"
            data-destination="${escapeHtml(dest.id)}">
      <span class="nav-link-label">${escapeHtml(dest.label)}</span>
      <span class="nav-link-eyebrow">${escapeHtml(dest.eyebrow)}</span>
    </button>
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
  const endpoints = (state.dashboard && state.dashboard.endpoints) || [];
  const mcpCount = endpoints.filter((e) => e.kind === 'mcp_server' && !e.isTemplate).length;
  const subAgentCount = endpoints.filter((e) => e.kind === 'sub_agent' && !e.isTemplate).length;
  const clientCount = state.clients.length;
  const enabledClients = state.clients.filter((c) => c.enabled).length;
  const pendingApprovals = state.approvals.filter((a) => a.status === 'pending').length;
  target.innerHTML = `
    <div class="summary-grid">
      <div class="summary-cell"><span class="summary-label">Host</span><span class="summary-value">${escapeHtml(t.hostName || '—')}</span></div>
      <div class="summary-cell"><span class="summary-label">CPU</span><span class="summary-value">${escapeHtml((t.cpuPercent != null ? t.cpuPercent.toFixed(0) : '—') + '%')}</span></div>
      <div class="summary-cell"><span class="summary-label">Memory</span><span class="summary-value">${escapeHtml((t.memoryPercent != null ? t.memoryPercent.toFixed(0) : '—') + '%')}</span></div>
      <div class="summary-cell"><span class="summary-label">LAN Clients</span><span class="summary-value">${enabledClients}/${clientCount}</span></div>
      <div class="summary-cell"><span class="summary-label">MCP Servers</span><span class="summary-value">${mcpCount}</span></div>
      <div class="summary-cell"><span class="summary-label">Sub-Agents</span><span class="summary-value">${subAgentCount}</span></div>
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
    case 'clients':    host.innerHTML = banner + renderClients();    bindClientsHandlers();    break;
    case 'governance': host.innerHTML = banner + renderGovernance(); bindGovernanceHandlers(); break;
    case 'runtime':    host.innerHTML = banner + renderRuntime();                              break;
    case 'activity':   host.innerHTML = banner + renderActivity();                             break;
    case 'exports':    host.innerHTML = banner + renderExports();    bindExportsHandlers();    break;
    case 'overview':
    default:           host.innerHTML = banner + renderOverview();   bindOverviewHandlers();   break;
  }
}

// ---- Overview ----

function renderOverview() {
  const t = (state.dashboard && state.dashboard.telemetry) || {};
  const cluPosture = state.dashboard && state.dashboard.governance
    ? state.dashboard.governance.posture
    : 'unknown';
  const enabledClients = state.clients.filter((c) => c.enabled);
  const autonomousClients = state.clients.filter((c) => c.autonomousMode);
  const pendingApprovals = state.approvals.filter((a) => a.status === 'pending');

  return `
    <div class="overview-grid">
      <article class="panel-block">
        <h3>Posture</h3>
        <p class="big-stat ${cluPosture === 'pass' ? 'good' : (cluPosture === 'blocked' ? 'bad' : 'warn')}">${escapeHtml(cluPosture)}</p>
        <p class="muted">CLU runtime posture. <code>pass</code> means all rules green.</p>
      </article>
      <article class="panel-block">
        <h3>LAN Clients</h3>
        <p class="big-stat">${enabledClients.length}<span class="big-stat-suffix">/${state.clients.length}</span></p>
        <p class="muted">${autonomousClients.length} autonomous · ${state.clients.length - enabledClients.length} disabled</p>
      </article>
      <article class="panel-block">
        <h3>Pending Approvals</h3>
        <p class="big-stat ${pendingApprovals.length > 0 ? 'warn' : ''}">${pendingApprovals.length}</p>
        <p class="muted">Deferred CLU decisions awaiting operator review.</p>
        ${pendingApprovals.length > 0 ? `<button type="button" data-action="goto-governance" class="route-button">Review</button>` : ''}
      </article>
      <article class="panel-block">
        <h3>Telemetry</h3>
        <ul class="kv-list">
          <li><span>Host</span><strong>${escapeHtml(t.hostName || '—')}</strong></li>
          <li><span>IP</span><strong>${escapeHtml(t.primaryIpAddress || '—')}</strong></li>
          <li><span>CPU</span><strong>${t.cpuPercent != null ? t.cpuPercent.toFixed(0) + '%' : '—'}</strong></li>
          <li><span>Memory</span><strong>${t.memoryPercent != null ? t.memoryPercent.toFixed(0) + '%' : '—'}</strong></li>
          <li><span>Disk</span><strong>${t.diskPercent != null ? t.diskPercent.toFixed(0) + '%' : '—'}</strong></li>
        </ul>
      </article>
      <article class="panel-block wide">
        <h3>Recent Activity</h3>
        ${renderActivityRows(state.activity.events.slice(-8).reverse(), { compact: true })}
      </article>
    </div>
  `;
}

function bindOverviewHandlers() {
  const goto = document.querySelector('[data-action="goto-governance"]');
  if (goto) {
    goto.addEventListener('click', () => {
      state.destination = 'governance';
      renderCurrent();
    });
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

  return `
    <div class="governance-shell">
      <article class="panel-block">
        <h3>Posture</h3>
        <p class="big-stat ${gov.posture === 'pass' ? 'good' : (gov.posture === 'blocked' ? 'bad' : 'warn')}">${escapeHtml(gov.posture || 'unknown')}</p>
        <p class="muted">Authority: ${escapeHtml(gov.unitName || 'Command Logic Unit')}</p>
        <p class="muted">Last evaluated: ${escapeHtml(shortDate(gov.lastEvaluatedUtc))}</p>
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

function renderRuntime() {
  const endpoints = (state.dashboard && state.dashboard.endpoints) || [];
  const mcp = endpoints.filter((e) => e.kind === 'mcp_server');
  const subs = endpoints.filter((e) => e.kind === 'sub_agent');
  return `
    <div class="runtime-grid">
      <article class="panel-block wide">
        <h3>MCP Servers (${mcp.length})</h3>
        <p class="muted">Universal-use catalog. Any authenticated LAN client may invoke these.</p>
        ${endpointTable(mcp)}
      </article>
      <article class="panel-block wide">
        <h3>Sub-Agents (${subs.length})</h3>
        ${endpointTable(subs)}
      </article>
    </div>
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
  return `
    <article class="panel-block wide">
      <h3>Activity stream (high water mark ${escapeHtml(state.activity.highWaterMarkId || '0')})</h3>
      ${renderActivityRows((state.activity.events || []).slice().reverse())}
    </article>
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
  setInterval(refreshAll, 5000);
}

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', boot);
} else {
  boot();
}

})();
