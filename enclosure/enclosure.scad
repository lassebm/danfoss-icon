// Danfoss Icon node — 3D-printable enclosure
// ===========================================
// Parametric enclosure for the ESP32-C3 node described in docs/HARDWARE.md:
//   - 1x ESP32-C3 Super Mini
//   - 2x half-duplex RS-485 auto-direction transceiver modules
//   - 1x double RJ45 breakout, mounted so its sockets face out a wall cutout
//
// The three PCBs (ESP32 + 2x RS-485) stand on edge against the long side walls
// (ESP32 + one RS-485 on the RIGHT, the other RS-485 on the LEFT, opposite it),
// pinned to the walls by small retaining tabs (no card guides); connectors face
// the central channel. Snap-fit lid, no USB cutout (open the lid to flash), a vent
// grille in the lid, and four external ears at the base for flush wall mounting.
//
// HOW TO USE
//   1. Measure YOUR parts with calipers and update the MEASUREMENTS block below.
//      Values marked  // MEASURE  are real-world dimensions to verify.
//   2. Set `part` and render (F5 preview / F6 render, export STL).
//   3. Snap fit is printer-dependent — print the lid first and tune snap_depth /
//      fit_clear.
//
// Coordinates: origin at inner-floor front-left corner. +X right, +Y back (away
// from the RJ45 wall), +Z up. RJ45 breakout sits against the FRONT (-Y) wall;
// the standing boards run their long edge along Y against the +/-X side walls.

// ------------------------------------------------------------------
// WHAT TO RENDER
// ------------------------------------------------------------------
part = "preview";   // "preview" | "base" | "lid" | "both"
show_boards = true;     // in "preview", draw translucent board placeholders
show_connectors = true; // in "preview", also draw the header+dupont clearance boxes

// ------------------------------------------------------------------
// MEASUREMENTS  (edit these — see docs/HARDWARE.md for the parts)
// ------------------------------------------------------------------

// -- ESP32-C3 Super Mini (stands on its long edge) -----------------
esp_l      = 24.8;  // MEASURE long edge  -> horizontal (along X)
esp_stand  = 23.4;  // MEASURE short edge -> vertical standing height

// -- RS-485 transceiver module (x2, stand on their long edge) ------
rs_l       = 42.8;  // MEASURE long edge  -> horizontal (along X)
rs_stand   = 16.8;  // MEASURE short edge -> vertical standing height

// Clearance on the connector side of each standing board for the pin header +
// seated dupont connector + wire bend. Two boards face inward from opposite
// walls, so this drives the enclosure WIDTH (see inner_x below).
conn_clear = 20.0;  // MEASURE header+connector+wire depth on one face

// -- Double RJ45 breakout (lies against the front wall) ------------
rj_l       = 37.4;  // MEASURE board length (along the front wall, X)
rj_w       = 28.2;  // MEASURE board depth  (whole board into the enclosure, Y)
rj_jack_d  = 18.4;  // MEASURE jack-housing depth from the front wall (Y)
rj_h       = 16.8;  // MEASURE floor to top of the jack housing (Z) — lid tab lands here
rj_win_w   = 31.9;  // MEASURE cutout width for the two jack faces (both jacks)
rj_win_h   = 12.0;  // MEASURE cutout height for the jack faces
rj_win_z   = 3.65;  // MEASURE jack-window bottom above the floor
rj_win_gap = 4.0;   // MEASURE gap between the two jacks -> center divider so the
                    // top prints as two short bridges (0 = one wide window)

// ------------------------------------------------------------------
// ENCLOSURE / FIT PARAMETERS
// ------------------------------------------------------------------
wall_t      = 2.0;  // wall thickness
floor_h     = 2.0;  // floor thickness
lid_h       = 2.0;  // lid plate thickness
corner_r    = 3.0;  // outer corner radius (vertical edges)

fit_clear = 0.4; // slack for the lid fit and RJ45 width (loosen if the lid is tight)
top_clear = 5.5; // air gap above the tallest board, under the lid (sets wall height)

// RJ45 pocket ribs (the standing boards use no guides)
rj_rib_h    = 6.0;  // side-rib height (seats the board against the front wall)
rj_rib_t    = 1.6;  // rib wall thickness
front_gap   = 3.0;  // gap between RJ45 breakout and the first standing board

// RJ45 retention: drop-in pocket (front wall + side ribs + back rib) plus a
// hold-down tab on the lid underside that presses the top of the JACK HOUSING
// (the rigid part, within rj_jack_d of the wall) when the lid is closed.
rj_backrib_h     = 4.0;  // height of the rib behind the RJ45 board (stops push-in;
                         // kept low to clear the dupont cables in the screw terminals)
rj_hold_len_frac = 0.6;  // lid hold-down tab length as a fraction of the board length
rj_hold_tip      = 4.0;  // tab thickness at the contact face (along Y)
rj_hold_root     = 9.0;  // tab thickness where it meets the lid (tapers to the tip)
rj_hold_pos_frac = 0.6;  // tab centre as a fraction of the jack-housing depth (from wall)
rj_hold_load     = -0.3; // minimal downward preload on the jack (nominal; print stackup
                         // adds a little). Light touch to settle the jack -> only a tiny lid gap.

// Snap-fit lid
lip_h        = 6.0;  // depth the lid plug lip reaches into the enclosure
lip_t        = 1.6;  // thickness of the plug lip
snap_depth   = 0.9;  // how far the snap tooth protrudes / engages (deeper = firmer/harder to open)
snap_h       = 1.8;  // tooth height; ~2x snap_depth keeps both faces near 45 deg
snap_z       = 4.0;  // tooth center below the top rim
snap_l       = 10.0; // length of each snap tooth along the wall
snap_n_short = 2;    // teeth per SHORT wall (front/back)
snap_n_long  = 2;    // teeth per LONG wall (sides) — hold the long lid edges down

// Ventilation — a fine slot grille in the lid over the central cable channel
// (heat rises out, keeps the PCBs out of sight/dust). Set lid_vents=false to seal it up.
lid_vents   = true;
vent_w      = 1.6;  // slot width (thin)
vent_gap    = 3.0;  // gap between slots (and crossbar width between rows)
vent_rows   = 2;    // split each lid slot along its length into N (stiffens the ribs)
vent_frac   = 0.6;  // fraction of the span covered by the slot band

// Wall-mount ears — flat flanges at the base with a screw hole, for flush
// surface mounting. Two per long side (+/-X), inset from the ends (4 total).
mount_ears    = true;
mount_ext     = 7.0;   // how far the ear reaches out past the wall (X)
mount_ear_w   = 12.0;  // ear width along the wall (Y)
mount_ear_h   = 3.0;   // ear/flange thickness (Z)
mount_hole_d  = 3.4;   // screw clearance hole (M3 -> 3.4)
mount_inset   = 13.0;  // ear centre distance in from each end (Y)
mount_gusset  = 7.0;   // gusset height up the wall for strength (0 = none)

// Board retaining tabs — small posts on the centre-facing side of each standing
// board that pin it against its wall (the tab lands on the board's back face; the
// header pins/wires pass beyond it toward the centre). Offset = wall -> tab face.
board_tabs  = true;
esp_tab_off = 4.1;  // ESP32:  tab distance from the RIGHT wall
rs_tab_off  = 3.4;  // RS-485: tab distance from its wall
tab_w       = 10.0; // tab width along the board (Y)
tab_h       = 8.0;  // retaining-face height from the floor (Z)
tab_top     = 2.5;  // flat top length toward the centre (before the brace slopes down)
tab_base    = 6.0;  // total buttress length toward the centre at the floor (braces the tab)

// Lid grip — a tiny channel in the UNDERSIDE of the jack-end edge, running inward
// from the outer edge, so a nail slides under the lip and catches to lift the lid.
lid_grip = true;
grip_w   = 8.0;   // channel width along the edge (X)
grip_l   = wall_t + fit_clear/2;  // reach in from the outer edge (Y) — ends right
                  // against the lip's outer face, without running over/past the lip
grip_d   = 0.7;   // channel depth up into the lid underside (Z; shallow keeps more
                  // plate above to take the upward nail force)

$fn = 48;

// ==================================================================
// LAYOUT
// ==================================================================
// The three PCBs stand on edge, pinned to the walls by small retaining tabs:
//   RIGHT (+X) long wall: ESP32 then one RS-485, end-to-end along Y
//   LEFT  (-X) long wall: the other RS-485, directly opposite the right one
// Each board's face is parallel to its wall; connectors face the central channel.
// The RJ45 breakout sits in its pocket against the FRONT (-Y) wall.

board_t_eff   = 2.5;  // per-board width allowance for inner_x (standoff set by *_tab_off)
row_gap       = 3.0;  // gap between the ESP32 and RS-485 sharing the RIGHT wall (Y)
end_margin    = 2.0;  // clear space behind the last board (Y)
wall_slack    = 0.5;  // gap between a board face and the wall it leans on (X)
mid_gap       = 10.0; // clear gap in the centre between the two connector fields

// One wall's board stack across the width: slack + board + inward connectors.
per_wall = wall_slack + board_t_eff + conn_clear;

// Front zone reserved for the RJ45 breakout.
front_zone = rj_w + front_gap;

// Longest board row along Y (the ESP32 + RS-485 pair sharing one wall).
board_zone = esp_l + row_gap + rs_l;

max_stand = max(esp_stand, rs_stand);

// Interior extents. Width = two facing board stacks + a central gap; must also
// clear the RJ45 board across the front wall.
inner_x = max(2*per_wall + mid_gap, rj_l + fit_clear + 2*rj_rib_t);
inner_y = front_zone + board_zone + end_margin;
inner_z = max(max_stand, rj_h) + top_clear;

// Outer envelope
out_x = inner_x + 2*wall_t;
out_y = inner_y + 2*wall_t;
out_z = floor_h + inner_z;

// ==================================================================
// HELPERS
// ==================================================================
module rrect(x, y, r) {
  hull() {
    translate([r, r])         circle(r);
    translate([x - r, r])     circle(r);
    translate([r, y - r])     circle(r);
    translate([x - r, y - r]) circle(r);
  }
}

// N positions evenly spread along a wall of the given length (interior spacing).
function spread(n, len) = [for (i = [0:n-1]) len*(i + 1)/(n + 1)];

// ==================================================================
// BASE
// ==================================================================
module base() {
  difference() {
    linear_extrude(out_z) rrect(out_x, out_y, corner_r);   // outer shell

    // Inner cavity (open top)
    translate([wall_t, wall_t, floor_h])
      linear_extrude(out_z)
        rrect(inner_x, inner_y, max(0.1, corner_r - wall_t));

    // RJ45 jack windows in the FRONT (-Y) wall. A center divider (rj_win_gap)
    // splits it into two openings so the top prints as two short bridges instead
    // of one wide one; the divider is a full-height strip of wall in the jack gap.
    rj_win_half = (rj_win_w - rj_win_gap) / 2;
    for (x0 = [wall_t + inner_x/2 - rj_win_gap/2 - rj_win_half,
               wall_t + inner_x/2 + rj_win_gap/2])
      translate([x0, -0.1, floor_h + rj_win_z])
        cube([rj_win_half, wall_t + 0.2, rj_win_h]);

    // Snap notches (receive the lid teeth)
    snap_grooves();
  }

  // RJ45 pocket: a connected U of ribs the board drops into (clear gap between
  // the side ribs = rj_l + fit_clear). The side ribs run from the front wall
  // back to the back rib, and the back rib spans the full outer width, so the
  // corners overlap and brace each other instead of just touching.
  rj_half  = rj_l/2 + fit_clear/2;
  rj_depth = rj_w + rj_rib_t;                       // side ribs reach the back rib
  for (sx = [wall_t + inner_x/2 - rj_half - rj_rib_t, wall_t + inner_x/2 + rj_half])
    translate([sx, wall_t, floor_h]) cube([rj_rib_t, rj_depth, rj_rib_h]);
  translate([wall_t + inner_x/2 - rj_half - rj_rib_t, wall_t + rj_w, floor_h])
    cube([2*rj_half + 2*rj_rib_t, rj_rib_t, rj_backrib_h]);

  // Board retaining tabs (pin each standing board against its wall)
  if (board_tabs) {
    board_tab(-1, wall_t + inner_x, wall_t + front_zone + esp_l/2 - 5, esp_tab_off);              // ESP, right wall; shifted 5mm toward the front so the tab bears on the USB-C connector, not bare PCB
    board_tab(-1, wall_t + inner_x, wall_t + front_zone + esp_l + row_gap + rs_l/2, rs_tab_off);  // RS-485, right wall
    board_tab(+1, wall_t,           wall_t + front_zone + esp_l + row_gap + rs_l/2, rs_tab_off);  // RS-485, left wall (opposite)
  }

  // Wall-mount ears (external flanges at the base)
  if (mount_ears)
    for (dir = [-1, 1])                                   // left / right wall
      for (yc = [wall_t + mount_inset, out_y - wall_t - mount_inset])
        mount_ear(dir, (dir < 0) ? 0 : out_x, yc);
}

// One board retaining tab: a buttress with a vertical retaining face `offset`
// from the wall (wall_x = that wall's inner face), a short flat top, then a brace
// ramping down to the floor toward the centre — stiff against the board tipping
// in, self-supporting to print. dir = +1 LEFT-wall board, -1 RIGHT-wall board.
module board_tab(dir, wall_x, yc, offset) {
  face_x = wall_x + dir*offset;
  top_x  = (dir > 0) ? face_x : face_x - tab_top;     // flat top toward centre
  base_x = (dir > 0) ? face_x : face_x - tab_base;    // base ramps to the floor
  translate([0, yc - tab_w/2, floor_h])
    hull() {
      translate([top_x,  0, tab_h - 0.01]) cube([tab_top,  tab_w, 0.01]);  // flat top
      translate([base_x, 0, 0])            cube([tab_base, tab_w, 0.01]);  // base
    }
}

// One flush-mount ear: a flange with a flat inner edge fused to the OUTER wall
// face (never into the cavity) reaching out to a rounded end with a screw hole,
// plus a gusset ramping down the outside of the wall so the flange resists bending.
module mount_ear(dir, x_face, yc) {
  r     = mount_ear_w/2;
  bite  = 0.5;                            // slight overlap into the wall to fuse
  x_in  = x_face - dir*bite;              // flat inner edge, just inside the wall
  x_out = x_face + dir*mount_ext;         // screw-hole centre, outboard
  difference() {
    hull() {
      translate([x_in, yc - r, 0]) cube([0.01, mount_ear_w, mount_ear_h]);  // flat inner edge
      translate([x_out, yc, 0])    cylinder(r = r, h = mount_ear_h);        // rounded outer end
    }
    translate([x_out, yc, -0.1]) cylinder(d = mount_hole_d, h = mount_ear_h + 0.2);
  }
  if (mount_gusset > 0)                    // brace ramping down the outside of the wall
    hull() {
      translate([x_in, yc - r, 0])                cube([0.01, mount_ear_w, mount_gusset]);
      translate([x_face + dir*mount_ext*0.6, yc - r, 0]) cube([0.01, mount_ear_w, mount_ear_h]);
    }
}

module snap_grooves() {
  z = out_z - snap_z;
  // short walls (front/back): slot runs snap_l along X, through the wall (Y)
  for (px = spread(snap_n_short, out_x))
    for (cy = [wall_t/2, out_y - wall_t/2])
      translate([px - snap_l/2, cy - wall_t, z - snap_h/2])
        cube([snap_l, wall_t*2, snap_h]);
  // long walls (sides): slot runs snap_l along Y, through the wall (X)
  for (py = spread(snap_n_long, out_y))
    for (cx = [wall_t/2, out_x - wall_t/2])
      translate([cx - wall_t, py - snap_l/2, z - snap_h/2])
        cube([wall_t*2, snap_l, snap_h]);
}

// ==================================================================
// LID  (snap-fit plug lid)
// ==================================================================
module lid() {
  difference() {
    union() {
      linear_extrude(lid_h) rrect(out_x, out_y, corner_r);   // top plate

      translate([0, 0, -lip_h])                              // plug lip
        linear_extrude(lip_h)
          difference() {
            offset(delta = -(wall_t + fit_clear/2)) rrect(out_x, out_y, corner_r);
            offset(delta = -(wall_t + fit_clear/2 + lip_t)) rrect(out_x, out_y, corner_r);
          }

      lid_snaps();
      rj_hold_tab();
    }
    // Vent grille through the plate, over the central cable channel
    if (lid_vents) lid_vent_cut();
    // Nail groove under the jack-end edge, to lift the lid off
    if (lid_grip) lid_grip_cut();
  }
}

// Tiny channel in the UNDERSIDE at the jack-end (front, -Y) edge: runs inward
// from the outer edge for a nail to catch and lift the lid, up to the lip
// (grip_l). The inner end is a rounded fillet (radius = depth) instead of a
// square corner, so there's no stress riser on a layer line to peel under the
// prying force. Hidden from the top; prints clean (underside faces up in print).
module lid_grip_cut() {
  x0 = out_x/2 - grip_w/2;
  r  = grip_d;                                   // fillet radius on the inner end
  hull() {
    translate([x0, -0.5,        -0.05])  cube([grip_w, 0.01, 0.01]);  // front, at underside
    translate([x0, -0.5,        grip_d]) cube([grip_w, 0.01, 0.01]);  // front, top of groove
    translate([x0, grip_l,      -0.05])  cube([grip_w, 0.01, 0.01]);  // inner, at underside
    translate([x0, grip_l - r,  grip_d - r]) rotate([0, 90, 0])
      cylinder(r = r, h = grip_w, $fn = 32);                          // rounded inner-top corner
  }
}

// Hold-down tab: presses the top of the RJ45 jack housing when the lid is closed.
// Lid-local z=0 is the plate underside (== top of the walls when installed); the
// tab reaches down to the housing top (rj_h) plus a small preload, centred over
// the jack housing (rj_hold_pos_frac of rj_jack_d in from the wall). Tapered — wide
// root at the lid, narrowing to the contact face — so it braces the weak (Y) axis
// at the root and prints self-supporting (the lid prints top-down).
module rj_hold_tab() {
  gap  = inner_z - rj_h;
  tabx = rj_l * rj_hold_len_frac;
  cx   = wall_t + inner_x/2;
  cy   = wall_t + rj_jack_d*rj_hold_pos_frac;
  hull() {
    translate([cx - tabx/2, cy - rj_hold_root/2, -0.5])       // root at the lid
      cube([tabx, rj_hold_root, 0.5]);
    translate([cx - tabx/2, cy - rj_hold_tip/2,               // contact face
               -(gap + rj_hold_load)])
      cube([tabx, rj_hold_tip, 0.5]);
  }
}

module lid_vent_cut() {
  band_x = inner_x * vent_frac;
  n  = floor(band_x / (vent_w + vent_gap));
  x0 = wall_t + inner_x/2 - (n*(vent_w + vent_gap) - vent_gap)/2;
  yA = wall_t + front_zone + 2;                 // start behind the RJ45 zone
  yB = wall_t + inner_y - end_margin - 2;       // stop before the back wall
  seg = (yB - yA - (vent_rows - 1)*vent_gap) / vent_rows;   // one slot segment (Y)
  for (i = [0:n-1], j = [0:vent_rows-1])      // grid: crossbars between rows tie the ribs
    translate([x0 + i*(vent_w + vent_gap), yA + j*(seg + vent_gap), -0.1])
      cube([vent_w, seg, lid_h + 0.2]);
}

module lid_snaps() {
  z = -snap_z;
  off = wall_t + fit_clear/2;                 // lip outer face, from each edge
  for (px = spread(snap_n_short, out_x)) {
    translate([px, off,         z])                   snap_bump();  // front (-Y)
    translate([px, out_y - off, z]) rotate([0, 0, 180]) snap_bump(); // back  (+Y)
  }
  for (py = spread(snap_n_long, out_y)) {
    translate([off,         py, z]) rotate([0, 0, -90]) snap_bump(); // left  (-X)
    translate([out_x - off, py, z]) rotate([0, 0,  90]) snap_bump(); // right (+X)
  }
}

module snap_bump() {
  // Symmetric tooth pointing -Y: the base spans the full height at the lip and
  // tapers to a tip at mid-height, so BOTH the lead-in and the retention face
  // slope (~45 deg at snap_h = 2*snap_depth) and print support-free with the lid
  // top-down. A crisp printed tooth grips the groove far better than a flat catch
  // whose downward overhang sags.
  hull() {
    translate([-snap_l/2, 0, -snap_h/2]) cube([snap_l, 0.01, snap_h]);      // base at lip
    translate([-snap_l/2, -snap_depth, -0.005]) cube([snap_l, 0.01, 0.01]); // tip, mid-height
  }
}

// ==================================================================
// PREVIEW ASSEMBLY
// ==================================================================
// One standing board leaning on a long wall. `depth` is its edge-on thickness
// (== its retaining-tab offset), so the drawn body reaches the tab; connectors
// extend beyond, toward the centre. side = -1 for the LEFT wall, +1 for RIGHT.
module wall_board(y0, len, stand, depth, side, col) {
  bt     = depth - wall_slack;                                     // board body thickness
  x_body = (side < 0) ? wall_t + wall_slack : wall_t + inner_x - depth;
  x_conn = (side < 0) ? wall_t + depth      : wall_t + inner_x - depth - conn_clear;
  translate([0, wall_t + y0, floor_h]) {
    color(col, 0.55) translate([x_body, 0, 0]) cube([bt, len, stand]);
    if (show_connectors)
      color("Peru", 0.4) translate([x_conn, 0, 0]) cube([conn_clear, len, min(stand, 12)]);
  }
}

module boards_preview() {
  // RJ45 breakout (in its pocket against the front wall): tall jack housing at
  // the front, board + rear screw terminals extending back to rj_w.
  color("SteelBlue", 0.5)
    translate([wall_t + inner_x/2 - rj_l/2, wall_t, floor_h]) {
      cube([rj_l, rj_w, 3]);              // board + low rear components
      cube([rj_l, rj_jack_d, rj_h]);      // jack housing (front, tall)
    }

  // RIGHT (+X) wall: ESP32, then one RS-485 behind it
  wall_board(front_zone,                 esp_l, esp_stand, esp_tab_off, +1, "DarkSlateGray");
  wall_board(front_zone + esp_l+row_gap, rs_l,  rs_stand,  rs_tab_off,  +1, "RoyalBlue");

  // LEFT (-X) wall: the other RS-485, directly opposite the right-wall RS-485
  wall_board(front_zone + esp_l + row_gap, rs_l, rs_stand, rs_tab_off, -1, "RoyalBlue");
}

// ==================================================================
// DISPATCH
// ==================================================================
if (part == "base") base();
else if (part == "lid") lid();
else if (part == "both") { base(); translate([out_x + 10, 0, 0]) lid(); }
else {
  base();
  if (show_boards) boards_preview();
  // Lid stood on its LONG edge beside the base, long side facing the base's
  // long wall (underside toward the base).
  color("gainsboro")
    translate([out_x + 20, 0, out_x]) rotate([0, 90, 0]) lid();
}
