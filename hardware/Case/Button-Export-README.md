# Button Export Workflow (Multicolor)

This guide describes the stable workflow for multicolor buttons (base + symbol) with OpenSCAD and Orca/Anycubic Next Slicer.

## Requirements

- File: `Controller-V1.5.scad`
- Split function: `SplitColorButton(...)`
- Parameter:
  - `symbol_z = 9.05` (Default)
  - `buttonExportMode = both|base|symbol`
  - `buttonExportTarget = all|stop|water|light|music`
  - `renderController = true|false`

## Export From OpenSCAD

1. In `Controller-V1.5.scad` set:
   - `renderController = false`
   - `buttonExportTarget = stop` (or `water`, `light`, `music`)
2. Export base:
   - `buttonExportMode = "base"`
   - `F6` (Render)
   - Export as STL, e.g. `Knopf_Stop_base.stl`
3. Export symbol:
   - `buttonExportMode = "symbol"`
   - `F6` (Render)
   - Export as STL, e.g. `Knopf_Stop_symbol.stl`
4. Repeat for all buttons.

## Recommended File Names

- `Knopf_Stop_base.stl` + `Knopf_Stop_symbol.stl`
- `Knopf_Wasser_base.stl` + `Knopf_Wasser_symbol.stl`
- `Knopf_Licht_base.stl` + `Knopf_Licht_symbol.stl`
- `Knopf_Musik_base.stl` + `Knopf_Musik_symbol.stl`

## Import in Orca / Anycubic Next Slicer

1. Load `*_base.stl` first.
2. Then add `*_symbol.stl` as a part to the same object:
   - Do not load it as a new, separate object.
3. Assign filament/color to each part.
4. Save project as 3MF.

## Important Notes

- Colors from OpenSCAD `color(...)` are primarily for preview and are not the print-relevant color assignment.
- Print-relevant assignment is done in the slicer via parts/volumes and filament slots.
- If symbol and base both land at Z=0, the symbol was imported as a new object instead of as a part.
