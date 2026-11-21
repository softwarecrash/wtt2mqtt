# MQTT und Home Assistant

Broker, Port, Benutzer, Passwort, Basis-Topic, Veröffentlichungsintervall, Startdauer, Discovery und JSON-Modus sind konfigurierbar. Leerer Broker deaktiviert MQTT. MQTT 3.1.1 über TCP, kein TLS in Version 0.1.0. ESP-IDF-MQTT arbeitet asynchron mit Wiederverbindung alle 5 s. Standardintervall 10 s, wählbar 2–3600 s; W-Bus-Sensorpolling bleibt unabhängig davon bei 2 s.

Für mehrere Geräte **eindeutiges Basis-Topic** setzen, beispielsweise `wtt2mqtt/garage`. Die Client-/Discovery-ID verwendet die ESP32-MAC. Discovery-Änderungen wirken nach Neustart; bei Änderung des Basis-Topics/Brokers alte retained Discovery-/State-Einträge zuvor am bisherigen Broker entfernen.

## Topics

`<base>` ist standardmäßig `wtt2mqtt`. Veröffentlichungen sind QoS 1 und retained. Befehle **niemals retained** senden. Retained-Befehle werden beim Empfang abgewiesen, fragmentierte/zu lange Nutzdaten ebenfalls. QoS 1 kann prinzipbedingt Duplikate liefern; die Zustandsverwaltung weist erneute Starts bei laufender oder gerade startender Anforderung ab. Es gibt kein automatisches Starten nach Wiederverbindung.

| Topic | Payload | Bedeutung |
|---|---|---|
| `<base>/command` | exakt `START` oder `STOP` | Start mit eingestellter Dauer bzw. regulärer Stopp |
| `<base>/command_result` | `queued_start`, `queued_stop` oder `rejected_...` | Annahme in lokale Queue, kein Heizgeräte-ACK |
| `<base>/availability` | `online` / `offline` | ESP/MQTT-Verfügbarkeit; Last Will `offline`, QoS 1 retained |
| `<base>/heater_availability` | `online` / `offline` | gültige Kommunikation und frische Sensordaten |
| `<base>/state/temperature` | Zahl °C / `unknown` | Kühlmittel |
| `<base>/state/voltage` | Zahl V / `unknown` | Versorgung |
| `<base>/state/flame` | `true` / `false` / `unknown` | gemeldete Flamme |
| `<base>/state/power` | Zahl W / `unknown` | gemeldete Heizleistung |
| `<base>/state/glow_milliohm` | Zahl mΩ / `unknown` | optionaler Glühstiftwiderstand |
| `<base>/state/requested` | `true` / `false` | eigene bestätigte Heizanforderung; kein Flammennachweis |
| `<base>/state/phase` | siehe unten | beobachtete/abgeleitete Phase |
| `<base>/state/online` | `true` / `false` | gültige Gerätekommunikation |
| `<base>/state/communication_error` | `ok` / Fehlertext | Ergebnis der zuletzt bearbeiteten Transaktion |
| `<base>/state/fault_count` | Zahl / `unknown` | Anzahl gespeicherter Fehler |
| `<base>/state/faults` | JSON-Array | `code`, `count`, `text`; nur zusammen mit `fault_count`/Alter interpretieren |
| `<base>/state/psa_temperature_c` | Zahl °C / `unknown` | PSA DID `C0`, WTT-Thermistor-Lookup |
| `<base>/state/psa_supply_voltage_v` | Zahl V / `unknown` | PSA DID `C1`, WTT-Skalierung |
| `<base>/state/psa_online` | `true` / `false` | PSA-KWP-Antwort innerhalb der letzten 15 s |
| `<base>/state/psa_fault_count` | Zahl / `unknown` | PSA-Fehlerdatensätze aus `17 FF 00` |
| `<base>/state/psa_faults` | JSON-Array | `code` als 16-Bit-Rohcode, optional `webasto_code`/`text` für WTT-zugeordnete Klassen plus `raw_data` (drei nicht zugeordnete Zusatzbytes) |
| `<base>/state/psa_raw_signals` | JSON-Array | DIDs und rohe Antwortbytes; unbekannte Felder absichtlich nicht umbenannt |
| `<base>/state/psa_measurement_age_seconds` | Zahl / `-1` | Alter der letzten C0/C1-Messung |
| `<base>/psa_availability` | `online` / `offline` | letzte gültige PSA-Antwort |
| `<base>/state/remaining_seconds` | Zahl | verbleibende Zeit der eigenen Anforderung |
| `<base>/state/command_result` | Ergebnistext | tatsächlicher Ausgang der lokalen Steuerungsverarbeitung |
| `<base>/status` | JSON-Objekt | optionales vollständiges Snapshot, unabhängig von Einzel-Topics |

JSON enthält zusätzlich Geräteinformationen, Zähler, Datenalter, Kommunikationszähler und Firmwaredaten. Fehlende Messwerte sind `null`. Fehlerlisten sind gespeicherte Historie; eine leere Liste bei `fault_count: null` bedeutet **nicht**, dass keine Fehler vorhanden sind. Identifikation und Zähler sind auf Anforderung über die Diagnose verfügbar, nicht beim Start erfunden.

Phasen: `unknown`, `combustion_observed`, `cooldown_estimated`, `start_or_control_pause_inferred`, `no_flame_observed`. Unbekannte Detailphasen werden nicht geraten. Kommunikationsfehler: `response_timeout`, `no_bus_echo`, `echo_mismatch`, `bad_checksum`, `invalid_length`, `unexpected_command`, `invalid_payload`.

Die Veröffentlichungen erfolgen beim MQTT-Verbindungsaufbau und dann im gewählten Intervall. Ein Ausfall kann deshalb erst mit dem nächsten Statusintervall sichtbar werden; zusätzlich greift das MQTT-Last-Will nach brokerseitiger Erkennung des Verbindungsverlusts (Keepalive 20 s). UI-Snapshots werden alle 2 s abgefragt. Die MQTT-Outbox ist begrenzt; bei einem zu langsamen Broker werden neue Snapshot-Veröffentlichungen übersprungen, statt Speicher unbegrenzt zu füllen.

## Beispiele

```sh
mosquitto_sub -h BROKER -u USER -P PASSWORD -t 'wtt2mqtt/#' -v
mosquitto_pub -h BROKER -u USER -P PASSWORD -t wtt2mqtt/command -m START -q 1
mosquitto_pub -h BROKER -u USER -P PASSWORD -t wtt2mqtt/command -m STOP -q 1
```

Kein `-r` bei Befehlen. Bereits retained vorhandene Befehle entfernen:

```sh
mosquitto_pub -h BROKER -t wtt2mqtt/command -n -r
```

## Home Assistant

Discovery unter `homeassistant/<domain>/<MAC-ID>/<key>/config` legt ein gemeinsames Gerät an:

- Sensoren für Temperatur, Spannung, Heizleistung, Glühstiftwiderstand, Phase, Kommunikationsdiagnose und gespeicherte Fehleranzahl.
- Binary-Sensor für beobachtete Flamme.
- Schalter **Heizanforderung** mit `START`/`STOP`, Zustandsrückmeldung und `retain: false` für Befehle.

Der Schalter ist kein Raumthermostat: WTT2MQTT implementiert keine Temperatursollwertregelung. Alle Entities berücksichtigen sowohl ESP-Availability als auch Heater-Availability. Optionale Messwerte bleiben bei fehlendem Register unbekannt. Discovery wird nach Verbindung und bei `homeassistant/status = online` erneut gesendet. Abschalten von Discovery und Neustart am selben Broker/Basis-Topic löscht die eigenen Konfigurationen durch leere retained Payloads.

MQTT-Login allein ersetzt keine Broker-Zugriffsregeln. Für eine externe Automatisierung im lokalen Broker nur die vorgesehenen Topics freigeben.
