// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const form = document.getElementById('setupForm');
const statusText = document.getElementById('setupStatus');
let applying = false, wifiScanBusy = false;
async function wifiApi(path, data) {
  const response = await fetch('/api/' + path, {
    method: data === undefined ? 'GET' : 'POST',
    headers: data === undefined ? {} : {'Content-Type':'application/json','X-WTT2MQTT':'1'},
    body: data === undefined ? undefined : JSON.stringify(data), signal:AbortSignal.timeout(8000)
  });
  if (!response.ok) throw new Error(await response.text());
  return response.json();
}
async function refresh(initial = false) {
  try {
    const state = await wifiApi('wifi');
    if (initial) {
      form.elements.ssid.value = state.ssid; form.elements.hostname.value = state.hostname;
      document.getElementById('wifiSelected').textContent = state.ssid ? `Gespeichert: ${state.ssid}` : 'Kein Netzwerk ausgewählt.';
    }
    if (!applying) statusText.textContent = state.ssid
      ? `Gespeichertes WLAN: ${state.ssid}. Während eines Scans pausiert die Wiederverbindung.`
      : 'Noch kein WLAN gespeichert. Bitte Zugangsdaten eingeben.';
  } catch (error) {
    statusText.textContent = applying
      ? 'Verbindung zum Setup-AP getrennt. Wenn das WLAN verbunden ist, die Geräteseite im normalen WLAN öffnen. Andernfalls erneut mit dem Setup-AP verbinden.'
      : error.message;
  }
}
form.onsubmit = async event => {
  event.preventDefault();
  const data = {ssid:form.elements.ssid.value, hostname:form.elements.hostname.value};
  if (form.elements.openWifi.checked) data.wifiPassword = '';
  else if (form.elements.wifiPassword.value) data.wifiPassword = form.elements.wifiPassword.value;
  document.getElementById('save').disabled = true;
  try {
    await wifiApi('wifi',data); applying = true;
    statusText.textContent = 'Gespeichert. Der ESP32 verbindet sich ohne Neustart mit dem WLAN. Der Setup-AP schließt nach erfolgreicher Verbindung.';
  } catch (error) { statusText.textContent = error.message; }
  finally { document.getElementById('save').disabled = false; }
};
function renderNetworks(items) {
  const list = document.getElementById('networks'); list.replaceChildren();
  for (const network of items) {
    if (!network.ssid) continue;
    const button = document.createElement('button'); button.type = 'button'; button.className = 'network-card';
    button.setAttribute('aria-pressed', String(network.ssid === form.elements.ssid.value));
    const name = document.createElement('strong'); name.textContent = network.ssid;
    const meta = document.createElement('span'); meta.className = 'network-meta';
    meta.textContent = `${network.rssi} dBm · ${network.secure ? 'Gesichert' : 'Offen'}${network.bssid ? ' · ' + network.bssid : ''}`;
    const signal = document.createElement('span'); signal.className = 'network-signal'; signal.setAttribute('aria-hidden','true');
    signal.textContent = network.rssi >= -60 ? '▂▄▆█' : network.rssi >= -75 ? '▂▄▆' : '▂▄';
    button.append(name, meta, signal); button.onclick = () => {
      form.elements.ssid.value = network.ssid;
      document.getElementById('wifiSelected').textContent = `Ausgewählt: ${network.ssid}${network.bssid ? ' · ' + network.bssid : ''}`;
      for (const row of list.querySelectorAll('.network-card')) row.setAttribute('aria-pressed', String(row === button));
    };
    list.append(button);
  }
}
document.getElementById('scan').onclick = async () => {
  if (wifiScanBusy) return; wifiScanBusy = true;
  const scanButton = document.getElementById('scan'), scanStatus = document.getElementById('wifiScanStatus');
  scanButton.disabled = true; scanStatus.textContent = 'Suche läuft …';
  try {
    for (;;) {
      const result = await wifiApi('scan');
      if (result.running) { await new Promise(resolve => setTimeout(resolve,1000)); continue; }
      renderNetworks(result.networks ?? []);
      scanStatus.textContent = result.error || `${(result.networks ?? []).length} Netzwerke gefunden.`;
      break;
    }
  } catch (error) { scanStatus.textContent = error.message; }
  finally { wifiScanBusy = false; scanButton.disabled = false; }
};
refresh(true); setInterval(()=>refresh(),5000);
