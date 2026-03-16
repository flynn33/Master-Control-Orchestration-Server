const state = {
  config: null,
  dashboard: null
};

const endpointTableBody = document.querySelector('#endpointTable tbody');
const telemetryGrid = document.querySelector('#telemetryGrid');
const environmentGrid = document.querySelector('#environmentGrid');
const providerList = document.querySelector('#providerList');
const exportList = document.querySelector('#exportList');
const installHistory = document.querySelector('#installHistory');
const healthBadge = document.querySelector('#healthBadge');
const dangerDialog = document.querySelector('#dangerDialog');

function metricCard(label, value) {
  return `<article class="telemetry-card"><div>${label}</div><div class="metric">${value}</div></article>`;
}

function statusPill(status) {
  return `<span class="status-pill status-${status}">${status}</span>`;
}

function boolField(form, name, value) {
  form.elements[name].checked = !!value;
}

function setValue(form, name, value) {
  if (form.elements[name]) {
    form.elements[name].value = value ?? '';
  }
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
      <td>${endpoint.displayName}</td>
      <td>${endpoint.kind}</td>
      <td>${endpoint.host}</td>
      <td>${endpoint.port}</td>
      <td>${statusPill(endpoint.status)}</td>
    </tr>
  `).join('');
}

function renderProviders(providers) {
  providerList.innerHTML = providers.map((provider) => `
    <article class="provider-card">
      <strong>${provider.displayName}</strong>
      <div>${provider.baseUrl}</div>
      <div>${provider.kind} | ${provider.enabled ? 'enabled' : 'disabled'} | autonomy ${provider.allowAutonomousControl ? 'on' : 'off'}</div>
    </article>
  `).join('');
}

function renderExports(exports) {
  exportList.innerHTML = exports.map((artifact) => `
    <article class="export-card">
      <strong>${artifact.fileName}</strong>
      <pre>${artifact.content}</pre>
    </article>
  `).join('');
}

function renderHistory(history) {
  installHistory.innerHTML = history.map((entry) => `
    <article class="history-item">
      <strong>${entry.kind}</strong>
      <div>${entry.source}</div>
      <div>${entry.installedAtUtc}</div>
      <div>${entry.executionSummary}</div>
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

async function refreshDashboard() {
  try {
    healthBadge.textContent = 'Live';
    const dashboard = await loadJson('/api/dashboard');
    state.dashboard = dashboard;
    renderTelemetry(dashboard.telemetry);
    renderEndpoints(dashboard.endpoints);
    renderProviders(dashboard.providers);
    renderExports(dashboard.exports);
    renderHistory(dashboard.installHistory);
    const config = await loadJson('/api/config');
    state.config = config;
    renderEnvironment(config);
    bindConfig(config);
  } catch (error) {
    healthBadge.textContent = 'Error';
    console.error(error);
  }
}

document.querySelector('#refreshButton').addEventListener('click', refreshDashboard);

document.querySelector('#configForm').addEventListener('submit', async (event) => {
  event.preventDefault();
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
  if (!response.ok && result.requiresConfirmation) {
    alert(result.message);
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

for (const [formId, url, mapForm] of [
  ['packageInstallForm', '/api/install/package', (form) => ({
    source: form.elements.source.value,
    localPath: '',
    kind: form.elements.kind.value,
    arguments: form.elements.arguments.value,
    allowUntrustedExecution: form.elements.allowUntrustedExecution.checked
  })],
  ['repoInstallForm', '/api/install/repo', (form) => ({
    repositoryUrl: form.elements.repositoryUrl.value,
    branch: form.elements.branch.value,
    manifestFile: form.elements.manifestFile.value,
    allowUntrustedExecution: form.elements.allowUntrustedExecution.checked
  })],
  ['zipInstallForm', '/api/install/zip', (form) => ({
    source: form.elements.source.value,
    manifestFile: form.elements.manifestFile.value,
    allowUntrustedExecution: form.elements.allowUntrustedExecution.checked
  })]
]) {
  document.querySelector(`#${formId}`).addEventListener('submit', async (event) => {
    event.preventDefault();
    const form = event.currentTarget;
    const result = await loadJson(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(mapForm(form))
    });
    alert(result.message);
    form.reset();
    await refreshDashboard();
  });
}

refreshDashboard();
setInterval(refreshDashboard, 5000);
