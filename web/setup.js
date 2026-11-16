// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const form = document.getElementById('setupForm');
const statusText = document.getElementById('setupStatus');
let applying = false;
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
    if (initial) { form.elements.ssid.value = state.ssid; form.elements.hostname.value = state.hostname; }
    if (!applying) statusText.textContent = state.ssid
      ? `Gespeichertes WLAN: ${state.ssid}. Automatische Wiederverbindung ist aktiv.`
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
document.getElementById('scan').onclick = async () => {
  try {
    const result = await wifiApi('scan');
    if (result.running) { statusText.textContent = 'WLAN-Suche läuft …'; setTimeout(()=>document.getElementById('scan').click(),2000); return; }
    const networks = document.getElementById('networks'); networks.replaceChildren();
    for (const network of result.networks ?? []) {
      const option = document.createElement('option'); option.value = network.ssid; option.label = network.rssi + ' dBm'; networks.append(option);
    }
    statusText.textContent = 'Gefundene Netzwerke stehen im SSID-Auswahlfeld zur Verfügung.';
  } catch (error) { statusText.textContent = error.message; }
};
refresh(true); setInterval(()=>refresh(),5000);
