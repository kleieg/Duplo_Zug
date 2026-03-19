# ToDo

Diese Datei ist weiterhin relevant.

Sie wurde auf den aktuellen Stand bereinigt und enthaelt jetzt nur noch offene oder bewusst zurueckgestellte Themen. Bereits erledigte Punkte wurden in die laufende Dokumentation, den Changelog oder das Tagesprotokoll ueberfuehrt.

## Offene Software-Themen

- Legoino-Konsolidierung ueber `myLegoHub`-Facade weiter umsetzen
  - Projekt-API in `myLegoHub` erweitern
  - `DuploHub` schrittweise auf die Facade umstellen
  - Hardwaretests pro Migrationsschritt durchfuehren

- Logging weiter aufraeumen
  - Debug-Ausgaben auf sinnvolle Betriebs- und Diagnosepfade fokussieren
  - ggf. Build-Flag-basierte Log-Tiefen trennen

## Offene Produkt-/Feature-Themen

- bevorzugten Zug speichern und bei Wiederverbindung priorisieren
- Pairing-Konzept nur weiterverfolgen, wenn Bedienung fuer Kinder einfach bleibt

## Offene Hardware-Themen

- echte Ueberwachung der Controller-Batterie nur mit Hardware-Anpassung
- moegliche zusaetzliche Taste fuer Pairing nur sinnvoll, wenn Pairing-Funktion wirklich umgesetzt wird

## Bewusst zurueckgestellt

- umfangreicher Pairing-Workflow mit Fallback nach 60 Sekunden
  - fachlich denkbar, aktuell aber nicht priorisiert
  - nur weiter sinnvoll, wenn Mehrzug-Betrieb wirklich benoetigt wird

## Erledigt und deshalb nicht mehr hier getrackt

- Replay laeuft nur noch einmal
- Eingaben waehrend Replay korrekt eingeschraenkt
- Farbsensor entprellt
- Sleep/Wake implementiert
- Init- und Reconnect-Latenz deutlich reduziert
- Ready-Zustand korrekt von BLE-Connected getrennt
- Reconnect nach Power-Cycle auf etwa 2 bis 3 Sekunden verbessert

Stand: 19.03.2026
