# Button Export Workflow (Mehrfarbig)

Diese Anleitung beschreibt den stabilen Workflow für mehrfarbige Knöpfe (Basis + Symbol) mit OpenSCAD und Orca/Anycubic Next Slicer.

## Voraussetzungen

- Datei: `Controller-V1.5.scad`
- Split-Funktion: `SplitColorButton(...)`
- Parameter:
  - `symbol_z = 9.05` (Default)
  - `buttonExportMode = both|base|symbol`
  - `buttonExportTarget = all|stop|water|light|music`
  - `renderController = true|false`

## Export aus OpenSCAD

1. In `Controller-V1.5.scad` setzen:
   - `renderController = false`
   - `buttonExportTarget = stop` (oder `water`, `light`, `music`)
2. Basis exportieren:
   - `buttonExportMode = "base"`
   - `F6` (Render)
   - Export als STL, z. B. `Knopf_Stop_base.stl`
3. Symbol exportieren:
   - `buttonExportMode = "symbol"`
   - `F6` (Render)
   - Export als STL, z. B. `Knopf_Stop_symbol.stl`
4. Für alle Buttons wiederholen.

## Empfohlene Dateinamen

- `Knopf_Stop_base.stl` + `Knopf_Stop_symbol.stl`
- `Knopf_Wasser_base.stl` + `Knopf_Wasser_symbol.stl`
- `Knopf_Licht_base.stl` + `Knopf_Licht_symbol.stl`
- `Knopf_Musik_base.stl` + `Knopf_Musik_symbol.stl`

## Import in Orca / Anycubic Next Slicer

1. Zuerst `*_base.stl` laden.
2. Danach `*_symbol.stl` als Teil (Part) zum gleichen Objekt hinzufügen:
   - Nicht als neues, separates Objekt laden.
3. Jedem Part Filament/Farbe zuweisen.
4. Projekt als 3MF speichern.

## Wichtige Hinweise

- Farben aus OpenSCAD `color(...)` sind primär Vorschau und nicht die druckrelevante Farbzuweisung.
- Die druckrelevante Zuordnung erfolgt im Slicer über Parts/Volumes und Filament-Slots.
- Wenn Symbol und Basis beide auf Z=0 landen, wurde das Symbol als neues Objekt statt als Part importiert.
