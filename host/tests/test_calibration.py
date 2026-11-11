"""Calibration schema/loader tests (example values, not hardware)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app.control.calibration import load_calibration  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = ROOT / "data" / "calibration" / "servo_map.example.json"


class CalibrationTests(unittest.TestCase):
    def test_load_example(self):
        cal = load_calibration(EXAMPLE)
        self.assertEqual(len(cal.servos), 8)
        self.assertEqual(cal.by_id(1).direction, 1)
        self.assertEqual(cal.by_id(5).direction, -1)

    def test_deg_raw_roundtrip(self):
        cal = load_calibration(EXAMPLE)
        raw = cal.deg_to_raw(1, 0.0)
        self.assertEqual(raw, 2048)
        self.assertAlmostEqual(cal.raw_to_deg(1, raw), 0.0, places=6)

    def test_clamps_degrees(self):
        cal = load_calibration(EXAMPLE)
        self.assertEqual(cal.deg_to_raw(1, 999.0), 3400)
        self.assertEqual(cal.deg_to_raw(1, -999.0), 700)


if __name__ == "__main__":
    unittest.main(verbosity=2)
