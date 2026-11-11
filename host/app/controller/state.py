"""Controller state vocabulary (no hardware)."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum

from ..control import stm32_protocol_v2 as proto


class ControllerState(str, Enum):
    IDLE = "IDLE"
    GRASPING = "GRASPING"
    HOLDING = "HOLDING"
    RELEASING = "RELEASING"
    FAULT = "FAULT"
    ESTOP = "ESTOP"


@dataclass(frozen=True)
class GraspPlan:
    pregrasp: tuple[int, ...] = (1000,) * 8
    close: tuple[int, ...] = (2000,) * 8
    load_threshold: tuple[int, ...] = (384,) * 8
    speed: tuple[int, ...] = (100,) * 8
    torque_limit: tuple[int, ...] = (500,) * 8
    timeout_ms: int = 500


@dataclass
class Motion:
    action: str
    plan: GraspPlan
    target: tuple[int, ...]
    revision: int = 1
    source: str = "cli"


def validate_plan(plan: GraspPlan) -> None:
    for name, values in (
        ("pregrasp", plan.pregrasp),
        ("close", plan.close),
        ("load_threshold", plan.load_threshold),
        ("speed", plan.speed),
        ("torque_limit", plan.torque_limit),
    ):
        if len(values) != proto.SERVO_COUNT:
            raise ValueError(f"{name} must have {proto.SERVO_COUNT} values")
    for v in plan.pregrasp + plan.close:
        if not 0 <= v <= 4095:
            raise ValueError("raw position out of range")
    if not 0 <= plan.timeout_ms <= 0xFFFF:
        raise ValueError("timeout out of range")


def execution_from_plan(mode: int, plan: GraspPlan | None = None,
                        target: tuple[int, ...] | None = None) -> proto.Execution:
    plan = plan or GraspPlan()
    validate_plan(plan)
    if target is None:
        target = plan.pregrasp
    if len(target) != proto.SERVO_COUNT:
        raise ValueError("target must have 8 values")
    return proto.Execution(
        mode=mode,
        target=target,
        close=plan.close,
        load_threshold=plan.load_threshold,
        speed=plan.speed,
        torque_limit=plan.torque_limit,
        target_timeout_ms=plan.timeout_ms,
    )
