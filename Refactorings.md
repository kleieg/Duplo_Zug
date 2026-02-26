# Refactorings

Dieses Dokument beschreibt die zuletzt umgesetzten Refactorings mit direkten Links auf die relevanten Code-Stellen und der jeweiligen Begründung.

## Status (2026-02-26)

- Refactoring-Änderungen sind auf dem Branch `refactorings/facade-plan-2026-02-26` isoliert.
- `main` bleibt unverändert auf dem zuletzt gepushten Stand.
- Dokumentation wurde synchron aktualisiert in:
	- `CHANGELOG.md` (Unreleased-Eintrag)
	- `ToDo.md` (Fortschritt Code-Optimierungen + Facade-Migration)
	- `README.md` (Dateiname korrigiert, Link auf dieses Dokument)

## Übersicht

| # | Refactoring | Code-Links | Begründung |
|---|---|---|---|
| 1 | Dateinamenfehler korrigiert: `SytemMemory.cpp` → `SystemMemory.cpp` | [SystemMemory Header](src/SystemMemory.cpp#L2), [Hauptfunktion `printMemoryInfo`](src/SystemMemory.cpp#L43), [CPU-Temperaturfunktion](src/SystemMemory.cpp#L105) | Einheitliche Benennung vermeidet Verwechslungen, verbessert Auffindbarkeit und reduziert Wartungsfehler. |
| 2 | Unbenutzte globale Variablen in `TrainController.cpp` entfernt | [Bereinigter Zustandsblock](src/TrainController.cpp#L113-L122) | Nicht genutzte Zustände erzeugen Rauschen, erschweren Reviews und erhöhen das Risiko, dass veraltete Logik versehentlich wiederverwendet wird. |
| 3 | Toten Encoder-Button-Callback und veraltete Hook-Referenz entfernt | [Encoder-Initialisierung (nur `onTurned`)](src/TrainController.cpp#L651-L653) | Entfernt nicht verwendete Pfade und macht die tatsächliche Eingabelogik klarer und konsistenter. |
| 4 | Unbenutzten Wake-Up-Reinitialisierungspfad entfernt (`esp_wake_up`) | [Aktueller Main-Loop als zentraler Laufzeitpfad](src/TrainController.cpp#L865) | Reduziert redundante Wake-Logik und verhindert Divergenz zwischen mehreren Reinitialisierungswegen. |
| 5 | Header/API bereinigt: unimplementierte `DuploHub`-Methoden entfernt | [Verbleibende low-level/public API um `playSoundDirect` und Sensoraktivierung](include/DuploHub.h#L415-L430) | Header und Implementierung sind wieder konsistent; vermeidet Missverständnisse und tote API-Oberfläche. |
| 6 | Wiederholte Command-Erzeugung in `DuploHub.cpp` vereinheitlicht | [Helper `createHubCommand`](src/DuploHub.cpp#L57), [Beispielnutzung `setHubName`](src/DuploHub.cpp#L495-L501), [Beispielnutzung `activateVoltageSensor`](src/DuploHub.cpp#L692-L697) | Weniger Copy/Paste senkt Fehlerwahrscheinlichkeit bei künftigen Änderungen und hält Timestamp/Initialisierung einheitlich. |
| 7 | LED-Update-Pfad mit Dirty-Flag optimiert | [Dirty-Flag im Header](include/StatusLED.h#L30), [Konstruktor-Initialisierung](src/StatusLED.cpp#L4), [Effizientes Update](src/StatusLED.cpp#L93-L170), [Blink-Änderungen](src/StatusLED.cpp#L180-L196) | Verhindert unnötige `FastLED.show()`-Aufrufe, spart Laufzeit und vermeidet überflüssige Hardware-Updates. |
| 8 | Wasser-Button-Logik fachlich korrigiert | [BUTTON_WATER-Case](src/TrainController.cpp#L211-L214) | Entfernt unnötige Farbzustands-Änderung und korrigiert Logtext; Verhalten und Diagnostik passen nun zur tatsächlichen Aktion (Wasser-Sound). |

## Hinweise

- Diese Refactorings wurden verifiziert durch erfolgreichen Build (`pio run`).
- Die Änderungen sind bewusst funktional minimal gehalten und zielen auf Wartbarkeit, Konsistenz und Laufzeitverhalten.

## Analyse: Legoino-Funktionen in myLegoHub konsolidieren

### Ausgangslage (Ist-Zustand)

Die Idee ist sinnvoll, aber der aktuelle Code nutzt deutlich mehr als nur wenige Helfer aus Legoino/Lpf2Hub:

- `myLegoHub` erbt direkt von `Lpf2Hub`: [Klassendeklaration](include/myLegoHub.h#L83)
- DUPLO-spezifische eigene Helfer existieren bereits: [myLegoHub Implementierung](src/myLegoHub.cpp#L24-L101)
- `DuploHub` verwendet jedoch zusätzlich viele Basisfunktionen aus `Lpf2Hub` über `hub`: [Nutzung in DuploHub](src/DuploHub.cpp#L274-L1304)
  - Verbindungs-/Lifecycle-Funktionen (`init`, `connectHub`, `isConnected`, `isConnecting`)
  - Port-/Device-Funktionen (`activatePortDevice`, `getPortForDeviceType`)
  - Aktorik (`setBasicMotorSpeed`, `stopBasicMotor`, `setHubName`, LED/Sound)
  - Sensorparser (`parseColor`, `parseSpeedometer`, `parseVoltageSensor`)

Fazit: Eine „harte“ Kopie nur einzelner Methoden reicht funktional nicht aus. Für vollständige Entkopplung müssten größere Teile von `Lpf2Hub` inklusive interner Abhängigkeiten übernommen werden.

### Risikominimierter Umsetzungsplan (schrittweise)

| Schritt | Maßnahme | Kritikalität | Warum / Risikoabsicherung |
|---|---|---|---|
| 1 | Vollständige API-Inventur erstellen: welche `hub.*`-Aufrufe sind produktiv notwendig | Gering | Verhindert blinde Migration; Basis für belastbare Scope-Grenze. Referenz: [DuploHub Verwendungen](src/DuploHub.cpp#L274-L1304). |
| 2 | In `myLegoHub` eine explizite öffentliche „Projekt-API“ definieren (nur benötigte Methoden) | Mittel | Entkoppelt `DuploHub` von Legoino-Details ohne sofortiges Kopieren großer Bibliotheksteile. |
| 3 | `DuploHub` auf diese API umstellen (keine direkten impliziten Lpf2Hub-Annahmen mehr) | Mittel | Schafft klare Abhängigkeitsgrenze; Regressionen bleiben überschaubar. |
| 4 | Adapter-Implementierung in `myLegoHub`: Methoden zunächst intern an bestehende `Lpf2Hub`-Funktionen delegieren | Gering | Funktional neutraler Zwischenschritt, schnell testbar. |
| 5 | Test-/Smoke-Matrix definieren (Connect, Motor, LED, Sound, Color/Speed/Voltage, Record/Replay) und nach jedem Schritt ausführen | Hoch | Kritischer Schutz gegen BLE-/Timing-Regressionen. |
| 6 | Erst danach selektiv Quellcode aus Legoino übernehmen: nur tatsächlich benötigte Funktionen + direkte Helper | Hoch | Kontrollierte Migration, reduziert Copy-Paste-Fehler und Folgeabhängigkeiten. |
| 7 | Übernommene Funktionen in separates internes Modul kapseln (z. B. `src/internal/lego_core_*`) | Mittel | Erleichtert Updates und Diff gegen Upstream. |
| 8 | Lizenz-/Urheberhinweise sauber übernehmen (MIT-Text, Herkunft, Version/Commit) | Sehr hoch | Rechtlich zwingend bei Codeübernahme aus Drittbibliothek. |
| 9 | Legoino-Abhängigkeit erst entfernen, wenn Build + Hardwaretests vollständig grün sind | Hoch | Verhindert „half migrated“-Zustand mit instabiler Firmware. |
| 10 | Wartungsstrategie dokumentieren (wie Upstream-Fixes künftig übernommen werden) | Mittel | Verhindert, dass Sicherheits-/Bugfixes aus Legoino verloren gehen. |

### Alternativen

1. **Facade/Adapter ohne Code-Kopie (empfohlen für Phase 1)**
	- `myLegoHub` wird zur stabilen Projekt-Fassade.
	- Intern weiterhin Delegation an Legoino.
	- Vorteil: niedrigstes technisches und funktionales Risiko.

2. **Teilweise Code-Kopie (Hybrid)**
	- Nur eng abgegrenzte, DUPLO-spezifische Teile übernehmen.
	- Rest über Legoino delegieren.
	- Vorteil: Reduziert Bibliotheksbreite schrittweise, bleibt aber wartbar.

3. **Vollständige Entkopplung von Legoino (Full Fork / Full Copy)**
	- Größter Freiheitsgrad, aber höchstes Risiko und größter Wartungsaufwand.
	- Nur sinnvoll, wenn langfristig klare eigene BLE-Stack-Ownership gewünscht ist.

### Empfehlung

Empfohlen ist **Alternative 1 als Start**, danach optional **Alternative 2**:

- Zuerst eine saubere, eigene `myLegoHub`-Projekt-API etablieren und `DuploHub` dagegen programmieren.
- Anschließend nur die nachweislich benötigten Legoino-Funktionen selektiv übernehmen.
- Eine vollständige Entkopplung erst durchführen, wenn Testtiefe und Wartungskapazität gesichert sind.

Damit wird das Ziel (nur genutzte Funktionen im eigenen Modul) erreicht, ohne frühzeitig hohe BLE- und Wartungsrisiken einzugehen.

### Konkreter Umsetzungsplan für Alternative 1 (Facade)

Ziel: `DuploHub` soll nur noch gegen eine stabile Projekt-API in `myLegoHub` arbeiten, während die tatsächliche Legoino-Nutzung intern gekapselt bleibt.

| Phase | Schritt | Konkrete Umsetzung | Kritikalität | Ergebnis / Exit-Kriterium |
|---|---|---|---|---|
| A | 1. API-Baseline festlegen | Alle aktuell benötigten Methoden aus [DuploHub.cpp](src/DuploHub.cpp#L274-L1304) in eine Soll-Liste überführen (Connect, Motor, LED, Sound, Sensoren, Hub-Metadaten). | Gering | Vollständige Methodenliste vorhanden und gegen Code gegengeprüft. |
| A | 2. Facade-Vertrag definieren | In [myLegoHub.h](include/myLegoHub.h#L1-L96) eine explizite Projekt-API deklarieren (z. B. `connect()`, `isConnectedSafe()`, `setMotorSpeedSafe()`, `activateColorSensorSafe(...)`). | Mittel | Header bildet alle DuploHub-Bedarfe ab, ohne direkte Lpf2Hub-Details nach außen zu leaken. |
| B | 3. Facade-Methoden implementieren | In [myLegoHub.cpp](src/myLegoHub.cpp#L1-L101) neue Methoden als reine Delegation auf bestehende Legoino/Lpf2Hub-Aufrufe implementieren. Keine Verhaltensänderung. | Mittel | Build grün, keine funktionale Änderung, nur Umleitung. |
| B | 4. DuploHub schrittweise umstellen | Direkte `hub.*`-Aufrufe in [DuploHub.cpp](src/DuploHub.cpp#L274-L1304) nach und nach durch neue Facade-Methoden ersetzen (in kleinen Commits nach Themenblöcken). | Hoch | Keine direkten Low-Level-Aufrufe mehr in DuploHub außerhalb der Facade-Verantwortung. |
| B | 5. Legacy-Pfade isolieren | Falls einzelne Sonderfälle noch direkt `Lpf2Hub` benötigen, temporär markieren und auf Restliste führen statt ad-hoc beizubehalten. | Mittel | Transparente Restliste statt versteckter technischer Schuld. |
| C | 6. Regressionstest je Themenblock | Nach jedem Block: `pio run`, Upload, Connect/Disconnect, Motor vor/zurück/stop, LED-Farben, Sound, Sensor-Callbacks, Record/Replay. | Hoch | Alle Kernfunktionen auf Hardware unverändert funktionsfähig. |
| C | 7. Facade-Abschlussprüfung | Prüfen, dass `DuploHub` nur noch Facade-Schnittstelle konsumiert und `myLegoHub` die einzige Legoino-Kontaktstelle ist. | Mittel | Architekturziel „eine Integrationsstelle“ erreicht. |
| D | 8. Dokumentation + Wartungsregeln | Kurzdoku ergänzen: API-Owner, Namensschema, wie neue Legoino-Features nur über Facade eingeführt werden. | Gering | Klares Team-Pattern für zukünftige Änderungen etabliert. |

#### Reihenfolge zur Risikominimierung

1. Zuerst deklarieren (Header), dann delegieren (Implementierung), erst danach konsumierende Aufrufer (`DuploHub`) umstellen.
2. Umstellung in vertikalen Scheiben: zuerst reine Getter/Status, dann Aktorik, zuletzt Sensor-/Callback-Pfade.
3. Nach jeder Scheibe sofort Build + Hardwaretest; keine große „Big-Bang“-Migration.

#### Praktische Zuschnitte (empfohlene Migrations-Scheiben)

- **Scheibe 1 (niedriges Risiko):** `isConnected`, `isConnecting`, `getHubName`, `getHubAddress`
- **Scheibe 2 (mittleres Risiko):** `setHubName`, `setBasicMotorSpeed`, `stopBasicMotor`, LED/Sound
- **Scheibe 3 (höheres Risiko):** `activatePortDevice` + Sensor-Parser + Callback-Registrierung
- **Scheibe 4 (höchstes Risiko):** Connect-Lifecycle (`init`, `connectHub`) und Timing-kritische Pfade

#### Rollback-Strategie

- Jede Scheibe separat committen.
- Bei Regression nur die letzte Scheibe zurückrollen, nicht den gesamten Umbau.
- Solange Alternative 1 läuft, bleibt Legoino als stabile Runtime-Basis unangetastet.