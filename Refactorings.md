# Refactorings

Dieses Dokument ist weiterhin relevant, aber jetzt klar als historisches und technisches Arbeitsprotokoll eingeordnet.

## Zweck dieses Dokuments

Die Datei dokumentiert groessere strukturelle Umbauten, die nicht nur Features hinzufuegen, sondern die Wartbarkeit, Latenz oder Verantwortungsverteilung verbessert haben.

Sie ist damit:

- weiterhin relevant als Architektur- und Wartungshistorie
- nicht als vollstaendige Aufgabenliste gedacht
- nicht als Ersatz fuer [ToDo.md](ToDo.md) gedacht

## Wichtige bereits umgesetzte Refactorings

- Bereinigung toter und ungenutzter Pfade in `TrainController`
- Konsolidierung der `DuploHub`-Command-Erzeugung
- Entprellung des Farbsensors ueber Stabilitaets-Timer
- Optimierung von LED-Updates ueber Dirty-Flag in `StatusLED`
- Trennung von `connected` und `ready`
- Verlagerung der Post-Connect-Initialisierung aus dem BLE-Callback in einen nicht-blockierenden Main-Loop-Ablauf
- Beschleunigung von Init, Reconnect und Befehlsannahme durch reduzierte Delays und kuerzere Reconnect-Scans

## Aktuell noch laufende Refactoring-Linie

### Legoino-Konsolidierung ueber `myLegoHub`

Dieser Themenblock ist weiterhin relevant.

Ziel:

- `DuploHub` soll nur gegen eine stabile Projekt-Fassade arbeiten
- DUPLO-spezifische Low-Level-Details sollen in `myLegoHub` konzentriert werden
- direkte Abhaengigkeit von Legoino-Interna soll weiter reduziert werden

Naechste sinnvolle Schritte:

1. benoetigte API in `myLegoHub` explizit vervollstaendigen
2. direkte `hub.*`-Aufrufe in `DuploHub` schrittweise auf diese Facade umstellen
3. pro Migrationsscheibe Build und Hardwaretest durchfuehren

## Nicht mehr als aktive Refactoring-Baustelle relevant

Die folgenden Punkte sind abgeschlossen und brauchen keine weitere eigene Tracking-Struktur mehr:

- SystemMemory-Dateibenennung und Konsistenz
- Replay-Endlosschleife entfernt
- unimplementierte `DuploHub`-Header-API bereinigt
- Wasser-Button-Verhalten korrigiert
- Status-LED-Grundlogik vereinheitlicht

Stand: 19.03.2026
