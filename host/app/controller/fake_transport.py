"""Deterministic fake STM32 transport for host smoke tests and CLI demo."""

from __future__ import annotations

import random

from ..control import stm32_protocol_v2 as proto
from .transport import Transport


class FakeTransport(Transport):
    def __init__(self, session_id: int | None = None) -> None:
        self.session_id = session_id if session_id is not None else random.getrandbits(32)
        self.now_ms = 0
        self.phase = proto.PHASE_IDLE
        self.stop_reason = proto.STOP_NONE
        self.fault_bits = 0
        self.torque = False
        self.target = [2048] * 8
        self.close = [2048] * 8
        self.threshold = [384] * 8
        self.raw = [2048] * 8
        self.load = [0] * 8
        self.temp = [30] * 8
        self.error = [0] * 8
        self._phase_start = 0
        self._last_exec = 0
        self._exec_set = False
        self.last_heartbeat = 0
        self.link_down = False
        self.auto_contact = True
        self._pending_operations: list[proto.Operation] = []
        self._op_id = 0
        self.last_heartbeat_seen = 0
        self.mode = 0

    def connect(self) -> proto.Accept:
        return proto.Accept(
            proto=proto.VERSION,
            status=proto.ACCEPT_OK,
            session_id=self.session_id,
            fw_version=0x0001,
            servo_count=proto.SERVO_COUNT,
            caps=0,
        )

    def close(self) -> None:
        pass

    def advance(self, ms: int) -> None:
        self.now_ms += ms
        if not self.link_down:
            self.last_heartbeat = self.now_ms
        self._update()

    def _move_toward(self) -> None:
        for i in range(proto.SERVO_COUNT):
            if self.raw[i] < self.target[i]:
                self.raw[i] = min(self.target[i], self.raw[i] + 50)
            elif self.raw[i] > self.target[i]:
                self.raw[i] = max(self.target[i], self.raw[i] - 50)

    def _update(self) -> None:
        if self.phase == proto.PHASE_FAULT:
            return
        if self.now_ms - self.last_heartbeat > 500:
            self._fault(proto.STOP_LINK_TIMEOUT)
            return
        if self._exec_set and self.now_ms - self._last_exec > 500:
            self._fault(proto.STOP_TARGET_TIMEOUT)
            return
        elapsed = self.now_ms - self._phase_start
        if self.phase == proto.PHASE_PREGRASP and elapsed >= 300:
            self.phase = proto.PHASE_CLOSING
            self.target = list(self.close)
            self._phase_start = self.now_ms
        elif self.phase == proto.PHASE_CLOSING:
            self._move_toward()
            if self.auto_contact and elapsed >= 60:
                self.phase = proto.PHASE_HOLD
                self.target = list(self.raw)
                self.load = [400] * 8
                self._phase_start = self.now_ms
            elif elapsed >= 3000:
                self._fault(proto.STOP_TARGET_TIMEOUT)
        elif self.phase == proto.PHASE_RELEASING:
            self._move_toward()
            if elapsed >= 500:
                self.phase = proto.PHASE_IDLE
                self.torque = False
                self._last_exec = 0
                self._exec_set = False
        elif self.phase == proto.PHASE_HOLD:
            self._move_toward()

    def _fault(self, reason: int) -> None:
        self.phase = proto.PHASE_FAULT
        self.stop_reason = reason
        self.fault_bits |= proto.FAULT_WATCHDOG
        self.torque = False
        self.load = [0] * 8

    def send_execution(self, execution: proto.Execution) -> None:
        self._last_exec = self.now_ms
        self._exec_set = True
        if execution.mode == proto.MODE_STOP:
            self.phase = proto.PHASE_FAULT
            self.stop_reason = proto.STOP_USER
            self.torque = False
            return
        if self.phase == proto.PHASE_FAULT:
            return
        if execution.mode == self.mode and self.phase in (proto.PHASE_PREGRASP, proto.PHASE_CLOSING,
                                                          proto.PHASE_HOLD, proto.PHASE_RELEASING):
            self._last_exec = self.now_ms
            return
        self.mode = execution.mode
        self.stop_reason = proto.STOP_NONE
        self.fault_bits = 0
        self.target = list(execution.target)
        self.close = list(execution.close)
        self.threshold = list(execution.load_threshold)
        self._phase_start = self.now_ms
        self.torque = True
        if execution.mode == proto.MODE_GRASP:
            self.phase = proto.PHASE_PREGRASP
        elif execution.mode == proto.MODE_HOLD:
            self.phase = proto.PHASE_HOLD
        elif execution.mode in (proto.MODE_OPEN, proto.MODE_RELEASE):
            self.phase = proto.PHASE_RELEASING
            self.load = [0] * 8

    def send_heartbeat(self) -> None:
        if not self.link_down:
            self.last_heartbeat = self.now_ms
            self.last_heartbeat_seen += 1

    def send_operation(self, op_type: int, data: bytes = b"") -> int:
        self._op_id = (self._op_id + 1) & 0xFFFFFFFF
        if op_type == proto.OP_RESET:
            self.phase = proto.PHASE_IDLE
            self.stop_reason = proto.STOP_NONE
            self.fault_bits = 0
            self.torque = False
            self._last_exec = 0
            self._exec_set = False
            self.load = [0] * 8
            self._pending_operations.append(proto.Operation(self._op_id, op_type, 0))
        return self._op_id

    def poll(self):
        actual = proto.Actual(
            raw=tuple(self.raw), load=tuple(self.load), temp_c=tuple(self.temp),
            online_mask=0xFF, sample_time_ms=self.now_ms, age_ms=0,
            error_bits=tuple(self.error),
        )
        flags = 0
        if self.torque:
            flags |= proto.HEALTH_FLAG_TORQUE
        if self.phase in (proto.PHASE_IDLE, proto.PHASE_FAULT) and not self.torque:
            flags |= proto.HEALTH_FLAG_STOPPED
        health = proto.Health(
            phase=self.phase, stop_reason=self.stop_reason, fault_bits=self.fault_bits,
            watchdog_state=0, flags=flags, counters=(0, 0, 0, 0),
        )
        ops = self._pending_operations
        self._pending_operations = []
        return [actual], [health], ops
