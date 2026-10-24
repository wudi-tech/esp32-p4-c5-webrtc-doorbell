"""Generate the V3 acrylic backplate manufacturing package."""

from __future__ import annotations

import csv
import math
import sys
import zipfile
from html import escape
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from cad.acrylic_plate.layout_spec import (
    CAMERA,
    COORDINATE_WARNING,
    CUTOUTS,
    DIMENSION_AUDIT,
    FRONT_REFERENCES,
    HOLES,
    PLATE,
    POWER_BOARD,
    REAR_REFERENCES,
)


def _dxf_pair(code, value):
    if isinstance(value, float):
        value = f"{value:.4f}"
    return f"{code}\n{value}\n"


def _dxf_line(layer, x1, y1, x2, y2):
    return "".join(
        [
            _dxf_pair(0, "LINE"),
            _dxf_pair(8, layer),
            _dxf_pair(10, float(x1)),
            _dxf_pair(20, float(y1)),
            _dxf_pair(30, 0.0),
            _dxf_pair(11, float(x2)),
            _dxf_pair(21, float(y2)),
            _dxf_pair(31, 0.0),
        ]
    )


def _dxf_arc(layer, cx, cy, radius, start_angle, end_angle):
    return "".join(
        [
            _dxf_pair(0, "ARC"),
            _dxf_pair(8, layer),
            _dxf_pair(10, float(cx)),
            _dxf_pair(20, float(cy)),
            _dxf_pair(30, 0.0),
            _dxf_pair(40, float(radius)),
            _dxf_pair(50, float(start_angle)),
            _dxf_pair(51, float(end_angle)),
        ]
    )


def _dxf_circle(layer, cx, cy, radius):
    return "".join(
        [
            _dxf_pair(0, "CIRCLE"),
            _dxf_pair(8, layer),
            _dxf_pair(10, float(cx)),
            _dxf_pair(20, float(cy)),
            _dxf_pair(30, 0.0),
            _dxf_pair(40, float(radius)),
        ]
    )


def _dxf_text(layer, x, y, height, value, rotation=0.0):
    ascii_value = value.encode("ascii", errors="replace").decode("ascii")
    return "".join(
        [
            _dxf_pair(0, "TEXT"),
            _dxf_pair(8, layer),
            _dxf_pair(10, float(x)),
            _dxf_pair(20, float(y)),
            _dxf_pair(30, 0.0),
            _dxf_pair(40, float(height)),
            _dxf_pair(1, ascii_value),
            _dxf_pair(50, float(rotation)),
        ]
    )


def _rounded_rect_entities(layer, left, bottom, width, height, radius):
    right = left + width
    top = bottom + height
    return [
        _dxf_line(layer, left + radius, bottom, right - radius, bottom),
        _dxf_arc(layer, right - radius, bottom + radius, radius, 270, 360),
        _dxf_line(layer, right, bottom + radius, right, top - radius),
        _dxf_arc(layer, right - radius, top - radius, radius, 0, 90),
        _dxf_line(layer, right - radius, top, left + radius, top),
        _dxf_arc(layer, left + radius, top - radius, radius, 90, 180),
        _dxf_line(layer, left, top - radius, left, bottom + radius),
        _dxf_arc(layer, left + radius, bottom + radius, radius, 180, 270),
    ]


def _rectangle_entities(layer, left, bottom, width, height):
    right = left + width
    top = bottom + height
    return [
        _dxf_line(layer, left, bottom, right, bottom),
        _dxf_line(layer, right, bottom, right, top),
        _dxf_line(layer, right, top, left, top),
        _dxf_line(layer, left, top, left, bottom),
    ]


def _dxf_header(layer_names):
    tables = [
        _dxf_pair(0, "SECTION"),
        _dxf_pair(2, "HEADER"),
        _dxf_pair(9, "$ACADVER"),
        _dxf_pair(1, "AC1009"),
        _dxf_pair(9, "$INSUNITS"),
        _dxf_pair(70, 4),
        _dxf_pair(9, "$EXTMIN"),
        _dxf_pair(10, 0.0),
        _dxf_pair(20, 0.0),
        _dxf_pair(30, 0.0),
        _dxf_pair(9, "$EXTMAX"),
        _dxf_pair(10, PLATE["width"]),
        _dxf_pair(20, PLATE["height"]),
        _dxf_pair(30, 0.0),
        _dxf_pair(0, "ENDSEC"),
        _dxf_pair(0, "SECTION"),
        _dxf_pair(2, "TABLES"),
        _dxf_pair(0, "TABLE"),
        _dxf_pair(2, "LAYER"),
        _dxf_pair(70, len(layer_names)),
    ]
    colors = {"CUT": 1, "REFERENCE": 3, "TEXT": 7, "DIMENSION": 5}
    for name in layer_names:
        tables.extend(
            [
                _dxf_pair(0, "LAYER"),
                _dxf_pair(2, name),
                _dxf_pair(70, 0),
                _dxf_pair(62, colors.get(name, 7)),
                _dxf_pair(6, "CONTINUOUS"),
            ]
        )
    tables.extend(
        [
            _dxf_pair(0, "ENDTAB"),
            _dxf_pair(0, "ENDSEC"),
            _dxf_pair(0, "SECTION"),
            _dxf_pair(2, "ENTITIES"),
        ]
    )
    return "".join(tables)


def _cut_entities():
    entities = _rounded_rect_entities(
        "CUT",
        0.0,
        0.0,
        PLATE["width"],
        PLATE["height"],
        PLATE["corner_radius"],
    )
    entities.extend(
        _dxf_circle("CUT", hole["x"], hole["y"], hole["diameter"] / 2)
        for hole in HOLES
    )
    for cutout in CUTOUTS:
        entities.extend(
            _rounded_rect_entities(
                "CUT",
                cutout["center_x"] - cutout["width"] / 2,
                cutout["center_y"] - cutout["height"] / 2,
                cutout["width"],
                cutout["height"],
                cutout["radius"],
            )
        )
    return entities


def _reference_entities():
    entities = []
    for feature in FRONT_REFERENCES:
        if feature["id"] == "CAMERA":
            entities.extend(
                _rectangle_entities(
                    "REFERENCE",
                    feature["head_x"],
                    feature["head_y"],
                    feature["head_width"],
                    feature["head_height"],
                )
            )
            entities.append(
                _dxf_circle(
                    "REFERENCE",
                    feature["lens_x"],
                    feature["lens_y"],
                    feature["lens_diameter"] / 2,
                )
            )
            entities.append(
                _dxf_line(
                    "REFERENCE",
                    feature["csi_center_x"],
                    feature["lens_y"],
                    feature["csi_center_x"],
                    feature["csi_y"],
                )
            )
        else:
            entities.extend(
                _rectangle_entities(
                    "REFERENCE",
                    feature["x"],
                    feature["y"],
                    feature["width"],
                    feature["height"],
                )
            )
        entities.append(
            _dxf_text(
                "TEXT",
                feature["x"],
                feature["y"] + feature["height"] + 2,
                2.5,
                f"FRONT {feature['label']}",
            )
        )
    for feature in REAR_REFERENCES:
        entities.extend(
            _rectangle_entities(
                "REFERENCE",
                feature["x"],
                feature["y"],
                feature["width"],
                feature["height"],
            )
        )
        entities.append(
            _dxf_text(
                "TEXT",
                feature["x"],
                feature["y"] + feature["height"] + 2,
                2.5,
                f"REAR {feature['label']} - FRONT DATUM",
            )
        )
    for hole in HOLES:
        entities.append(
            _dxf_text("TEXT", hole["x"] + 2.0, hole["y"] + 2.0, 2.0, hole["id"])
        )
    for cutout in CUTOUTS:
        entities.append(
            _dxf_text(
                "TEXT",
                cutout["center_x"] + cutout["width"] / 2 + 2,
                cutout["center_y"],
                2.0,
                cutout["id"],
            )
        )
    entities.extend(
        [
            _dxf_line("DIMENSION", 0, -8, PLATE["width"], -8),
            _dxf_line("DIMENSION", -8, 0, -8, PLATE["height"]),
            _dxf_text("DIMENSION", 95, -6, 3.0, "220.00 +/-0.20"),
            _dxf_text("DIMENSION", -6, 72, 3.0, "190.00 +/-0.20", 90),
            _dxf_text("TEXT", 20, 183, 2.5, COORDINATE_WARNING),
        ]
    )
    return entities


def write_dxf(path, layered=False):
    layers = ["CUT", "REFERENCE", "TEXT", "DIMENSION"] if layered else ["CUT"]
    entities = _cut_entities()
    if layered:
        entities.extend(_reference_entities())
    text = (
        _dxf_header(layers)
        + "".join(entities)
        + _dxf_pair(0, "ENDSEC")
        + _dxf_pair(0, "EOF")
    )
    path.write_text(text, encoding="ascii", newline="\n")


CSV_COLUMNS = [
    "ID",
    "TYPE",
    "X",
    "Y",
    "WIDTH_OR_DIAMETER",
    "HEIGHT_OR_LENGTH",
    "RADIUS",
    "QTY",
    "PURPOSE",
    "REMARK",
]


def coordinate_rows():
    rows = []
    for hole in HOLES:
        rows.append(
            {
                "ID": hole["id"],
                "TYPE": "CIRCULAR HOLE",
                "X": f"{hole['x']:.3f}",
                "Y": f"{hole['y']:.3f}",
                "WIDTH_OR_DIAMETER": f"DIA {hole['diameter']:.2f}",
                "HEIGHT_OR_LENGTH": "",
                "RADIUS": "",
                "QTY": "1",
                "PURPOSE": hole["purpose"],
                "REMARK": hole["remark"],
            }
        )
    for cutout in CUTOUTS:
        rows.append(
            {
                "ID": cutout["id"],
                "TYPE": "ROUNDED RECTANGLE",
                "X": f"{cutout['center_x']:.3f}",
                "Y": f"{cutout['center_y']:.3f}",
                "WIDTH_OR_DIAMETER": f"{cutout['width']:.2f}",
                "HEIGHT_OR_LENGTH": f"{cutout['height']:.2f}",
                "RADIUS": f"R{cutout['radius']:.2f}",
                "QTY": "1",
                "PURPOSE": cutout["purpose"],
                "REMARK": f"{cutout['orientation']}; {cutout['remark']}",
            }
        )
    return rows


def write_csv(path):
    with path.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=CSV_COLUMNS)
        writer.writeheader()
        writer.writerows(coordinate_rows())


def _svg_y(y):
    return PLATE["height"] - y


def _svg_rect(x, y, width, height, css_class, radius=1.5, element_id=""):
    svg_y = PLATE["height"] - y - height
    id_attr = f' id="{escape(element_id)}"' if element_id else ""
    return (
        f'<rect{id_attr} x="{x:.3f}" y="{svg_y:.3f}" width="{width:.3f}" '
        f'height="{height:.3f}" rx="{radius:.3f}" class="{css_class}"/>'
    )


def _svg_text(x, y, value, css_class="label", anchor="start"):
    return (
        f'<text x="{x:.3f}" y="{_svg_y(y):.3f}" text-anchor="{anchor}" '
        f'class="{css_class}">{escape(value)}</text>'
    )


def _svg_base(title, subtitle):
    width = PLATE["width"]
    height = PLATE["height"]
    return [
        '<?xml version="1.0" encoding="UTF-8"?>',
        (
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{width:g}mm" '
            f'height="{height:g}mm" viewBox="0 0 {width:g} {height:g}">'
        ),
        "<style>",
        ".plate{fill:#f8fbfa;stroke:#1f3b36;stroke-width:.6}",
        ".cut{fill:none;stroke:#d43d3d;stroke-width:.35}",
        ".component{fill:#dfeee9;stroke:#1e6556;stroke-width:.45}",
        ".camera{fill:#f5c36a;stroke:#8d5a00;stroke-width:.4}",
        ".radar{fill:#225b8a;stroke:#123750;stroke-width:.45}",
        ".speaker{fill:#e9e5dc;stroke:#5b554b;stroke-width:.45}",
        ".servo{fill:#5f84a2;stroke:#263e50;stroke-width:.45}",
        ".rear{fill:#f0dfb5;stroke:#9b6d18;stroke-width:.45}",
        ".keepout{fill:none;stroke:#a87324;stroke-width:.35;stroke-dasharray:2 1}",
        ".axis{fill:none;stroke:#9b6500;stroke-width:.4;stroke-dasharray:2 1}",
        ".label{font-family:Arial,'Microsoft YaHei',sans-serif;font-size:3px;fill:#17201e}",
        ".small{font-size:2.4px}.title{font-size:4.2px;font-weight:bold}",
        ".warning{font-size:2.7px;font-weight:bold;fill:#a32626}",
        "</style>",
        f'<rect x=".3" y=".3" width="{width - .6:.3f}" height="{height - .6:.3f}" rx="{PLATE["corner_radius"]:.3f}" class="plate"/>',
        _svg_text(110, 185, title, "label title", "middle"),
        _svg_text(110, 180.5, subtitle, "label small", "middle"),
    ]


def _svg_cut_features(parts):
    for hole in HOLES:
        parts.append(
            f'<circle cx="{hole["x"]:.3f}" cy="{_svg_y(hole["y"]):.3f}" '
            f'r="{hole["diameter"] / 2:.3f}" class="cut"/>'
        )
        parts.append(_svg_text(hole["x"] + 2, hole["y"] + 2, hole["id"], "label small"))
    for cutout in CUTOUTS:
        parts.append(
            _svg_rect(
                cutout["center_x"] - cutout["width"] / 2,
                cutout["center_y"] - cutout["height"] / 2,
                cutout["width"],
                cutout["height"],
                "cut",
                cutout["radius"],
                cutout["id"],
            )
        )
        parts.append(
            _svg_text(
                cutout["center_x"],
                cutout["center_y"],
                f"{cutout['id']} 32x18 R3",
                "label small",
                "middle",
            )
        )


def write_front_svg(path):
    parts = _svg_base(
        "PAGE 2 - FRONT ASSEMBLY REFERENCE",
        "FRONT VIEW DATUM (0,0) LOWER-LEFT - UNITS mm - +Y FACES USER",
    )
    _svg_cut_features(parts)
    mainboard = FRONT_REFERENCES[0]
    parts.append(
        _svg_rect(
            mainboard["x"],
            mainboard["y"],
            mainboard["width"],
            mainboard["height"],
            "component",
            1.0,
            mainboard["id"],
        )
    )
    parts.append(_svg_text(80, 130, "WT99P4C5-S1 120x90", "label", "middle"))
    parts.append(
        _svg_rect(140, 85, 35, 90, "keepout", 0, "RIGHT_INTERFACE_KEEPOUT")
    )
    parts.append(_svg_text(157.5, 168, "USB / RJ45 PLUG KEEPOUT", "label small", "middle"))
    parts.append(
        f'<line x1="{CAMERA["csi_center_x"]:.3f}" y1="{_svg_y(CAMERA["csi_y"]):.3f}" '
        f'x2="{CAMERA["lens_x"]:.3f}" y2="{_svg_y(CAMERA["lens_y"]):.3f}" class="axis"/>'
    )
    parts.append(
        _svg_rect(113.0, 61.0, 12.0, 24.0, "camera", 0.5, "CAMERA_WIDE_FLEX")
    )
    taper_points = [
        (113.0, 61.0),
        (116.0, 53.0),
        (122.0, 53.0),
        (125.0, 61.0),
    ]
    points = " ".join(f"{x:.3f},{_svg_y(y):.3f}" for x, y in taper_points)
    parts.append(f'<polygon points="{points}" class="camera"/>')
    parts.append(
        _svg_rect(116.0, 39.25, 6.0, 13.75, "camera", 0.4, "CAMERA_NARROW_FLEX")
    )
    parts.append(
        _svg_rect(
            CAMERA["head_x"],
            CAMERA["head_y"],
            CAMERA["head_width"],
            CAMERA["head_height"],
            "camera",
            0.8,
            "CAMERA_HEAD",
        )
    )
    parts.append(
        f'<circle cx="{CAMERA["lens_x"]:.3f}" cy="{_svg_y(CAMERA["lens_y"]):.3f}" '
        f'r="{CAMERA["lens_diameter"] / 2:.3f}" class="camera"/>'
    )
    parts.append(_svg_text(127, 57, "CAMERA - CSI STRAIGHT / NO LATERAL TWIST", "label small"))
    parts.append(_svg_text(127, 52.5, "PHOTO-SCALE ESTIMATE; NO CUT FEATURES", "label small"))
    radar = next(item for item in FRONT_REFERENCES if item["id"] == "RADAR")
    parts.append(
        _svg_rect(
            radar["x"],
            radar["y"],
            radar["width"],
            radar["height"],
            "radar",
            1.1,
            radar["id"],
        )
    )
    parts.append(_svg_text(radar["center_x"], 73, "LD2450 15x44 VERTICAL", "label small", "middle"))
    parts.append(_svg_text(radar["center_x"], 21, "ANTENNA FACE +Y (OUT OF PAGE)", "label small", "middle"))
    speaker = next(item for item in FRONT_REFERENCES if item["id"] == "SPEAKER")
    parts.append(
        _svg_rect(
            speaker["x"],
            speaker["y"],
            speaker["width"],
            speaker["height"],
            "speaker",
            6.0,
            speaker["id"],
        )
    )
    parts.append(_svg_text(70, 48, "3525 / 4 OHM / 3 W", "label small", "middle"))
    parts.append(_svg_text(70, 31.5, "1 mm EVA TAPE - NO HOLES", "label small", "middle"))
    servo = next(item for item in FRONT_REFERENCES if item["id"] == "SERVO")
    parts.append(
        _svg_rect(
            servo["x"],
            servo["y"],
            servo["width"],
            servo["height"],
            "servo",
            1.0,
            servo["id"],
        )
    )
    parts.append(
        f'<circle cx="{servo["center_x"]:.3f}" cy="{_svg_y(servo["center_y"]):.3f}" '
        'r="3.5" class="servo"/>'
    )
    parts.append(_svg_text(181, 124, "MG90S - HOT GLUE", "label small"))
    parts.append(_svg_text(181, 119.5, "SHAFT NORMAL TO PLATE", "label small"))
    parts.append(_svg_text(6, 6, "ORIGIN (0,0)", "label small"))
    parts.append(_svg_text(110, 3, COORDINATE_WARNING, "label warning", "middle"))
    parts.append("</svg>")
    path.write_text("\n".join(parts), encoding="utf-8")


def write_rear_svg(path):
    parts = _svg_base(
        "PAGE 3 - REAR ASSEMBLY REFERENCE",
        "REAR COMPONENTS SHOWN IN FRONT-DATUM COORDINATES - DO NOT FLIP X",
    )
    _svg_cut_features(parts)
    power = POWER_BOARD
    parts.append(
        _svg_rect(
            power["x"],
            power["y"],
            power["envelope_width"],
            power["envelope_height"],
            "keepout",
            1.5,
            "POWER_MAX_ENVELOPE",
        )
    )
    parts.append(
        _svg_rect(
            power["x"],
            power["y"],
            power["width"],
            power["height"],
            "rear",
            1.5,
            power["id"],
        )
    )
    parts.append(_svg_text(162.55, 45, "POWER BOARD 85.10x56.00", "label", "middle"))
    parts.append(_svg_text(162.55, 40, "MAX ENVELOPE 85.10x61.45", "label small", "middle"))
    parts.append(_svg_text(162.55, 35, "4x M2 x 10 STANDOFF", "label small", "middle"))
    parts.append(_svg_text(162.55, 30, "COMPONENTS FACE REAR-OUT", "label small", "middle"))
    parts.append(_svg_text(110, 8, COORDINATE_WARNING, "label warning", "middle"))
    parts.append(_svg_text(110, 4, "DO NOT MIRROR CUTTING GEOMETRY", "label warning", "middle"))
    parts.append("</svg>")
    path.write_text("\n".join(parts), encoding="utf-8")


def _pdf_font():
    from reportlab.pdfbase import pdfmetrics
    from reportlab.pdfbase.ttfonts import TTFont

    font_name = "AcrylicV3CJK"
    if font_name not in pdfmetrics.getRegisteredFontNames():
        for candidate in (
            Path(r"C:\Windows\Fonts\msyh.ttc"),
            Path(r"C:\Windows\Fonts\simhei.ttf"),
        ):
            if candidate.exists():
                pdfmetrics.registerFont(TTFont(font_name, str(candidate)))
                return font_name
        return "Helvetica"
    return font_name


def _pdf_title(canvas, font, title, page_number):
    from reportlab.lib.colors import HexColor
    from reportlab.lib.pagesizes import A4, landscape

    page_width, page_height = landscape(A4)
    canvas.setFillColor(HexColor("#173f35"))
    canvas.setFont(font, 14)
    canvas.drawString(28, page_height - 27, title)
    canvas.setFillColor(HexColor("#5d6865"))
    canvas.setFont(font, 6.5)
    canvas.drawRightString(
        page_width - 26,
        page_height - 23,
        f"ACRYLIC BACKPLATE FINAL PRODUCTION DRAWING V3  |  PAGE {page_number}/4",
    )
    canvas.setStrokeColor(HexColor("#9eb0ab"))
    canvas.setLineWidth(0.5)
    canvas.line(28, page_height - 34, page_width - 26, page_height - 34)


def _pdf_plate(canvas, origin_x, origin_y, scale, font, face=None, show_ids=True):
    from reportlab.lib.colors import HexColor

    canvas.setStrokeColor(HexColor("#1f3b36"))
    canvas.setLineWidth(0.9)
    canvas.roundRect(
        origin_x,
        origin_y,
        PLATE["width"] * scale,
        PLATE["height"] * scale,
        PLATE["corner_radius"] * scale,
        stroke=1,
        fill=0,
    )
    canvas.setStrokeColor(HexColor("#d43d3d"))
    canvas.setFillColor(HexColor("#9b2525"))
    canvas.setLineWidth(0.55)
    for hole in HOLES:
        x = origin_x + hole["x"] * scale
        y = origin_y + hole["y"] * scale
        radius = hole["diameter"] * scale / 2
        canvas.circle(x, y, radius, stroke=1, fill=0)
        if show_ids:
            canvas.setFont(font, 4.8)
            canvas.drawString(x + radius + 1.5, y + radius + 1.0, hole["id"])
    for cutout in CUTOUTS:
        canvas.roundRect(
            origin_x + (cutout["center_x"] - cutout["width"] / 2) * scale,
            origin_y + (cutout["center_y"] - cutout["height"] / 2) * scale,
            cutout["width"] * scale,
            cutout["height"] * scale,
            cutout["radius"] * scale,
            stroke=1,
            fill=0,
        )
        if show_ids:
            canvas.setFont(font, 5.2)
            canvas.drawCentredString(
                origin_x + cutout["center_x"] * scale,
                origin_y + cutout["center_y"] * scale - 2,
                "W1",
            )
    if face == "front":
        _pdf_front_references(canvas, origin_x, origin_y, scale, font)
    elif face == "rear":
        _pdf_rear_references(canvas, origin_x, origin_y, scale, font)


def _pdf_rect(canvas, origin_x, origin_y, scale, feature, color, dash=None):
    canvas.saveState()
    canvas.setStrokeColor(color)
    canvas.setLineWidth(0.65)
    if dash:
        canvas.setDash(*dash)
    canvas.rect(
        origin_x + feature["x"] * scale,
        origin_y + feature["y"] * scale,
        feature["width"] * scale,
        feature["height"] * scale,
        stroke=1,
        fill=0,
    )
    canvas.restoreState()


def _pdf_front_references(canvas, origin_x, origin_y, scale, font):
    from reportlab.lib.colors import HexColor

    green = HexColor("#1e6556")
    amber = HexColor("#9b6500")
    blue = HexColor("#225b8a")
    grey = HexColor("#5b554b")
    mainboard = next(item for item in FRONT_REFERENCES if item["id"] == "MAINBOARD")
    _pdf_rect(canvas, origin_x, origin_y, scale, mainboard, green)
    canvas.setFillColor(green)
    canvas.setFont(font, 5.3)
    canvas.drawCentredString(
        origin_x + (mainboard["x"] + mainboard["width"] / 2) * scale,
        origin_y + (mainboard["y"] + mainboard["height"] / 2) * scale,
        "WT99P4C5-S1 120x90",
    )
    keepout = {"x": 140.0, "y": 85.0, "width": 35.0, "height": 90.0}
    _pdf_rect(canvas, origin_x, origin_y, scale, keepout, amber, (3, 2))
    canvas.setFillColor(amber)
    canvas.setFont(font, 4.4)
    canvas.drawCentredString(
        origin_x + 157.5 * scale,
        origin_y + 168 * scale,
        "RIGHT PORT PLUG KEEPOUT",
    )
    canvas.saveState()
    canvas.setStrokeColor(amber)
    canvas.setDash(3, 2)
    canvas.line(
        origin_x + CAMERA["csi_center_x"] * scale,
        origin_y + CAMERA["lens_y"] * scale,
        origin_x + CAMERA["csi_center_x"] * scale,
        origin_y + CAMERA["csi_y"] * scale,
    )
    canvas.restoreState()
    camera_head = {
        "x": CAMERA["head_x"],
        "y": CAMERA["head_y"],
        "width": CAMERA["head_width"],
        "height": CAMERA["head_height"],
    }
    _pdf_rect(canvas, origin_x, origin_y, scale, camera_head, amber)
    canvas.setStrokeColor(amber)
    canvas.circle(
        origin_x + CAMERA["lens_x"] * scale,
        origin_y + CAMERA["lens_y"] * scale,
        CAMERA["lens_diameter"] * scale / 2,
        stroke=1,
        fill=0,
    )
    radar = next(item for item in FRONT_REFERENCES if item["id"] == "RADAR")
    _pdf_rect(canvas, origin_x, origin_y, scale, radar, blue)
    speaker = next(item for item in FRONT_REFERENCES if item["id"] == "SPEAKER")
    _pdf_rect(canvas, origin_x, origin_y, scale, speaker, grey)
    servo = next(item for item in FRONT_REFERENCES if item["id"] == "SERVO")
    _pdf_rect(canvas, origin_x, origin_y, scale, servo, blue)
    labels = [
        (35.0, 72.0, "LD2450 VERTICAL"),
        (70.0, 48.0, "3525 4R 3W"),
        (190.0, 124.0, "MG90S HOT GLUE"),
        (126.0, 55.0, "CAMERA / CSI STRAIGHT"),
    ]
    canvas.setFont(font, 4.5)
    canvas.setFillColor(HexColor("#24312e"))
    for x, y, text in labels:
        canvas.drawCentredString(origin_x + x * scale, origin_y + y * scale, text)


def _pdf_rear_references(canvas, origin_x, origin_y, scale, font):
    from reportlab.lib.colors import HexColor

    power = POWER_BOARD
    envelope = {
        "x": power["x"],
        "y": power["y"],
        "width": power["envelope_width"],
        "height": power["envelope_height"],
    }
    _pdf_rect(
        canvas,
        origin_x,
        origin_y,
        scale,
        envelope,
        HexColor("#9b6d18"),
        (3, 2),
    )
    _pdf_rect(
        canvas,
        origin_x,
        origin_y,
        scale,
        power,
        HexColor("#9b6d18"),
    )
    canvas.setFillColor(HexColor("#715117"))
    canvas.setFont(font, 5.0)
    canvas.drawCentredString(
        origin_x + (power["x"] + power["width"] / 2) * scale,
        origin_y + (power["y"] + power["height"] / 2) * scale,
        "POWER BOARD 85.10x56.00",
    )


def _pdf_dimension(canvas, font, x1, y1, x2, y2, label, vertical=False):
    from reportlab.lib.colors import HexColor

    canvas.saveState()
    canvas.setStrokeColor(HexColor("#46534f"))
    canvas.setFillColor(HexColor("#26312f"))
    canvas.setLineWidth(0.45)
    canvas.line(x1, y1, x2, y2)
    canvas.setFont(font, 6.2)
    if vertical:
        canvas.line(x1 - 3, y1 + 5, x1 + 3, y1)
        canvas.line(x2 - 3, y2 - 5, x2 + 3, y2)
        canvas.saveState()
        canvas.translate(x1 - 7, (y1 + y2) / 2)
        canvas.rotate(90)
        canvas.drawCentredString(0, 0, label)
        canvas.restoreState()
    else:
        canvas.line(x1 + 5, y1 - 3, x1, y1 + 3)
        canvas.line(x2 - 5, y2 - 3, x2, y2 + 3)
        canvas.drawCentredString((x1 + x2) / 2, y1 + 4, label)
    canvas.restoreState()


def _pdf_table(canvas, data, x, y_top, col_widths, row_heights, font, font_size=6.0):
    from reportlab.lib import colors
    from reportlab.platypus import Table, TableStyle

    table = Table(data, colWidths=col_widths, rowHeights=row_heights)
    table.setStyle(
        TableStyle(
            [
                ("FONTNAME", (0, 0), (-1, -1), font),
                ("FONTSIZE", (0, 0), (-1, -1), font_size),
                ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#dce9e5")),
                ("TEXTCOLOR", (0, 0), (-1, 0), colors.HexColor("#173f35")),
                ("GRID", (0, 0), (-1, -1), 0.35, colors.HexColor("#91a29d")),
                ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
                ("LEFTPADDING", (0, 0), (-1, -1), 3),
                ("RIGHTPADDING", (0, 0), (-1, -1), 3),
                ("TOPPADDING", (0, 0), (-1, -1), 2),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 2),
            ]
        )
    )
    width, height = table.wrapOn(canvas, 0, 0)
    table.drawOn(canvas, x, y_top - height)
    return width, height


def _pdf_page_one(canvas, font):
    from reportlab.lib.colors import HexColor
    from reportlab.lib.pagesizes import A4, landscape

    page_width, page_height = landscape(A4)
    _pdf_title(canvas, font, "PAGE 1 - CUTTING / MACHINING DRAWING", 1)
    origin_x, origin_y, scale = 52.0, 67.0, 2.28
    _pdf_plate(canvas, origin_x, origin_y, scale, font, show_ids=True)
    _pdf_dimension(
        canvas,
        font,
        origin_x,
        origin_y - 18,
        origin_x + PLATE["width"] * scale,
        origin_y - 18,
        "220.00 +/- 0.20",
    )
    _pdf_dimension(
        canvas,
        font,
        origin_x - 18,
        origin_y,
        origin_x - 18,
        origin_y + PLATE["height"] * scale,
        "190.00 +/- 0.20",
        vertical=True,
    )
    canvas.setFillColor(HexColor("#173f35"))
    canvas.setFont(font, 8.5)
    canvas.drawString(580, page_height - 55, "MANUFACTURING DATA")
    canvas.setFillColor(HexColor("#222222"))
    canvas.setFont(font, 6.3)
    notes = [
        "MATERIAL: CLEAR CAST PMMA",
        "THICKNESS: 5.00 mm",
        "CORNER: 4x R10.00",
        "OUTLINE TOLERANCE: +/-0.20",
        "HOLE/CUTOUT TOLERANCE: +/-0.10",
        "PROCESS: CNC OR LASER",
        "UNITS: mm   SCALE: 1:1",
        "DO NOT SCALE / FIT TO PAGE",
        "DEBURR; KEEP PROTECTIVE FILM",
    ]
    y = page_height - 72
    for note in notes:
        canvas.drawString(580, y, note)
        y -= 12
    canvas.setFillColor(HexColor("#a32626"))
    canvas.setFont(font, 6.1)
    for warning in (
        "ALL MACHINING COORDINATES ARE",
        "DEFINED FROM FRONT VIEW.",
        "DO NOT MIRROR CUTTING GEOMETRY.",
    ):
        canvas.drawString(580, y - 2, warning)
        y -= 11
    canvas.setFillColor(HexColor("#173f35"))
    canvas.setFont(font, 8.0)
    canvas.drawString(580, y - 5, "FEATURE SCHEDULE")
    y -= 18
    canvas.setFillColor(HexColor("#222222"))
    canvas.setFont(font, 5.7)
    for row in coordinate_rows():
        if row["TYPE"] == "CIRCULAR HOLE":
            line = (
                f"{row['ID']:>3} X={float(row['X']):7.3f} "
                f"Y={float(row['Y']):7.3f} {row['WIDTH_OR_DIAMETER']}"
            )
        else:
            line = "W1 C=(170.000,91.000) 32.00 x 18.00 R3.00 H"
        canvas.drawString(580, y, line)
        y -= 10
    canvas.setFillColor(HexColor("#173f35"))
    canvas.setFont(font, 6.1)
    canvas.drawString(
        52,
        35,
        "MAINBOARD PITCH 114.95 x 84.96 | POWER PITCH 58.00 x 49.00 | W1 32.00 x 18.00 R3.00",
    )


def _pdf_page_two(canvas, font):
    from reportlab.lib.colors import HexColor
    from reportlab.lib.pagesizes import A4, landscape

    page_width, page_height = landscape(A4)
    _pdf_title(canvas, font, "PAGE 2 - FRONT ASSEMBLY REFERENCE", 2)
    origin_x, origin_y, scale = 50.0, 67.0, 2.30
    _pdf_plate(canvas, origin_x, origin_y, scale, font, face="front", show_ids=False)
    canvas.setFillColor(HexColor("#173f35"))
    canvas.setFont(font, 8.5)
    canvas.drawString(590, page_height - 56, "FRONT ASSEMBLY")
    canvas.setFillColor(HexColor("#222222"))
    canvas.setFont(font, 6.3)
    notes = [
        "MAINBOARD: 4x M3, 8 mm STANDOFF",
        "CSI CONNECTOR ON LOWER EDGE",
        "LENS CENTER: X119.00 Y35.00",
        "PCB EDGE TO LENS: APPROX. 50.00",
        "CAMERA FLEX: STRAIGHT, NO SIDE TWIST",
        "CAMERA: THIN DOUBLE-SIDED TAPE",
        "RADAR: 15x44 VERTICAL, NOTCH UP",
        "RADAR ANTENNA FACE: +Y / USER",
        "RADAR: 2x M2 NOTCH CLAMPS",
        "SPEAKER: 1 mm EVA TAPE",
        "SERVO: HOT GLUE; SHAFT OUTWARD",
        "CAMERA/SPEAKER/SERVO: NO CUT HOLES",
    ]
    y = page_height - 75
    for note in notes:
        canvas.drawString(590, y, note)
        y -= 14
    canvas.setFillColor(HexColor("#a32626"))
    canvas.setFont(font, 6.1)
    canvas.drawString(590, y - 4, "REFERENCE OUTLINES ARE NOT CUT GEOMETRY.")
    canvas.drawString(590, y - 17, "VERIFY PORT ACCESS BEFORE FINAL ASSEMBLY.")
    canvas.setFillColor(HexColor("#173f35"))
    canvas.setFont(font, 6.3)
    canvas.drawCentredString(
        page_width / 2,
        32,
        "CAMERA FOCAL PLANE, MAINBOARD FRONT, AND RADAR ANTENNA PLANE FACE THE SAME DIRECTION (+Y).",
    )


def _pdf_page_three(canvas, font):
    from reportlab.lib.colors import HexColor
    from reportlab.lib.pagesizes import A4, landscape

    page_width, page_height = landscape(A4)
    _pdf_title(canvas, font, "PAGE 3 - REAR ASSEMBLY REFERENCE", 3)
    origin_x, origin_y, scale = 50.0, 67.0, 2.30
    _pdf_plate(canvas, origin_x, origin_y, scale, font, face="rear", show_ids=True)
    canvas.setFillColor(HexColor("#173f35"))
    canvas.setFont(font, 8.5)
    canvas.drawString(590, page_height - 56, "REAR ASSEMBLY")
    canvas.setFillColor(HexColor("#222222"))
    canvas.setFont(font, 6.3)
    notes = [
        "POWER PCB ORIGIN: X120.00 Y15.00",
        "PCB: 85.10 x 56.00 x 1.60",
        "MAX PLAN ENVELOPE: 85.10 x 61.45",
        "ASSEMBLED DEPTH: APPROX. 18.10",
        "MOUNT: 4x M2, 10 mm STANDOFF",
        "COMPONENT SIDE FACES REAR-OUT",
        "HOLE PITCH: 58.00 x 49.00",
        "W1 CLEARANCE ABOVE ENVELOPE: 5.55",
        "ROUTE POWER/SIGNAL THROUGH W1",
        "KEEP WIRES OUT OF CAMERA FLEX PATH",
    ]
    y = page_height - 75
    for note in notes:
        canvas.drawString(590, y, note)
        y -= 14
    canvas.setFillColor(HexColor("#a32626"))
    canvas.setFont(font, 6.2)
    warnings = [
        "THIS PAGE SHOWS REAR COMPONENTS",
        "USING FRONT-VIEW DATUM COORDINATES.",
        "DO NOT FLIP X VALUES.",
        "DO NOT MIRROR CUTTING GEOMETRY.",
    ]
    for warning in warnings:
        canvas.drawString(590, y - 4, warning)
        y -= 13
    canvas.setFillColor(HexColor("#173f35"))
    canvas.setFont(font, 6.3)
    canvas.drawCentredString(page_width / 2, 32, COORDINATE_WARNING)


def _pdf_page_four(canvas, font):
    from reportlab.lib.colors import HexColor
    from reportlab.lib.pagesizes import A4, landscape

    page_width, page_height = landscape(A4)
    _pdf_title(canvas, font, "PAGE 4 - HOLE / CUTOUT COORDINATE TABLE", 4)
    coordinate_data = [
        ["ID", "TYPE", "X", "Y", "WIDTH / DIA", "HEIGHT / LENGTH", "RADIUS", "PURPOSE / REMARK"]
    ]
    for row in coordinate_rows():
        coordinate_data.append(
            [
                row["ID"],
                row["TYPE"],
                row["X"],
                row["Y"],
                row["WIDTH_OR_DIAMETER"],
                row["HEIGHT_OR_LENGTH"],
                row["RADIUS"],
                f"{row['PURPOSE']}; {row['REMARK']}".strip("; "),
            ]
        )
    _pdf_table(
        canvas,
        coordinate_data,
        28,
        page_height - 48,
        [30, 72, 42, 42, 62, 70, 42, 255],
        [17] + [15] * (len(coordinate_data) - 1),
        font,
        5.3,
    )
    audit_data = [["ITEM", "OLD V2", "SOURCE / USER INPUT", "FINAL V3", "RESULT"]]
    for row in DIMENSION_AUDIT:
        audit_data.append(
            [row["item"], row["old"], row["source"], row["final"], row["status"]]
        )
    audit_top = 282
    canvas.setFillColor(HexColor("#173f35"))
    canvas.setFont(font, 8.0)
    canvas.drawString(28, audit_top + 10, "DIMENSION VERIFICATION AND CONFLICT CHECK")
    _pdf_table(
        canvas,
        audit_data,
        28,
        audit_top,
        [72, 155, 210, 195, 150],
        [17] + [22] * (len(audit_data) - 1),
        font,
        5.1,
    )
    canvas.setFillColor(HexColor("#a32626"))
    canvas.setFont(font, 5.8)
    canvas.drawString(
        28,
        56,
        "CAMERA DIMENSIONS ARE USER-AUTHORIZED PHOTO-SCALE ESTIMATES: +/-1 mm LATERAL, +/-2 mm LENGTH/LENS POSITION.",
    )
    canvas.drawString(
        28,
        44,
        "CAMERA GEOMETRY IS ASSEMBLY REFERENCE ONLY AND DOES NOT CONTROL ANY CUT FEATURE.",
    )
    canvas.setFillColor(HexColor("#173f35"))
    canvas.drawString(28, 27, COORDINATE_WARNING)


def write_pdf(path):
    from reportlab.lib.pagesizes import A4, landscape
    from reportlab.pdfgen import canvas

    font = _pdf_font()
    pdf = canvas.Canvas(str(path), pagesize=landscape(A4), invariant=1)
    pdf.setTitle("Acrylic Backplate Final Production Drawing V3")
    pdf.setAuthor("OpenAI Codex")
    pdf.setSubject("220 x 190 x 5 mm clear cast PMMA engineering drawing")
    _pdf_page_one(pdf, font)
    pdf.showPage()
    _pdf_page_two(pdf, font)
    pdf.showPage()
    _pdf_page_three(pdf, font)
    pdf.showPage()
    _pdf_page_four(pdf, font)
    pdf.showPage()
    pdf.save()


README_TEXT = """# Acrylic Backplate Final Production Package V3

## Vendor cutting instructions / 加工说明

- Primary production file: `acrylic_plate_cut_only_v3.dxf`
- Layered engineering file: `acrylic_plate_layered_v3.dxf`
- Units: mm
- Scale: 1:1
- Finished size: 220.00 x 190.00 x 5.00 mm
- Material: transparent clear cast PMMA / 透明浇铸亚克力
- Four external corners: R10.00
- Outline tolerance: +/-0.20 mm
- Hole and cutout tolerance: +/-0.10 mm
- Process: CNC or laser cutting
- Deburr every hole and edge; retain protective film for delivery.
- Do not scale, fit to page, mirror, or manually estimate geometry.
- ALL MACHINING COORDINATES ARE DEFINED FROM FRONT VIEW.
- DO NOT MIRROR CUTTING GEOMETRY.

## Machining summary / 加工特征

- 4x corner-support holes, diameter 3.20 mm.
- 4x mainboard holes, diameter 3.20 mm.
- 2x LD2450 diagonal-notch clamp holes, diameter 2.20 mm.
- 4x rear power-board holes, diameter 2.20 mm.
- 1x rounded rectangular cable pass-through, 32.00 x 18.00 mm, R3.00.
- Camera, 3525 speaker, and MG90S servo have no acrylic cutting holes.

## Assembly notes / 装配说明

- Mainboard: 4x M3, 8 mm brass standoffs, insulating washers.
- Camera: thin double-sided tape; keep CSI flex straight; lens centre approximately
  50 mm below the mainboard edge. Camera dimensions are photo-scale estimates and
  do not control cutting geometry.
- LD2450: 15 x 44 mm vertical, notch end upward, antenna face toward the user;
  clamp at the two official R1.1 diagonal notch centres using M2 standoffs and
  nylon washers with 0.5-0.8 mm overlap.
- 3525 speaker: 4 ohm / 3 W, fixed with 1 mm EVA double-sided tape.
- MG90S: fixed with hot glue; output shaft normal to the plate and facing outward.
- Power board: 4x M2, 10 mm brass standoffs, components facing rear-out.

## Package files

- `acrylic_plate_cut_only_v3.dxf` - authoritative cut-only geometry
- `acrylic_plate_layered_v3.dxf` - CUT/REFERENCE/TEXT/DIMENSION layers
- `acrylic_plate_manufacturing_drawing_v3.pdf` - four-page engineering drawing
- `acrylic_plate_front_assembly_v3.svg` - front assembly reference
- `acrylic_plate_rear_assembly_v3.svg` - rear assembly reference
- `acrylic_plate_hole_table_v3.csv` - machining coordinate table
"""


def build_vendor_package(acrylic_dir: Path, pdf_path: Path):
    acrylic_dir = Path(acrylic_dir)
    pdf_path = Path(pdf_path)
    readme_path = acrylic_dir / "README.md"
    readme_path.write_text(README_TEXT, encoding="utf-8")
    package_path = acrylic_dir / "acrylic_plate_manufacturing_package_v3.zip"
    delivery_files = [
        acrylic_dir / "acrylic_plate_cut_only_v3.dxf",
        acrylic_dir / "acrylic_plate_layered_v3.dxf",
        pdf_path,
        acrylic_dir / "acrylic_plate_front_assembly_v3.svg",
        acrylic_dir / "acrylic_plate_rear_assembly_v3.svg",
        acrylic_dir / "acrylic_plate_hole_table_v3.csv",
        readme_path,
    ]
    with zipfile.ZipFile(
        package_path, "w", compression=zipfile.ZIP_DEFLATED
    ) as archive:
        for source in delivery_files:
            entry = zipfile.ZipInfo(
                source.name, date_time=(2000, 1, 1, 0, 0, 0)
            )
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o644 << 16
            archive.writestr(entry, source.read_bytes())
    return package_path


def generate_all(output_dir: Path, include_pdf=True, pdf_output_dir=None):
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    outputs = {
        "cut_dxf": output_dir / "acrylic_plate_cut_only_v3.dxf",
        "layered_dxf": output_dir / "acrylic_plate_layered_v3.dxf",
        "front_svg": output_dir / "acrylic_plate_front_assembly_v3.svg",
        "rear_svg": output_dir / "acrylic_plate_rear_assembly_v3.svg",
        "csv": output_dir / "acrylic_plate_hole_table_v3.csv",
    }
    write_dxf(outputs["cut_dxf"], layered=False)
    write_dxf(outputs["layered_dxf"], layered=True)
    write_front_svg(outputs["front_svg"])
    write_rear_svg(outputs["rear_svg"])
    write_csv(outputs["csv"])
    if include_pdf:
        pdf_dir = Path(pdf_output_dir) if pdf_output_dir else output_dir
        pdf_dir.mkdir(parents=True, exist_ok=True)
        outputs["pdf"] = pdf_dir / "acrylic_plate_manufacturing_drawing_v3.pdf"
        write_pdf(outputs["pdf"])
    return outputs


def main():
    repo_root = Path(__file__).resolve().parents[2]
    outputs = generate_all(
        repo_root / "output" / "acrylic",
        include_pdf=True,
        pdf_output_dir=repo_root / "output" / "pdf",
    )
    outputs["package"] = build_vendor_package(
        repo_root / "output" / "acrylic", outputs["pdf"]
    )
    for output in outputs.values():
        print(output)


if __name__ == "__main__":
    main()
