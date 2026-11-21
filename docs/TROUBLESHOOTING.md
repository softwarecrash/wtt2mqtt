# Watchdog beim Aufruf der Weboberfläche

RX-Pull-up: Die Firmware aktiviert jetzt am konfigurierten RX-GPIO den internen ESP32-Pull-up. Er bleibt während UART-Betrieb, Wakeup-Phasen und Protokollwechseln aktiv; nach jeder UART-Initialisierung wird er erneut gesetzt. Im Startlog steht `RX internal pull-up enabled`. Dies definiert HIGH bei offenem Open-Drain-Rückkanal, kann aber einen aktiv nach LOW gezogenen Eingang nicht übersteuern. Die Wirksamkeit am konkreten Modul ist noch am Gerät zu prüfen; der interne Pull-up ersetzt keinen gegebenenfalls erforderlichen externen Pull-up für zuverlässige elektrische Flanken.

Der gemeldete Absturz mit ELF-Kennung `96d03d3f930f2887` wurde anhand des passenden ELF dekodiert:

```text
networkTask → Network::tick → WebServer::handleClient
→ WebServer::_parseRequest → Stream::readStringUntil
→ Stream::timedRead → WiFiClient::read
```

Arduino-ESP32 2.0.17 wartet in `Stream::timedRead()` aktiv auf weitere Zeichen, ohne den Task zu blockieren. Bei unvollständigen oder langsam eintreffenden HTTP-Anfragen kann dies länger dauern. Der Netzwerk-Task lief mit Priorität 1 auf CPU 0 und konnte dadurch den Idle-Task mit Priorität 0 verdrängen. Der Watchdog meldete entsprechend `IDLE0`, während `network` auf CPU 0 lief.

Die Korrektur setzt den Netzwerk-Task auf `tskIDLE_PRIORITY`. Das im verwendeten FreeRTOS aktivierte präemptive Time-Slicing lässt damit auch IDLE0 während einer aktiven HTTP-Warteschleife laufen. Zusätzlich ist die Pause zwischen Netzwerkdurchläufen auf mindestens einen Tick begrenzt. Der Watchdog wird weder abgeschaltet noch verlängert; W-Bus-Task, UART, Flashaufteilung und gespeicherte Einstellungen bleiben unverändert. Der synchrone HTTP-Parser kann weiterhin bis zu seinem Timeout warten; die Änderung verhindert dessen Verdrängung von IDLE0.

Die Meldungen `No core dump partition found` nach dem Abort beziehen sich auf den fehlenden Flash-Core-Dump-Bereich im vorgesehenen Dual-OTA-Layout. Sie sind nicht die Ursache dieses Watchdog-Absturzes. Wegen dieser Meldungen weder NVS löschen noch den Flash neu partitionieren.

## Prüfung am ESP32

Die aktualisierte Firmware per normalem PlatformIO-Upload oder als OTA-App-Image übertragen. Das USB-Gesamtimage setzt Einstellungen zurück und ist für diese Korrektur nicht erforderlich.

```sh
pio run -e esp32s3_supermini -t upload --upload-port COM7
pio device monitor --port COM7 --baud 115200
```

Nach dem Start muss `Network worker: idle priority (HTTP watchdog fix)` erscheinen. Im offenen AP die separate WLAN-Seite mehrfach öffnen; im normalen WLAN zusätzlich Navigation und Statusaktualisierung der vollständigen Oberfläche prüfen. Browserverbindungen unterbrechen und länger als die zuvor beobachteten 90 Sekunden testen; auch langsame bzw. abgebrochene HTTP-Anfragen berücksichtigen. Es darf kein `IDLE0`-Watchdog mehr auftreten. W-Bus-Telegramme müssen unabhängig davon weiter verarbeitet werden.

Der Fix wurde kompiliert und die Ursache am passenden ELF nachvollzogen. Die Wiederholungsprüfung auf dem betroffenen ESP32 steht noch aus.

## Nur W-Bus-TX, keine Antwort

Ein `W-Bus TX ...` zeigt nur den Sendeversuch des ESP32. `Command 2 accepted` bedeutet, dass der ESP32 den lokalen Diagnoseauftrag angenommen hat; es ist kein ACK der Webasto.

Die aktuelle Firmware protokolliert am Start die tatsächlich gespeicherten RX-/TX-GPIOs und führt zuerst den konfigurierten Protokollscan aus. Nach jeder normalen W-Bus-Anfrage folgt ein `RESULT`, beispielsweise:

```text
W-Bus RESULT cmd=50 no_bus_echo echo=0/5 received=0 elapsed=750 ms
W-Bus RESULT cmd=50 response_timeout echo=5/5 received=5 elapsed=750 ms
W-Bus ECHO mismatch index=0 expected=F4 received=00
```

- `echo=0/5 received=0`: kein Rücklauf. Zuerst ESP-GPIOs, tatsächliche TXD-/RXD-Signale, Transceiver-Versorgung, NSLP und Pull-up prüfen. Ein passendes Bus-Echo ist unabhängig davon, ob die ECU selbst antwortet.
- `echo=5/5` mit `response_timeout`: der vollständige eigene Request kommt zurück, aber keine akzeptierte Antwort. Heizgeräteversorgung, gemeinsame Masse, tatsächlichen Diagnoseanschluss und Steuergerätevariante anhand der Teilenummer prüfen.
- `echo_mismatch`: Rohbytes und Index auswerten; nicht durch Ignorieren des Echos einen funktionierenden Bus vortäuschen. Möglich sind Pegel-/Leitungsprobleme, Störungen oder Restbytes. Der Empfänger nimmt jetzt zunächst das ganze Transaktionsfenster auf, bevor erneut gesendet wird.

Zusätzlich zeigt `W-Bus UART since last result` die hardwareseitig gemeldeten Paritäts-, Rahmen-, Break-, FIFO- und Pufferfehler seit dem vorherigen Ergebnis. `RX=HIGH/LOW` ist nur der digitale Pegel beim Protokollieren, keine Spannungs- oder Flankenmessung. Der UART-Callback zählt ausschließlich Ereignisse; er verändert weder Empfangsformat noch Baudrate und führt keine Logausgabe im Treiber-Task aus. Fehler können auf falsches Empfangsformat oder gestörte Signale hindeuten, beweisen aber allein keine andere ECU-Baudrate.

`W-Bus RX_TIMING` ergänzt eine begrenzte digitale Flankenmessung am eingestellten ESP-RX-GPIO während des Empfangsfensters: `edges` zählt bis zu 256 Übergänge, `low_max_us` die längste beobachtete Low-Phase und `samples_us` die ersten zwölf High-/Low-Abschnitte in Mikrosekunden. Bei 2400 Baud dauert ein Bit ungefähr 417 µs. Eine lange Low-Phase statt der erwarteten wechselnden Datenbits erklärt einen RX-Break; sie identifiziert aber nicht allein deren elektrische Ursache. Die Messung erfolgt mit GPIO-Interrupts und kann durch Interruptlatenz beeinflusst werden; sie ersetzt kein Oszilloskop und misst keine LIN-Spannung. Nach 256 Übergängen wird nur dieser Diagnose-Interrupt abgeschaltet; die UART empfängt weiter. ISR und UART-Callback schreiben keine Logs.

ESP32 mit 3,3 V versorgen. Beim TJA1020 ist BAT/VBAT die separate Busversorgung; im 12-V-Aufbau liegt dort 12 V an. NSLP/SLP liegt für den Betrieb High, RXD benötigt einen geeigneten Pull-up nach 3,3 V. TXD ist der Eingang vom ESP-TX, RXD der Ausgang zum ESP-RX. Modulaufdrucke können anders gemeint sein; Signalfunktionen nach Schaltung prüfen. Quelle: [NXP TJA1020](https://www.nxp.com/docs/en/data-sheet/TJA1020.pdf).

Ob ein einzelnes Thermo-Top-Steuergerät die verwendete W-Bus-Adressierung direkt unterstützt, lässt sich ohne konkrete Teilenummer und Anschlussbelegung nicht feststellen. Keine Pinbelegung, alternative Baudrate oder fremden Diagnosebefehle raten. Ein nacktes Steuergerät ist noch kein Nachweis für oder gegen Protokollkompatibilität; fehlende Peripherie und fehlende Kommunikation getrennt untersuchen.
