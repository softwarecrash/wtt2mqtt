// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const $ = id => document.getElementById(id);
let config, latest, serialPort, serialWriter, noticeTimer, polling = false, uploading = false;
const phases = {unknown:'Unbekannt',combustion_observed:'Verbrennung erkannt',cooldown_estimated:'Nachlauf angenommen',start_or_control_pause_inferred:'Start / Regelpause abgeleitet',no_flame_observed:'Keine Flamme erkannt'};
function text(id, value) { $(id).textContent = value ?? '—'; }
function notice(message) { text('notice', message); $('notice').hidden = false; clearTimeout(noticeTimer); noticeTimer = setTimeout(() => $('notice').hidden = true, 8000); }
const logView = new LogViewer('log',{lock:'logLock',copy:'copyLog',select:'selectLog',clear:'clearLog',download:'downloadLog',wrap:'logWrap',status:'logStatus',filename:'wtt2mqtt.log'},notice);
const serialView = new LogViewer('serialLog',{lock:'serialLock',copy:'serialCopy',select:'serialSelect',clear:'serialClear',download:'serialDownload',wrap:'serialWrap',status:'serialStatus',filename:'wtt2mqtt-usb.log'},notice);
const scanLogView = new LogViewer('scanLog',{lock:'scanLogLock',copy:'scanLogCopy',select:'scanLogSelect',clear:'scanLogClear',download:'scanLogDownload',wrap:'scanLogWrap',status:'scanLogStatus',filename:'wtt2mqtt-scan.log'},notice);
$('scanLogLoad').onclick = async () => {
  try { const value=await api('scan-log'); scanLogView.buffer=value; scanLogView.setLocked(false); notice(value ? 'Scan-Mitschnitt geladen.':'Noch kein Scan-Mitschnitt vorhanden.'); }
  catch(e) { notice(e.message); }
};
async function api(path, data) {
  const response = await fetch('/api/' + path, {method:data === undefined ? 'GET':'POST', headers:data === undefined ? {} : {'Content-Type':'application/json','X-WTT2MQTT':'1'}, body:data === undefined ? undefined : JSON.stringify(data), signal:AbortSignal.timeout(8000)});
  if (!response.ok) throw new Error(await response.text());
  return response.headers.get('content-type')?.includes('application/json') ? response.json() : response.text();
}
function route() {
  const titles = {dashboard:'WTT2MQTT',menu:'Einstellungen',wifi:'WLAN',mqtt:'MQTT',diagnose:'Webasto-Diagnose',firmware:'Firmware',debug:'Debugging'};
  const requested = location.hash.slice(1) || 'dashboard';
  const id = requested in titles ? requested : 'dashboard';
  for (const section of document.querySelectorAll('main>section')) section.hidden = section.id !== id;
  for (const a of document.querySelectorAll('nav a')) a.setAttribute('aria-current', a.hash === '#' + id ? 'page':'false');
  text('pageTitle',titles[id]); document.title = titles[id] === 'WTT2MQTT' ? titles[id] : titles[id] + ' · WTT2MQTT';
  $('headerBack').href = id === 'menu' || id === 'dashboard' ? '#dashboard' : '#menu';
}
addEventListener('hashchange', route); route();
function details(id, entries) {
  const target = $(id); target.replaceChildren();
  for (const [key,value] of entries) { const dt = document.createElement('dt'), dd = document.createElement('dd'); dt.textContent = key; dd.textContent = value ?? 'Nicht verfügbar'; target.append(dt, dd); }
}
async function loadConfig() {
  config = await api('config');
  for (const form of document.querySelectorAll('form.panel')) for (const input of form.querySelectorAll('input[name]')) {
    if (!(input.name in config)) continue;
    if (input.type === 'checkbox') input.checked = config[input.name]; else input.value = config[input.name];
  }
  $('duration').value = config.minutes;
}
for (const id of ['mqttForm','wifiForm','hardwareForm','securityForm']) $(id).addEventListener('submit', async event => {
  event.preventDefault(); const payload = {};
  for (const input of event.currentTarget.querySelectorAll('input[name]')) {
    if (input.name === 'openWifi' || input.name === 'disableWebAuth') continue;
    if (input.type === 'password' && !input.value) continue;
    payload[input.name] = input.type === 'checkbox' ? input.checked : input.type === 'number' ? Number(input.value) : input.value;
  }
  if (id === 'wifiForm' && $('wifiForm').elements.openWifi.checked) payload.wifiPassword = '';
  if (id === 'securityForm') {
    if ($('securityForm').elements.disableWebAuth.checked || !payload.adminUser) {
      payload.adminUser = ''; payload.adminPassword = '';
    }
  }
  try { await api('config',payload); notice('Gespeichert. Nach einem Neustart werden die neuen Einstellungen geladen.'); }
  catch (e) { notice(e.message); }
});
async function command(action) {
  try { await api('command',{action,minutes:Number($('duration').value)}); notice('Befehl eingereiht. Ergebnis steht im Dashboard.'); }
  catch (e) { notice(e.message); }
}
$('start').onclick = () => command('start'); $('stop').onclick = () => command('stop'); $('diagnoseButton').onclick = () => command('diagnose');
$('protocolScan').onclick = () => command('scan'); $('protocolScanStop').onclick = () => command('scan-stop');
$('restart').onclick = async () => { try { await api('restart',{}); notice('ESP32 startet neu. Seite in wenigen Sekunden neu laden.'); } catch (e) { notice(e.message); } };
$('scan').onclick = async () => {
  try { const d = await api('scan'); if (d.running) { notice('Suche läuft …'); setTimeout(() => $('scan').click(),2000); return; }
    $('networks').replaceChildren(); for (const n of d.networks ?? []) { const o = document.createElement('option'); o.value = n.ssid; o.label = n.rssi + ' dBm'; $('networks').append(o); } notice('Netzwerke im SSID-Auswahlfeld verfügbar.');
  } catch (e) { notice(e.message); }
};
async function refresh() {
  if (polling || uploading) return; polling = true;
  try {
    const s = latest = await api('status'); text('connection',s.online ? 'W-Bus online':'W-Bus ohne Antwort');
    $('protocolScan').disabled = s.requested || s.scan_active || s.ota_active;
    $('protocolScanStop').disabled = !s.scan_active;
    $('diagnoseButton').disabled = !!s.scan_active;
    const outcomes={untested:'Nicht getestet',no_echo:'Kein Echo',echo_only:'Nur Echo, keine Antwort',invalid_response:'Ungültige Antwort',negative_response:'Gültige negative Antwort',positive_response:'Gültige positive Antwort',unsupported_transport:'UART-Profil nicht verfügbar'};
    text('protocolScanState',s.scan_active ? `Suche läuft: Profil ${s.scan_profile+1}/${(s.scan_results??[]).length}` : (s.scan_results??[]).some(p=>p.result!=='untested') ? 'Suche beendet / abgebrochen; Normalbetrieb mit 2400/8E1' : 'Noch nicht gestartet');
    details('protocolScanResults',(s.scan_results??[]).map(p=>[p.name,outcomes[p.result]??p.result]));
    text('phase',phases[s.phase] ?? s.phase); text('temperature',s.temperature); text('voltage',s.voltage?.toFixed(2)); text('power',s.power);
    text('flame',s.flame === null ? '—' : s.flame ? 'Ja':'Nein'); text('commandResult',s.command_result);
    text('remaining',s.requested ? `Heizanforderung aktiv · noch ${Math.ceil(s.remaining_seconds/60)} Minuten` : 'Keine eigene Heizanforderung aktiv');
    $('start').disabled = !s.online || s.temperature === null || s.cooldown || s.requested || s.flame !== false || s.ota_active;
    $('uploadButton').disabled = !s.update_safe;
    $('restart').disabled = s.requested || s.ota_active;
    text('wifiState',`WLAN ${s.wifi_connected ? 'verbunden':'getrennt'}`); text('mqttState',`MQTT ${s.mqtt_connected ? 'verbunden':'getrennt'}`); text('busState',`W-Bus ${s.online ? 'online':'offline'}`);
    $('wifiState').dataset.online = s.wifi_connected; $('wifiState').title = s.ip;
    $('mqttState').dataset.online = s.mqtt_connected; $('busState').dataset.online = s.online;
    details('deviceInfo',[['Gerätename',s.device || null],['Fertigung',s.manufacture || null],['W-Bus-Version',s.wbus_version || null],['Arbeitszeit',s.counters ? `${s.counters.work_hours} h ${s.counters.work_minutes} min`:null],['Betriebszeit',s.counters ? `${s.counters.operating_hours} h ${s.counters.operating_minutes} min`:null],['Starts',s.counters?.starts]]);
    text('faultState',s.fault_count === null ? 'Keine gültige Fehlerliste vorhanden' : `${s.fault_count} gespeicherte Fehler · Abfrage vor ${s.fault_age_seconds} s`);
    $('faults').replaceChildren(); for (const f of s.faults) { const li = document.createElement('li'); li.textContent = `${f.code} · ${f.text} · ${f.count}×`; $('faults').append(li); }
    details('measurements',[['Glühstift',s.glow_milliohm === null ? null : s.glow_milliohm + ' mΩ'],['Messwertalter',s.sensor_age_seconds < 0 ? 'Veraltet / unbekannt':s.sensor_age_seconds+' s'],['Letztes Ergebnis',s.communication_error],['Gültige Antworten',s.successes],['Fehlversuche',s.failures]]);
    details('firmwareInfo',[['Version',s.firmware],['Laufzeit',s.uptime+' s'],['Freier Heap',s.free_heap+' Byte'],['Flashlayout','4 MB · app0 + app1 · NVS + OTA-Metadaten']]);
    text('updateState',s.ota_active ? 'Firmwareübertragung läuft …' : s.update_safe ? 'Bereit für OTA – auch ohne angeschlossenes Heizgerät.' : 'Vor dem Update die eigene Heizanforderung stoppen.');
    if (location.hash === '#debug') logView.snapshot(await api('log'));
  } catch (e) { text('connection','ESP32 nicht erreichbar'); $('start').disabled = $('uploadButton').disabled = true; }
  finally { polling = false; }
}
$('otaForm').onsubmit = async event => {
  event.preventDefault(); const file = $('firmwareFile').files[0];
  if (!file || file.size > 0x1f0000 || !/\.(bin|ota)$/i.test(file.name)) { notice('Eine OTA-App (.ota oder .bin) mit maximal 2031616 Byte wählen.'); return; }
  uploading = true; $('uploadButton').disabled = true; $('uploadProgress').hidden = false;
  const data = new FormData(); data.append('firmware',file);
  try { const r = await fetch('/api/update',{method:'POST',headers:{'X-WTT2MQTT':'1'},body:data}); const message = await r.text(); if (!r.ok) throw new Error(message); notice(message); }
  catch (e) { notice(e.message); } finally { uploading = false; $('uploadProgress').hidden = true; }
};
$('serialConnect').onclick = async () => {
  try {
    if (!navigator.serial || !isSecureContext) throw new Error('WebSerial benötigt Chrome/Edge auf localhost oder HTTPS. Alternativ PlatformIO Serial Monitor nutzen.');
    serialPort = await navigator.serial.requestPort(); await serialPort.open({baudRate:115200});
    serialWriter = serialPort.writable.getWriter(); const reader = serialPort.readable.getReader(), decoder = new TextDecoder();
    $('serialConnect').disabled = true;
    try { for (;;) { const {value,done} = await reader.read(); if (done) break; serialView.append(decoder.decode(value,{stream:true})); } }
    finally { reader.releaseLock(); serialWriter.releaseLock(); serialWriter = null; await serialPort.close(); $('serialConnect').disabled = false; }
  } catch (e) { notice(e.message); }
};
$('serialForm').onsubmit = async e => { e.preventDefault(); if (!serialWriter) return notice('Zuerst USB-Konsole verbinden.'); try { await serialWriter.write(new TextEncoder().encode($('serialCommand').value+'\n')); } catch (err) { notice(err.message); } };
loadConfig().catch(e=>notice(e.message)); refresh(); setInterval(refresh,2000);
