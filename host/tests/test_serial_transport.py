"""SerialTransport wiring test with an in-memory fake serial port (no hardware)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app.controller import serial_transport  # noqa: E402
from app.control import stm32_protocol_v2 as proto  # noqa: E402


class FakeSerial:
    def __init__(self, *args, **kwargs):
        self.rx = bytearray()
        self.tx = bytearray()
        self.closed = False

    @property
    def in_waiting(self):
        return len(self.rx)

    def read(self, n):
        out = bytes(self.rx[:n])
        del self.rx[:n]
        return out

    def write(self, data):
        self.tx += data
        # respond to HELLO with ACCEPT
        for frame in proto.FrameParser().feed(bytes(data)):
            if frame.type == proto.MSG_HELLO:
                hello = proto.parse_hello(frame.payload)
                self.rx += proto.build_accept(1, proto.ACCEPT_OK, hello.session_id)
        return len(data)

    def close(self):
        self.closed = True


class SerialTransportTests(unittest.TestCase):
    def setUp(self):
        self.fake = FakeSerial()
        self.patcher = mock.patch.object(serial_transport, "serial", mock.Mock(Serial=lambda **kw: self.fake))
        self.patcher.start()
        self.transport = serial_transport.SerialTransport(port="fake", baud=115200)
        self.accept = self.transport.connect()

    def tearDown(self):
        self.transport.close()
        self.patcher.stop()

    def test_handshake(self):
        self.assertEqual(self.accept.status, proto.ACCEPT_OK)
        self.assertEqual(self.accept.session_id, self.transport.session_id)

    def test_execution(self):
        ex = proto.Execution(
            mode=proto.MODE_GRASP,
            target=(1000,) * 8,
            close=(2000,) * 8,
            load_threshold=(384,) * 8,
            speed=(100,) * 8,
            torque_limit=(500,) * 8,
            target_timeout_ms=500,
        )
        self.transport.send_execution(ex)
        frames = list(proto.FrameParser().feed(bytes(self.fake.tx)))
        self.assertEqual(frames[-1].type, proto.MSG_EXECUTION)
        self.assertEqual(proto.parse_execution(frames[-1].payload).mode, proto.MODE_GRASP)

    def test_poll_actual_health(self):
        actual = proto.build_actual(1, (1,) * 8, (2,) * 8, (30,) * 8, 0xFF, 10, 0, (0,) * 8)
        health = proto.build_health(2, proto.PHASE_HOLD, proto.STOP_NONE, 0, 0,
                                    proto.HEALTH_FLAG_STOPPED, (1, 0, 0, 0))
        self.fake.rx += actual + health
        actuals, healths, ops = self.transport.poll()
        self.assertEqual(len(actuals), 1)
        self.assertEqual(len(healths), 1)
        self.assertEqual(healths[0].flags, proto.HEALTH_FLAG_STOPPED)
        self.assertEqual(ops, [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
