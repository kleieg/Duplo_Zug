# Projekt-Tagesprotokoll

## 2026-03-19

- Ursache fuer die wahrgenommene Verzoegerung nach BLE-Verbindungsaufbau analysiert.
- Festgestellt, dass die Verbindungs-Callback-Logik den BLE-Task mehrere Sekunden blockiert hat und dadurch Befehle erst deutlich spaeter ausgefuehrt wurden.
- Initialisierung des Duplo-Hubs in eine nicht-blockierende Schrittfolge im Main-Loop verschoben.
- Neuer Ready-Zustand eingefuehrt: Blau signalisiert weiterhin "verbunden, Initialisierung laeuft", Gruen blinkt erst, wenn Befehle tatsaechlich verarbeitet werden koennen.
- Eingaben fuer Buttons und Drehgeber bis zum Ready-Zustand sauber gesperrt.
- Firmware erfolgreich mit `platformio run -e esp32-s3-fh4r2` gebaut.
- Reconnect-Verzoegerung nach Aus- und Einschalten des Zuges weiter analysiert.
- Legoino-Scanfenster fuer Wiederverbindung von 10 s auf 2 s verkuerzt und wiederholtes Neustarten der Suche in jeder BLE-Schleife verhindert.
- BLE-Client-Verbindungsparameter auf kuerzeren Supervision-Timeout abgestimmt, damit Stromverlust des Zuges schneller erkannt wird.
- Firmware nach Reconnect-Tuning erneut erfolgreich mit `platformio run -e esp32-s3-fh4r2` gebaut.
- Hardwaretest bestaetigt: Reconnect nach Aus- und Einschalten des Zuges nun in ca. 2-3 s.
- Hardwaretest bestaetigt: Nach dem Reconnect reagiert der Zug sofort auf Befehle.
- Projektinterne Header-Dateien mit konsistenteren `@file`-, Klassen- und API-Beschreibungen erweitert.
- `README.md`, `ARCHITECTURE.md` und `Benutzerhandbuch_Kurz_DE.md` auf den aktuell implementierten Ready-, Reconnect- und Sleep/Wake-Stand synchronisiert.
- `Refactorings.md` als technische Historie gestrafft und `ToDo.md` auf noch offene Themen reduziert.
