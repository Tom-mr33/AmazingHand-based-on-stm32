"""STM32 control protocol v2 tests (no hardware).

Run: python host/tests/test_stm32_protocol_v2.py
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app.control import stm32_protocol_v2 as p  # noqa: E402


class CrcFrameTests(unittest.TestCase):
    def test_crc_known_vector(self):
        self.assertEqual(p.crc16_ccitt(b"123456789"), 0x29B1)

    def test_frame_roundtrip(self):
        raw = p.encode_frame(p.MSG_HEARTBEAT, 0x1234, b"abc")
        self.assertEqual(raw[2], 2)
        frames = p.FrameParser().feed(raw)
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].type, p.MSG_HEARTBEAT)
        self.assertEqual(frames[0].seq, 0x1234)
        self.assertEqual(frames[0].payload, b"abc")

    def test_fragmented_feed(self):
        raw = p.build_hello(1, 0x12345678)
        parser = p.FrameParser()
        self.assertEqual(parser.feed(raw[:4]), [])
        frames = parser.feed(raw[4:])
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].type, p.MSG_HELLO)

    def test_garbage_and_bad_crc(self):
        good = p.build_hello(2, 7)
        bad = bytearray(good)
        bad[-1] ^= 0x01
        frames = p.FrameParser().feed(b"junk" + bytes(bad) + good)
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].type, p.MSG_HELLO)

    def test_wrong_version_rejected(self):
        raw = bytearray(p.build_hello(3, 7))
        raw[2] = 1
        self.assertEqual(p.FrameParser().feed(bytes(raw)), [])

    def test_decode_frame_rejects_length(self):
        raw = p.build_hello(4, 7) + b"x"
        with self.assertRaises(p.ProtocolError):
            p.decode_frame(raw)


class GoldenVectorTests(unittest.TestCase):
    def test_hello(self):
        raw = p.build_hello(1, 0x12345678)
        self.assertEqual(raw.hex(), "aa55020101000a02017856341201000000a8f0")
        self.assertEqual(len(raw), 19)

    def test_execution(self):
        raw = p.build_execution(
            2, p.MODE_GRASP, tuple(range(8)), tuple(2000 + i for i in range(8)),
            (384,) * 8, (100,) * 8, (500,) * 8, 500
        )
        self.assertEqual(
            raw.hex(),
            "aa5502040200530200000100020003000400050006000700d007d107d207d307d407d507d607d707"
            "8001800180018001800180018001800164006400640064006400640064006400f401f401f401f401"
            "f401f401f401f401f401b01c",
        )

    def test_actual(self):
        raw = p.build_actual(
            3, tuple(range(8)), (-1,) * 8, (30,) * 8, 0xFF, 1000, 5, (0,) * 8
        )
        self.assertEqual(
            raw.hex(),
            "aa55020503003700000100020003000400050006000700ffffffffffffffffffffffffffffffff"
            "1e1e1e1e1e1e1e1effe8030000050000000000000000009751",
        )


class PayloadTests(unittest.TestCase):
    def test_hello(self):
        raw = p.build_hello(1, 0xAABBCCDD, profile_rev=3, caps=0x55)
        f = p.FrameParser().feed(raw)[0]
        self.assertEqual(p.parse_hello(f.payload), p.Hello(2, p.NODE_PI, 0xAABBCCDD, 3, 0x55))

    def test_accept(self):
        raw = p.build_accept(1, p.ACCEPT_VERSION_MISMATCH, 9, 0x0102, 8, 3)
        f = p.FrameParser().feed(raw)[0]
        self.assertEqual(p.parse_accept(f.payload).status, p.ACCEPT_VERSION_MISMATCH)

    def test_heartbeat(self):
        raw = p.build_heartbeat(2, 9, 123456)
        f = p.FrameParser().feed(raw)[0]
        self.assertEqual(p.parse_heartbeat(f.payload), p.Heartbeat(9, 123456))

    def test_execution_roundtrip(self):
        raw = p.build_execution(3, p.MODE_HOLD, (1, -2, 3, -4, 5, -6, 7, -8),
                                (10,) * 8, (20,) * 8, (30,) * 8, (40,) * 8, 300)
        f = p.FrameParser().feed(raw)[0]
        ex = p.parse_execution(f.payload)
        self.assertEqual(ex.mode, p.MODE_HOLD)
        self.assertEqual(ex.target, (1, -2, 3, -4, 5, -6, 7, -8))
        self.assertEqual(ex.close, (10,) * 8)
        self.assertEqual(ex.load_threshold, (20,) * 8)
        self.assertEqual(ex.target_timeout_ms, 300)

    def test_actual_online(self):
        raw = p.build_actual(4, (0,) * 8, (0,) * 8, (25,) * 8, 0b10101010, 9, 2,
                             (0, 1, 0, 0, 0, 0, 0, 0))
        f = p.FrameParser().feed(raw)[0]
        act = p.parse_actual(f.payload)
        self.assertEqual(act.online, (False, True, False, True, False, True, False, True))
        self.assertEqual(act.error_bits[1], 1)

    def test_health(self):
        raw = p.build_health(5, p.PHASE_HOLD, p.STOP_TARGET_TIMEOUT,
                             p.FAULT_WATCHDOG, 2, 0, (1, 2, 3, 4))
        f = p.FrameParser().feed(raw)[0]
        h = p.parse_health(f.payload)
        self.assertEqual(h.stop_reason, p.STOP_TARGET_TIMEOUT)
        self.assertEqual(h.counters, (1, 2, 3, 4))

    def test_operation(self):
        raw = p.build_operation(6, 0x11223344, p.OP_CALIBRATE, b"\x01\x02", 0)
        f = p.FrameParser().feed(raw)[0]
        op = p.parse_operation(f.payload)
        self.assertEqual(op.op_id, 0x11223344)
        self.assertEqual(op.data, b"\x01\x02")

    def test_info(self):
        raw = p.build_info(7, 1, 2, 8, 0x33, 0x55667788)
        f = p.FrameParser().feed(raw)[0]
        i = p.parse_info(f.payload)
        self.assertEqual((i.proto, i.fw_major, i.servo_count, i.build_id), (2, 1, 8, 0x55667788))


class ValidationTests(unittest.TestCase):
    def test_bad_servo_count(self):
        with self.assertRaises(p.ProtocolError):
            p.build_execution(1, p.MODE_OPEN, (0,) * 7, (0,) * 8, (0,) * 8, (0,) * 8, (0,) * 8, 1)

    def test_value_range(self):
        with self.assertRaises(p.ProtocolError):
            p.build_execution(1, p.MODE_OPEN, (0,) * 8, (0,) * 8, (0,) * 8, (0,) * 8, (0,) * 8, 0x10000)

    def test_operation_too_long(self):
        with self.assertRaises(p.ProtocolError):
            p.build_operation(1, 1, p.OP_RESET, b"x" * 100)

    def test_payload_too_long(self):
        with self.assertRaises(p.ProtocolError):
            p.encode_frame(p.MSG_INFO, 1, b"x" * 100)


if __name__ == "__main__":
    unittest.main(verbosity=2)
