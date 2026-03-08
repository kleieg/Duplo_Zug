/* [Input] */
stl_file = "Knopf_Stop.stl";
z_cut = 6.0;

/* [Output] */
mode = "preview"; // [preview, base, symbol]

/* [Tuning] */
overlap = 0.05;
big = 2000;

$fn = 100;

module src_mesh() {
    import(stl_file, convexity = 10);
}

module lower_cut() {
    intersection() {
        src_mesh();
        translate([-big / 2, -big / 2, -big / 2])
            cube([big, big, big / 2 + z_cut + overlap], center = false);
    }
}

module upper_cut() {
    intersection() {
        src_mesh();
        translate([-big / 2, -big / 2, z_cut - overlap])
            cube([big, big, big / 2], center = false);
    }
}

if (mode == "preview") {
    color("black")
        lower_cut();
    color("white")
        upper_cut();
}

if (mode == "base") {
    lower_cut();
}

if (mode == "symbol") {
    upper_cut();
}