/**
* MachineBlocks
* https://machineblocks.com/examples/boxes-enclosures
*
* Channel 12x3
* Copyright (c) 2022 - 2025 Jan Philipp Knoeller <pk@pksoftware.de>
*
* Published under license:
* Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International 
* https://creativecommons.org/licenses/by-nc-sa/4.0/
*
*/
use <lib/block.scad>;

/* [View] */
// How to view the brick in the editor
viewMode = "print"; // [print, assembled, cover]

/* [Button Export] */
// "both" = Vorschau/Normalbetrieb, "base" = nur Grundkörper, "symbol" = nur Symbol-Erhebung
buttonExportMode = "both"; // [both, base, symbol]
// false = Knöpfe als einteiliges STL (empfohlen für Farbwechsel über Layer)
// true  = Knöpfe in Grundkörper/Symbol splitten
splitImportedButtons = false;
// Beim einteiligen STL-Import: dünne Verschweißung am Symbol-Übergang erzeugen,
// damit der Slicer den Knopf als EIN Objekt erkennt.
fuseImportedButtons = true;
fuseSymbolZ = 9.05;
fuseHeight = 0.12;
// Optional nur einen bestimmten importierten Knopf ausgeben
buttonExportTarget = "all"; // [all, stop, water, light, music]
// Für STL-Export der Knöpfe: auf false setzen, damit Gehäuse/Lid nicht mit exportiert werden
renderController = true;
// Export-Checkliste (für jeden Ziel-Button):
// 1) renderController = false
// 2) buttonExportTarget = stop|water|light|music
// 3) buttonExportMode = base   -> exportiere als: Knopf_<Name>_base.stl
// 4) buttonExportMode = symbol -> exportiere als: Knopf_<Name>_symbol.stl
// 5) Im Slicer zuerst *_base laden, dann *_symbol als Part zum gleichen Objekt hinzufügen
// Empfohlene Dateinamen:
// Knopf_Stop_base.stl   + Knopf_Stop_symbol.stl
// Knopf_Wasser_base.stl + Knopf_Wasser_symbol.stl
// Knopf_Licht_base.stl  + Knopf_Licht_symbol.stl
// Knopf_Musik_base.stl  + Knopf_Musik_symbol.stl
// Hinweis: Wenn splitImportedButtons = false, wird buttonExportMode ignoriert.

/* [Size] */
/*
    Das Controller board muss ca. 2-3mm  smaller sein damit es auch in ein Gehäuse 
    mit 5x16 Noppen und 5 Platten Höhe passt 
*/

// Box size in X-direction specified as multiple of an 1x1 brick.
boxSizeX = 16; // [1:32] 
// Box size in Y-direction specified as multiple of an 1x1 brick.
boxSizeY = 6; // [1:32] 
// Total box height specified as number of layers. Each layer has the height of one plate.
boxLayers = 6; // [1:24]

/* [Appearance] */

// Type of cut-out on the underside.
baseCutoutType = "classic"; // [none, classic]
// Whether the base should have knobs
baseKnobs = false;
// Type of the base knobs
baseKnobType = "classic"; // [classic, technic]
// Whether base knobs should be centered.
baseKnobCentered = false;
// Whether the pit should contain knobs
basePitKnobs = false;
// Type of the base pit knobs
basePitKnobType = "classic"; // [classic, technic]
// Whether base pit knobs should be centered.
basePitKnobCentered = false;
// Pit wall thickness
basePitWallThickness = 0.333;
// Pit wall gaps
basePitWallGaps = [[0, 0, 0], [0, 0, 0]];
// Whether the base should have a tongue
baseTongue = true;

// Whether the box should have a lid
lid = true;
// Lid height specified as number of layers. Each layer has the height of one plate.
lidLayers = 1; // [1:24]
// Whether the lid should have knobs
lidKnobs = false;
// Type of the lid knobs
lidKnobType = "classic"; // [classic, technic]
// Whether lid knobs should be centered.
lidKnobCentered = false;
// Whether lid should have pillars
lidPillars = true;
// Whether lid should be permanent (non removable)
lidPermanent = true;

/* [Quality] */

// Quality of the preview in relation to the final rendering.
previewQuality = 0.5; // [0.1:0.1:1]
// Number of drawn fragments for roundings in the final rendering.
roundingResolution = 64; // [16:8:128]

/* [Calibration] */

//Adjustment of the height (mm)
baseHeightAdjustment = 0.0;
//Adjustment of each side (mm)
baseSideAdjustment = -0.1;
//Diameter of the knobs (mm)
knobSize = 5.0;
//Thickness of the walls (mm)
wallThickness = 1.5;
//Diameter of the Z-Tubes (mm)
tubeZSize = 6.4;


$fn = 200;

// Koordinatenpositionen vom Ursprungin Mitte des Deckels
//l = 127.5;  // Laenge 16 Noppen aussen
l = boxSizeX * 8 - 0.2;
echo("Laenge = ",l);
//b = 39.7;   // Breite 5 Noppen aussen
b = boxSizeY * 8 - 0.2;
echo("Breite = ", b);
//h = boxLayers * 3.2;
//echo("Hoehe = ", h);
delta = 1;  // Abstand vom Innenwand

function toX0(x) = l/2 - wallThickness - delta - x;


// Alle x Positionen von oberer Kante

// Drehencoder 
enc_x = toX0(8.5); 
enc_y = 0;       
enc_r = 3.6;

// RGB LED
led_x = toX0(27.0);
led_y = 0;
led_r = 1;

// Reset Hole
rst_x = led_x;
rst_y = 9;
rst_r = 1.5;

// Reset Button
rst_r1 = rst_r - 0.25;
rst_h1 = 5;
rst_r2 = rst_r + 1;
rst_h2 = 1.5;


// Ausbruch für Knöpfe
btn_dx = 14.8;
btn_dy = 14.8;


// Button (1,1)  Record
btn1_x = toX0(53.6);
btn1_y = 10;

// Button (1,2)  Play
btn2_x = toX0(53.6);
btn2_y = btn1_y - 20 ;

// Button (2,1)  Stop
btn3_x = toX0(53.6 + 20);
btn3_y = btn1_y ;

// Button (2,2)  LED
btn4_x = toX0(53.6 + 20);
btn4_y = btn1_y - 20;

// Button (3,1)  Water
btn5_x = toX0(53.6 + 2 * 20);
btn5_y = btn1_y;

// Button (3,2)  Sound
btn6_x = toX0(53.6 + 2 * 20);
btn6_y = btn1_y - 20;

// USB Typ C links
usb_x = toX0(32);  // mitte          
usb_z = 13.5;
usb_dz = 9.5;
usb_dx = 12;



module makeHole(x,y,r)
{
    echo("makeHole: Variable x = ", x);
    echo("Variable y = ", y);
    color([1,0,0])
        translate([x, y, 1])
            cylinder(10,r,r,true);
}


module makeRectHole(x,y,dx,dy)
{
    echo("makeRectHole: Variable x = ", x);
    echo("Variable y = ", y);
    color([1,0,0])
        translate([x, y, 0])
            cube([dx, dy, 10], center=true);
}


module makeLeftHole(x,z,dx,dz)
{
    echo("makeLeftHole: Variable x = ", x);
    echo("Variable z = ", z, "dz =", dz);
    color([1,0,0])
        translate([x, b/2-2, z])
            cube([dx, 5, dz], center = true);
}


// Druckknöpfe
module button(farbe)
{
    color(farbe)
        {
            cube([16, 16, 2], center = true);
            translate([0,0,4])
                cube([14.5, 14.5, 8], center = true);
        }
}


module ButtonRecord(farbe){
    union(){
        button(farbe);
        translate([0,0,8])
            color([1,0,0])
                cylinder(1,3,3,true);
    }
}

module ButtonPlay(farbe){
    union(){
        button(farbe);
        translate([0,0,8])
            color([1,1,1])
                cylinder(1,4,4,true, $fn=3);
    }
}

// Knopf für Drehencoder
module Knob(farbe)
{
    height = 13;
    r1 = 12;
    r2 = 10;
    eps = -0.15;   // Klemmung bei Materialschrupfung um 0,2mm
    w_wall = 2.5;
    r_hole = 3 - eps;
    
   color(farbe)  
        {
            difference(){
                cylinder(height + 5,r1,r2,true);
                translate([0,0,-2])
                    cylinder(height+5,r1-w_wall,r2-w_wall,true);
           }
            translate([0,0,2.5])difference(){
                cylinder(height, r_hole + w_wall, r_hole + w_wall, true);
                translate([0,0,-w_wall])
                   cylinder(height,r_hole,r_hole, true);
            }
    }
}


/*
 * SplitColorButton
 *
 * Schneidet ein importiertes STL in der Z-Achse in zwei getrennte Teilobjekte
 * und färbt beide Teile unterschiedlich ein.
 *
 * - Basisobjekt: gesamte Geometrie bis einschließlich symbol_z (+ overlap)
 * - Symbolobjekt: Geometrie ab symbol_z (- overlap), nur die aufgelegte Erhebung
 *
 * Parameter:
 * stlFile     : Dateiname/Pfad der STL-Datei
 * baseColor   : Farbe des Knopfkörpers
 * symbolColor : Farbe des Symbols (aufgelegte Erhebung)
 * symbol_z    : Start-Höhe des Symbols in mm (Default: 9.05)
 * overlap     : kleiner Überlappungsbereich gegen Spalte/Z-Fighting
 * big         : Größe des Clipping-Quaders (muss deutlich größer als das Teil sein)
 */
module SplitColorButton(stlFile, baseColor, symbolColor, symbol_z = 9.05, overlap = 0.02, big = 2000, part = "both")
{
    module _src() {
        import(stlFile, convexity = 10);
    }

    module _basePart() {
        intersection() {
            _src();
            translate([-big / 2, -big / 2, -big / 2])
                cube([big, big, big / 2 + symbol_z + overlap], center = false);
        }
    }

    module _symbolPart() {
        intersection() {
            _src();
            translate([-big / 2, -big / 2, symbol_z - overlap])
                cube([big, big, big / 2], center = false);
        }
    }

    union() {
        if (part == "both" || part == "base") {
            color(baseColor)
                _basePart();
        }
        if (part == "both" || part == "symbol") {
            color(symbolColor)
                _symbolPart();
        }
    }
}

module ImportedButtons(part = "both", target = "all")
{
    module _fusedSingleImport(stlFile, baseColor, symbol_z = fuseSymbolZ, bridge_h = fuseHeight, big = 2000) {
        color(baseColor)
            union() {
                import(stlFile, convexity = 10);

                if (fuseImportedButtons) {
                    translate([0, 0, symbol_z - bridge_h / 2])
                        linear_extrude(height = bridge_h, center = true)
                            projection(cut = true)
                                translate([0, 0, -symbol_z])
                                    import(stlFile, convexity = 10);
                }
            }
    }

    module _renderImported(stlFile, baseColor, symbolColor) {
        if (splitImportedButtons)
            SplitColorButton(stlFile, baseColor, symbolColor, part = part);
        else
            _fusedSingleImport(stlFile, baseColor);
    }

    translate([-8, -8, 0])
    {
        // Stop Button
        if (target == "all" || target == "stop")
            translate([-60,50,0])
                _renderImported("Knopf_Stop.stl", "red", "white");

        // Water Button
        if (target == "all" || target == "water")
            translate([-90,50,0])
                _renderImported("Knopf_Wasser.stl", "blue", "white");

        // LED Button
        if (target == "all" || target == "light")
            translate([-120,50,0])
                _renderImported("Knopf_Licht.stl", "white", "black");

        // Sound Button
        if (target == "all" || target == "music")
            translate([-150,50,0])
                _renderImported("Knopf_Musik.stl", "yellow", "black");
    }
}




module case() {
    color("yellow");
    difference() {
        block(
            grid=[boxSizeX, boxSizeY],
            baseLayers = boxLayers - (lid ? lidLayers : 0),
            baseCutoutType = baseCutoutType,

            knobs = baseKnobs,
            knobType = baseKnobType,
            knobCentered = baseKnobCentered,
            
            pit=true,
            //pitWallGaps = basePitWallGaps, //boxType != "box" ? (boxType == "channel_corner" ? [ [ 0, 0, 0 ], [ 2, 0, 0 ] ] : [ [ 0, 0, 0 ], [ 1, 0, 0 ] ]) : [],
            pitWallThickness = basePitWallThickness,
            pitKnobs = basePitKnobs,
            pitKnobType = basePitKnobType,
            pitKnobCentered = basePitKnobCentered,

            tongue = baseTongue,
            tongueHeight = lidPermanent ? 2.0 : 1.8,
            tongueClampThickness = lidPermanent ? 0.1 : 0,
            tongueOuterAdjustment = lidPermanent ? 0.0 : -0.1,
            tongueRoundingRadius = lidPermanent ? 0.0 : 0.4,
            
            baseHeightAdjustment = baseHeightAdjustment,
            baseSideAdjustment = baseSideAdjustment,
            knobSize = knobSize,
            wallThickness = wallThickness,
            tubeZSize = tubeZSize
        );
        makeLeftHole(usb_x,usb_z,usb_dx,usb_dz);
    }
}


module steg(l, b, h)
{
    rotate([0,0,90])
     cube([l,b,h], center=true);
}

s_dz = -4;  // Steg z-offset 
s_h = 6.5;  // Steg Hoehe


module ResetBtn(r1,h1,r2,h2){
    color("Black") union(){
        cylinder(h2, r2,r2);
        translate([0,0,h2])
            cylinder(h1,r1,r1);
    }

}

module lid() {
    color("yellow");
    translate(viewMode != "print" ? [0, 0, ((boxLayers - lidLayers) + (viewMode == "cover" ? 2*lidLayers : 0)) * 3.2] : [boxSizeX > boxSizeY ? 0 : (boxSizeX + 0.5) * 8.0, boxSizeX > boxSizeY ? -(boxSizeY + 0.5) * 8.0 : 0, 0])
    {
            difference() {
            union () {
            block(
                grid=[boxSizeX, boxSizeY],
                baseLayers = lidLayers,
                baseCutoutType = lidPermanent ? "groove" : "classic",

                knobs = lidKnobs,
                knobType = lidKnobType,
                knobCentered = lidKnobCentered,

                pillars = lidPillars,
                //pitWallGaps = basePitWallGaps, //boxType != "box" ? (boxType == "channel_corner" ? [ [ 0, 0, 0 ], [ 2, 0, 0 ] ] : [ [ 0, 0, 0 ], [ 1, 0, 0 ] ]) : [],

                baseHeightAdjustment = baseHeightAdjustment,
                baseSideAdjustment = baseSideAdjustment,
                knobSize = knobSize,
                wallThickness = wallThickness,
                tubeZSize = tubeZSize
            );
            
            // Dicke der Platte unterhalb der Buttons um 1.5mm erhöhen für bessere Führung der Knöpfe
            color("green") 
                translate([-44, -20, -1.45])
                    cube([82, 40, 1.5]);
            
            translate([-43, 0, s_dz])
                steg(30, 2, s_h);        
            translate([-22, 0, s_dz])
                steg(30, 2, s_h);
            translate([-2, 0, s_dz])
                steg(30, 2, s_h);
            translate([20, 0, s_dz])
                steg(30, 2, s_h);
           }
            makeHole(enc_x, enc_y, enc_r);
            makeHole(led_x, led_y, led_r);
            makeHole(rst_x, rst_y, rst_r);
            makeRectHole(btn1_x, btn1_y, btn_dx, btn_dy);
            makeRectHole(btn2_x, btn2_y, btn_dx, btn_dy);
            makeRectHole(btn3_x, btn3_y, btn_dx, btn_dy);
            makeRectHole(btn4_x, btn4_y, btn_dx, btn_dy);
            makeRectHole(btn5_x, btn5_y, btn_dx, btn_dy);
            makeRectHole(btn6_x, btn6_y, btn_dx, btn_dy);
        }
    }
}

if (renderController) {
    translate([20,50,0])
        ResetBtn(rst_r1, rst_h1, rst_r2, rst_h2);

    translate([50,50,0]) 
        Knob("red");
    translate([0,50,0])
        ButtonRecord("black");
    translate([-30,50,0])
        ButtonPlay("black");

    ImportedButtons("both", "all");

    // Unterteil
    case();

    // Deckel
    if(lid){
        lid();
    }
}
else {
    ImportedButtons(buttonExportMode, buttonExportTarget);
}