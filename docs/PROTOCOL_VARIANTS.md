# Diagnoseprofile in der aktuellen Firmware

Sieben begrenzte Leseprofile, aus den lokalen Original-WTT-Dateien unabhängig implementiert. Details und Herkunft: [WTT-Binäranalyse](WTT_BINARY_ANALYSIS.md). Die OEM-Profile sind noch nicht an echter Hardware bestätigt und aktivieren keine Heizungssteuerung.

| Profil | UART / Wakeup in ms | Anfragen |
|---|---|---|
| W-Bus | 2400/8E1, 50/50 | Version und Name `51/0A`, `51/0B` |
| TT-C KWP 25/25 | 10400/8N1, 25/25 | Sitzung `81 51 F1 81 44`, dann Info `3C 00` und Fehler lesen `A1` |
| TT-C KWP 30/25 | 10400/8N1, 30/25 | dieselben Anfragen |
| TT-C WTT | 10400/8N1, 300/50/25/25 | dieselben Anfragen |
| Fiat | 10400/8N1, 300/50/25/25, 10 ms Bytepause | `81 51 F1 81 44`, nach `C1 E9 8F`: `82 51 F1 1A 80 5E` |
| PSA | 10400/8N1, 300/50/25/25, 10 ms Bytepause | `81 01 F1 81 F4`, nach `C1 D0 8F`: `82 01 F1 21 80 15` |
| BMW | 9600/8E1, 300/50/25/25 | Identifikation `3F 03 6B 00 57`; Antwort Quelle `6B`, Ziel `3F`, Länge 16, Dienst `A0`, XOR |

Lange Wakeup-Sequenz: LOW 300, HIGH 50, LOW 25, HIGH 25 ms. Der frühere zusätzliche 3025-ms-Aufschub entfällt. Fiat/PSA senden ohne blockierende Wartezeit byteweise und erfassen das Echo währenddessen. Vollständige geprüfte Antworten werden sofort ausgewertet. Sitzungsversuche sind auf drei begrenzt. Folgeabfragen sind an eine passende positive Sitzung gebunden. BMW nutzt einen eigenen WTT-Rahmen, keinen generischen DS2-Test. Rover bleibt unimplementiert.

**Testen:** Webasto-Diagnose -> **Varianten testen**, alternativ USB `scan`. Danach **Gespeicherter Scan-Mitschnitt -> Mitschnitt laden** und kopieren oder herunterladen. Alle sieben Profile laufen einmal durch; danach wieder normaler W-Bus mit 2400/8E1. UART- und Wakeup-RX-Diagnose bleiben aktiv. Der TJA1020-Dominant-Timeout wird dadurch nicht umgangen.

## Historischer Recherchestand

Die folgenden Recherchegrenzen zu Fiat/PSA/BMW und das bisherige 3025-ms-Profil sind durch die obige Implementierung ersetzt; sie werden zur Nachvollziehbarkeit erhalten.

# Recherche: ältere Thermo Top Z/C und OEM-Protokolle

Stand: 17.11.2026. W-Bus und alte TT-C-K-Line-Diagnose sind als begrenzte Diagnoseproben implementiert. Keine der zusätzlichen Varianten ist an den vorhandenen Steuerplatinen bestätigt; keine Heizungssteuerung über die alten Diagnoseprofile.

## Implementierter Scan

Erweiterung: Während der Aufwecksequenzen werden jetzt `SCAN_WAKE_LOW`, `SCAN_WAKE_HIGH`, gegebenenfalls `SCAN_WAKE_LOW2` und `SCAN_WAKE_SETTLE` protokolliert. Jede Zeile enthält Beobachtungsdauer in Mikrosekunden, RX-Anfangs-/Endpegel, Flankenzahl, längste LOW-Phase und bis zu zwölf High-/Low-Zeitabschnitte. Diese GPIO-Messung umfasst den TJA-Rückkanal, nicht die analoge LIN-Spannung, und kann durch Interruptlatenz beeinflusst werden. Maximal 256 Flanken pro Phase; danach wird nur der Messinterrupt abgeschaltet. Die Diagnose umgeht oder deaktiviert den TJA-Dominant-Timeout nicht.

Nach positiver alter Sitzungsantwort und dem Geräteinfo-Versuch folgt jetzt die zusätzliche Leseanfrage `81 51 F1 A1 64`. Die beobachtete Antwort `84 F1 51 E1 FF FF 00 A5` wird als positive Dienstantwort geprüft, noch nicht in Fehlertexte oder einen garantierten leeren Fehlerspeicher umgedeutet. `SCAN attempt` protokolliert jeden Dienst separat, auch wenn vorherige Dienste bereits positiv waren.

Der ESP hält einen separaten Scan-Mitschnitt im RAM (16 KiB, bei Überlauf mit Hinweis), bis ein neuer Scan beginnt oder das Gerät neu startet. Normale Statusabfragen überschreiben ihn nicht. In **Webasto-Diagnose → Gespeicherter Scan-Mitschnitt → Mitschnitt laden** lässt sich der Text laden, markieren, kopieren und herunterladen; HTTP-Endpunkt `/api/scan-log` mit demselben Zugangsschutz wie die Diagnose. Im Setup-AP bleibt er gesperrt. Der Mitschnitt verwendet denselben nicht wartenden Logging-Zugriff wie das Live-Log; bei konkurrierendem Zugriff können einzelne Zeilen ausfallen. Kein Dateisystem oder Flash-Schreiben nötig.

In Webasto-Diagnose **Varianten testen**, alternativ USB-Befehl `scan`. Abbrechen mit **Suche abbrechen** oder USB `scan-stop`. Der Scan läuft nur ohne eigene aktive/ausstehende Heizanforderung, wartet eine laufende W-Bus-Transaktion ab und übernimmt anschließend allein den UART. Normale Abfragen pausieren. Ein expliziter Stopp beendet den Scan und wird weiterhin als regulärer W-Bus-Stopp behandelt; für ein ausschließlich anderssprachiges Gerät ist dessen Wirksamkeit nicht zugesichert. OTA/Neustart brechen den Scan ab. Nach Ende/Abbruch wird 2400/8E1 wiederhergestellt; vorherige Messwerte werden vor Scanbeginn ungültig.

| Profil | UART und Wakeup | Anfragen |
|---|---|---|
| W-Bus | 2400/8E1, 50 ms LOW + 50 ms HIGH | 51/0A, 51/0B |
| TT-C KWP, Fast-Init-Kandidat | 10400/8N1, 25 ms LOW + 25 ms HIGH | `81 51 F1 81 44`, nach positiver Antwort Geräteinfo |
| TT-C KWP, 30-ms-Kandidat | 10400/8N1, 30 ms LOW + 25 ms HIGH | dieselben Lese-/Sitzungsanfragen |
| TT-C Legacy, berichtete lange Initialisierung | 10400/8N1, 300 ms LOW + 50 ms HIGH + 25 ms LOW, UART an, 3025 ms warten | dieselben Lese-/Sitzungsanfragen |

Vor jedem Profil 1500 ms Ruhezustand. Alte Profile versuchen den Sitzungsaufbau maximal dreimal mit 1 s Empfangsfenster und 1 s Abstand. Geräteinfo `82 51 F1 3C 00 00` folgt nur nach positiver Sitzungsantwort. Keine Start-, Aktortest-, Lösch-, Codier- oder Entsperrtelegramme. Der Sitzungsaufbau verändert den Diagnosezustand, keine dauerhaften Einstellungen.

Neuer Primärbeleg: [Thermo-Top-C-Mitschnitt von Herb Peyerl und Bestätigung des libwbus-Autors, 2010; Initialisierungsbericht Ivo Linask, 2020](https://sourceforge.net/p/libwbus/discussion/756917/thread/5f5865e8/). Dort sind 10400/8N1, Anfrage `81 51 F1 81 44`, positive Antwort `83 F1 51 C1 C2 46 8E` und Geräteinfo `82 51 F1 3C 00 00` erfasst. Die lange Initialisierung wird vom späteren Autor als getestet beschrieben. Die 25/25- und 30/25-Kandidaten sind gezielte Timingversuche, keine allgemein verifizierten C/Z-Profile. Die teilweise widersprüchlichen Kommentare des Beispielcodes wurden nicht als Hardwarebeweis übernommen. Eigene nicht blockierende Implementierung; keine Quellcodekopie aus dem Forenbeitrag.

Antworten müssen zur Gegenadresse und zum angefragten Dienst passen und ihre Prüfsumme erfüllen: W-Bus XOR, alte TT-C-KWP-Frames additive 8-Bit-Summe. Eigenes Echo zählt niemals als Treffer. UI/API unterscheiden kein Echo, nur Echo, ungültige Antwort, gültige negative Antwort und gültige positive Antwort. Eine positive Sitzungsantwort beweist noch keine erfolgreich gelesene Geräteinformation und aktiviert keinen anderen Heizmodus. Alle Rohbytes stehen im Log; Parserfelder werden nicht als Temperatur/Spannung umgedeutet.

DS2, Fiat, PSA und Rover bleiben ausdrücklich **nicht getestet**. Die folgenden Recherchegrenzen für diese Familien bestehen weiter.

## Kernergebnis

## Zusätzlicher Befund: Transceiver begrenzt Wakeup-LOW

[NXP/Philips TJA1020, Abschnitt TXD dominant time-out und Kennwert tdom](https://www.nxp.com/docs/en/data-sheet/TJA1020.pdf): Nach 6–20 ms (typisch 12 ms) dauerhaft LOW an Chip-TXD deaktiviert der Transceiver seinen Sender; eine positive TXD-Flanke setzt den Timer zurück. Damit garantieren die ESP-seitigen 25-/30-/50-/300-ms-LOW-Zeiten keine gleich langen dominanten Busimpulse. Die bisherige RX_TIMING-Aufzeichnung beginnt erst beim UART-Schreiben, nicht während der Wakeup-Sequenz. Die ausgeführten Profile sind deshalb nur Software-Timingversuche, keine verifizierten Messungen der tatsächlichen Businitialisierung. Das erklärt nicht automatisch das anhaltende Empfangs-LOW der ersten Platine. Insbesondere lässt sich aus diesem Befund allein weder ein Defekt noch das benötigte Protokoll ableiten.

Weitere alte TT-C/E-Leseanfrage: [MaksVV, Arduino.ru, Januar 2019](https://arduino.ru/forum/proekty/gsm-ustroistvo-upravleniya-kotlami-webasto-i-ne-tolko?page=8) nennt `81 51 F1 A1 64` als Fehlerabfrage und `84 F1 51 E1 FF FF 00 A5` als Antwort ohne Fehler. Additive Prüfsummen stimmen. Der Autor formuliert die Zuordnung vorsichtig und schließt die Übertragbarkeit auf Fiat/Peugeot-OEM-Geräte ausdrücklich nicht ein. Jetzt als Diagnoseprobe nach positivem Sitzungsaufbau implementiert; ersetzt keinen erfolgreichen Diagnoseaufbau.

Eine belastbare vollständige Fiat-/PSA-Initialisierung mit konkreter Adresse und Antwortvalidierung wurde in dieser zusätzlichen Recherche weiterhin nicht gefunden. Windows-Gerätemanagerangaben aus Erfahrungsberichten sind keine Messung der tatsächlichen Busbaudrate und werden nicht als neues Profil übernommen.

Ergänzung aus [Webasto Thermo Test 2.16, Abschnitte 1.4 und 1.5, Seiten 8–13](https://manuals.plus/m/5122c6d4806669697aad42c7a33d75ad25de5d4b4baf8bc89514fd0ed5fc5ea0.pdf): Die K-Line-Tabelle ordnet Ducato 244, Multipla 186 und Stilo 192 der Fiat-Diagnoseauswahl zu; Berlingo M59, C5 X4, C8, Xsara Picasso N68 sowie Peugeot 807/Jumper/Partner der PSA-Auswahl. Fiat Ulysse verwendet dort ebenfalls die PSA-Auswahl. Für diese Anwendungen nennt der Hersteller direkten Diagnosezugang an Steuergeräte-Pin 2. Auch E53 und die aufgeführten Range-Rover-/Rover-75-Anwendungen sind mit BMW-Auswahl direkt an Pin 2 genannt. Damit ist ein direkter OEM-Diagnoseweg belegt, ohne dass das Dokument Baudrate, Initialadresse oder Requestbytes offenlegt. Diese Familien lassen sich folglich prinzipiell als eigene Profile ergänzen, aber nicht anhand bloßer Markennamen als funktionierende Telegramme implementieren.

Das vom Benutzer bereitgestellte Log vom 17.11.2026 enthält ausschließlich normale `50 05`-Abfragen, kein `SCAN_TX` und kein Profilresultat. Es zeigt durchgehend `no_bus_echo`, RX LOW und null Flanken. Daraus lässt sich kein Ergebnis der alten 10400/8N1-Profile ableiten. Der Scan ist manuell über **Varianten testen** oder USB `scan` auszulösen; ein Firmwareupload startet ihn nicht automatisch.

Die Modellbezeichnung Thermo Top C/Z bestimmt das Diagnoseprotokoll nicht eindeutig. Webasto unterscheidet W-Bus und ältere geräte-/fahrzeugspezifische Diagnoseauswahlen. Ein Diagnoseanschluss mit K-Line-ähnlicher Elektrik ist kein Beleg für W-Bus oder für ISO9141/KWP2000. Fahrzeugseitige Betriebssteuerung, fahrzeugseitige Diagnose und direkter Diagnoseanschluss am Heizgerät müssen getrennt betrachtet werden.

## Quellen und Evidenz

1. [Webasto Thermo Test 2.07, Herstelleranleitung, Abschnitt 1.3 und 2.2](https://slomkowski.eu/cars/mercedes-w124-webasto-thermo-top-evo-installation/webasto-thermo-test-software.pdf). Eigenständige Diagnoseauswahl neben W-Bus. Die W-Bus-Anwendungstabelle nennt unter anderem VW T5, Phaeton, Touareg, Audi A8 D3, Porsche Cayenne, BMW E65, Mazda 6/MPV und bestimmte Mercedes-Baureihen/Bauzeitstände. Das lässt sich nicht auf sämtliche Heizgeräte dieser Marken übertragen.
2. [Webasto Thermo Test 3.4, Herstelleranleitung, Seiten 7–8](https://manuals.plus/m/586880ad2ad3b1b61e58c7df46028a3f5c997eb349b91ff1e673821cd4ea1457.pdf). WTT 3 unterstützt W-Bus ab Version 4.0; für ältere Geräte startet WTT 2. Dessen Liste enthält getrennt W-Bus, Thermo Top C/E/Handel/P/C-Z MB-DC, BMW, Fiat, PSA und Rover. Auch eine neue Thermo-Test-Version macht die alten Diagnosefamilien nicht automatisch zu W-Bus.
3. [Blackbox Solutions, Systemübersicht Range Rover L322](https://www.blackbox-solutions.com/site/product?sku=SM131). Die Tabelle der älteren BMW-basierten Fahrzeuge nennt Webasto Fuel Burning Heater, Modul SM074, Kommunikationstyp DS2. Dies belegt eine reale Nicht-W-Bus-Diagnosefamilie. Es belegt noch nicht, welcher physische Pin einer unbekannten ausgebauten Platine direkt dieselben Telegramme annimmt oder ob ein Fahrzeug-Gateway beteiligt ist.
4. [OBD2_KLine_Library, tatsächliche Protokollkonfiguration](https://github.com/muki01/OBD2_KLine_Library/blob/main/src/KLine_Protocol.cpp). Allgemeines DS2-Profil: 9600 Baud, 8E1, Adressbyte, Gesamtlängenbyte, XOR-Prüfsumme, Anfrage/Ping statt W-Bus-Wakeup. Die voreingestellte Adresse 0x12 ist ausdrücklich DME, **keine belegte Webasto-Adresse**. Kein Webasto-spezifischer Identifikationsbefehl aus dieser Quelle übernommen.
5. [I-BUS App, Herstellerdokumentation zur Standheizungssteuerung](https://wiki.ibus-app.de/index.php?title=Resler_Modul_Einstellungen). E38/E39/E53 haben Bus-Steuerungsvarianten; E46/E83/E85 werden dort über ein zusätzliches Relais/12-V-Signal angesteuert. Das dokumentiert Unterschiede der Betriebssteuerung; es beschreibt kein universelles direktes Webasto-Diagnosetelegramm. I-/K-Bus nicht mit K-Line oder DS2 gleichsetzen.
6. [libwbus, ursprüngliche beobachtete W-Bus-Protokollbeschreibung](https://github.com/futuranet/libwbus/blob/master/webasto_wbus.txt). 2400 Baud, 8E1, 50-ms-LOW/50-ms-HIGH-Wakeup, F4/4F, XOR. Beispielinitialisierung: 51/0A, 51/0B, 51/0C und 38. Primär an Thermo Top V beobachtet; kein universeller C/Z-Kompatibilitätsnachweis.

## Kandidaten für die Firmware

| Familie | Nachgewiesen | Noch erforderlich |
|---|---|---|
| W-Bus | 2400/8E1, Frames und Leseabfragen dokumentiert | Gültige Antwort der konkreten Steuerplatine |
| Ältere C/E/P und C/Z MB/DC | Eigenständige Diagnoseauswahl; alter TT-C-Mitschnitt mit 10400/8N1 liegt vor | Zuordnung zu den vorhandenen Platinen; reale Antworten auf die implementierten Proben |
| BMW/L322 DS2 | Webasto-Anwendung im Diagnosegerät und allgemeines 9600/8E1-DS2-Profil | Heizgeräteadresse, Read-Ident-Telegramm, direkter Anschlussweg |
| Fiat/PSA/Rover | Separate Webasto-Diagnoseauswahlen | Jeweils konkrete Transport- und Telegrammbelege; Markenname allein reicht nicht |
| VW/Audi KWP1281/KWP2000 | Allgemeine offene Implementierungen existieren | Nachweis für genau ein C/Z-Steuergerät und dessen direkten Anschluss; bislang keine Freigabe |

Für DS2 und die übrigen OEM-Familien hat die Recherche noch keine ausreichend belegte Telegrammfolge für die zwei unbekannten vorhandenen Platinen ergeben. Insbesondere werden keine allgemeinen DME-/Motor-OBD-Adressen als Webasto-Adressen ausgegeben. Ein W-Bus-Telegramm bei 9600 Baud ist kein DS2-Test.

## Konsequenz für die bisherigen Messungen

Die zweite Platine lässt sauberes Echo zu, antwortet aber weder auf W-Bus-Version noch Gerätename oder Sensoren. Das schließt einen funktionierenden Controller mit anderem Protokoll nicht aus. Die erste Platine verändert ihre Leistungsaufnahme beim Anschluss der Diagnoseleitung und zeigt zeitweise LOW am Empfang. Das belegt weder einen Protokolltyp noch einen Defekt.

Die expliziten Diagnoseprofile mit getrennten Parsern und UART-Einstellungen sind jetzt implementiert. DS2 erst mit belegter Heizgeräteadresse und Identifikation freigeben; zusätzliche ältere Webasto-Familien erst mit belegtem Initialisierungs-/Frameformat. Keine automatische Folge beliebiger Schreib-, Start-, Entsperr- oder Codierbefehle an unbekannte Platinen.
