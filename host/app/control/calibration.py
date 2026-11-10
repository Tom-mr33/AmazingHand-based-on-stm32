"""Servo calibration schema/loader (hardware values are filled later)."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

SERVO_COUNT = 8


@dataclass(frozen=True)
class ServoCalibration:
    servo_id: int
    middle_raw: int
    direction: int
    min_raw: int
    max_raw: int
    min_deg: float
    max_deg: float
    max_speed: int
    torque_limit: int
    load_threshold: int


@dataclass(frozen=True)
class Calibration:
    servos: tuple[ServoCalibration, ...]

    def by_id(self, servo_id: int) -> ServoCalibration:
        for servo in self.servos:
            if servo.servo_id == servo_id:
                return servo
        raise KeyError(f"servo {servo_id} not calibrated")

    def deg_to_raw(self, servo_id: int, deg: float) -> int:
        servo = self.by_id(servo_id)
        deg = max(servo.min_deg, min(servo.max_deg, deg))
        if servo.direction > 0:
            if deg >= 0:
                raw = servo.middle_raw + deg / servo.max_deg * (servo.max_raw - servo.middle_raw)
            else:
                raw = servo.middle_raw + deg / abs(servo.min_deg) * (servo.middle_raw - servo.min_raw)
        else:
            if deg >= 0:
                raw = servo.middle_raw - deg / servo.max_deg * (servo.middle_raw - servo.min_raw)
            else:
                raw = servo.middle_raw - deg / abs(servo.min_deg) * (servo.max_raw - servo.middle_raw)
        return max(servo.min_raw, min(servo.max_raw, int(round(raw))))

    def raw_to_deg(self, servo_id: int, raw: int) -> float:
        servo = self.by_id(servo_id)
        raw = max(servo.min_raw, min(servo.max_raw, raw))
        if servo.direction > 0:
            if raw >= servo.middle_raw:
                frac = (raw - servo.middle_raw) / (servo.max_raw - servo.middle_raw)
            else:
                frac = -(servo.middle_raw - raw) / (servo.middle_raw - servo.min_raw)
        else:
            if raw <= servo.middle_raw:
                frac = (servo.middle_raw - raw) / (servo.middle_raw - servo.min_raw)
            else:
                frac = -(raw - servo.middle_raw) / (servo.max_raw - servo.middle_raw)
        return frac * (servo.max_deg if frac >= 0 else abs(servo.min_deg))


def load_calibration(path: str | Path) -> Calibration:
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    servos = []
    for item in data["servos"]:
        servos.append(ServoCalibration(
            servo_id=int(item["id"]),
            middle_raw=int(item["middle_raw"]),
            direction=int(item["direction"]),
            min_raw=int(item["min_raw"]),
            max_raw=int(item["max_raw"]),
            min_deg=float(item["min_deg"]),
            max_deg=float(item["max_deg"]),
            max_speed=int(item.get("max_speed", 0)),
            torque_limit=int(item.get("torque_limit", 0)),
            load_threshold=int(item.get("load_threshold", 0)),
        ))
    if len(servos) != SERVO_COUNT:
        raise ValueError(f"expected {SERVO_COUNT} servos, got {len(servos)}")
    ids = {s.servo_id for s in servos}
    if ids != set(range(1, SERVO_COUNT + 1)):
        raise ValueError(f"servo ids must be 1..{SERVO_COUNT}: {sorted(ids)}")
    for s in servos:
        if s.direction not in (-1, 1):
            raise ValueError(f"servo {s.servo_id}: direction must be +1/-1")
        if not (0 <= s.min_raw < s.max_raw <= 4095):
            raise ValueError(f"servo {s.servo_id}: bad raw range")
        if not s.min_deg < s.max_deg:
            raise ValueError(f"servo {s.servo_id}: bad degree range")
        if not s.min_raw <= s.middle_raw <= s.max_raw:
            raise ValueError(f"servo {s.servo_id}: middle outside range")
    return Calibration(tuple(servos))
