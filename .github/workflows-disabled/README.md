# Automatische Builds vorerst deaktiviert

Die Workflows liegen bewusst außerhalb von `.github/workflows/` und werden nicht automatisch ausgeführt. Der vorhandene Workflow wurde zusätzlich direkt auf GitHub deaktiviert.

Lokale Builds bleiben mit `pio run -e esp32s3_supermini` möglich. Zur späteren Reaktivierung die YAML-Datei zurück nach `.github/workflows/` verschieben und den Workflow auf GitHub wieder aktivieren.
