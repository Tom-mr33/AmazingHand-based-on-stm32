"""Pi controller tests with deterministic fake STM32 (no hardware)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app.controller.controller import Controller, ControllerState  # noqa: E402
from app.controller.fake_transport import FakeTransport  # noqa: E402
from app.control import stm32_protocol_v2 as proto  # noqa: E402


class ControllerTests(unittest.TestCase):
    def setUp(self):
        self.fake = FakeTransport(session_id=0x12345678)
        self.ctl = Controller(self.fake, clock=lambda: self.fake.now_ms / 1000.0)
        self.ctl.connect()

    def sim(self, ms: int) -> None:
        for _ in range(max(1, ms // 100)):
            self.fake.advance(100)
            self.ctl.step()

    def test_connect(self):
        self.assertEqual(self.ctl.state, ControllerState.IDLE)
        self.assertEqual(self.ctl.session_id, 0x12345678)

    def test_grasp_to_hold(self):
        self.assertTrue(self.ctl.grasp())
        self.assertEqual(self.ctl.state, ControllerState.GRASPING)
        self.sim(1000)
        self.assertEqual(self.ctl.state, ControllerState.HOLDING)
        self.assertIsNotNone(self.ctl.last_actual)

    def test_release(self):
        self.assertTrue(self.ctl.grasp())
        self.sim(1000)
        self.assertTrue(self.ctl.release())
        self.sim(1000)
        self.assertEqual(self.ctl.state, ControllerState.IDLE)

    def test_stop_barrier_and_reset(self):
        self.assertTrue(self.ctl.grasp())
        self.sim(300)
        self.assertTrue(self.ctl.stop())
        self.assertEqual(self.ctl.state, ControllerState.ESTOP)
        self.assertFalse(self.ctl.stop_confirmed)
        self.assertFalse(self.ctl.grasp())
        self.sim(100)
        self.assertTrue(self.ctl.stop_confirmed)
        self.assertFalse(self.ctl.grasp())
        self.assertTrue(self.ctl.reset())
        self.assertEqual(self.ctl.state, ControllerState.IDLE)

    def test_link_timeout(self):
        self.fake.link_down = True
        self.sim(700)
        self.assertEqual(self.ctl.state, ControllerState.FAULT)
        self.assertEqual(self.ctl.last_health.stop_reason, proto.STOP_LINK_TIMEOUT)

    def test_target_timeout(self):
        self.assertTrue(self.ctl.grasp())
        self.ctl.current_execution = None  # stop periodic refresh
        self.sim(700)
        self.assertEqual(self.ctl.state, ControllerState.FAULT)
        self.assertEqual(self.ctl.last_health.stop_reason, proto.STOP_TARGET_TIMEOUT)

    def test_session_is_stable(self):
        first = self.ctl.session_id
        self.ctl.step()
        self.assertEqual(self.ctl.session_id, first)


if __name__ == "__main__":
    unittest.main(verbosity=2)
