# WTT2MQTT

Eigenständige ESP32-S3-Firmware für eine direkte W-Bus-Verbindung zu älteren **Webasto Thermo Top Z/C-D**. Lokale Weboberfläche, MQTT und Home Assistant, ohne Fahrzeug-CAN, Gateway oder Cloud. **Version 0.1.0 ist eine kompilierte Entwicklungsfirmware; keine Freigabe für alle Z/C-D-Steuergeräte.**

Die Architektur und Benutzerführung orientieren sich an [Solar2MQTT](https://github.com/softwarecrash/Solar2MQTT). Die W-Bus-Protokollimplementierung basiert auf [WTTC](https://github.com/matatunos/wttc). Solar2MQTT-Code wird wegen dessen CC BY-NC-ND-Lizenz nicht eingebunden. WTT2MQTT und die eingebundenen WTTC-Bestandteile stehen unter **GPL-3.0-or-later**; siehe [Herkunft und Lizenzen](docs/REFERENCES.md).

## Was die erste Firmware enthält

- Nicht blockierende W-Bus-Zustandsmaschine: 2400 Baud, **8E1**, Break, Echo, begrenzte Telegramme, XOR, Timeouts und getrennte Fehlerursachen.
- Start mit begrenzter Laufzeit (1–120 Minuten), Haltebefehl alle 5 Sekunden, regulärer Stopp und Stoppversuche nach unbestätigtem Start.
- Kühlmitteltemperatur, Spannung, optional Flamme, Heizleistung und Glühstiftwiderstand. Fehlende Messwerte werden `null`/`unknown` und niemals als Nullwert ausgegeben.
- Gespeicherte Fehler einschließlich deutschem Klartext; Name, Fertigung, W-Bus-Version und Zähler bei unterstützten Registern.
- WLAN mit DHCP, automatischer Wiederverbindung, offenem Setup-AP, Captive-DNS, asynchronem WLAN-Scan und mDNS.
- MQTT mit Anmeldung, QoS 1, Last Will, Wiederverbindung, Einzel-Topics, optional JSON und Home Assistant Discovery (Sensoren, Flammensensor, Schalter für die Heizanforderung). Retained-Steuerbefehle werden abgewiesen.
- Kompakte Oberfläche mit maximal 520 Pixeln Breite, Statusleiste und Einstellungsmenü wie in der Solar2MQTT-Projektfamilie. Alle Ansichten werden innerhalb einer eingebetteten HTML-Seite umgeschaltet: Status, Diagnose, MQTT, WLAN/GPIOs, Firmware und Debugging/WebSerial.
- Validierte Einstellungen in NVS; gzip-Webressourcen im Programmflash. Zwei App-Slots im 4-MB-Flash, **kein Dateisystem, kein vorausgesetztes PSRAM**.

Die Steuergeräteregister liefern hier **keinen verifizierten vollständigen Heizphasencode**. Die UI unterscheidet deshalb „Verbrennung erkannt“, „Start / Regelpause abgeleitet“, „Nachlauf angenommen“, „Keine Flamme erkannt“ und „Unbekannt“. Ein ACK bedeutet nicht, dass die Heizung tatsächlich brennt. Nach jedem Start der Firmware und nach einem Stopp gilt eine konservative 180-s-Startsperre. Sie ist kein gemessener Nachlauf.

Nicht implementiert: freie Roh-Schreibbefehle, Entriegeln/Fehlerlöschen, Aktortests, Codierung, Leistungs-Sollwert, verifizierte ECU-Firmwareversion, Bluetooth, Android, Telegram, Displays, externe Sensoren, Zeitprogramme und CAN.

## Hardware und Verdrahtung

ESP32-S3 SuperMini, 4 MB Flash, externer stabiler 3,3-V-Wandler und TJA1020. Standard **GPIO16 RX / GPIO17 TX**, in der Weboberfläche änderbar; USB GPIO19/20 bleibt frei. Der TJA1020 benötigt zusätzlich seine busseitige Batteriespannung; 3,3 V versorgen den ESP32, nicht den BAT-Anschluss des Transceivers.

Siehe **[Verdrahtungsplan und Modulhinweise](docs/WIRING.md)**. Ob GPIO17 bei deinem konkreten SuperMini herausgeführt ist, muss anhand der Platine geprüft werden. Webasto-Stecker unterscheiden sich nach Ausführung; es wird keine universelle Pinbelegung behauptet. Nur ein W-Bus-Master darf gleichzeitig steuern.

## Bauen und erstmalig flashen

Voraussetzung: Python 3.10+, Git, PlatformIO Core **6.2.0** (getestet). Versionierte Plattform: Espressif32 **6.10.0**, Arduino-ESP32 **2.0.17**. Die Basis-Boarddefinition nennt 8 MB; die Projektkonfiguration überschreibt Flashgröße, Uploadgröße und Partitionen ausdrücklich auf 4 MB. PSRAM wird nicht aktiviert.

```sh
python -m pip install platformio==6.2.0
pio run -e esp32s3_supermini
pio run -e esp32s3_supermini -t upload --upload-port COM7
pio device monitor --port COM7 --baud 115200
```

Unter Linux statt `COM7` beispielsweise `/dev/ttyACM0`. Falls nötig BOOT beim Anschluss halten, dann RESET drücken. Der erste Upload erfolgt per USB und schreibt Bootloader, Partitionstabelle und App. Keine Dateisystem-Upload-Schritte.

Jeder normale `pio run` erzeugt automatisch im Projektordner **`firmware/`**:

| Datei | Verwendung |
|---|---|
| `WTT2MQTT_esp32s3_supermini_V0.1.0.bin` | Vollständiges 4-MB-USB-Image ab Offset `0x0`, einschließlich Bootloader und Partitionen |
| `WTT2MQTT_esp32s3_supermini_V0.1.0.bin.ota` | App-Image für die Firmware-Update-Funktion, wie das `.bin.ota`-Format der Vorlage |
| `manifest.json` | Dateigrößen, SHA-256-Prüfsummen und Zuordnung |

Es ist kein zusätzlicher Verpackungsschritt nötig. Auch wenn die Firmware bereits aktuell ist, werden die Exportdateien bei `pio run` erzeugt. Die Endung `.ota` bezeichnet das unveränderte ESP32-App-Image, keinen speziellen Container. Das vollständige USB-Image niemals über OTA hochladen: Es ist für Ersteinrichtung/Wiederherstellung und **setzt NVS-Einstellungen und optionale Zugangsdaten zurück**. Für Aktualisierungen mit Einstellungsübernahme die `.bin.ota`-Datei oder den normalen PlatformIO-Upload verwenden.

Unter `.pio/build/esp32s3_supermini/` bleiben `firmware.bin` als App-Image und `firmware.elf` mit Debugsymbolen verfügbar. `python tools/package_firmware.py` erstellt bei Bedarf zusätzlich ein Quellenarchiv und weitere Release-Dateien unter `build/release/`.

## Ersteinrichtung

1. Mit dem offenen WLAN `wtt2mqtt` verbinden; es ist kein AP-Passwort erforderlich.
2. `http://192.168.4.1/` aufrufen. Im AP-Modus ist ausschließlich eine separate WLAN-Konfigurationsseite ohne Anmeldung erreichbar. Dashboard, Steuerung, Diagnose, Logs, MQTT-Einstellungen, GPIOs, Webzugang und OTA sind über diesen Zugang auch auf API-Ebene gesperrt. Captive-Assistenten öffnen die Seite ggf. nicht automatisch; Adresse dann manuell aufrufen.
3. WLAN-Name, Passwort und optional Gerätename eingeben und „Speichern und verbinden“ drücken. Die Verbindung wird ohne Neustart umgestellt; bei falschen Zugangsdaten bleibt der AP erreichbar. Ein leeres Passwortfeld behält das bisherige Passwort; die Checkbox „Offenes WLAN“ entfernt es.
4. Nach erfolgreicher Verbindung die vollständige Oberfläche im normalen WLAN öffnen, um MQTT, GPIOs und optionalen Webzugang einzurichten. Diese übrigen Konfigurationsänderungen gelten nach Neustart. Vor Neustart oder OTA eine eigene aktive Heizanforderung stoppen. Messwerte und die Startsperre sind keine Voraussetzung für die Firmware-Wartung.
5. Nach WLAN-Verbindung wird der Setup-AP automatisch geschlossen. Bei Netzverlust versucht der ESP32 die Wiederverbindung; nach 30 Sekunden ohne Verbindung öffnet er zusätzlich den AP und versucht weiterhin etwa alle 30 Sekunden das gespeicherte WLAN. Sobald es wieder erreichbar ist und die Verbindung gelingt, schließt der AP ohne Neustart. Erreichbarkeit im normalen WLAN zusätzlich über `http://<hostname>.local/`, sofern das Netzwerk mDNS unterstützt.
6. Zuerst ohne Brennerstart Diagnose und plausible Messwerte prüfen. Nach Neustart fragt die Firmware zunächst W-Bus-Version und Gerätename ab, danach Sensoren. Sie sendet beim Booten keinen automatischen Stopp und setzt einen vorigen Heizwunsch nicht fort.

Die Weboberfläche ist standardmäßig ohne Anmeldung erreichbar, auch im normalen WLAN. Unter **WLAN / Webzugang (optional)** können dort Benutzername und Passwort selbst gesetzt werden. Nur wenn beide gesetzt sind, wird HTTP-Basic-Auth im normalen WLAN aktiv. Ein leerer Benutzername oder ein leeres gespeichertes Passwort deaktiviert den Schutz; die Checkbox zum Deaktivieren entfernt beide Zugangsdaten. Ein leeres Passwortfeld beim Speichern behält ein vorhandenes Passwort bei. Änderungen gelten nach Neustart. Im Setup-AP bleibt ausschließlich die WLAN-Konfiguration ohne Anmeldung erreichbar; der optionale Webzugang lässt sich dort nicht ändern.

Beim ersten Start dieser Firmware werden die von bisherigen Builds automatisch vergebenen Zugangsdaten entfernt. WLAN-, MQTT- und sonstige Einstellungen bleiben erhalten.

HTTP-Basic-Auth und MQTT werden in dieser Version ohne TLS übertragen. Nur lokale vertrauenswürdige Netze verwenden. Die Firmware lässt keine fremden Web-Ursprünge für Schreibzugriffe zu; diese benötigen bei aktiviertem Webzugang im normalen WLAN Authentifizierung und immer den Header `X-WTT2MQTT: 1`. Keine Cloud-Anmeldung oder Telemetrie.

## Benutzung und OTA

Für unbekannte ältere Steuerplatinen: **Webasto-Diagnose → Varianten testen**. Der begrenzte Scan prüft W-Bus sowie alte TT-C-Diagnose mit 10400 Baud/8N1 und drei Wakeup-Varianten. Normale Abfragen pausieren; Rohtelegramme erscheinen im Live-Log, Ergebnisse pro Profil auf der Diagnoseseite. USB: `scan` und `scan-stop`. Nach Ende/Abbruch gilt wieder 2400/8E1. Ein Treffer schaltet keine Heizungssteuerung für das andere Protokoll frei. DS2/Fiat/PSA/Rover bleiben mangels verifizierter Anfragen ungetestet. [Details und Quellen](docs/PROTOCOL_VARIANTS.md).

Das Live-Log und das USB-Log scrollen automatisch zum neuesten Eintrag. **Sperren**, Hochscrollen oder Markieren friert die Ansicht ein; eingehende Daten werden weiter gesammelt. **Entsperren** zeigt wieder den aktuellen Stand. **Log kopieren** funktioniert auch auf der HTTP-Geräteseite; alternativ stehen **Alles markieren** und **Herunterladen** zur Verfügung. Der Download enthält die angezeigte Ansicht. **Leeren** löscht nur den Browserverlauf. Der Browser hält maximal 2.000 vollständige Zeilen beziehungsweise 256 KiB Text vor; die Abfrage kann bei unterbrochener Verbindung Einträge verpassen.

Start/Stopp im Dashboard oder über [MQTT](docs/MQTT.md). „Diagnose lesen“ fragt die freigegebenen Lese-Register ab. Der Debugbereich zeigt TX/RX und Ergebnisse. USB-Befehle: `status`, `diagnose`, `stop`, `credentials`. Start über USB ist bewusst nicht freigegeben. Der zusätzliche Browser-WebSerial-Zugang benötigt Chrome/Edge und einen sicheren Ursprung (HTTPS/localhost); über die normale HTTP-Geräteseite ist stattdessen das Live-Log nutzbar.

Lokales OTA: Unter **Einstellungen → Firmware-Update** die Datei **`firmware/WTT2MQTT_esp32s3_supermini_V0.1.0.bin.ota`** wählen. Auch ein App-Image mit Endung `.bin` wird akzeptiert. **Kein angeschlossenes oder versorgtes Heizgerät und keine Messwerte erforderlich.** Der angenommene Nachlauf und die initiale Startsperre blockieren das Update nicht. Nur eine eigene aktive oder gerade startende Heizanforderung muss zuvor beendet werden; während des Updates werden neue Starts abgewiesen. Im Setup-AP bleibt OTA entsprechend der WLAN-only-Vorgabe gesperrt, im normalen WLAN ist es unabhängig vom W-Bus verfügbar.

Das aktuelle Image bleibt während des Uploads im anderen Slot; vor Umschaltung prüft Arduino Update das Image. Keine kryptografische Signatur und kein zugesicherter automatischer Rollback nach einem fehlerhaften Anwendungsstart. Bei OTA-Abbruch/Fehler wird nicht neu gestartet. USB-Wiederherstellung bleibt möglich. OTA-Stromausfallverhalten und Flash-Latenzen sind noch am Gerät zu prüfen.

## Tests und Projektstruktur

```sh
pio run -e esp32s3_supermini
python tools/verify_artifacts.py
# Optionaler Browser-Test mit gemockter API, kein Hardwaretest:
python -m pip install -r tests/requirements-ui.txt
python tools/test_ui.py
```

Die Funktionsprüfung erfolgt an echter ESP32-/TJA1020-/Webasto-Hardware. Der [Hardware-Prüfplan](docs/TESTING.md) beschreibt Diagnose, Start/Stopp, Kommunikationsausfall, MQTT und OTA. Browserprüfungen mit einer gemockten HTTP-API dienen ausschließlich der Oberflächenprüfung und sind kein Heizgerätetest.

| Pfad | Verantwortung |
|---|---|
| `lib/WBus/` | Hardwareunabhängige Frames, Parser, UART-Zustandsmaschine |
| `lib/Heater/` | Heizwunsch, Prioritäten, Haltebefehl, Messwerte, Diagnose |
| `src/main.cpp` | UART-Adapter und alleiniger Besitzer der Heizungssteuerung |
| `src/Config.cpp` | Validierung und NVS |
| `src/Network.cpp` | WLAN, Captive-DNS, HTTP/API, OTA |
| `src/Mqtt.cpp` | ESP-IDF-MQTT-Client, Topics, Home Assistant |
| `src/Runtime.cpp` | Bounded Command-Queue, Snapshots, Log-Ring |
| `web/` | Eigenständig geschriebene Oberfläche ohne externe Ressourcen |
| `third_party/wttc/` | Originale Fehlertexte und Herkunftsnachweise |

Die Arduino-Hauptschleife besitzt UART und Heizungszustand auf Core 1. Der Netzwerk-Task läuft auf Core 0; MQTT hat seinen asynchronen ESP-IDF-Task. Nur POD-Snapshots und eine begrenzte Queue verbinden beide Seiten. Keine Antwort-, WLAN- oder MQTT-Warteschleifen in der Steuerung; `vTaskDelay(1)` gibt lediglich CPU-Zeit ab. HTTP darf im Netzwerk-Task auf einen langsamen Client warten, ohne die Steuerung logisch zu blockieren; Flash-Schreiben beeinflusst auf ESP32 dennoch beide Cores und muss real vermessen werden.

**Nicht auf echter Webasto-/TJA1020-Hardware geprüft:** elektrische Pegel und Break-Verhalten, variantenspezifische Protokollunterstützung, reale Heizphasen, Fehlertexte im konkreten Gerät, Dauerbetrieb, WLAN/MQTT am Board sowie OTA/Reset unter realen Bedingungen. Protokollbelege sind nachvollziehbar in [Referenzanalyse](docs/REFERENCES.md) und [Protokollumfang](docs/PROTOCOL.md). Die Kompatibilität wird am konkreten Heizgerät geprüft.
