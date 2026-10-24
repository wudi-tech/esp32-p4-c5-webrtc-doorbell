import csv
import math
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


class GeometryTests(unittest.TestCase):
    def _spec(self):
        from cad.acrylic_plate import layout_spec

        return layout_spec

    def test_plate_matches_approved_manufacturing_baseline(self):
        spec = self._spec()
        self.assertEqual(
            (
                spec.PLATE["width"],
                spec.PLATE["height"],
                spec.PLATE["thickness"],
                spec.PLATE["corner_radius"],
            ),
            (220.0, 190.0, 5.0, 10.0),
        )

    def test_only_approved_holes_remain(self):
        spec = self._spec()
        counts = {
            group: len([hole for hole in spec.HOLES if hole["group"] == group])
            for group in {hole["group"] for hole in spec.HOLES}
        }
        self.assertEqual(
            counts,
            {
                "corner_support": 4,
                "mainboard": 4,
                "radar_clamp": 2,
                "power": 4,
            },
        )
        self.assertEqual(len(spec.HOLES), 14)
        self.assertTrue(
            {"camera", "speaker", "servo", "door_hinge"}.isdisjoint(counts)
        )

    def test_corner_support_holes_are_ten_millimetres_from_edges(self):
        spec = self._spec()
        actual = {
            (hole["x"], hole["y"], hole["diameter"])
            for hole in spec.HOLES
            if hole["group"] == "corner_support"
        }
        self.assertEqual(
            actual,
            {
                (10.0, 10.0, 3.2),
                (210.0, 10.0, 3.2),
                (10.0, 180.0, 3.2),
                (210.0, 180.0, 3.2),
            },
        )

    def test_mainboard_holes_are_derived_from_approved_outline(self):
        spec = self._spec()
        board = next(
            item for item in spec.FRONT_REFERENCES if item["id"] == "MAINBOARD"
        )
        self.assertEqual(
            (board["x"], board["y"], board["width"], board["height"]),
            (20.0, 85.0, 120.0, 90.0),
        )
        points = [hole for hole in spec.HOLES if hole["group"] == "mainboard"]
        self.assertEqual(
            {(point["x"], point["y"], point["diameter"]) for point in points},
            {
                (22.525, 87.52, 3.2),
                (137.475, 87.52, 3.2),
                (22.525, 172.48, 3.2),
                (137.475, 172.48, 3.2),
            },
        )
        xs = sorted({point["x"] for point in points})
        ys = sorted({point["y"] for point in points})
        self.assertAlmostEqual(xs[1] - xs[0], 114.95, places=3)
        self.assertAlmostEqual(ys[1] - ys[0], 84.96, places=3)

    def test_camera_axis_is_straight_and_has_no_cut_features(self):
        spec = self._spec()
        camera = next(
            item for item in spec.FRONT_REFERENCES if item["id"] == "CAMERA"
        )
        self.assertEqual(camera["csi_center_x"], 119.0)
        self.assertEqual((camera["lens_x"], camera["lens_y"]), (119.0, 35.0))
        self.assertEqual(camera["board_edge_to_lens"], 50.0)
        self.assertFalse(any("CAM" in hole["id"] for hole in spec.HOLES))

    def test_radar_uses_two_official_diagonal_notch_centres(self):
        spec = self._spec()
        radar = next(
            item for item in spec.FRONT_REFERENCES if item["id"] == "RADAR"
        )
        self.assertEqual(
            (radar["x"], radar["y"], radar["width"], radar["height"]),
            (27.5, 26.0, 15.0, 44.0),
        )
        holes = [hole for hole in spec.HOLES if hole["group"] == "radar_clamp"]
        self.assertEqual(len(holes), 2)
        self.assertEqual(
            {
                (round(hole["x"], 4), round(hole["y"], 4), hole["diameter"])
                for hole in holes
            },
            {
                (28.2778, 26.7778, 2.2),
                (41.7222, 69.2222, 2.2),
            },
        )

    def test_speaker_and_servo_are_reference_only(self):
        spec = self._spec()
        references = {item["id"]: item for item in spec.FRONT_REFERENCES}
        self.assertEqual(
            (
                references["SPEAKER"]["center_x"],
                references["SPEAKER"]["center_y"],
                references["SPEAKER"]["width"],
                references["SPEAKER"]["height"],
            ),
            (70.0, 48.0, 35.0, 25.0),
        )
        self.assertEqual(
            (references["SERVO"]["center_x"], references["SERVO"]["center_y"]),
            (190.0, 145.0),
        )
        self.assertFalse(
            any(hole["group"] in {"speaker", "servo"} for hole in spec.HOLES)
        )

    def test_power_board_holes_match_approved_absolute_coordinates(self):
        spec = self._spec()
        power = next(
            item for item in spec.REAR_REFERENCES if item["id"] == "POWER_BOARD"
        )
        self.assertEqual(
            (power["x"], power["y"], power["width"], power["height"]),
            (120.0, 15.0, 85.1, 56.0),
        )
        points = [hole for hole in spec.HOLES if hole["group"] == "power"]
        self.assertEqual(
            {(point["x"], point["y"], point["diameter"]) for point in points},
            {
                (143.6, 18.5, 2.2),
                (201.6, 18.5, 2.2),
                (143.6, 67.5, 2.2),
                (201.6, 67.5, 2.2),
            },
        )
        xs = sorted({point["x"] for point in points})
        ys = sorted({point["y"] for point in points})
        self.assertAlmostEqual(xs[1] - xs[0], 58.0, places=3)
        self.assertAlmostEqual(ys[1] - ys[0], 49.0, places=3)

    def test_w1_is_the_approved_rounded_rectangular_cutout(self):
        spec = self._spec()
        self.assertEqual(len(spec.CUTOUTS), 1)
        cutout = spec.CUTOUTS[0]
        self.assertEqual(
            (
                cutout["id"],
                cutout["type"],
                cutout["center_x"],
                cutout["center_y"],
                cutout["width"],
                cutout["height"],
                cutout["radius"],
                cutout["orientation"],
            ),
            (
                "W1",
                "rounded_rectangle",
                170.0,
                91.0,
                32.0,
                18.0,
                3.0,
                "HORIZONTAL",
            ),
        )

    def test_every_cut_feature_stays_inside_plate(self):
        spec = self._spec()
        for hole in spec.HOLES:
            radius = hole["diameter"] / 2
            self.assertGreaterEqual(hole["x"] - radius, 0, hole["id"])
            self.assertGreaterEqual(hole["y"] - radius, 0, hole["id"])
            self.assertLessEqual(
                hole["x"] + radius, spec.PLATE["width"], hole["id"]
            )
            self.assertLessEqual(
                hole["y"] + radius, spec.PLATE["height"], hole["id"]
            )
        for cutout in spec.CUTOUTS:
            self.assertGreaterEqual(
                cutout["center_x"] - cutout["width"] / 2, 0, cutout["id"]
            )
            self.assertGreaterEqual(
                cutout["center_y"] - cutout["height"] / 2, 0, cutout["id"]
            )
            self.assertLessEqual(
                cutout["center_x"] + cutout["width"] / 2,
                spec.PLATE["width"],
                cutout["id"],
            )
            self.assertLessEqual(
                cutout["center_y"] + cutout["height"] / 2,
                spec.PLATE["height"],
                cutout["id"],
            )

    def test_distinct_holes_have_two_millimetres_of_material(self):
        spec = self._spec()
        for index, left in enumerate(spec.HOLES):
            for right in spec.HOLES[index + 1 :]:
                distance = math.hypot(
                    left["x"] - right["x"], left["y"] - right["y"]
                )
                required = left["diameter"] / 2 + right["diameter"] / 2 + 2.0
                self.assertGreaterEqual(distance, required, (left["id"], right["id"]))

    def test_holes_clear_the_cable_cutout_by_two_millimetres(self):
        spec = self._spec()
        cutout = spec.CUTOUTS[0]
        left = cutout["center_x"] - cutout["width"] / 2
        right = cutout["center_x"] + cutout["width"] / 2
        bottom = cutout["center_y"] - cutout["height"] / 2
        top = cutout["center_y"] + cutout["height"] / 2
        for hole in spec.HOLES:
            closest_x = min(max(hole["x"], left), right)
            closest_y = min(max(hole["y"], bottom), top)
            distance = math.hypot(
                hole["x"] - closest_x, hole["y"] - closest_y
            )
            self.assertGreaterEqual(
                distance, hole["diameter"] / 2 + 2.0, hole["id"]
            )


class VectorOutputTests(unittest.TestCase):
    def test_generator_can_run_directly_from_repo_root(self):
        repo_root = Path(__file__).resolve().parents[2]
        script = repo_root / "cad" / "acrylic_plate" / "generate_plate.py"
        result = subprocess.run(
            [sys.executable, str(script)],
            cwd=repo_root,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("acrylic_plate_cut_only_v3.dxf", result.stdout)

    def test_dxf_and_csv_outputs_have_v3_structure(self):
        from cad.acrylic_plate.generate_plate import generate_all

        with tempfile.TemporaryDirectory() as temp_dir:
            outputs = generate_all(Path(temp_dir), include_pdf=False)
            cut_text = outputs["cut_dxf"].read_text(encoding="ascii")
            layered_text = outputs["layered_dxf"].read_text(encoding="utf-8")
            with outputs["csv"].open(encoding="utf-8-sig", newline="") as stream:
                rows = list(csv.DictReader(stream))

        self.assertIn("$INSUNITS", cut_text)
        self.assertIn("\n70\n4\n", cut_text)
        self.assertIn("CUT", cut_text)
        self.assertNotIn("REFERENCE", cut_text)
        self.assertNotIn("DIMENSION", cut_text)
        for layer in ("CUT", "REFERENCE", "TEXT", "DIMENSION"):
            self.assertIn(layer, layered_text)
        self.assertEqual(
            list(rows[0]),
            [
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
            ],
        )
        self.assertEqual(
            {row["ID"] for row in rows},
            {
                "C1",
                "C2",
                "C3",
                "C4",
                "MB1",
                "MB2",
                "MB3",
                "MB4",
                "R1",
                "R2",
                "P1",
                "P2",
                "P3",
                "P4",
                "W1",
            },
        )

    def test_separate_front_and_rear_svg_references(self):
        from cad.acrylic_plate.generate_plate import generate_all

        with tempfile.TemporaryDirectory() as temp_dir:
            outputs = generate_all(Path(temp_dir), include_pdf=False)
            front = outputs["front_svg"].read_text(encoding="utf-8")
            rear = outputs["rear_svg"].read_text(encoding="utf-8")

        self.assertIn('width="220mm"', front)
        self.assertIn('height="190mm"', front)
        for label in ("WT99P4C5-S1", "CAMERA", "LD2450", "3525", "MG90S", "+Y"):
            self.assertIn(label, front)
        self.assertIn("CSI STRAIGHT", front)
        self.assertIn("POWER BOARD", rear)
        self.assertIn("DO NOT MIRROR CUTTING GEOMETRY", rear)


class PdfOutputTests(unittest.TestCase):
    def test_pdf_has_four_pages_and_v3_manufacturing_content(self):
        from pypdf import PdfReader

        from cad.acrylic_plate.generate_plate import generate_all

        with tempfile.TemporaryDirectory() as temp_dir:
            outputs = generate_all(Path(temp_dir), include_pdf=True)
            reader = PdfReader(str(outputs["pdf"]))
            text = "\n".join(
                page.extract_text() or "" for page in reader.pages
            )

        self.assertEqual(len(reader.pages), 4)
        self.assertEqual(
            reader.metadata.title,
            "Acrylic Backplate Final Production Drawing V3",
        )
        for label in (
            "PAGE 1 - CUTTING / MACHINING DRAWING",
            "PAGE 2 - FRONT ASSEMBLY REFERENCE",
            "PAGE 3 - REAR ASSEMBLY REFERENCE",
            "PAGE 4 - HOLE / CUTOUT COORDINATE TABLE",
            "ALL MACHINING COORDINATES ARE DEFINED FROM FRONT VIEW",
            "DO NOT MIRROR CUTTING GEOMETRY",
            "220.00",
            "190.00",
            "32.00 x 18.00",
            "W1",
        ):
            self.assertIn(label, text)


class PackageTests(unittest.TestCase):
    def test_vendor_zip_contains_complete_v3_delivery(self):
        from cad.acrylic_plate.generate_plate import (
            build_vendor_package,
            generate_all,
        )

        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            acrylic_dir = root / "acrylic"
            pdf_dir = root / "pdf"
            outputs = generate_all(
                acrylic_dir, include_pdf=True, pdf_output_dir=pdf_dir
            )
            package = build_vendor_package(acrylic_dir, outputs["pdf"])
            with zipfile.ZipFile(package) as archive:
                names = sorted(archive.namelist())
                member_times = {entry.date_time for entry in archive.infolist()}

        self.assertEqual(
            names,
            [
                "README.md",
                "acrylic_plate_cut_only_v3.dxf",
                "acrylic_plate_front_assembly_v3.svg",
                "acrylic_plate_hole_table_v3.csv",
                "acrylic_plate_layered_v3.dxf",
                "acrylic_plate_manufacturing_drawing_v3.pdf",
                "acrylic_plate_rear_assembly_v3.svg",
            ],
        )
        self.assertEqual(member_times, {(2000, 1, 1, 0, 0, 0)})


if __name__ == "__main__":
    unittest.main()
