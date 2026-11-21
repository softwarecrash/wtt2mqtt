# Statische Analyse der bereitgestellten Thermo-Test-Pakete

Untersuchung vom 17.11.2026. Die Installer und ihre enthaltenen Webasto-Programme wurden ausschließlich als Archive/PE-Dateien gelesen. Kein Webasto-Installer, Treiber oder Anwendungsprogramm wurde ausgeführt. Analysierte Originaldateien und entpackte Inhalte bleiben lokal in den ignorierten Verzeichnissen `wtt/` und `build/wtt-analysis/`. Sie sind weder Bestandteil der Firmware noch des Quellpakets.

## Eingaben und Herkunft

| Original | SHA-256 |
|---|---|
| `Webasto_Thermo_Test_Setup_v208/WebastoThermoTest.msi` | `b51257ba4852c3bf933ff986b74a373ee05885bc47c2c8f8ebae2b3f84039146` |
| `WTT_V3.0_Setup.zip` | `27b4c13a68a99b550ca25b09d9610bccd83cbb980194c9fbf68c8eb3993af95f` |

Das MSI enthält `WTT2002_RU.exe`, intern Cabinet-Eintrag `_00E1D0E9BE3C4AECA3E0A25C2DCCE04D`, SHA-256 `d05d8de6cc7835be856bff1db6742f05ca0799201e11d29b89f363b7eb833b56`. Das 3.0-ZIP enthält zwei NSIS-Pakete für Anwendung und Diagnoseframework. Im Anwendungspaket liegt zusätzlich `WTT2/WTT.exe`, SHA-256 `0154c71972681225152d6f6a6f15157db2301d5b03f9d69c3119916aad1eea75`.

Die Paketbezeichnungen werden nicht mit den teils abweichenden internen PE-Dateiversionen gleichgesetzt: Die beiden alten EXE-Dateien melden 2.6.5.0 beziehungsweise 1.1.23.0. Die ältere Anwendung aus dem 3.0-Paket wird hier deshalb nur als „gebündeltes WTT2“ bezeichnet, nicht als gesichert identische Version 2.16.

## Eindeutig zugeordnete UART-Profile

Die RT_STRING-Ressourcen benennen die Heizgeräte. Eine Funktionstabelle in `.rdata` verbindet die Ressourcen-ID mit der jeweiligen Initialisierungsfunktion. Dort werden Baudrate und DCB-Felder gesetzt; die serielle Hilfsfunktion reicht sie an Windows `SetCommState` weiter. Damit sind dies konkrete Konfigurationen und keine bloßen gefundenen Textfragmente. Alle folgenden virtuellen Adressen gelten für die EXE aus dem 2.08-MSI bei ImageBase `0x00400000`.

| Ressource | Menübezeichnung | UART | Gerätetabelle | Initialisierungsfunktion |
|---|---|---|---|---|
| 9801 | Thermo Top BMW | 9600, 8E1 | `0x00497ECC` | `0x00444F50` |
| 9803 | Thermo Top C/E/Handel/P/C-Z MB/DC | 10400, 8N1 | `0x004979EC` | `0x004560F5` |
| 9804 | Thermo Top C/Z Fiat | 10400, 8N1 | `0x00498004` | `0x00448A6D` |
| 9805 | Thermo Top C/Z PSA | 10400, 8N1 | `0x004980A0` | `0x0044D6D1` |
| 9806 | Thermo Top (98) | 10400, 8N1 | `0x00497B24` | `0x0043E9E3` |
| 9818 | Thermo Top C/Z Rover | 10400, 8N1 | `0x0049813C` | `0x00451F3C` |

Das gebündelte WTT2 setzt dieselben UART-Parameter für diese sechs Menüfamilien. Die dortigen Initialisierungsadressen sind BMW `0x00459745`, universelle C/E `0x0047685E`, Fiat `0x004647C3`, PSA `0x0046B702`, TT98 `0x00451BEB`, Rover `0x004712FA`. Auch dort existiert zusätzlich ein eigenständiges „Thermo Top Evo PSA“-Profil mit 10400/8N1. Dessen Kommandos wurden hier nicht als alte C/Z-Kommandos übernommen.

## Fiat: Ziel 0x51, Tester 0xF1

Default-Adressen stehen an `0x004E0678`: `51 F1`. Die Sendefunktion `0x0044AF63` erzeugt den Rahmen: `80+N`, Ziel, Tester, N Nutzdatenbytes, additive 8-Bit-Prüfsumme. Die Befehlstabelle beginnt bei `0x004E06E8` mit Nutzdaten `81` und danach `1A 80`.

Daraus ergeben sich diese vollständigen Anfragen:

```text
Sitzungsaufbau: 81 51 F1 81 44
Identifikation: 82 51 F1 1A 80 5E
```

Der Sitzungsantwort-Parser bei `0x004473E0` erwartet den positiven Dienst `C1`, drei Nutzdatenbytes und die Schlüsselbytes `E9 8F` (Vergleich bei `0x00447496`). Diese Schlüsselbytes sind nicht die bisher als universelles Beispiel verwendeten `C2 46`.

Der RS232-Sender schreibt ein Byte pro `WriteFile`-Aufruf und ruft danach `Sleep(10)` auf (Schleife `0x0044B309` bis `0x0044B352`). Die tatsächliche Buszeit hängt zusätzlich von Windows und Interface ab; die 10-ms-Pause zwischen den Host-Schreibaufrufen ist im Code eindeutig vorhanden.

## PSA: Ziel 0x01, Tester 0xF1

Default-Adressen an `0x004E0BB8`: `01 F1`. Sendefunktion `0x0044F896`, derselbe Längen-/Summenaufbau wie Fiat. Befehlstabelle bei `0x004E0BF8`: Sitzungsaufbau `81`, Identifikation `21 80`.

```text
Sitzungsaufbau: 81 01 F1 81 F4
Identifikation: 82 01 F1 21 80 15
```

Parser `0x0044BAF0` erwartet Dienst `C1`, drei Nutzdatenbytes und Schlüsselbytes `D0 8F` (Vergleich bei `0x0044BBA6`). Der RS232-Sender verwendet ebenfalls einzelne Bytes mit `Sleep(10)` (Schleife `0x0044FC3C` bis `0x0044FC85`). Die PSA-Identifikationsparser und Messwerttabellen unterscheiden sich von Fiat; ein identisches KWP-Containerformat bedeutet keine identische Nutzdateninterpretation.

Die alte PSA-Gerätetabelle des gebündelten WTT2 liegt bei VA `0x005464D0`. Nach der Identabfrage sind dort die Leseanfragen `21 C0`, `21 C1`, `21 C2`–`21 C9`, `21 CC`–`21 CE`, `21 A0` sowie `17 FF 00` eingetragen. WTT erwartet positive Dienste `61` bzw. `57`; das Fehlerlöschen `14 FF 00` ist ein separater Eintrag und wird nicht mit der Leseabfrage verwechselt.

Parseradressen der gebündelten WTT2-EXE: `0x00469918` für C0, `0x00469A1E` für C1, `0x00469B34`–`0x0046A1BC` für C2–C9, `0x0046A2BF`/`0x0046A400` für CC/CD, `0x0046A541` für CE, `0x0046A64C` für A0 und `0x0046A847` für Fehler. Die C0-Tabelle bei `0x004F4808` enthält 36 Temperatur-/Rohwertpaare (−40…135 °C); WTT interpoliert zwischen ihnen. C1 wird über `MulDiv(raw, 0x1AAA, 100)` und anschließend `×0.001` in Volt umgerechnet. C2–C9, CC–CE und A0 werden in der Firmware derzeit als Rohbytes belassen, weil ihre konkreten Feldnamen bzw. Wertebereiche aus den Parsern allein nicht sicher hervorgehen.

`17 FF 00` erwartet `57`, danach eine Anzahl und pro Fehler einen Fünf-Byte-Datensatz. Die ersten zwei Bytes ergeben den 16-Bit-Rohcode; die folgenden drei Bytes werden von WTT für zusätzliche Darstellung/Status verwendet. Der Konverter bei `0x0046B033` maskiert den Fehlercode auf 12 Bit und mappt die belegten Werte: `001→01`, `002→02`, `003→03`, `006→05`, `013→83`, `608→08`, `609→09`, `60A→0A`, `60B→0B`, `60C→13`, `818→88`, `819→89`, `81A→8A`, `81B→8B`, `A05→84`, `C04→04`, `C07→06` (Hexadezimal). Die Werte `004` und `005` sowie sonstige nicht aufgeführte Werte fallen auf unbekannt. Die Firmware zeigt diese belegten Klassen samt generischem Fehlertext, sonst den PSA-Rohcode; die folgenden drei Bytes bleiben ohne erratene Bedeutung Rohdaten. Der Codepfad `14 FF 00` löscht Fehler.

Die Tabelle enthält zusätzlich `3B C3 00 00`, `30 00 11`, `30 00 00`, zweimal `31 A3 00` und `31 A1 00 00`; einige Parameter werden vom WTT-UI dynamisch ersetzt. Das sind bedienungs-/zustandsabhängige Aktorausgänge bzw. Routinen. Aus diesen Tabellenzeilen allein folgt kein sicher bestätigtes Start-/Stopp-Mapping oder eine Rückstellfolge. Deshalb sendet die Firmware diese Telegramme nicht und sperrt PSA-Start/Stopp. Weder Fehlerlöschen noch diese aktiven Einträge werden beim automatischen Scan verwendet.

## BMW: eigener Busrahmen, nicht pauschal generisches DS2

UART ist 9600/8E1. Default-Adressen an `0x004E0370`: Heizgerät `6B`, Tester `3F`. Sendefunktion `0x00446443` erzeugt:

```text
Tester | N+2 | Heizgerät | Nutzdaten[N] | XOR
```

Die erste Tabellenanfrage bei `0x004E03D8` hat Nutzdaten `00`. Daraus folgt:

```text
Identifikation: 3F 03 6B 00 57
```

Der zugehörige Parser `0x00443DD0` erwartet Antwortdienst `A0` und ein Längenbyte von 16 (`Länge-3 == 13`). Dies ist ein BMW-Busrahmen mit getrennter Quell-/Zieladresse; die frühere allgemeine DS2-Vermutung für den direkten Anschluss darf nicht als Beschreibung dieses WTT-Profils verwendet werden. Eine gültige Antwort auf diesen Rahmen wurde am vorhandenen Steuergerät noch nicht beobachtet.

## Originale lange Aufwecksequenz

Die gemeinsame Hilfsfunktion `0x0041DA97` setzt und löscht den seriellen Break mit folgenden Wartezeiten:

```text
LOW 300 ms -> HIGH 50 ms -> LOW 25 ms -> HIGH 25 ms
```

Die Fiat-, PSA-, BMW- und universellen C/E-Codepfade rufen diese Funktion auf. Anschließend wird der Empfangspuffer geleert. In dieser Hilfsfunktion gibt es keine feste zusätzliche 3-Sekunden-Wartezeit. Im PSA-Zustandsautomaten wird daneben ein 3000-ms-Zeitgeber angelegt; das ist kein `Sleep(3000)` vor dem ersten Telegramm. Eine vollständige Rekonstruktion aller Zustandsübergänge steht noch aus.

Das vorherige Firmwareprofil mit zusätzlichen 3025 ms stammt aus dem zuvor gefundenen experimentellen Forenbeispiel. Es bildet den nun vorliegenden Original-WTT-Helfer nicht exakt ab. Die aktuelle Firmware setzt jetzt die originale lange Sequenz und die nachgewiesenen Fiat-/PSA-Bytepausen um.

Der TJA1020-Dominant-Timeout bleibt eine eigenständige Hardwaregrenze: Eine softwareseitige LOW-Dauer beweist nicht deren tatsächliche Dauer am LIN-Pin. Die Aufzeichnung der Wakeup-RX-Flanken muss bei neuen Profiltests weiterlaufen.

## Reproduzierbarkeit und Grenzen

Die lokale Datei `build/wtt-analysis/evidence/protocol-evidence.json` enthält Profile, Prüfsummen, Datenadressen und Disassemblierung der relevanten Ausschnitte. `v216-profiles.json` enthält die Gegenprüfung am gebündelten WTT2; der Dateiname ist nur eine Arbeitsbezeichnung, kein Versionsnachweis.

Der Extraktor ist [tools/analyze_wtt.py](../tools/analyze_wtt.py). Er akzeptiert nur den geprüften SHA-256 der alten EXE und lädt die EXE nicht als ausführbares Programm. Benötigt Python-Pakete `pefile` und `capstone`. Beispiel nach dem Entpacken:

```powershell
python tools/analyze_wtt.py build/wtt-analysis/v208/_00E1D0E9BE3C4AECA3E0A25C2DCCE04D --deps build/wtt-analysis/python
```

Extraktion: Das MSI ist ein OLE-Container mit eingebettetem Cabinet; das 3.0-ZIP enthält NSIS-Archive. 7-Zip konnte beides ohne Installation entpacken. Die 3.0-Anwendung enthält außerdem eingebettete W-Bus-ODX-Ressourcen und .NET-Protokollmodule. Für die alten C/Z-Varianten ist jedoch das native WTT2 entscheidend, nicht allein die modernen ODX-Dateien.

Keine Entsperrung von Dongles oder Änderung der Originalprogramme. Keine proprietären Routinen in die GPL-Firmware kopiert. Die vorliegenden Ergebnisse sind Protokollfakten und daraus berechnete Anfragebytes; Hardwarekompatibilität, genaue Gerätezuordnung und vollständige Sensor-/Fehlerlayouts sind damit noch nicht bestätigt. Die daraus abgeleiteten sieben Diagnoseprofile sind inzwischen implementiert; siehe [Protokollvarianten](PROTOCOL_VARIANTS.md). Native Tests prüfen Rahmen, Prüfsummen, OEM-Schlüssel, Bytepausen, begrenzte Wiederholungen und UART-Wiederherstellung. Reale Antworten dieser OEM-Profile liegen noch nicht vor.
