# Tests an echter Hardware

Die Funktionsprüfung erfolgt mit ESP32-S3 SuperMini, TJA1020 und dem konkreten Webasto-Heizgerät. Ein zweiter ESP32 ist nicht vorgesehen. Bisher wurden keine Prüfungen an angeschlossener Hardware ausgeführt; die folgenden Schritte sind ein Prüfplan und keine bestätigten Ergebnisse.

## Softwareprüfungen

```sh
pio run -e esp32s3_supermini
python tools/verify_artifacts.py
python tools/package_firmware.py
```

Der Build prüft die Kompilierbarkeit. Die Binärprüfung kontrolliert Partitionstabelle, ESP32-S3-Chipkennung, 4-MB-Flashheader, OTA-Slotgröße und Herkunft der WTTC-Fehlertexte. Das bestätigt keine Kommunikation mit dem Heizgerät.

Für Oberflächenänderungen kann separat `python tools/test_ui.py` ausgeführt werden. Dieser Browsercheck verwendet eine gemockte HTTP-API und prüft nur Navigation, Darstellung und Bedienung. Er trifft keine Aussage über UART, MQTT am Board oder Heizungsfunktionen.

## Aufbau und Protokollierung

1. Nach [WIRING.md](WIRING.md) ESP32, TJA1020 und Heizgerät verbinden. Konkrete W-Bus-Steckerbelegung und Modulpegel prüfen. Nur einen W-Bus-Master anschließen.
2. Firmware per USB flashen, Serial-Monitor auf 115200 öffnen und mit `credentials` den Zugang auslesen. WLAN und MQTT konfigurieren. GPIO16 RX und GPIO17 TX anhand der Platine kontrollieren.
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
| WLAN-Ausfall | WLAN unterbrechen und wiederherstellen | W-Bus und Haltebefehle laufen weiter; AP-Fallback und Wiederverbindung |
| MQTT-Ausfall | Broker stoppen und wieder starten | W-Bus bleibt stabil; Last Will, Wiederverbindung, Discovery und Zustände |
| MQTT-Befehle | START/STOP ohne Retain senden | Annahme, Ergebnis und Zustandsrückmeldung; retained START wird abgewiesen |
| Kommunikationsausfall | W-Bus kontrolliert trennen, Versorgung beibehalten | Timeout, verfallende Messwerte, Ende der eigenen Anforderung und tatsächliches geräteseitiges Abschalten |
| Wiederkehr | Kommunikation wiederherstellen | Gültige Messwerte kehren zurück; kein automatischer neuer Heizstart |
| Neustart | Bei stillstehendem Gerät auslösen | Regulärer Stoppversuch; kein gespeicherter Heizwunsch wird fortgesetzt |
| OTA | Bei stillstehendem Gerät und frischen Messwerten passendes Image laden | Start gesperrt, gültiges Image, Neustart und Erhalt der Einstellungen |
| Upload-Abbruch | Übertragung vor Abschluss unterbrechen | Aktuelle App bleibt startfähig; keine unvollständige App aktivieren |
| Dauerbetrieb | Vollständige Zyklen und Netzunterbrechungen | Heapreserve, Antwortzeiten, Kommunikationsfehler und stabile Steuerung |

Fehler nicht durch absichtlichen Kraftstoffmangel oder Manipulation von Schutzsensoren erzeugen. Vorhandene Fehler und reguläre Diagnosedaten zum Abgleich verwenden. Fehlerlöschen und Entriegeln sind nicht freigegeben.

## Offene Punkte

Am Gerät zu bestätigen sind die varianteabhängige W-Bus-Unterstützung, Sensorlayouts, Fehlertexte, Identifikation, reale Heizphasen, Nachlaufdauer und Abschaltfrist ohne Haltebefehl. Die 180-s-Sperre ist eine konservative Annahme, kein gemessener Nachlauf.

Auch WLAN/Captive-Portal, NVS, MQTT/Home Assistant, OTA-Stromausfallverhalten und Flash-Latenzen müssen am ESP32 geprüft werden. Ein automatischer Rollback nach einem formal gültigen, aber fehlerhaften Anwendungsimage ist nicht zugesichert. Ergebnisse erst nach tatsächlich ausgeführten Versuchen als bestanden eintragen.
