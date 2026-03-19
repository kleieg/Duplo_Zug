# DUPLO Train Controller NG – Kurz-Anleitung

## 1. Einschalten und Verbinden

1. Controller einschalten oder mit kurzem Druck auf den Drehencoder aufwecken.
2. DUPLO-Zug einschalten.
3. Der Controller sucht automatisch nach dem Zug.
4. Sobald die Controller-LED gruen signalisiert, ist der Zug betriebsbereit.

## 2. Bedeutung der Controller-LED

- Gelb blinkend: Controller startet
- Blau blinkend: Verbindung steht, Initialisierung des Zuges laeuft noch
- Gruen blinkend: Zug ist bereit fuer Befehle
- Gruen kurze Pulse: betriebsbereit, aber gerade ohne Aktion
- Violett blinkend: Aufnahme aktiv
- Gelb blinkend: Wiedergabe aktiv
- Rot dauerhaft: Nothalt aktiv
- Weiss blinkend: Verbindung zum Zug wurde getrennt
- 5x Gruen: Controller ist gerade aus dem Sleep aufgewacht

Merksatz: Erst bei Gruen ist der Zug wirklich befehlsbereit.

## 3. Bedienung

- Drehencoder drehen: Geschwindigkeit rueckwaerts oder vorwaerts einstellen
- Drehencoder kurz druecken: Controller aus dem Sleep aufwecken
- STOP: Nothalt ein oder aus; laufende Aufnahme oder Wiedergabe wird vorher beendet
- REC: Aufnahme starten oder stoppen
- PLAY: letzte Aufnahme einmal abspielen
- LIGHT: Zug-Lichtfarbe wechseln
- SOUND: Sounds durchschalten
- WATER: Wasser-Aktion ausloesen

## 4. Sleep und Wake

- Nach 5 Minuten ohne Eingabe geht der Controller in den Sleep-Modus.
- Im Sleep ist die LED aus.
- Aufwecken nur ueber den Drehencoder-Taster.
- Die Fronttasten wecken den Controller nicht auf.

## 5. Upload-Modus

- Beim Start den Encoder mindestens 2 Sekunden dauerhaft gedrueckt halten.
- Danach oeffnet sich ein 10-Sekunden-Fenster fuer Firmware-Upload.
- Kurzes Antippen startet den Upload-Modus nicht.

## 6. Farbsteine auf der Strecke

- Gelb: Hupe
- Rot: Zug stoppen
- Gruen: stoppen und Fahrtrichtung wechseln
- Weiss: Zug-Licht ein oder aus
- Blau: Wasser-Aktion und anschliessend Weiterfahrt mit letzter Geschwindigkeit

## 7. Typische Fehlerbilder

- Keine Verbindung: Zug naeher an den Controller bringen oder Zug neu einschalten
- Zug verbunden, reagiert aber nicht: auf gruenen Ready-Zustand warten
- Controller wacht nicht auf: Encoder druecken, nicht STOP

## 8. Aktueller Stand

- Reconnect nach Aus- und Einschalten des Zuges: typischerweise 2 bis 3 Sekunden
- Nach Reconnect reagiert der Zug sofort, sobald die LED gruen signalisiert

Stand: 19.03.2026
