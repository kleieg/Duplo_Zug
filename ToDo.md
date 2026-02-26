## Aktuelle ToDos

### 1. Record /Replay Funktion:
- [x] Die Replay Liste wird nicht nur genau einmal abgefahren sondern in einer Schleife -> Sollverhalten: Liste einmal abfahren und dann den Replay Modus beenden
- [x] Während Replay müssen alle Kommandos außer Beenden des Replaymodus und Stop ignoriert werden
- [x] Ebenso sollten die  über den Color Sensor identifizierten Aktionen im Replay Modus NICHT ausgeführt werden
- [x] Im Record Modus jedoch sollen die über den Color Sensor identifizierten Befehle mit aufgezeichnet werden
- [x] Das Timing der aufgezeichneten Befehle muss überprüft werden und sollte bis auf 500ms mit der Aufgezeichneten Sequenz beim Abspielen übereinstimmen

### 2. Sonstige Softwareänderungen
- [x] Der Voltage Sensor muss aktiviert werden um die Spannung der Duplobatterie zu überwachen. Klären, bei welchem Spannungslevel eine Warnung ausgegeben werden soll (hat 4 AAA Batterien) -> Level wurde auf 6,0V gesetzt testweise
- [x] Die RGB LED auf dem Controller soll nur für Statusrückmeldungen verwendet werden:
  - [x] Initialisierung angeschlossen, Controller bereit -> von gelb leuchten, dann grün permanent
  - [x] Duplo Hub connected -> blau 3 x blinken dann aus
  - [x] Button Stop gedrückt -> rot dauer blinken, wird aufgehoben bei erneutem Drücken auf Stop
  - [x] Button Record gedrückt: -> blau dauer blinken, aufgehoben bei erneutem Drücken auf Record
  - [x] Button Play gedrückt: -> gelbes dauer blinken bis Sequenz abgeschlossen oder Record erneut gedrückt wurde
  - [x] Bei Disconnect des Duplo Hubs 3 x weiß blinken
  - [x] Duplo Batterie Level low: 10x hektisches Blinken in rot alle 30 Sekunden
  - [x] Duplo Color Sensor gibt bei reflektierenden Oberflächen manchmal kurz hintereinander unterschiedliche Farbewerte aus
    - [x] Sensor muss "entprellt" werden
      - [x] Sensor Events innerhalb einer kurzen Zeitspanne (< 250ms) ignorieren
      - [x] Aufruf des Callbacks nur, wenn Sensor Event stabil ist

- [x] Controller Sleep Modus einbauen
  - [x] wenn keine GUI Aktion für mehr als 5 Minuten soll der Controller in den Sleep Modus gehen
    - [x] Wake-Up über Encoder-Taste (RTC EXT1 ALL_LOW, mit GPIO-Light-Sleep-Fallback)
    - [x] Wake wird optisch quittiert (5x grün blinken)
    - [x] Sleep-Eintritt wird optisch signalisiert, danach LED aus

### 3. Code Optimierungen

- [ ] Optimierung des Codes
  - [x] Redundanzen ausbauen (Phase 1):
    - [x] DuploHub Command-Erzeugung vereinheitlicht
    - [x] StatusLED Hardware-Updates per Dirty-Flag effizienter gemacht
    - [x] Wasser-Button-Logik in TrainController fachlich korrigiert
    - [x] Unused/Dead Code in TrainController entfernt
    - [x] Datei-/Namenskonsistenz für SystemMemory hergestellt
    - [x] Unimplementierte DuploHub-Header-API bereinigt
  - [x] Klare Verteilung der Verantwortlichkeiten:
    - [x] DuploHub -> Verbindung zur Hardware, Senden von Befehlen und Weiterleiten von Sensordaten
    - [x] myLegoHub ergänzt und erweitert die Legoino Lib und wird nur von DuploHub verwendet
    - [x] TrainController ist für die Umsetzung der GUI Kommandos und die Verarbeitung von Sensorrückmeldungen verantwortlich
  - [x] Reduzieren der Delays so dass die Initialisierung und die Herstellung der Bereitschaft des Controllers deutlich schneller geht. Auch die Latenz bei Kommandos muss reduziert werden. Klären, welche minimalen Delays notwendig sind, damit die BLE Kommandos sicher ankommen.
  - [ ] Legoino-Konsolidierung über `myLegoHub`-Facade umsetzen (Alternative 1)
    - [x] Risikoanalyse und Migrationsplan dokumentiert
    - [ ] Phase A: API-Baseline + Facade-Vertrag
    - [ ] Phase B: Facade-Delegation implementieren + DuploHub schrittweise umstellen
    - [ ] Phase C: Hardware-Regressionstest pro Migrationsscheibe
    - [ ] Phase D: Wartungsregeln und Abschlussdoku

### 4. Hardware Modifikationen
- [x] Modifikation der Platine:
- [ ] Controller Batterie Level Low: hektisches Dauerblinken in rot - erfordert Modifikation der Platine und Überwachung der Spannung durch MCU -> aktuell nicht realisierbar
  - [x] Stop Button wird direkt über einen Digital Pin eingelesen
  - [ ] Neben den Analog Buttons LED, Sound, Play und Replay noch einen Button für das Pairing und Wasser aufnehmen einbauen. --> Pairing scheint nicht sinnvoll, da zu kompliziert
    - [ ] Bei Pairing wird die ID des aktuell verbundenen Zuges gespeichert und primär beim nächsten Connect verwendet. Wird kein Hub mit der gespeicherten ID nach 60 Sekunden gefunden wird jeder aktive Hub verbunden
    - [ ] Pairing kann aufgehoben werden durch erneutes drücken des Knopfes


### 5. Dokumentation
- [x] Dokumentation des gesamten Systems überarbeiten:
  - [x] HW Architektur
  - [x] SW Architektur
  - [x] Build und Inbetriebnahme
  - [x] Benutzerhandbuch
- [x] Design eines kindgerechten Gehäuses für den Controller