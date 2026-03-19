# DUPLO Train Controller NG

ESP32-S3-basierter Handcontroller fuer LEGO DUPLO Zuege mit BLE-Anbindung, Replay/Record-Funktion, Sensor-Auswertung und stromsparendem Sleep/Wake-Verhalten.

## Ueberblick

Das Projekt umfasst:

- die Firmware des Handcontrollers
- die Schaltplan- und PCB-Daten
- die OpenSCAD- und 3D-Druckdaten fuer das Gehaeuse

Die Firmware laeuft auf einem ESP32-S3-Board mit RGB-Status-LED, 6 Tastern ueber ADC-Widerstandsleiter und einem Drehencoder als Fahrregler.

## Aktueller Funktionsstand

- automatische BLE-Verbindung zum DUPLO-Zug
- Reconnect nach Zug-Power-Cycle in der Praxis ca. 2 bis 3 Sekunden
- Befehlsannahme direkt nach Ready-Zustand
- thread-sichere Trennung von UI-Logik und BLE-Kommunikation
- Record/Replay von Bedienfolgen mit Zeitstempeln
- Sensorverarbeitung fuer Farbe, Geschwindigkeit und Spannung
- Farb-Sensor-Entprellung ueber Stabilitaets-Timer
- Sleep nach 5 Minuten Inaktivitaet
- Wake ueber Encoder-Taster, inkl. RTC- und GPIO-Fallback
- Upload-Fenster nur bei bewusstem Encoder-Hold waehrend des Starts

## Status-LED

Die Controller-LED signalisiert den aktuellen Zustand wie folgt:

- Gelb blinkend: Controller startet
- Blau blinkend: BLE-Verbindung steht, Hub-Initialisierung laeuft noch
- Gruen blinkend: Hub ist betriebsbereit und Befehle koennen ausgefuehrt werden
- Gruen gelegentliche kurze Pulse: betriebsbereit und idle
- Violett blinkend: Aufnahme aktiv
- Gelb blinkend: Replay aktiv
- Rot dauerhaft: Nothalt aktiv
- Rot kurze Blinkfolge: Low-Voltage-Warnung des Zuges
- Weiss blinkend: Hub wurde getrennt
- 5x Gruen: Wake aus Sleep bestaetigt
- 2x Rot: Sleep-Eintritt wird angezeigt

Wichtig: Gruen bedeutet jetzt explizit nicht nur "verbunden", sondern "bereit fuer Befehle".

## Bedienung

- Encoder drehen: Geschwindigkeit rueckwaerts, Stopp, vorwaerts
- Encoder kurz druecken: Wake aus Sleep
- Encoder beim Start >= 2 s halten: 10-Sekunden-Upload-Fenster
- REC: Aufnahme starten/stoppen
- PLAY: letzte Aufnahme einmal abspielen
- STOP: Replay bzw. Aufnahme beenden und Nothalt toggeln
- LIGHT: Zugfarbe durchschalten
- SOUND: Sounds durchschalten
- WATER: Wasser-Aktion ausloesen

## Farbsteine

- Gelb: Hupe
- Rot: Zug stoppen
- Gruen: stoppen und Fahrtrichtung wechseln
- Weiss: Licht toggeln
- Blau: Wasser-Aktion, danach Weiterfahrt mit letzter Geschwindigkeit

## Architektur in Kurzform

- Core 0: BLE-Task (`DuploHub`)
- Core 1: Arduino-Loop (`TrainController`)
- Main Loop -> `commandQueue` -> BLE-Task
- BLE-Task -> `responseQueue` -> Main Loop

Details stehen in [ARCHITECTURE.md](ARCHITECTURE.md).

## Projektstruktur

- [src](src): projekt-eigene Implementierung
- [include](include): projekt-eigene Header
- [hardware](hardware): Schaltplan, PCB, Gehaeuse und Hardware-Doku
- [lib/Legoino](lib/Legoino): vendorte Legoino-Bibliothek mit projektspezifischen Anpassungen
- [lib/NimBLE-Arduino](lib/NimBLE-Arduino): vendorte BLE-Bibliothek

## Build

Hauptumgebung:

```bash
platformio run -e esp32-s3-fh4r2
```

Upload:

```bash
platformio run -e esp32-s3-fh4r2 --target upload
```

Serieller Monitor:

```bash
platformio device monitor --environment esp32-s3-fh4r2
```

Diagnose-Umgebung fuer den ADC-Monitor:

```bash
platformio run -e esp32-s3-input-monitor
```

## Hardwarevoraussetzungen

- ESP32-S3-Board mit RGB-LED und USB-C-Ladefunktion
- 6 Taster am ADC-Widerstandsnetzwerk
- 1 Drehencoder mit Drucktaster
- LEGO DUPLO Zug-Hub

## Implementierungsdetails

- `TrainController.cpp` enthaelt die komplette Anwendungslogik, Eingabeverarbeitung, Sleep/Wake und UI-Status.
- `DuploHub.cpp` kapselt BLE-Lifecycle, Queue-Verarbeitung, Sensor-Routing und Replay/Record.
- `myLegoHub.cpp` enthaelt DUPLO-spezifische Legoino-Helfer.
- Die Hub-Initialisierung nach Connect laeuft nicht blockierend im Main Loop, damit der BLE-Task nicht blockiert.
- Reconnect-Scans laufen mit kuerzerem Suchfenster; dadurch wurde die reale Wiederverbindungszeit deutlich reduziert.

## Dokumentation

- [ARCHITECTURE.md](ARCHITECTURE.md): technische Architektur und Laufzeitmodell
- [Benutzerhandbuch_Kurz_DE.md](Benutzerhandbuch_Kurz_DE.md): kompakte Bedienanleitung
- [hardware/Hardware.md](hardware/Hardware.md): Hardware-Hintergrund
- [Kommunikation intern/Protokolle/Projekt-Tagesprotokoll.md](Kommunikation%20intern/Protokolle/Projekt-Tagesprotokoll.md): Arbeitsprotokoll

## Offene Themen

Aktuell noch relevant sind vor allem:

- weitere Facade-Konsolidierung ueber `myLegoHub`
- moegliche Pairing-/Praeferenzlogik fuer einen bevorzugten Zug
- Hardwareseitige Controller-Batterieueberwachung

Die detailierte Arbeitsliste steht in [ToDo.md](ToDo.md).
