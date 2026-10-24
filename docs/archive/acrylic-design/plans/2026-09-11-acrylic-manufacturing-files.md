# Acrylic Manufacturing Files Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Generate a dimensionally checked DXF cut file, an annotated SVG reference, and a rendered PDF manufacturing drawing for the 220 x 190 x 5 mm single-acrylic test rig.

**Architecture:** Keep every coordinate in one Python geometry module using a front-view, lower-left origin in millimetres. A generator consumes that geometry to write a minimal ASCII DXF for laser cutting, an annotated SVG for inspection, and a ReportLab PDF for the acrylic vendor. Unit tests validate dimensions, hole spacing, edge clearances, and collisions before any output is accepted.

**Tech Stack:** Python 3, standard-library unittest, hand-written DXF R12 entities, SVG, ReportLab, Poppler.

---

### Task 1: Lock the manufacturing geometry

**Files:**
- Create: cad/acrylic_plate/layout_spec.py
- Create: cad/acrylic_plate/test_geometry.py

- [ ] **Step 1: Write geometry tests**

~~~python
import math
import unittest
from layout_spec import PLATE, HOLES

class GeometryTests(unittest.TestCase):
    def test_plate_size(self):
        self.assertEqual(
            (PLATE["width"], PLATE["height"], PLATE["thickness"]),
            (220.0, 190.0, 5.0),
        )

    def test_mainboard_hole_spacing(self):
        pts = [h for h in HOLES if h["group"] == "mainboard"]
        xs = sorted({p["x"] for p in pts})
        ys = sorted({p["y"] for p in pts})
        self.assertAlmostEqual(xs[1] - xs[0], 114.95, places=2)
        self.assertAlmostEqual(ys[1] - ys[0], 84.96, places=2)

    def test_power_hole_spacing(self):
        pts = [h for h in HOLES if h["group"] == "power"]
        xs = sorted({p["x"] for p in pts})
        ys = sorted({p["y"] for p in pts})
        self.assertAlmostEqual(xs[1] - xs[0], 58.0, places=2)
        self.assertAlmostEqual(ys[1] - ys[0], 49.0, places=2)

    def test_every_hole_stays_inside_plate(self):
        for hole in HOLES:
            radius = hole["diameter"] / 2
            self.assertGreaterEqual(hole["x"] - radius, 0)
            self.assertGreaterEqual(hole["y"] - radius, 0)
            self.assertLessEqual(hole["x"] + radius, PLATE["width"])
            self.assertLessEqual(hole["y"] + radius, PLATE["height"])

    def test_distinct_holes_have_material_between_them(self):
        for i, left in enumerate(HOLES):
            for right in HOLES[i + 1:]:
                distance = math.hypot(left["x"] - right["x"], left["y"] - right["y"])
                required = left["diameter"] / 2 + right["diameter"] / 2 + 2.0
                self.assertGreaterEqual(distance, required, (left["id"], right["id"]))
~~~

- [ ] **Step 2: Run tests and confirm failure**

Run:

~~~powershell
& 'C:\Users\dell\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' -m unittest cad\acrylic_plate\test_geometry.py -v
~~~

Expected: failure because layout_spec does not exist.

- [ ] **Step 3: Implement the coordinate source**

Create constants for:

- Plate: 220 x 190 x 5 mm, R10.
- Shared corner holes: (10,10), (210,10), (10,180), (210,180), diameter 3.2.
- Mainboard reference lower-left: (15,85); hole centres (17.525,87.52), (132.475,87.52), (17.525,172.48), (132.475,172.48), diameter 3.2.
- Camera-bracket horizontal slots: centres (105,25), (129,25), (105,65), (129,65), size 6 x 3.2.
- Radar-bracket holes: (65,20), (95,20), (65,75), (95,75), diameter 2.7.
- Speaker-bracket holes: (150,25), (200,25), (150,65), (200,65), diameter 2.7.
- Servo-bracket holes: (155,120), (195,120), (155,165), (195,165), diameter 2.7.
- Door-hinge holes: (210,115), (210,140), (210,165), diameter 2.7.
- Rear power-board holes: (141.55,86.225), (199.55,86.225), (141.55,135.225), (199.55,135.225), diameter 2.2.
- Cable pass-through: (145,72), diameter 8.

Also define non-cut footprints for the board, camera axis, vertical radar, 3525 speaker, servo, door, and rear power board.

- [ ] **Step 4: Run geometry tests**

Expected: all tests pass.

- [ ] **Step 5: Commit geometry and tests**

~~~powershell
git add cad/acrylic_plate/layout_spec.py cad/acrylic_plate/test_geometry.py
git commit -m "feat: define acrylic plate manufacturing geometry"
~~~

### Task 2: Generate DXF and SVG files

**Files:**
- Create: cad/acrylic_plate/generate_plate.py
- Create: output/acrylic/acrylic_plate_cut_v1.dxf
- Create: output/acrylic/acrylic_plate_reference_v1.svg

- [ ] **Step 1: Add output-content tests**

Extend test_geometry.py to call generate_all(temp_dir) and assert:

~~~python
self.assertIn("$INSUNITS", dxf_text)
self.assertIn("\n70\n4\n", dxf_text)
self.assertIn("CUT_OUTLINE", dxf_text)
self.assertIn("CUT_HOLES", dxf_text)
self.assertIn('width="220mm"', svg_text)
self.assertIn("3525 SPEAKER", svg_text)
self.assertIn("RADAR 44x15 VERTICAL", svg_text)
~~~

- [ ] **Step 2: Run tests and confirm failure**

Expected: import or missing-output failure because generate_plate.py does not exist.

- [ ] **Step 3: Implement deterministic vector generation**

Implement:

~~~python
def generate_all(output_dir: Path) -> dict[str, Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    dxf = output_dir / "acrylic_plate_cut_v1.dxf"
    svg = output_dir / "acrylic_plate_reference_v1.svg"
    write_dxf(dxf)
    write_svg(svg)
    return {"dxf": dxf, "svg": svg}
~~~

DXF contents:

- Rounded outer profile on layer CUT_OUTLINE.
- All circular holes and rounded slots on layer CUT_HOLES.
- Millimetre unit header.
- No text or reference geometry in the vendor cut file.

SVG contents:

- Red cut lines.
- Dark-green front references.
- Dashed amber rear power-board reference.
- Hole IDs and lower-left origin marker.
- Note that coordinates are front-view and must not be mirrored.

- [ ] **Step 4: Generate and test**

~~~powershell
& 'C:\Users\dell\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' cad\acrylic_plate\generate_plate.py
& 'C:\Users\dell\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' -m unittest cad\acrylic_plate\test_geometry.py -v
~~~

Expected: both files generated and all tests pass.

- [ ] **Step 5: Commit vector outputs**

~~~powershell
git add cad/acrylic_plate output/acrylic
git commit -m "feat: generate acrylic plate DXF and SVG"
~~~

### Task 3: Generate the annotated manufacturing PDF

**Files:**
- Modify: cad/acrylic_plate/generate_plate.py
- Create: output/pdf/acrylic_plate_manufacturing_drawing_v1.pdf

- [ ] **Step 1: Mark the PDF authoring operation**

Run exactly once before creating the PDF:

~~~powershell
& 'C:\Users\dell\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe' 'C:\Users\dell\.cache\codex-runtimes\codex-primary-runtime\container_tools\mark_artifact_operation_started.mjs' --operation-kind create --expected-output-count 1 --output-format pdf
~~~

- [ ] **Step 2: Add PDF tests**

Generate into a temporary directory, reopen with pypdf.PdfReader, and assert:

~~~python
self.assertEqual(len(reader.pages), 2)
self.assertEqual(reader.metadata.title, "Single Acrylic Test Rig Manufacturing Drawing")
~~~

- [ ] **Step 3: Run tests and confirm the PDF test fails**

Expected: PDF output is absent.

- [ ] **Step 4: Implement ReportLab drawing**

Page 1 contains the 1:1-coordinate plate drawing, 220 x 190 dimensions, hole IDs, hole schedule, material, thickness, tolerance, and vendor notes.

Page 2 contains front and rear assembly references for the mainboard, 50 mm straight camera path, vertical LD2450, 3525 4-ohm 3-watt speaker, MG90S door mechanism, rear power board, and shared corner supports.

- [ ] **Step 5: Generate PDF and rerun tests**

Expected: two pages, matching metadata, and all tests pass.

- [ ] **Step 6: Commit PDF**

~~~powershell
git add cad/acrylic_plate/generate_plate.py output/pdf/acrylic_plate_manufacturing_drawing_v1.pdf
git commit -m "docs: add acrylic manufacturing drawing PDF"
~~~

### Task 4: Validate and package the vendor files

**Files:**
- Create: output/acrylic/README.md
- Create: output/acrylic/acrylic_plate_manufacturing_package_v1.zip
- Create temporarily: tmp/pdfs/acrylic_plate_check/page-*.png

- [ ] **Step 1: Validate DXF geometry**

Check for:

- One rounded outer profile.
- 28 circular holes.
- Four rounded slots.
- Maximum extents exactly 220 x 190 mm.

- [ ] **Step 2: Inspect PDF metadata**

~~~powershell
pdfinfo output\pdf\acrylic_plate_manufacturing_drawing_v1.pdf
~~~

Expected: two pages, A4 landscape, unencrypted.

- [ ] **Step 3: Render and inspect PDF**

~~~powershell
New-Item -ItemType Directory -Force tmp\pdfs\acrylic_plate_check | Out-Null
pdftoppm -png -r 180 output\pdf\acrylic_plate_manufacturing_drawing_v1.pdf tmp\pdfs\acrylic_plate_check\page
~~~

Inspect both pages for clipped labels, overlaps, broken glyphs, and unreadable line weights.

- [ ] **Step 4: Write vendor README**

State that the cut DXF is 1:1 in millimetres, material is 5 mm clear cast PMMA, cut tolerance is plus-or-minus 0.2 mm, hole tolerance is plus-or-minus 0.1 mm, the drawing must not be scaled or mirrored, edges must be deburred, and protective film should remain.

- [ ] **Step 5: Create package**

ZIP contents:

- acrylic_plate_cut_v1.dxf
- acrylic_plate_reference_v1.svg
- acrylic_plate_manufacturing_drawing_v1.pdf
- README.md

- [ ] **Step 6: Final verification and commit**

Run all tests, list ZIP entries, and run git diff --check.

~~~powershell
git add output/acrylic output/pdf
git commit -m "chore: package acrylic vendor files"
~~~
