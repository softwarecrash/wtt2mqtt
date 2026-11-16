# Referenzanalyse und Wiederverwendung

Untersuchungsstand: **2026-11-16**. Beide Repositories wurden lokal mit festgehaltenem Commit heruntergeladen und ihre Verzeichnisse inventarisiert. Die Arbeitskopien liegen nur im ignorierten `.reference/`-Verzeichnis; das neue Projekt benötigt sie weder beim Build noch zur Laufzeit.

## Solar2MQTT

Quelle: https://github.com/softwarecrash/Solar2MQTT, Commit **618a839d228aca3d233539cafdb1195adf9027e6**.

| Bereich und untersuchte Quellen | Erkenntnis / Umsetzung in WTT2MQTT |
|---|---|
| `platformio.ini`, Partitionen, `tools/pre_build.py` | Modulare PlatformIO-Struktur, Embedded-gzip-Webassets. Eigener deterministischer Assetgenerator, versioniertes kleines Arduino-2-Profil statt der deutlich umfangreicheren Solar-Plattform, feste 4-MB-Dual-OTA-Tabelle |
| `src/main.cpp`, `core/*`, `SolarInverterService` | Trennung von Gerät, Netzwerk, Web, Konfiguration. Eigene hardwareunabhängige W-Bus-/Heater-Klassen; kein Wechselrichter-/Modbus-/PI-Code übernommen |
| `WiFiManager.*`, WLAN-Wiki | AP-Fallback, Captive-DNS, mDNS und Reconnect als Benutzerführung. Die Referenz enthält teilweise wartende Verbindungsversuche; hier eigene ereignis-/zeitgesteuerte Netzverwaltung außerhalb des Bus-Owners |
| `SettingsPrefs.*`, Schema | NVS/Preferences statt notwendigem Dateisystem. Eigene kleine validierte Konfiguration mit atomarem NVS-Blob und erst nach Neustart wirksamen Änderungen |
| `MqttHandler.*`, `HaDiscoveryCatalog.h`, HA-Wiki | Gerätediscovery, Availability und Einzel-/JSON-Topics. Neue Webasto-Entities und ESP-IDF-Async-MQTT, keine Inverterdeskriptoren |
| `WebServerHandler.*`, `webUI/*.html`, `app.js`, `style.css`, `statusbar.js` | Dunkles Blau, gelbe Akzente, Karten, klare Statusanzeige und separate Konfigurationsseiten als Stilvorlage. HTML/CSS/JS unabhängig neu geschrieben; keine Assets, Logos, Bilder oder JS kopiert |
| `FirmwareWriter.h`, `GitHubOtaUpdater.*`, Firmware-Webseite | Zwei Slots, Update-Exklusivität, klarer Updatezustand. Nur lokales Upload-OTA mit Service-Handschlag; kein fremdes GitHub-Releaseziel oder Cloud-Updater |
| `LogSerial.*`, `Logging.cpp`, Debug-/WebSerial-Seiten, Auth-Wiki | Live-Diagnose und USB erhalten. Neuer begrenzter Logring, eigenes HTTP-Log und native Browser-WebSerial-Option; kein MycilaWebSerial-Code kopiert |
| Hosttests / Wiki / Bilder / Protokollverzeichnisse | Inventar als Architektur-/Testkontext. Inverterprotokolle, Solar-/Energiezähler, externe Sensoren, Ethernet und Hardwarevarianten außerhalb des gewünschten Umfangs nicht portiert |

**Lizenzentscheidung:** Das Solar2MQTT-`LICENSE` enthält **CC BY-NC-ND 4.0**, keine allgemein passende Open-Source-Softwarelizenz. Trotz gleicher Projektfamilie und Eigentümerschaft des Auftraggebers wird keine stillschweigende Umlizenzierung von Referenzcode behauptet. Deshalb gibt es keine Code-/Assetkopien aus Solar2MQTT in diesem GPL-Projekt. Wiederverwendet werden architektonische Ansätze und vertraute Bedienkonzepte durch Neuimplementierung. Eine spätere explizit erlaubte Übernahme kann gesondert dokumentiert werden.

## WTTC

Quelle: https://github.com/matatunos/wttc, Commit **adbbf985a46c425c476f0dd9db3e8422f0bc7ea1**. Zusätzlich https://wttc.favala.es/en/.

Als technische Quellen dienen die W-Bus-Bereiche des zentralen `firmware/WTTC/WTTC.ino`, die Frame-/Sensor-/Diagnoseparser in `logica.h`, `averias.h`, Start/Stopp-/Keepalive-/Nachlauflogik und Diagnose `runCheck`. Android, Telegram, Serverstatistik, Displays, Raumthermostat und Heizprogramme werden nicht eingebunden. Die Funktionsvalidierung von WTT2MQTT erfolgt an echter Hardware.

Die Transportdetails sind anhand des Quellcodes überprüft: 2400/8E1, F4/4F, Längenbyte, XOR, Echo, 50/50-ms-Wakeup, Sensorindex 05, Fehler 56/01 und Start/Stopp/Keepalive. WTTCs blockierende UART-Warteschleifen wurden nicht übernommen. Verfügbare Diagnoseindizes für Identifikation und Zähler sind optional übernommen; unbekannte Befehle werden nicht geraten. [Protokolltabelle](PROTOCOL.md) dokumentiert auch die bewusst nicht exponierten Fehlerlösch-/Detailbefehle.

**Wichtige Evidenzgrenze:** WTTC bezeichnet sich selbst im README als Testversion ohne Prüfung an einer realen Webasto. Quellcodebeschreibungen sind deshalb keine unabhängige Bestätigung der ECU-Sensorlayouts oder der Kompatibilität zu allen Thermo Top Z/C-D. Die tatsächlichen Antworten und Zustandswechsel werden am konkreten Heizgerät geprüft und protokolliert.

**Lizenz:** WTTC GPLv3 oder später. Originaltexte und abgeleitete Protokollteile sind gekennzeichnet; [NOTICE](../third_party/wttc/NOTICE.md) nennt Dateien, Commit, Änderungen und Herkunft. GPL-Text liegt unter [LICENSE](../LICENSE). ArduinoJson (MIT), Arduino-ESP32 und ESP-IDF enthalten eigene Lizenztexte in den versioniert installierten Abhängigkeiten; bei Binärverteilung diese mit den zugehörigen Quellen berücksichtigen.

## Hardwarequelle

Die elektrischen TJA1020-Hinweise wurden zusätzlich mit den Herstellerdokumenten abgeglichen: [NXP TJA1020](https://www.nxp.com/docs/en/data-sheet/TJA1020.pdf), [NXP AN00093](https://www.nxp.com/docs/en/application-note/AN00093.pdf). Der Projekt-Verdrahtungsplan ersetzt nicht den Schaltplan eines unbekannten Breakout-Moduls oder der konkreten Webasto.
