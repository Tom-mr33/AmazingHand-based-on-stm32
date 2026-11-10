"""Serial transport for protocol v2 (Pi UART <-> STM32)."""

from __future__ import annotations

import random
import time

from ..control import stm32_protocol_v2 as proto
from .transport import Transport

try:  # optional dependency on non-Pi hosts
    import serial  # type: ignore
except Exception:  # pragma: no cover
    serial = None


class SerialTransport(Transport):
    def __init__(self, port: str = "/dev/serial0", baud: int = 115200, timeout: float = 0.0) -> None:
        if serial is None:
            raise RuntimeError("pyserial is required for --transport serial")
        self.port = port
        self.baud = baud
        self.ser = serial.Serial(port=port, baudrate=baud, timeout=timeout, write_timeout=0.2)
        self.parser = proto.FrameParser()
        self.seq = 0
        self.session_id = random.getrandbits(32)
        self.accept: proto.Accept | None = None
        self._actuals: list[proto.Actual] = []
        self._healths: list[proto.Health] = []
        self._operations: list[proto.Operation] = []

    def _next_seq(self) -> int:
        self.seq = (self.seq + 1) & 0xFFFF
        return self.seq

    def _write(self, frame: bytes) -> None:
        self.ser.write(frame)

    def _drain(self) -> None:
        n = self.ser.in_waiting
        if not n:
            return
        data = self.ser.read(n)
        for frame in self.parser.feed(data):
            if frame.type == proto.MSG_ACCEPT:
                self.accept = proto.parse_accept(frame.payload)
            elif frame.type == proto.MSG_ACTUAL:
                self._actuals.append(proto.parse_actual(frame.payload))
            elif frame.type == proto.MSG_HEALTH:
                self._healths.append(proto.parse_health(frame.payload))
            elif frame.type == proto.MSG_OPERATION:
                self._operations.append(proto.parse_operation(frame.payload))

    def connect(self) -> proto.Accept:
        self._write(proto.build_hello(self._next_seq(), self.session_id))
        deadline = time.monotonic() + 1.0
        while time.monotonic() < deadline:
            self._drain()
            if self.accept is not None:
                if self.accept.status != proto.ACCEPT_OK:
                    raise RuntimeError(f"handshake rejected: {self.accept.status}")
                self.session_id = self.accept.session_id
                return self.accept
            time.sleep(0.005)
        raise TimeoutError("no ACCEPT from STM32")

    def close(self) -> None:
        try:
            self.ser.close()
        except Exception:
            pass

    def send_execution(self, execution: proto.Execution) -> None:
        self._write(proto.build_execution(
            self._next_seq(), execution.mode, execution.target, execution.close,
            execution.load_threshold, execution.speed, execution.torque_limit,
            execution.target_timeout_ms,
        ))

    def send_heartbeat(self) -> None:
        uptime = int(time.monotonic() * 1000) & 0xFFFFFFFF
        self._write(proto.build_heartbeat(self._next_seq(), self.session_id, uptime))

    def send_operation(self, op_type: int, data: bytes = b"") -> int:
        op_id = random.getrandbits(32)
        self._write(proto.build_operation(self._next_seq(), op_id, op_type, data))
        return op_id

    def poll(self):
        self._drain()
        actuals, healths, operations = self._actuals, self._healths, self._operations
        self._actuals, self._healths, self._operations = [], [], []
        return actuals, healths, operations
