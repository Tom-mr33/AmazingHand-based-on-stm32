"""Pi-side business controller.

Business state lives here. The STM32 only executes device-level operations
and reports actual/health; it never owns the business state machine.
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field

from ..control import stm32_protocol_v2 as proto
from .state import ControllerState, GraspPlan, Motion, execution_from_plan
from .transport import Transport


@dataclass
class Controller:
    transport: Transport
    clock: object = time.monotonic
    state: ControllerState = ControllerState.IDLE
    session_id: int = 0
    motion: Motion | None = None
    last_actual: proto.Actual | None = None
    last_health: proto.Health | None = None
    last_health_at: float = 0.0
    last_heartbeat_at: float = 0.0
    stop_confirmed: bool = True
    error: str = ""
    current_execution: proto.Execution | None = None
    last_exec_at: float = 0.0
    _seq: int = 0

    def connect(self) -> proto.Accept:
        accept = self.transport.connect()
        if accept.status != proto.ACCEPT_OK:
            raise RuntimeError(f"handshake rejected: {proto.ACCEPT_STATUS_NAMES.get(accept.status, accept.status)}")
        self.session_id = accept.session_id
        self.state = ControllerState.IDLE
        self.stop_confirmed = True
        self.last_health_at = float(self.clock())
        return accept

    def close(self) -> None:
        self.transport.close()

    def _next_seq(self) -> int:
        self._seq = (self._seq + 1) & 0xFFFF
        return self._seq

    def _send(self, execution: proto.Execution) -> None:
        self.transport.send_execution(execution)
        self.current_execution = execution
        now = float(self.clock())
        self.last_heartbeat_at = now
        self.last_exec_at = now

    def _refresh_execution(self, now: float) -> None:
        if (self.current_execution is not None and
                self.state in (ControllerState.GRASPING, ControllerState.HOLDING, ControllerState.RELEASING) and
                now - self.last_exec_at >= 0.2):
            self.transport.send_execution(self.current_execution)
            self.last_exec_at = now

    def can_move(self) -> bool:
        return self.state in (ControllerState.IDLE, ControllerState.HOLDING) and self.stop_confirmed

    def grasp(self, plan: GraspPlan | None = None) -> bool:
        if not self.can_move() or self.state != ControllerState.IDLE:
            return False
        plan = plan or GraspPlan()
        self._send(execution_from_plan(proto.MODE_GRASP, plan))
        self.motion = Motion("grasp", plan, plan.pregrasp)
        self.state = ControllerState.GRASPING
        return True

    def open(self) -> bool:
        if not self.can_move():
            return False
        plan = GraspPlan()
        self._send(execution_from_plan(proto.MODE_OPEN, plan, target=(2048,) * 8))
        self.motion = Motion("open", plan, (2048,) * 8)
        self.state = ControllerState.RELEASING
        return True

    def hold(self) -> bool:
        if not self.can_move():
            return False
        target = self.last_actual.raw if self.last_actual is not None else (2048,) * 8
        plan = GraspPlan()
        self._send(execution_from_plan(proto.MODE_HOLD, plan, target=target))
        self.motion = Motion("hold", plan, target)
        self.state = ControllerState.HOLDING
        return True

    def release(self) -> bool:
        if not self.can_move() or self.state not in (ControllerState.HOLDING, ControllerState.GRASPING):
            return False
        plan = GraspPlan()
        self._send(execution_from_plan(proto.MODE_RELEASE, plan, target=(2048,) * 8))
        self.motion = Motion("release", plan, (2048,) * 8)
        self.state = ControllerState.RELEASING
        return True

    def stop(self) -> bool:
        plan = GraspPlan()
        self._send(execution_from_plan(proto.MODE_STOP, plan))
        self.state = ControllerState.ESTOP
        self.stop_confirmed = False
        self.motion = Motion("stop", plan, (2048,) * 8)
        return True

    def reset(self) -> bool:
        if not self.stop_confirmed:
            return False
        self.transport.send_operation(proto.OP_RESET)
        self.state = ControllerState.IDLE
        self.motion = None
        self.current_execution = None
        self.last_exec_at = 0.0
        self.error = ""
        return True

    def _heartbeat(self, now: float) -> None:
        if now - self.last_heartbeat_at >= 0.1:
            self.transport.send_heartbeat()
            self.last_heartbeat_at = now

    def _apply_health(self, health: proto.Health, now: float) -> None:
        self.last_health = health
        self.last_health_at = now
        if health.fault_bits != 0 or health.stop_reason != proto.STOP_NONE:
            if health.stop_reason == proto.STOP_ESTOP:
                self.state = ControllerState.ESTOP
            elif health.stop_reason == proto.STOP_USER:
                self.state = ControllerState.ESTOP
            else:
                self.state = ControllerState.FAULT
            self.stop_confirmed = bool(health.flags & proto.HEALTH_FLAG_STOPPED)
            return
        if health.phase == proto.PHASE_HOLD and self.state == ControllerState.GRASPING:
            self.state = ControllerState.HOLDING
        elif health.phase == proto.PHASE_IDLE and self.state == ControllerState.RELEASING:
            self.state = ControllerState.IDLE
            self.motion = None
        if health.flags & proto.HEALTH_FLAG_STOPPED:
            self.stop_confirmed = True

    def step(self) -> None:
        now = float(self.clock())
        self._heartbeat(now)
        self._refresh_execution(now)
        actuals, healths, operations = self.transport.poll()
        if actuals:
            self.last_actual = actuals[-1]
        for health in healths:
            self._apply_health(health, now)
        if self.state not in (ControllerState.ESTOP, ControllerState.FAULT):
            if now - self.last_health_at > 1.0:
                self.state = ControllerState.FAULT
                self.error = "stale telemetry"

    def status(self) -> dict:
        health = self.last_health
        actual = self.last_actual
        return {
            "state": self.state.value,
            "session_id": self.session_id,
            "stop_confirmed": self.stop_confirmed,
            "phase": health.phase if health else None,
            "stop_reason": health.stop_reason if health else None,
            "fault_bits": health.fault_bits if health else 0,
            "actual_age_ms": actual.age_ms if actual else None,
            "error": self.error,
        }
