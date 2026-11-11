"""Protocol-level fake STM32 standin used by host e2e smoke tests.

The standin still uses the real v2 encoder/parser on every exchange, so the
wire format is exercised even though no UART exists.
"""

from __future__ import annotations

from ..control import stm32_protocol_v2 as proto
from .fake_transport import FakeTransport
from .transport import Transport


class StandinTransport(Transport):
    def __init__(self, session_id: int | None = None) -> None:
        self.device = FakeTransport(session_id=session_id)
        self.tx_seq = 0
        self.parser = proto.FrameParser()

    @property
    def now_ms(self) -> int:
        return self.device.now_ms

    def advance(self, ms: int) -> None:
        self.device.advance(ms)

    def connect(self) -> proto.Accept:
        return self.device.connect()

    def close(self) -> None:
        self.device.close()

    def send_execution(self, execution: proto.Execution) -> None:
        self.tx_seq = (self.tx_seq + 1) & 0xFFFF
        raw = proto.build_execution(
            self.tx_seq, execution.mode, execution.target, execution.close,
            execution.load_threshold, execution.speed, execution.torque_limit,
            execution.target_timeout_ms,
        )
        parsed = proto.parse_execution(proto.decode_frame(raw).payload)
        self.device.send_execution(parsed)

    def send_heartbeat(self) -> None:
        self.tx_seq = (self.tx_seq + 1) & 0xFFFF
        self.device.send_heartbeat()

    def send_operation(self, op_type: int, data: bytes = b"") -> int:
        self.tx_seq = (self.tx_seq + 1) & 0xFFFF
        raw = proto.build_operation(self.tx_seq, 1, op_type, data)
        parsed = proto.parse_operation(proto.decode_frame(raw).payload)
        return self.device.send_operation(parsed.op_type, parsed.data)

    def poll(self):
        actuals, healths, operations = self.device.poll()
        out_actual = []
        out_health = []
        out_op = []
        for a in actuals:
            self.tx_seq = (self.tx_seq + 1) & 0xFFFF
            raw = proto.build_actual(self.tx_seq, a.raw, a.load, a.temp_c,
                                     a.online_mask, a.sample_time_ms, a.age_ms, a.error_bits)
            out_actual.append(proto.parse_actual(proto.decode_frame(raw).payload))
        for h in healths:
            self.tx_seq = (self.tx_seq + 1) & 0xFFFF
            raw = proto.build_health(self.tx_seq, h.phase, h.stop_reason, h.fault_bits,
                                     h.watchdog_state, h.flags, h.counters)
            out_health.append(proto.parse_health(proto.decode_frame(raw).payload))
        for o in operations:
            self.tx_seq = (self.tx_seq + 1) & 0xFFFF
            raw = proto.build_operation(self.tx_seq, o.op_id, o.op_type, o.data, o.status)
            out_op.append(proto.parse_operation(proto.decode_frame(raw).payload))
        return out_actual, out_health, out_op
