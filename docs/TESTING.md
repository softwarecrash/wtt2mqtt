# Tests, Prüfstand und offene Hardwarevalidierung

Stand: 2026-11-16. Erfolgreiche Tests sind hier ausdrücklich nach ihrer Aussagekraft getrennt.

## Ausgeführt

1. **PlatformIO-Releasebuild** `pio run -e esp32s3_supermini`: 4-MB-Flashheader, feste Dual-OTA-Tabelle, keine PSRAM-Voraussetzung. Ergebnis und genaue Binärgröße zusätzlich unter `build/release/manifest.json` nach Paketierung.
2. **Simulatorbuild** `pio run -e simulator`: Original-WTTC-Sketch für einen zweiten ESP32-S3, gleicher 4-MB-Partitionsplan. Ursprünglich fehlende LDF-Erkennung von `Preferences.h` durch explizites Include im Wrapper behoben. Originaldatei bleibt unverändert. Der unveränderte Sketch wurde erfolgreich auch mit dem hier gepinnten Arduino-2.0.17-Kern gebaut; upstream beschreibt Arduino 3.x.
3. **Host-C++-Tests** `python tools/test_host.py`: **1.553 erfolgreiche Assertions** mit `-Wall -Wextra -Werror -pedantic`. Echte neue `WBus.cpp`/`Heater.cpp` gegen unverändertes `webasto_modelo.h`; keine bloße Nachbildung der neuen Steuerung im Test.
4. **Browser** `python tools/test_ui.py`: Microsoft Edge/Chromium, 1280 und 390 px. Alle sechs Seiten, keine horizontale Überbreite, Start/Diagnose, Speichern mit Passwortbeibehaltung, WLAN-Scan, Fehlertexte, fehlende ID-Daten und gesperrte Start-/OTA-Buttons bei veralteten Daten. Keine JavaScript-Laufzeitfehler. HTTP-API im Test **gemockt**, kein ESP32-HTTP-Server ausgeführt. Desktop-/Mobil-Screenshots unter `build/ui/` wurden visuell geprüft.
5. **Binärprüfung** `python tools/verify_artifacts.py`: tatsächliche Partitionstabelle, ESP32-S3-Chipkennung und 4-MB-Header von Bootloader/App, Slotgröße und bytegleiche WTTC-Dateien geprüft. Es gibt zwei App-Partitionen plus notwendige NVS-/OTA-Metadaten, keine Dateisystempartition.

Der Hosttest umfasst: Golden Frame `F4 03 50 05 A2`, XOR/Adress-/Befehlsprüfung, zu kleine Puffer, unvollständige Antwort, optionale Messwerte, alle 256 Längenbytes, simulierte byteweise 2400-Baud-Empfangszeiten, fehlendes Echo, falsches Echo, fehlende Antwort, falscher Befehl, fehlerhafte Prüfsumme, Initial-Break, Startverweigerung mit unbekanntem Zustand, Startup-Stopp, 5-s-Haltebefehl, beobachtete Flamme, regulärer Stopp, Nachlaufannahme, begrenzte Laufzeit, historische Fehler mit Häufigkeiten, beschädigte Fehlerliste, unbeantwortete Identifikationsregister, Kommunikationsverlust, Wiederkehr, fehlende optionale Sensorwerte, kein Kraftstoff, Stopp nach unbestätigtem Start, `millis()`-Überlauf, doppelter Start während ausstehendem ACK und Stopp während ausstehendem Start-ACK.

Tests für W-Bus-Identifikation/Zähler bestätigen hier ausschließlich das saubere Verhalten bei **Nichtunterstützung**. Es werden keine Antworten für diese Befehle in das Originalmodell hineinerfunden. Die Parser richten sich nach WTTCs Leseimplementierung; reale Erfolgsantworten müssen noch aufgezeichnet werden.

## Reproduzieren

```sh
python tools/test_host.py
pio run -e esp32s3_supermini -e simulator
python -m pip install -r tests/requirements-ui.txt
# Auf Linux oder ohne vorhandenes Edge:
python -m playwright install chromium
python tools/test_ui.py
python tools/package_firmware.py
```

Auf Linux/GCC zusätzlich `python tools/test_host.py --sanitize` für ASan/UBSan. Dieser Sanitizerlauf ist in CI vorgesehen; der lokale Windows-Lauf verwendet die vorhandene MinGW-Toolchain ohne Sanitizer. GitHub Actions ist eingerichtet, wurde in dieser Sitzung aber nicht remote ausgelöst.

## Zweiter ESP32: Testablauf

1. `pio run -e simulator -t upload --upload-port <SIMULATOR_PORT>` auf einen separaten ESP32-S3 flashen. Nicht auf den späteren Controller verwechseln. Für andere ESP32-Varianten ein eigenes Boardprofil verwenden.
2. Direktverkabelung ohne Transceiver wie in [WIRING.md](WIRING.md). Simulator-USB auf 115200 Baud, `eco si`, `pines 16 17`. Bei physischem LIN mit zwei Transceivern `eco no`.
3. Controller starten, Zugang per `credentials` abfragen und initiale 180-s-Sperre abwarten. Diagnose lesen: Sensoren und Fehlerliste sollen gültig werden, Geräte-ID/Zähler bleiben nicht verfügbar.
4. Start 10 Minuten: Simulator `estado`/`resumen`, Telegrammfolge `21`, `50 05` und `44 21 00` prüfen. Nach ungefähr 90 s Modellzeit erscheint Flamme, nach weiterer Stabilisierung Leistung.
5. Stopp: Simulator wechselt auf `postbarrido`, Controller auf angenommene Nachlaufphase. Sofortiger erneuter Start wird abgelehnt, Versorgung bleibt an.
6. `fallo mudo`: Echo bleibt in Direktverkabelung vorhanden, Antwort fehlt. Controller meldet `response_timeout`, Messwerte verfallen, eigene Anforderung endet. `fallo ninguno`: Messwerte sollen wiederkommen; kein automatischer Neustart der Heizung.
7. Transceiver/USB-UART-Echo unterbrechen: `no_bus_echo` oder `echo_mismatch` statt beliebiger scheinbarer Messwerte.
8. `fallo gasoil`: Modellstart scheitert, Fehler `02` erscheint beim nächsten Lesen. `fallo llama` während Verbrennung: Fehler `03`. Historische Fehler werden nicht automatisch gelöscht. Für erneute Modelltests darf an der **Simulator-Konsole** `borrar` ausgeführt werden; WTT2MQTT sendet keinen Fehlerlöschbefehl.
9. WLAN-AP abschalten/Broker stoppen: W-Bus-TX/RX und 5-s-Haltebefehle sollen weiterlaufen. Broker wieder einschalten, Availability/Discovery/Status prüfen; retained `START` muss abgewiesen werden.
10. Bei stillstehendem Modell und frischen Sensoren OTA-Datei übertragen; Start während Update muss abgewiesen werden. Unterbrochener Upload darf keine neue aktive App wählen.

Diese Zwei-Board-Prüfschritte sind ein **noch auszuführender Prüfplan**, kein Bericht einer in dieser Sitzung angeschlossenen Hardware. `rapido` beschleunigt Modellphasen, aber auch dessen Keepalive-Watchdog: für UART-End-to-End-Tests zunächst `rapido 1` verwenden, sonst kann ein realer 5-s-Haltebefehl im beschleunigten Modell zu spät kommen.

## Noch offen

| Prüfung | Status |
|---|---|
| Verdrahtung/Pegel, Transceiver-Echo, Break auf echter Leitung | nicht ausgeführt |
| W-Bus-Fähigkeit und Adressierung konkreter Z/C-D-ECU | nicht ausgeführt |
| Sensorlayouts/Skalierung, Fehlertexte und Identifikation am echten Heizgerät | nicht ausgeführt |
| Reale Phasen, Nachlaufdauer, Heizgeräte-Watchdog | nicht ausgeführt |
| WLAN-Captive-Portal, NVS-Recovery, mDNS, MQTT/HA am ESP32 | nicht ausgeführt |
| Langzeitbetrieb und Task-/Heapreserve unter Netzfehlern | nicht ausgeführt |
| OTA-Stromausfall, Imageablehnung, Flash-Stalls und Wiederherstellung am ESP32 | nicht ausgeführt |
| Anwendungs-Rollback nach defektem, aber formal gültigem OTA-Image | nicht zugesichert / nicht implementiert |

Das Protokollmodell ist eine Näherung und darf keinen echten Heizungsversuch als bestanden ersetzen.
