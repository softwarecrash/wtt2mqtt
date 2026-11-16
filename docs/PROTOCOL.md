# W-Bus-Implementierungsumfang

Quelle: WTTC-Commit `adbbf985a46c425c476f0dd9db3e8422f0bc7ea1`, `firmware/WTTC/WTTC.ino` (W-Bus, readSensors, runCheck), `logica.h` (wbBuild/wbCheck/wbParse*).

## Transport

- UART 2400 Baud, 8 Datenbits, gerade Parität, 1 Stoppbit. USB ist eine separate Schnittstelle.
- Anfrage: `F4 LL CMD DATA... XOR`. Antwort: `4F LL (CMD|80) DATA... XOR`.
- `LL` zählt Befehl, Nutzdaten und Prüfsumme. Gesamtlänge `LL+2`. XOR über sämtliche Bytes ergibt 0.
- Beispiel Sensoranfrage: `F4 03 50 05 A2`.
- `F4` ist die von WTTC eingesetzte ThermoTest-Adressierung. Keine automatische Suche nach alternativen Adressen.
- Vor erster Übertragung oder nach mindestens 10 s Transportpause: TX 50 ms Low, dann 50 ms High, UART wieder anschließen. Beide Phasen erfolgen zeitgesteuert ohne `delay`.
- Alle gesendeten Bytes müssen als identisches Echo zurückkommen. Danach wird nach Antwortadresse `4F` gesucht. Kein stilles Übergehen eines fehlenden oder abweichenden Echos.
- 750 ms Gesamt-Empfangsfenster ab Senden; 64 Byte maximale Framegröße. Bei 2400/8E1 benötigen 64 Byte ungefähr 293 ms, der aktuelle kurze Request plus Echo und Antwort passen in dieses Fenster. Keine unbegrenzten Empfangspuffer.
- Fehler: `no_bus_echo`, `echo_mismatch`, `response_timeout`, `invalid_length`, `bad_checksum`, `unexpected_command`, `invalid_payload`.

## Freigegebene Befehle

| Anfrage | Interpretation | Implementierung und Nachweis |
|---|---|---|
| `21 mm` | Parkheizen für `mm` Minuten | ACK `mm`, 1–120 Minuten. WTTC-Quellcode; Hardwareprüfung ausstehend |
| `44 21 00` | Aktive Parkheizanforderung halten | alle 5 s, Antwort `00` aktiv / `01` nicht aktiv; WTTC-Quellcode; Hardwareprüfung ausstehend |
| `10` | Regulärer Stopp | ACK `00`, bis zu 3 Transportversuche; WTTC-Quellcode; Hardwareprüfung ausstehend |
| `50 05` | Sensoren | Mindestantwort 4 Bytes inkl. Subindex; WTTC-Quellcode; Hardwareprüfung ausstehend |
| `56 01` | Gespeicherte Fehler | strikte Anzahl-/Längenprüfung; WTTC-Quellcode; Hardwareprüfung ausstehend |
| `51 0B` | Gerätename | druckbare ASCII-Zeichen; WTTC runCheck; Hardwareprüfung ausstehend |
| `51 05` | Fertigungsdatum Heizgerät | Tag, Monat, zweistelliges Jahr; WTTC runCheck; Hardwareprüfung ausstehend |
| `51 0A` | W-Bus-Version | High-/Low-Nibble; WTTC runCheck; Hardwareprüfung ausstehend |
| `50 06` | Zähler | Arbeitsstunden/-minuten, Betriebsstunden/-minuten, Starts; WTTC runCheck; Hardwareprüfung ausstehend |

Sensorantwort ab Nutzdatenbyte 0: `05`, Temperatur+50, Spannung mV Big Endian (2), optional Flamme, Leistung W Big Endian (2), Glühstiftwiderstand mΩ Big Endian (2). Fehlende optionale Felder werden bei jeder neuen Antwort wieder unbekannt. Keine frei erfundene Brennerleistung aus Temperatur oder Laufzeit.

Fehlerliste: `01 NN [Code Anzahl]×NN`. Es passen maximal 29 Einträge in die 60 Nutzdatenbytes des Antwortpuffers. Unvollständige Listen sind Fehler und werden nicht als leer dargestellt. Unbekannte Fehler behalten ihren Hexcode; deutsche Texte stammen aus WTTC `averias.h`. Historische Verriegelungscodes werden nicht als sicher aktive Verriegelung ausgegeben.

Name/Datum/W-Bus-Version sind **keine verifizierte Firmwareversion der ECU**. Unbeantwortete Abfragen bleiben leer bzw. `null`. Identifikation wird auf Anforderung abgefragt, Sensoren alle 2 s, Fehlerliste alle 60 s oder am Ende einer Diagnose. Haltebefehle und Stopp haben vor Diagnose Vorrang. Einzelne optionale Register dürfen fehlschlagen, ohne aktuelle Sensorantworten zu ersetzen.

## Zustände und Fail-safe

Eigene Heizanforderung, beobachtete Flamme und angenommene Phase sind getrennte Größen. Ein Start-ACK wird nur als ACK ausgewertet. Bei verlorenem Start-ACK folgen reguläre Stoppversuche; kein automatisches erneutes Starten. Bei 15 s ohne gültige Antwort wird die Verbindung offline, Sensoren verfallen separat 15 s nach dem letzten Sensorpaket. Nach 25 s ohne gültige Kommunikation wird die eigene Heizanforderung verworfen und regulärer Stopp versucht. Ohne weiteren Haltebefehl muss das Heizgerät selbst seine protokollseitige Abschaltung ausführen; das tatsächliche Verhalten und die Frist sind am konkreten Heizgerät zu prüfen.

Stopp, Laufzeitende und Neustart führen zu einer **angenommenen 180-s-Nachlaufphase**. Keine elektrische Spannungsabschaltung: Wasserpumpe/Gebläse müssen ihren regulären Nachlauf ausführen können. Die tatsächlichen Detailphasen Gebläse, Glühstift, Zündung, Stabilisierung, Voll-/Teillast und Regelpause werden nicht als direkt gelesene ECU-Zustände behauptet. Nach Ende der angenommenen Sperre kann `no_flame_observed` weiterhin keine sichere Aussage über ein extern betriebenes Gerät liefern. Nur einen Master verwenden.

## Analysiert, aber nicht freigegeben

WTTC unterstützt zusätzlich `56 02 <code>` für Fehlerdetails und `56 03` zum Löschen/Entriegeln. Diese Befehle sind hier nicht exponiert. Keine freie Diagnose-Schreibkonsole, Codierung, Aktortests, Kraftstoffvorförderung, Ventilationsmodi oder unbekannten Register. Für vollständige Heizphasencodes liegt hier kein am realen Gerät bestätigter Statusbefehl vor.

Die Baudrate ist im Quellcode nachgewiesen. Ob eine konkrete Thermo Top Z/C-D-Platine diese W-Bus-Befehle direkt annimmt, ist ohne deren Hardware nicht nachgewiesen. Manche fahrzeugabhängigen Varianten setzen andere Ansteuerung voraus. CAN/Gateway wird nicht implementiert.
