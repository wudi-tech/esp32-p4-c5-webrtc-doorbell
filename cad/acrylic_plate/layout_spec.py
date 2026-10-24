"""Authoritative V3 manufacturing geometry for the acrylic backplate.

All machining coordinates use the FRONT VIEW datum: origin at the theoretical
lower-left corner of the 220 x 190 mm plate, X right, Y up, units millimetres.
Rear assembly references use the same coordinates and must not be mirrored for
cutting.
"""

PLATE = {
    "width": 220.0,
    "height": 190.0,
    "thickness": 5.0,
    "corner_radius": 10.0,
    "material": "Clear cast PMMA",
    "outline_tolerance": 0.20,
    "feature_tolerance": 0.10,
}


def _hole(feature_id, group, x, y, diameter, purpose, remark=""):
    return {
        "id": feature_id,
        "type": "circle",
        "group": group,
        "x": float(x),
        "y": float(y),
        "diameter": float(diameter),
        "purpose": purpose,
        "remark": remark,
    }


MAINBOARD = {
    "id": "MAINBOARD",
    "label": "WT99P4C5-S1 120x90",
    "face": "front",
    "x": 20.0,
    "y": 85.0,
    "width": 120.0,
    "height": 90.0,
    "pcb_thickness": 1.6,
    "mounting": "4x M3 on 8 mm standoffs",
    "orientation": "LANDSCAPE; CSI ON LOWER EDGE",
}

CAMERA = {
    "id": "CAMERA",
    "label": "OV5647 FLEX CAMERA - ESTIMATED REFERENCE ONLY",
    "face": "front",
    "x": 113.0,
    "y": 29.0,
    "width": 12.0,
    "height": 56.0,
    "head_x": 114.75,
    "head_y": 30.75,
    "head_width": 8.5,
    "head_height": 8.5,
    "lens_x": 119.0,
    "lens_y": 35.0,
    "lens_diameter": 4.0,
    "csi_center_x": 119.0,
    "csi_y": 85.0,
    "board_edge_to_lens": 50.0,
    "estimate_xy_error": 1.0,
    "estimate_length_error": 2.0,
    "mounting": "THIN DOUBLE-SIDED TAPE; NO CUT FEATURES",
    "orientation": "CSI STRAIGHT - NO LATERAL TWIST",
}

RADAR = {
    "id": "RADAR",
    "label": "HLK-LD2450 15x44 VERTICAL",
    "face": "front",
    "x": 27.5,
    "y": 26.0,
    "center_x": 35.0,
    "center_y": 48.0,
    "width": 15.0,
    "height": 44.0,
    "orientation": "NOTCH END UP; ANTENNA FRONT +Y",
    "mounting": "2x M2 CLAMP AT OFFICIAL DIAGONAL R1.1 NOTCHES",
}

SPEAKER = {
    "id": "SPEAKER",
    "label": "3525 SPEAKER 4 OHM 3 W",
    "face": "front",
    "x": 52.5,
    "y": 35.5,
    "center_x": 70.0,
    "center_y": 48.0,
    "width": 35.0,
    "height": 25.0,
    "mounting": "1 mm EVA DOUBLE-SIDED TAPE; NO CUT FEATURES",
}

SERVO = {
    "id": "SERVO",
    "label": "MG90S SERVO - HOT GLUE",
    "face": "front",
    "x": 183.95,
    "y": 128.85,
    "center_x": 190.0,
    "center_y": 145.0,
    "width": 12.1,
    "height": 32.3,
    "depth": 33.0,
    "mounting": "HOT GLUE; NO CUT FEATURES",
    "orientation": "OUTPUT SHAFT NORMAL TO PLATE, FACING OUT",
}

POWER_BOARD = {
    "id": "POWER_BOARD",
    "label": "POWER BOARD 85.10x56.00",
    "face": "rear",
    "x": 120.0,
    "y": 15.0,
    "width": 85.1,
    "height": 56.0,
    "envelope_width": 85.1,
    "envelope_height": 61.45,
    "assembled_depth": 18.1,
    "mounting": "4x M2 on 10 mm standoffs; components face rear-out",
}

FRONT_REFERENCES = [MAINBOARD, CAMERA, RADAR, SPEAKER, SERVO]
REAR_REFERENCES = [POWER_BOARD]

# Mainboard source: 120 x 90 landscape rotation of the 90 x 120 PCB.
# Hole insets: (120 - 114.95) / 2 = 2.525;
#              (90 - 84.96) / 2 = 2.520.
_mb_left = MAINBOARD["x"] + 2.525
_mb_right = _mb_left + 114.95
_mb_bottom = MAINBOARD["y"] + 2.520
_mb_top = _mb_bottom + 84.96

# Power-board source drawing: PCB 85.10 x 56.00, 58 x 49 hole pitch,
# with 3.50 mm top/bottom and right-edge offsets.
_power_right = POWER_BOARD["x"] + POWER_BOARD["width"] - 3.5
_power_left = _power_right - 58.0
_power_bottom = POWER_BOARD["y"] + 3.5
_power_top = _power_bottom + 49.0

# Official LD2450 DXF normalized outline is 44 x 15 mm. Its two R1.1
# diagonal semicircular notch centres are (0.7778, 14.2222) and
# (43.2222, 0.7778). Rotating to the approved 15 x 44 vertical orientation
# gives local centres (0.7778, 0.7778) and (14.2222, 43.2222).
_radar_local_notches = ((0.7778, 0.7778), (14.2222, 43.2222))

HOLES = [
    _hole("C1", "corner_support", 10.0, 10.0, 3.2, "M3 corner support"),
    _hole("C2", "corner_support", 210.0, 10.0, 3.2, "M3 corner support"),
    _hole("C3", "corner_support", 10.0, 180.0, 3.2, "M3 corner support"),
    _hole("C4", "corner_support", 210.0, 180.0, 3.2, "M3 corner support"),
    _hole("MB1", "mainboard", _mb_left, _mb_bottom, 3.2, "Mainboard M3"),
    _hole("MB2", "mainboard", _mb_right, _mb_bottom, 3.2, "Mainboard M3"),
    _hole("MB3", "mainboard", _mb_left, _mb_top, 3.2, "Mainboard M3"),
    _hole("MB4", "mainboard", _mb_right, _mb_top, 3.2, "Mainboard M3"),
    _hole(
        "R1",
        "radar_clamp",
        RADAR["x"] + _radar_local_notches[0][0],
        RADAR["y"] + _radar_local_notches[0][1],
        2.2,
        "LD2450 lower-left notch clamp",
        "M2 standoff and nylon washer; washer overlap 0.5-0.8",
    ),
    _hole(
        "R2",
        "radar_clamp",
        RADAR["x"] + _radar_local_notches[1][0],
        RADAR["y"] + _radar_local_notches[1][1],
        2.2,
        "LD2450 upper-right notch clamp",
        "M2 standoff and nylon washer; washer overlap 0.5-0.8",
    ),
    _hole("P1", "power", _power_left, _power_bottom, 2.2, "Power board M2"),
    _hole("P2", "power", _power_right, _power_bottom, 2.2, "Power board M2"),
    _hole("P3", "power", _power_left, _power_top, 2.2, "Power board M2"),
    _hole("P4", "power", _power_right, _power_top, 2.2, "Power board M2"),
]

CUTOUTS = [
    {
        "id": "W1",
        "type": "rounded_rectangle",
        "group": "cable_pass",
        "center_x": 170.0,
        "center_y": 91.0,
        "width": 32.0,
        "height": 18.0,
        "radius": 3.0,
        "orientation": "HORIZONTAL",
        "purpose": "Cable and complete USB-A plug pass-through",
        "remark": "Deburr all edges",
    }
]

# Compatibility alias: V3 has no obround camera slots.
SLOTS = []

DIMENSION_AUDIT = [
    {
        "item": "BACKPLATE",
        "old": "220x190x5 R10",
        "source": "User-confirmed",
        "final": "220x190x5 R10",
        "status": "MATCH",
    },
    {
        "item": "MAIN BOARD",
        "old": "90x120; old absolute position",
        "source": "WT99P4C5-S1 mechanical drawing",
        "final": "120x90 landscape at (20,85)",
        "status": "REPOSITIONED",
    },
    {
        "item": "CAMERA",
        "old": "4x 6x3.2 slots",
        "source": "Photo scale estimate authorized by user",
        "final": "Reference only; no cut",
        "status": "OLD SLOTS REMOVED",
    },
    {
        "item": "LD2450",
        "old": "4-hole bracket",
        "source": "Official 44x15 DXF/STEP; user clamp decision",
        "final": "15x44 vertical; 2 notch clamps",
        "status": "REBUILT",
    },
    {
        "item": "3525 SPEAKER",
        "old": "4-hole bracket",
        "source": "User-confirmed 35x25, 4 ohm, 3 W",
        "final": "Adhesive reference only",
        "status": "OLD HOLES REMOVED",
    },
    {
        "item": "MG90S",
        "old": "4-hole bracket plus door",
        "source": "MG90S drawing; user hot-glue decision",
        "final": "Hot glue; no cut",
        "status": "OLD HOLES REMOVED",
    },
    {
        "item": "POWER BOARD",
        "old": "58x49 pitch; wrong absolute location",
        "source": "85.10x56 drawing, 58x49, 3.50 edge offset",
        "final": "PCB at (120,15); 4x dia 2.20",
        "status": "REPOSITIONED",
    },
    {
        "item": "W1",
        "old": "dia 12 circle",
        "source": "User-confirmed full USB-A clearance",
        "final": "32x18 R3 at (170,91)",
        "status": "REBUILT",
    },
]

COORDINATE_WARNING = (
    "ALL MACHINING COORDINATES ARE DEFINED FROM FRONT VIEW. "
    "DO NOT MIRROR CUTTING GEOMETRY."
)
