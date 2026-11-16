# Tests an echter Hardware

Die Funktionsprüfung erfolgt mit ESP32-S3 SuperMini, TJA1020 und dem konkreten Webasto-Heizgerät. Ein zweiter ESP32 ist nicht vorgesehen. Bisher wurden keine Prüfungen an angeschlossener Hardware ausgeführt; die folgenden Schritte sind ein Prüfplan und keine bestätigten Ergebnisse.

## Softwareprüfungen

```sh
pio run -e esp32s3_supermini
python tools/verify_artifacts.py
python tools/test_setup_access.py
python tools/package_firmware.py
```

Der Build prüft die Kompilierbarkeit. Die Binärprüfung kontrolliert Partitionstabelle, ESP32-S3-Chipkennung, 4-MB-Flashheader, OTA-Slotgröße und Herkunft der WTTC-Fehlertexte. Das bestätigt keine Kommunikation mit dem Heizgerät.

Die 51 Prüfungen in `tools/test_setup_access.py` verwenden die tatsächliche AP-Zugriffsregel der Firmware und prüfen erlaubte WLAN-Routen sowie die Ablehnung anderer APIs, Assets und Konfigurationsfelder. Es wird kein Heizgerät emuliert. Die Regeln werden vor Verarbeitung der HTTP-Anfragen und separat vor Beginn eines OTA-Uploads angewendet.

Derselbe Aufruf prüft die tatsächlichen W-Bus-/Heater-Klassen mit einer nicht angeschlossenen Schnittstelle: OTA und Neustart sind sofort nach Boot, bei fehlendem Echo, ohne Messwerte und während des angenommenen Nachlaufs erlaubt. Ein W-Bus-Heizgerätemodell wird dabei nicht verwendet. Im Browser wird zusätzlich die Auswahl und Übertragung einer `.bin.ota`-Datei bei fehlenden Heizgerätedaten geprüft; die HTTP-API bleibt bei diesem Browsercheck gemockt.

Zusätzlich laufen 30 UART-Prüfungen mit expliziten Bytefolgen: kein Echo, unvollständiges Echo, vollständiges Echo ohne Antwort, abweichendes Echo mit Aufnahme restlicher Bytes und Sperre eines zweiten Requests bis zum Fensterende sowie eine prüfsummengültige Antwort. Getestet wird der echte Transportcode ohne Heizgerätemodell. Ergebnisfelder für Echo, Rohbyteanzahl und Laufzeit werden ebenfalls geprüft.

Die RX-Timing-Auswertung wird mit Datenimpulsen, langen Low-Phasen, statischem High/Low, Zeitstempelüberlauf und der festen Speicher-/Flankenbegrenzung geprüft. Diese Tests prüfen die Auswertungslogik; die GPIO-Interrupt-Erfassung selbst muss auf dem ESP32 bestätigt werden.

Für Oberflächenänderungen kann separat `python tools/test_ui.py` ausgeführt werden. Dieser Browsercheck verwendet eine gemockte HTTP-API und prüft nur Navigation, Darstellung und Bedienung, einschließlich der separaten WLAN-Seite bei 390/1280 Pixeln. Er trifft keine Aussage über UART, MQTT am Board oder Heizungsfunktionen.

## Aufbau und Protokollierung

1. Nach [WIRING.md](WIRING.md) ESP32, TJA1020 und Heizgerät verbinden. Konkrete W-Bus-Steckerbelegung und Modulpegel prüfen. Nur einen W-Bus-Master anschließen.
2. Firmware per USB flashen, mit dem offenen Setup-AP verbinden und dort ausschließlich WLAN konfigurieren. Nach automatischer Verbindung im normalen WLAN MQTT und bei Bedarf den optionalen Webzugang konfigurieren. GPIO16 RX und GPIO17 TX anhand der Platine kontrollieren.
3. Heizgerätetyp, Teilenummer, ECU-Ausführung, Platine, Transceivermodul, Firmwarestand und Versorgungsspannung protokollieren. Zeitstempel und TX-/RX-Log zu jedem Versuch sichern.
4. Vor Brennerstart Kühlmittelkreislauf, Kraftstoffversorgung und Abgasführung betriebsbereit herstellen. Brennerprüfungen im Freien durchführen; Heizgeräteversorgung für den regulären Nachlauf angeschlossen lassen.

## Prüfablauf

| Schritt | Durchführung | Zu prüfen und festzuhalten |
|---|---|---|
| Firmwarestart | ESP32 einschalten, keinen Heizstart auslösen | Regulärer Stoppversuch, kein automatischer Heizstart, initiale Startsperre |
| UART und Wakeup | TX/RX und LIN beobachten | 2400 Baud, 8E1, Low-/High-Wakeup, identisches Echo, Antwortadresse und XOR |
| Messwerte | `50 05` lesen | Rohtelegramm, Temperatur gegen Referenz, Spannung gegen Multimeter; optionale Felder nicht voraussetzen |
| Identifikation | Diagnose lesen | Antworten auf `51 0B`, `51 05`, `51 0A`, `50 06`; nicht unterstützte Register bleiben unbekannt |
| Fehlerspeicher | `56 01` lesen | Rohcodes, Anzahl und Häufigkeiten gegen vorhandene Diagnosedaten; Historie nicht als aktiven Fehler ausgeben |
| Start | Nach Ende der Startsperre definierte Laufzeit anfordern | Start-ACK getrennt von tatsächlicher Flamme; Temperatur- und Leistungsverlauf |
| Haltebefehl | Während des Betriebs aufzeichnen | `44 21 00` ungefähr alle 5 s und gültige Antworten |
| Regulärer Stopp | Web-Stopp betätigen | `10`, Antwort, Flamme erlischt, vollständiger Pumpen-/Gebläsenachlauf; tatsächliche Dauer messen |
| Erneuter Start | Während angenommener Nachlaufphase anfordern | Ablehnung ohne zusätzliches Starttelegramm |
| Laufzeitende | Eingestellte Laufzeit abwarten | Eigene Anforderung endet, regulärer Stopp, Nachlauf möglich |
| WLAN-Ausfall | WLAN unterbrechen und wiederherstellen | W-Bus und Haltebefehle laufen weiter; nach 30 s nur WLAN-Konfiguration im AP; automatische Wiederverbindung und AP-Abschaltung |
| AP-Zugriff | Im AP direkt Dashboard-/Konfigurations-/Steuerungs-/OTA-URLs anfragen | Nur WLAN-Seite, Netzsuche und WLAN-API erreichbar; verbotene APIs liefern 403, OTA schreibt nichts |
| WLAN-Korrektur | Im AP falsche und anschließend richtige WLAN-Zugangsdaten speichern | Ohne Neustart übernehmen; bei Fehler bleibt der AP, bei Verbindung schließt er |
| MQTT-Ausfall | Broker stoppen und wieder starten | W-Bus bleibt stabil; Last Will, Wiederverbindung, Discovery und Zustände |
| MQTT-Befehle | START/STOP ohne Retain senden | Annahme, Ergebnis und Zustandsrückmeldung; retained START wird abgewiesen |
| Kommunikationsausfall | W-Bus kontrolliert trennen, Versorgung beibehalten | Timeout, verfallende Messwerte, Ende der eigenen Anforderung und tatsächliches geräteseitiges Abschalten |
| Wiederkehr | Kommunikation wiederherstellen | Gültige Messwerte kehren zurück; kein automatischer neuer Heizstart |
| Neustart | Bei stillstehendem Gerät auslösen | Regulärer Stoppversuch; kein gespeicherter Heizwunsch wird fortgesetzt |
| OTA ohne Heizgerät | ESP32 im normalen WLAN ohne W-Bus-Verbindung betreiben, `.bin.ota` laden | Upload ohne Messwerte und ohne initiale Wartezeit möglich; gültiges Image, Neustart und Erhalt der Einstellungen |
| OTA bei eigener Heizanforderung | Eigene Anforderung stoppen, anschließend Update ausführen | Laufende Anforderung verhindert Wartung, aber keine Messwert-/Nachlaufvoraussetzung; neue Starts während Upload gesperrt |
| Upload-Abbruch | Übertragung vor Abschluss unterbrechen | Aktuelle App bleibt startfähig; keine unvollständige App aktivieren |
| Dauerbetrieb | Vollständige Zyklen und Netzunterbrechungen | Heapreserve, Antwortzeiten, Kommunikationsfehler und stabile Steuerung |

Fehler nicht durch absichtlichen Kraftstoffmangel oder Manipulation von Schutzsensoren erzeugen. Vorhandene Fehler und reguläre Diagnosedaten zum Abgleich verwenden. Fehlerlöschen und Entriegeln sind nicht freigegeben.

## Offene Punkte

Zum gemeldeten HTTP-Watchdog auf CPU 0 siehe [Analyse und Korrektur](TROUBLESHOOTING.md). Die Wiederholungsprüfung dieses Fehlers ist Teil der realen Hardwareprüfung.

Am Gerät zu bestätigen sind die varianteabhängige W-Bus-Unterstützung, Sensorlayouts, Fehlertexte, Identifikation, reale Heizphasen, Nachlaufdauer und Abschaltfrist ohne Haltebefehl. Die 180-s-Sperre ist eine konservative Annahme, kein gemessener Nachlauf.

Auch WLAN/Captive-Portal, NVS, MQTT/Home Assistant, OTA-Stromausfallverhalten und Flash-Latenzen müssen am ESP32 geprüft werden. Ein automatischer Rollback nach einem formal gültigen, aber fehlerhaften Anwendungsimage ist nicht zugesichert. Ergebnisse erst nach tatsächlich ausgeführten Versuchen als bestanden eintragen.
