"""main.py --controller compatibility path (no hardware)."""

from __future__ import annotations

import io
import sys
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app.main import main  # noqa: E402


class MainControllerTests(unittest.TestCase):
    def test_controller_fake_script(self):
        out = io.StringIO()
        with redirect_stdout(out):
            rc = main([
                "--controller",
                "--controller-transport", "fake",
                "--controller-script", "grasp,release,status",
            ])
        self.assertEqual(rc, 0)
        text = out.getvalue()
        self.assertIn("HOLDING", text)
        self.assertIn("IDLE", text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
