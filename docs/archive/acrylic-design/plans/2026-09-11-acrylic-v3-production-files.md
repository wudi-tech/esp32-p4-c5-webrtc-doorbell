# Acrylic V3 Production Files Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the 220×190×5 mm acrylic carrier as a verified V3 manufacturing package with exact cut geometry, four-page engineering PDF, front/rear assembly references, and a coordinate table.

**Architecture:** Keep all authoritative dimensions in `layout_spec.py` using one front-view lower-left datum. Generate cut-only and layered DXF, two SVG assembly drawings, CSV, PDF, and ZIP from the same geometry model so no drawing can drift from the coordinate table. Validate source dimensions, clearances, layer contents, contour closure, PDF page count, and package contents with deterministic unit tests.

**Tech Stack:** Python 3.12, standard-library `unittest`, ASCII DXF R12, SVG, CSV, ReportLab, pypdf/PyMuPDF, bundled PDF rendering tools.

---

### Task 1: Replace V2 geometry with the approved V3 datum model

**Files:**
- Modify: `cad/acrylic_plate/layout_spec.py`
- Modify: `cad/acrylic_plate/test_geometry.py`

- [ ] **Step 1: Write failing V3 geometry tests**

Add tests that require plate 220×190×5 R10; corner holes at 10 mm; mainboard outline at (20,85) with 114.95×84.96 spacing; power-board outline at (120,15) with 58×49 spacing; W1 as 32×18 R3 at (170,91); no camera/speaker/servo/door cut features; and exactly two radar clamp holes.

- [ ] **Step 2: Run the focused tests and verify RED**

Run:

```powershell
python -m unittest cad.acrylic_plate.test_geometry -v
```

Expected: failures naming the obsolete V2 camera slots, four-hole radar bracket, speaker/servo/door holes, round W1, and old positions.

- [ ] **Step 3: Implement the minimum V3 geometry model**

Define typed dictionaries for `PLATE`, `HOLES`, `CUTOUTS`, `FRONT_REFERENCES`, `REAR_REFERENCES`, `DIMENSION_AUDIT`, and `NOTES`. Derive mainboard and power-hole coordinates from their approved lower-left origins rather than duplicating absolute values.

- [ ] **Step 4: Run the geometry tests and verify GREEN**

Expected: all geometry tests pass, including edge/material and feature-collision checks.

### Task 2: Extract and validate the two LD2450 clamp locations

**Files:**
- Modify: `cad/acrylic_plate/layout_spec.py`
- Modify: `cad/acrylic_plate/test_geometry.py`
- Reference: `E:/Espressif/radardatasheet/HLK-LD2450--3D图/HLK-LD2450--3Dͼ/HLK-LD2450-3D.step`
- Reference: `E:/Espressif/radardatasheet/HLK-LD2450(V1.0)封装/HLK-LD2450(V1.0)封装.dxf`

- [ ] **Step 1: Add a failing test for official diagonal-notch mapping**

Assert that the two M2 clamp-hole centers map to opposite LD2450 corner notches after rotation to the approved 15×44 vertical orientation, and that no radar hole intersects the 15×44 module body except at the intended washer overlap.

- [ ] **Step 2: Verify RED**

Run the single radar test and confirm it fails because V3 clamp locations are absent.

- [ ] **Step 3: Read the official STEP/DXF geometry and enter the derived centers**

Record the exact local notch-center coordinates and the rotation transform in comments next to the resulting global coordinates. Use two Ø2.20 acrylic holes for M2 fasteners and a drawing note requiring Ø5 nylon washers with 0.5–0.8 mm overlap.

- [ ] **Step 4: Verify GREEN and retain a source trace**

Run all geometry tests. The test failure messages must print both global radar-hole IDs and their local source coordinates.

### Task 3: Generate both manufacturing DXFs and coordinate CSV

**Files:**
- Modify: `cad/acrylic_plate/generate_plate.py`
- Create: `output/acrylic/acrylic_plate_cut_only_v3.dxf`
- Create: `output/acrylic/acrylic_plate_layered_v3.dxf`
- Create: `output/acrylic/acrylic_plate_hole_table_v3.csv`
- Modify: `cad/acrylic_plate/test_geometry.py`

- [ ] **Step 1: Add failing output tests**

Require `$INSUNITS=4`, V3 filenames, CUT/REFERENCE/TEXT/DIMENSION layers in the layered DXF, CUT-only entities in the vendor DXF, one closed R10 plate outline, all closed inner contours, and CSV columns `ID,TYPE,X,Y,WIDTH_OR_DIAMETER,HEIGHT_OR_LENGTH,RADIUS,QTY,PURPOSE,REMARK`.

- [ ] **Step 2: Verify RED**

Generate in a temporary directory and confirm the tests fail because the generator still emits V2 names and geometry.

- [ ] **Step 3: Implement deterministic DXF and CSV output**

Represent every rounded rectangle as four LINE and four ARC entities sharing exact endpoints. Put no text or reference entities in the cut-only file. Emit reference footprints, labels, and dimensions only in the layered file.

- [ ] **Step 4: Verify GREEN**

Parse the generated DXF entities in tests and assert expected contour/entity counts, unique geometry, exact extents 220×190, and no zero-length segments.

### Task 4: Generate separate front and rear assembly SVGs

**Files:**
- Modify: `cad/acrylic_plate/generate_plate.py`
- Create: `output/acrylic/acrylic_plate_front_assembly_v3.svg`
- Create: `output/acrylic/acrylic_plate_rear_assembly_v3.svg`
- Modify: `cad/acrylic_plate/test_geometry.py`

- [ ] **Step 1: Add failing SVG tests**

Require a 220 mm×190 mm viewBox, true mainboard/camera/radar/speaker/servo/power envelopes, CSI straight-axis annotation, +Y radar direction, interface keep-outs, hot-glue/double-sided-tape notes, and the non-mirrored rear-coordinate warning.

- [ ] **Step 2: Verify RED**

Confirm failure because the existing generator emits one obsolete combined V2 SVG.

- [ ] **Step 3: Implement the two reference drawings**

Draw components on reference layers only. Show the rear view visually from the rear but print all machining coordinates from the front-view datum and include `DO NOT MIRROR CUTTING GEOMETRY` prominently.

- [ ] **Step 4: Verify GREEN**

Parse both SVGs as XML and assert the required labels and component IDs occur exactly once.

### Task 5: Generate the four-page engineering PDF

**Files:**
- Modify: `cad/acrylic_plate/generate_plate.py`
- Create: `output/pdf/acrylic_plate_manufacturing_drawing_v3.pdf`
- Modify: `cad/acrylic_plate/test_geometry.py`

- [ ] **Step 1: Mark the PDF artifact operation exactly once**

Use the bundled artifact marker immediately before the first command that authors the V3 PDF.

- [ ] **Step 2: Add failing PDF tests**

Require exactly four pages, V3 title metadata, the machining-coordinate warning, page titles, all feature IDs, overall dimensions, tolerances, and the dimension-audit table.

- [ ] **Step 3: Verify RED**

Run the PDF tests and confirm failure because no V3 PDF exists.

- [ ] **Step 4: Implement the PDF pages**

PAGE 1 contains only machining geometry and complete manufacturing dimensions. PAGE 2 shows the front assembly. PAGE 3 shows the rear assembly. PAGE 4 contains the feature coordinate table, dimension audit, sources, and unresolved camera-estimate notice.

- [ ] **Step 5: Verify GREEN**

Reopen the PDF with pypdf/PyMuPDF and assert four pages and extractable required text.

### Task 6: Render, mechanically inspect, and package

**Files:**
- Modify: `output/acrylic/README.md`
- Create: `output/acrylic/acrylic_plate_manufacturing_package_v3.zip`
- Create temporarily: `tmp/pdfs/acrylic_v3_check/page-*.png`

- [ ] **Step 1: Run the complete automated suite**

Run all geometry/output tests and `git diff --check`; expected result is zero failures and no whitespace errors.

- [ ] **Step 2: Render all four PDF pages**

Render at 180–220 dpi with the bundled PDF renderer and inspect each page for clipped dimensions, overlapping labels, missing glyphs, ambiguous rear orientation, and unreadable tables.

- [ ] **Step 3: Correct any visual or mechanical defect through RED/GREEN tests**

For every discovered issue, first add a failing assertion, then minimally fix the generator and rerun the suite.

- [ ] **Step 4: Write the V3 vendor README and ZIP package**

Package the cut-only DXF, layered DXF, four-page PDF, both assembly SVGs, CSV, and README. State mm/1:1/no scale/no mirror, material, tolerances, deburring, and protective-film requirements.

- [ ] **Step 5: Verify the package**

List ZIP entries, compare their hashes to the standalone outputs, and confirm the pure-cut DXF contains no REFERENCE/TEXT/DIMENSION geometry.

