"""Pi controller <-> protocol-level fake STM32 e2e (no hardware)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app.controller.controller import Controller, ControllerState  # noqa: E402
from app.controller.standin_transport import StandinTransport  # noqa: E402


class StandinE2ETests(unittest.TestCase):
    def setUp(self):
        self.standin = StandinTransport(session_id=0x0BADF00D)
        self.ctl = Controller(self.standin, clock=lambda: self.standin.now_ms / 1000.0)
        self.ctl.connect()

    def sim(self, ms: int) -> None:
        for _ in range(max(1, ms // 100)):
            self.standin.advance(100)
            self.ctl.step()

    def test_grasp_flow_over_wire_codec(self):
        self.assertTrue(self.ctl.grasp())
        self.sim(1000)
        self.assertEqual(self.ctl.state, ControllerState.HOLDING)
        self.assertIsNotNone(self.ctl.last_actual)

    def test_stop_and_reset_over_wire_codec(self):
        self.assertTrue(self.ctl.grasp())
        self.sim(300)
        self.ctl.stop()
        self.sim(100)
        self.assertTrue(self.ctl.stop_confirmed)
        self.assertTrue(self.ctl.reset())
        self.sim(100)
        self.assertEqual(self.ctl.state, ControllerState.IDLE)


if __name__ == "__main__":
    unittest.main(verbosity=2)
