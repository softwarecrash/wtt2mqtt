# WTT2MQTT

Eigenständige ESP32-S3-Firmware für eine direkte W-Bus-Verbindung zu älteren **Webasto Thermo Top Z/C-D**. Lokale Weboberfläche, MQTT und Home Assistant, ohne Fahrzeug-CAN, Gateway oder Cloud. **Version 0.1.0 ist eine kompilierte Entwicklungsfirmware; keine Freigabe für alle Z/C-D-Steuergeräte.**

Die Architektur und Benutzerführung orientieren sich an [Solar2MQTT](https://github.com/softwarecrash/Solar2MQTT). Das W-Bus-Protokoll und die Tests basieren auf [WTTC](https://github.com/matatunos/wttc). Solar2MQTT-Code wird wegen dessen CC BY-NC-ND-Lizenz nicht eingebunden. WTT2MQTT und die eingebundenen WTTC-Bestandteile stehen unter **GPL-3.0-or-later**; siehe [Herkunft und Lizenzen](docs/REFERENCES.md).

## Was die erste Firmware enthält

- Nicht blockierende W-Bus-Zustandsmaschine: 2400 Baud, **8E1**, Break, Echo, begrenzte Telegramme, XOR, Timeouts und getrennte Fehlerursachen.
- Start mit begrenzter Laufzeit (1–120 Minuten), Haltebefehl alle 5 Sekunden, regulärer Stopp und Stoppversuche nach unbestätigtem Start.
- Kühlmitteltemperatur, Spannung, optional Flamme, Heizleistung und Glühstiftwiderstand. Fehlende Messwerte werden `null`/`unknown` und niemals als Nullwert ausgegeben.
- Gespeicherte Fehler einschließlich deutschem Klartext; Name, Fertigung, W-Bus-Version und Zähler bei unterstützten Registern.
- WLAN mit DHCP, automatischer Wiederverbindung, passwortgeschütztem Setup-AP, Captive-DNS, asynchronem WLAN-Scan und mDNS.
- MQTT mit Anmeldung, QoS 1, Last Will, Wiederverbindung, Einzel-Topics, optional JSON und Home Assistant Discovery (Sensoren, Flammensensor, Schalter für die Heizanforderung). Retained-Steuerbefehle werden abgewiesen.
- Sechs responsive UI-Seiten: Dashboard, Diagnose, MQTT, WLAN/GPIOs, Firmware/OTA, Debugging/WebSerial. USB-Debugging und begrenzter Live-Log mit W-Bus-Telegrammen.
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

Builddateien unter `.pio/build/esp32s3_supermini/`: `firmware.bin` ist das OTA-Image; `firmware.elf` enthält Debugsymbole. `python tools/package_firmware.py` erzeugt zusätzlich ein USB-Gesamtimage, OTA-Image, Prüfsummen und Quellenarchiv unter `build/release/`. Ein Gesamtimage niemals über die OTA-Seite hochladen. Das USB-Gesamtimage ab Offset `0x0` ist für Ersteinrichtung/Wiederherstellung und **setzt NVS-Einstellungen und Zugangspasswort zurück**; für Aktualisierungen mit Einstellungsübernahme das OTA-Image oder normalen PlatformIO-Upload verwenden.

## Ersteinrichtung

1. USB-Serial-Monitor öffnen und `credentials` mit Enter senden. Benutzer ist `admin`; ein beim ersten Boot zufällig erzeugtes, dauerhaft gespeichertes 24-Zeichen-Passwort gilt für HTTP und Setup-AP. Es steht ausschließlich auf USB, nicht im Web-Log oder in der Konfigurations-API.
2. Mit `WTT2MQTT-xxxxxx` verbinden und `http://192.168.4.1/` öffnen. Derselbe Zugang schützt auch den AP-Modus. Captive-Assistenten öffnen die Seite ggf. nicht automatisch; Adresse dann manuell aufrufen.
3. WLAN, MQTT und bei Bedarf GPIOs konfigurieren und speichern. Mehrere gespeicherte Änderungen werden zusammengeführt. Passwortfelder leer lassen erhält das alte Passwort; die Checkbox „Offenes WLAN“ entfernt das WLAN-Passwort.
4. Nach Ablauf der initialen 180 Sekunden unter Firmware neu starten. Auch ohne angeschlossenes Heizgerät ist die Einrichtung möglich. Konfiguration lässt sich jederzeit speichern; sie wird erst beim Neustart aktiv. Ein Neustart ist bei Heizanforderung, erkannter Flamme oder angenommener Nachlaufphase gesperrt. Physischer Reset bleibt technisch möglich.
5. Nach WLAN-Verbindung wird der Setup-AP geschlossen. Ohne Verbindung entsteht nach 30 Sekunden erneut ein AP; der ESP32 versucht parallel die Wiederverbindung. Erreichbarkeit zusätzlich über `http://<hostname>.local/`, sofern das Netzwerk mDNS unterstützt.
6. Zuerst ohne Brennerstart Diagnose und plausible Messwerte prüfen. Nach Neustart fordert die Firmware zunächst regulären Stopp an; sie setzt einen vorigen Heizwunsch nicht fort.

HTTP-Basic-Auth und MQTT werden in dieser Version ohne TLS übertragen. Nur lokale vertrauenswürdige Netze verwenden. Die Firmware lässt keine fremden Web-Ursprünge für Schreibzugriffe zu; diese benötigen Authentifizierung und den Header `X-WTT2MQTT: 1`. Keine Cloud-Anmeldung oder Telemetrie.

## Benutzung und OTA

Start/Stopp im Dashboard oder über [MQTT](docs/MQTT.md). „Diagnose lesen“ fragt die freigegebenen Lese-Register ab. Der Debugbereich zeigt TX/RX und Ergebnisse. USB-Befehle: `status`, `diagnose`, `stop`, `credentials`. Start über USB ist bewusst nicht freigegeben. Der zusätzliche Browser-WebSerial-Zugang benötigt Chrome/Edge und einen sicheren Ursprung (HTTPS/localhost); über die normale HTTP-Geräteseite ist stattdessen das Live-Log nutzbar.

Lokales OTA: **nur `firmware.bin` für dieses Projekt und Board** hochladen. Es benötigt frische Sensordaten, keine Heizanforderung, keine Flamme, gemeldete Leistung 0 W und Ende der Nachlaufsperre. Ein Service-Handshake mit der Steuerung verhindert konkurrierende Starts. Das aktuelle Image bleibt während des Uploads im anderen Slot; vor Umschaltung prüft Arduino Update das Image. Keine kryptografische Signatur und kein zugesicherter automatischer Rollback nach einem fehlerhaften Anwendungsstart. Bei OTA-Abbruch/Fehler wird nicht neu gestartet. USB-Wiederherstellung bleibt möglich. OTA-Stromausfallverhalten und Flash-Latenzen sind noch am Gerät zu prüfen.

## Tests und Projektstruktur

```sh
python tools/test_host.py
pio run -e esp32s3_supermini -e simulator
# Optionaler Browser-Test mit gemockter API, kein Hardwaretest:
python -m pip install -r tests/requirements-ui.txt
python tools/test_ui.py
```

Die C++-Tests verwenden **die echten WTT2MQTT-Protokoll- und Zustandsklassen** mit dem unveränderten WTTC-Modell. Sie prüfen Telegramme, Echo, byteweise Empfangszeiten, Fehler, Start/Haltebefehl/Stopp, Kommunikationsausfall und `millis()`-Überlauf. `g++`/`clang++` erforderlich; alternativ wird unter Windows die vorhandene PlatformIO-MinGW-Toolchain gefunden. Linux zusätzlich `python tools/test_host.py --sanitize` möglich.

Ein **zweiter ESP32** kann mit `pio run -e simulator -t upload` als unveränderte WTTC-Webasto-Simulation dienen. Details und Grenzen: [Tests und Prüfplan](docs/TESTING.md). Die Browser-Simulation auf der WTTC-Website ist keine Messung eines realen Heizgeräts.

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
| `third_party/wttc/` | Originale Fehlertexte, Simulator und Herkunftsnachweise |

Die Arduino-Hauptschleife besitzt UART und Heizungszustand auf Core 1. Der Netzwerk-Task läuft auf Core 0; MQTT hat seinen asynchronen ESP-IDF-Task. Nur POD-Snapshots und eine begrenzte Queue verbinden beide Seiten. Keine Antwort-, WLAN- oder MQTT-Warteschleifen in der Steuerung; `vTaskDelay(1)` gibt lediglich CPU-Zeit ab. HTTP darf im Netzwerk-Task auf einen langsamen Client warten, ohne die Steuerung logisch zu blockieren; Flash-Schreiben beeinflusst auf ESP32 dennoch beide Cores und muss real vermessen werden.

**Nicht auf echter Webasto-/TJA1020-Hardware geprüft:** elektrische Pegel und Break-Verhalten, variantenspezifische Protokollunterstützung, reale Heizphasen, Fehlertexte im konkreten Gerät, Dauerbetrieb, WLAN/MQTT am Board sowie OTA/Reset unter realen Bedingungen. Protokollbelege sind nachvollziehbar in [Referenzanalyse](docs/REFERENCES.md) und [Protokollumfang](docs/PROTOCOL.md). Ein Modelltest beweist keine Z/C-D-Kompatibilität.
