const state = {
  config: null,
  dashboard: null,
  exports: [],
  selectedExportId: '',
  importMode: 'package'
};

const endpointTableBody = document.querySelector('#endpointTable tbody');
const telemetryGrid = document.querySelector('#telemetryGrid');
const environmentGrid = document.querySelector('#environmentGrid');
const providerList = document.querySelector('#providerList');
const exportList = document.querySelector('#exportList');
const installHistory = document.querySelector('#installHistory');
const healthBadge = document.querySelector('#healthBadge');
const dangerDialog = document.querySelector('#dangerDialog');
const importStatus = document.querySelector('#importStatus');
const exportStatus = document.querySelector('#exportStatus');
const exportSelector = document.querySelector('#exportSelector');
const exportPreview = document.querySelector('#exportPreview');
const selectedExportFileName = document.querySelector('#selectedExportFileName');
const selectedExportMediaType = document.querySelector('#selectedExportMediaType');
const refreshExportsButton = document.querySelector('#refreshExportsButton');
const downloadSelectedExportButton = document.querySelector('#downloadSelectedExportButton');
const downloadAllExportsButton = document.querySelector('#downloadAllExportsButton');
const importModeButtons = [...document.querySelectorAll('[data-import-mode]')];
const importForms = {
  package: document.querySelector('#packageInstallForm'),
  repo: document.querySelector('#repoInstallForm'),
  zip: document.querySelector('#zipInstallForm')
};

function escapeHtml(value) {
  return String(value ?? '')
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;');
}

function metricCard(label, value) {
  return `<article class="telemetry-card"><div>${escapeHtml(label)}</div><div class="metric">${escapeHtml(value)}</div></article>`;
}

function statusPill(status) {
  const safeStatus = String(status || 'unknown');
  return `<span class="status-pill status-${escapeHtml(safeStatus)}">${escapeHtml(safeStatus)}</span>`;
}

function boolField(form, name, value) {
  if (form?.elements[name]) {
    form.elements[name].checked = !!value;
  }
}

function setValue(form, name, value) {
  if (form?.elements[name]) {
    form.elements[name].value = value ?? '';
  }
}

function setStatus(element, message, tone = 'info') {
  element.textContent = message;
  element.dataset.tone = tone;
}

async function loadJson(url, options) {
  const response = await fetch(url, options);
  const json = await response.json();
  if (!response.ok) {
    throw new Error(json.message || 'Request failed');
  }
  return json;
}

function renderTelemetry(snapshot) {
  telemetryGrid.innerHTML = [
    metricCard('CPU', `${snapshot.cpuPercent.toFixed(1)}%`),
    metricCard('Memory', `${snapshot.memoryPercent.toFixed(1)}%`),
    metricCard('Disk', `${snapshot.diskPercent.toFixed(1)}%`),
    metricCard('Host', snapshot.hostName || 'n/a'),
    metricCard('Primary IP', snapshot.primaryIpAddress || 'n/a'),
    metricCard('Beacon MAC', snapshot.primaryMacAddress || 'n/a'),
    metricCard('TX/s', `${snapshot.bytesSentPerSecond}`),
    metricCard('RX/s', `${snapshot.bytesReceivedPerSecond}`)
  ].join('');
}

function renderEnvironment(config) {
  const profile = config.activeProfile || {};
  environmentGrid.innerHTML = [
    metricCard('Profile', profile.environmentName || 'n/a'),
    metricCard('Preferred IP', profile.preferredBindAddress || 'n/a'),
    metricCard('MAC', profile.macAddress || 'n/a'),
    metricCard('Bind Address', config.bindAddress || 'n/a'),
    metricCard('Browser Port', `${config.browserPort ?? 'n/a'}`),
    metricCard('Beacon Port', `${config.beaconPort ?? 'n/a'}`)
  ].join('');
}

function renderEndpoints(endpoints) {
  endpointTableBody.innerHTML = endpoints.map((endpoint) => `
    <tr>
      <td>${escapeHtml(endpoint.displayName)}</td>
      <td>${escapeHtml(endpoint.kind)}</td>
      <td>${escapeHtml(endpoint.host)}</td>
      <td>${escapeHtml(endpoint.port)}</td>
      <td>${statusPill(endpoint.status)}</td>
    </tr>
  `).join('');
}

function renderProviders(providers) {
  providerList.innerHTML = providers.map((provider) => `
    <article class="provider-card">
      <strong>${escapeHtml(provider.displayName)}</strong>
      <div>${escapeHtml(provider.baseUrl)}</div>
      <div>${escapeHtml(provider.kind)} | ${provider.enabled ? 'enabled' : 'disabled'} | autonomy ${provider.allowAutonomousControl ? 'on' : 'off'}</div>
    </article>
  `).join('');
}

function renderHistory(history) {
  installHistory.innerHTML = history.map((entry) => `
    <article class="history-item">
      <strong>${escapeHtml(entry.kind)}</strong>
      <div>${escapeHtml(entry.source)}</div>
      <div>${escapeHtml(entry.installedAtUtc)}</div>
      <div>${escapeHtml(entry.executionSummary)}</div>
    </article>
  `).join('');
}

function bindConfig(config) {
  const form = document.querySelector('#configForm');
  setValue(form, 'instanceName', config.instanceName);
  setValue(form, 'bindAddress', config.bindAddress);
  setValue(form, 'browserPort', config.browserPort);
  setValue(form, 'beaconPort', config.beaconPort);
  boolField(form, 'beaconEnabled', config.beaconEnabled);
  boolField(form, 'aiAutonomyEnabled', config.aiAutonomyEnabled);
  boolField(form, 'allowOpenLanAccess', config.security.allowOpenLanAccess);
  boolField(form, 'securityProtocolsEnabled', config.security.securityProtocolsEnabled);
}

function setImportMode(mode) {
  state.importMode = mode;
  for (const button of importModeButtons) {
    button.classList.toggle('is-active', button.dataset.importMode === mode);
  }
  for (const [formMode, form] of Object.entries(importForms)) {
    form.classList.toggle('is-hidden', formMode !== mode);
  }
}

function currentExport() {
  return state.exports.find((artifact) => artifact.id === state.selectedExportId) || null;
}

function updateExportActions() {
  const hasExports = state.exports.length > 0;
  refreshExportsButton.disabled = false;
  downloadAllExportsButton.disabled = !hasExports;
  downloadSelectedExportButton.disabled = !currentExport();
}

function updateExportPreview() {
  const artifact = currentExport();
  if (!artifact) {
    selectedExportFileName.textContent = 'No artifact selected';
    selectedExportMediaType.textContent = 'n/a';
    exportPreview.value = '';
    updateExportActions();
    return;
  }

  selectedExportFileName.textContent = artifact.fileName;
  selectedExportMediaType.textContent = artifact.mediaType || 'text/plain';
  exportPreview.value = artifact.content || '';
  updateExportActions();
}

function renderExports(artifacts) {
  exportList.innerHTML = artifacts.map((artifact) => `
    <button type="button" class="export-card export-card-button ${artifact.id === state.selectedExportId ? 'is-selected' : ''}" data-export-id="${escapeHtml(artifact.id)}">
      <strong>${escapeHtml(artifact.fileName)}</strong>
      <div>${escapeHtml(artifact.mediaType || 'text/plain')}</div>
      <div>${escapeHtml((artifact.content || '').slice(0, 96))}${(artifact.content || '').length > 96 ? '...' : ''}</div>
    </button>
  `).join('');
}

function bindExports(artifacts) {
  state.exports = artifacts || [];
  if (!state.exports.some((artifact) => artifact.id === state.selectedExportId)) {
    state.selectedExportId = state.exports[0]?.id || '';
  }

  exportSelector.innerHTML = state.exports.map((artifact) => `
    <option value="${escapeHtml(artifact.id)}">${escapeHtml(artifact.fileName)} | ${escapeHtml(artifact.mediaType || 'text/plain')}</option>
  `).join('');
  exportSelector.value = state.selectedExportId;

  renderExports(state.exports);
  updateExportPreview();
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

async function refreshExportsFromApi(showStatus = false) {
  try {
    refreshExportsButton.disabled = true;
    const artifacts = await loadJson('/api/exports');
    bindExports(artifacts);
    if (showStatus) {
      setStatus(exportStatus, `Loaded ${artifacts.length} export artifact${artifacts.length === 1 ? '' : 's'} from the local admin API.`, 'success');
    }
  } catch (error) {
    setStatus(exportStatus, error.message, 'error');
  } finally {
    updateExportActions();
  }
}

async function refreshDashboard() {
  try {
    healthBadge.textContent = 'Live';
    const dashboard = await loadJson('/api/dashboard');
    state.dashboard = dashboard;
    renderTelemetry(dashboard.telemetry);
    renderEndpoints(dashboard.endpoints);
    renderProviders(dashboard.providers);
    renderHistory(dashboard.installHistory);
    bindExports(dashboard.exports || []);

    const config = await loadJson('/api/config');
    state.config = config;
    renderEnvironment(config);
    bindConfig(config);

    setStatus(importStatus, 'Import operations are executed through the local admin API.', 'info');
    if (!exportStatus.dataset.tone || exportStatus.dataset.tone === 'info') {
      setStatus(exportStatus, 'Exports are generated by the service and downloaded directly through the browser.', 'info');
    }
  } catch (error) {
    healthBadge.textContent = 'Error';
    console.error(error);
  }
}

async function handleImportSubmission(form, url, mapForm, successReset = true) {
  const button = form.querySelector('button[type="submit"]');
  try {
    button.disabled = true;
    setStatus(importStatus, 'Running import through the local admin API.', 'info');
    const result = await loadJson(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(mapForm(form))
    });
    setStatus(importStatus, result.message, 'success');
    if (successReset) {
      form.reset();
    }
    await refreshDashboard();
  } catch (error) {
    setStatus(importStatus, error.message, 'error');
  } finally {
    button.disabled = false;
  }
}

document.querySelector('#refreshButton').addEventListener('click', refreshDashboard);

document.querySelector('#configForm').addEventListener('submit', async (event) => {
  event.preventDefault();
  if (!state.config) {
    return;
  }

  const form = event.currentTarget;
  const nextConfig = structuredClone(state.config);
  nextConfig.instanceName = form.elements.instanceName.value;
  nextConfig.bindAddress = form.elements.bindAddress.value;
  nextConfig.browserPort = Number(form.elements.browserPort.value);
  nextConfig.beaconPort = Number(form.elements.beaconPort.value);
  nextConfig.beaconEnabled = form.elements.beaconEnabled.checked;
  nextConfig.aiAutonomyEnabled = form.elements.aiAutonomyEnabled.checked;
  nextConfig.security.allowOpenLanAccess = form.elements.allowOpenLanAccess.checked;
  nextConfig.security.securityProtocolsEnabled = form.elements.securityProtocolsEnabled.checked;

  let confirmUnsafe = false;
  if (state.config.security.securityProtocolsEnabled && !nextConfig.security.securityProtocolsEnabled) {
    dangerDialog.showModal();
    const result = await new Promise((resolve) => {
      dangerDialog.addEventListener('close', () => resolve(dangerDialog.returnValue === 'confirm'), { once: true });
    });
    if (!result) {
      return;
    }
    confirmUnsafe = true;
  }

  const response = await fetch('/api/config', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'X-Confirm-Unsafe': confirmUnsafe ? '1' : '0'
    },
    body: JSON.stringify(nextConfig)
  });
  const result = await response.json();
  if (!response.ok) {
    alert(result.message || 'Configuration update failed.');
    return;
  }
  await refreshDashboard();
});

document.querySelector('#providerForm').addEventListener('submit', async (event) => {
  event.preventDefault();
  const form = event.currentTarget;
  await loadJson('/api/providers', {
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
  form.reset();
  await refreshDashboard();
});

document.querySelector('#packageInstallForm').addEventListener('submit', async (event) => {
  event.preventDefault();
  await handleImportSubmission(event.currentTarget, '/api/install/package', (form) => ({
    source: form.elements.source.value,
    localPath: '',
    kind: form.elements.kind.value,
    arguments: form.elements.arguments.value,
    allowUntrustedExecution: form.elements.allowUntrustedExecution.checked
  }));
});

document.querySelector('#repoInstallForm').addEventListener('submit', async (event) => {
  event.preventDefault();
  await handleImportSubmission(event.currentTarget, '/api/install/repo', (form) => ({
    repositoryUrl: form.elements.repositoryUrl.value,
    branch: form.elements.branch.value || 'main',
    manifestFile: form.elements.manifestFile.value || 'mcp-bootstrap.json',
    allowUntrustedExecution: form.elements.allowUntrustedExecution.checked
  }));
});

document.querySelector('#zipInstallForm').addEventListener('submit', async (event) => {
  event.preventDefault();
  await handleImportSubmission(event.currentTarget, '/api/install/zip', (form) => ({
    source: form.elements.source.value,
    manifestFile: form.elements.manifestFile.value || 'mcp-bootstrap.json',
    allowUntrustedExecution: form.elements.allowUntrustedExecution.checked
  }));
});

for (const button of importModeButtons) {
  button.addEventListener('click', () => setImportMode(button.dataset.importMode));
}

exportSelector.addEventListener('change', (event) => {
  state.selectedExportId = event.currentTarget.value;
  renderExports(state.exports);
  updateExportPreview();
});

exportList.addEventListener('click', (event) => {
  const card = event.target.closest('[data-export-id]');
  if (!card) {
    return;
  }

  state.selectedExportId = card.dataset.exportId;
  exportSelector.value = state.selectedExportId;
  renderExports(state.exports);
  updateExportPreview();
});

refreshExportsButton.addEventListener('click', () => refreshExportsFromApi(true));

downloadSelectedExportButton.addEventListener('click', () => {
  const artifact = currentExport();
  if (!artifact) {
    setStatus(exportStatus, 'Select an export artifact before downloading it.', 'warning');
    return;
  }
  downloadArtifact(artifact);
  setStatus(exportStatus, `Downloaded ${artifact.fileName}.`, 'success');
});

downloadAllExportsButton.addEventListener('click', () => {
  if (!state.exports.length) {
    setStatus(exportStatus, 'No export artifacts are available yet.', 'warning');
    return;
  }

  for (const artifact of state.exports) {
    downloadArtifact(artifact);
  }
  setStatus(exportStatus, `Downloaded ${state.exports.length} export artifact${state.exports.length === 1 ? '' : 's'}.`, 'success');
});

setImportMode(state.importMode);
refreshDashboard();
setInterval(refreshDashboard, 5000);
