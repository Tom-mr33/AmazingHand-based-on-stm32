"""STM32 control protocol v2 (Pi <-> STM32F103).

Frame::

    AA 55 | VER=2 | TYPE | SEQ u16 LE | LEN u8 | PAYLOAD | CRC16-LE

CRC16-CCITT (poly 0x1021, init 0xFFFF) covers VER..PAYLOAD.
This is the typed golden model used by the Pi controller and tests. The C
implementation lives in firmware/stm32/Core/{Inc,Src}/ah_protocol_v2.*.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

SOF0 = 0xAA
SOF1 = 0x55
SOF = bytes([SOF0, SOF1])
VERSION = 2
MAX_PAYLOAD = 96
MAX_FRAME = MAX_PAYLOAD + 9

# message types
MSG_HELLO = 0x01
MSG_ACCEPT = 0x02
MSG_HEARTBEAT = 0x03
MSG_EXECUTION = 0x04
MSG_ACTUAL = 0x05
MSG_HEALTH = 0x06
MSG_OPERATION = 0x07
MSG_INFO = 0x08

# node types
NODE_PI = 1
NODE_STM32 = 2

# execution modes
MODE_OPEN = 1
MODE_GRASP = 2
MODE_HOLD = 3
MODE_RELEASE = 4
MODE_STOP = 5
MODE_NAMES = ("?", "OPEN", "GRASP", "HOLD", "RELEASE", "STOP")

# device phases
PHASE_IDLE = 0
PHASE_PREGRASP = 1
PHASE_CLOSING = 2
PHASE_HOLD = 3
PHASE_RELEASING = 4
PHASE_FAULT = 5
PHASE_NAMES = ("IDLE", "PREGRASP", "CLOSING", "HOLD", "RELEASING", "FAULT")

# stop reasons
STOP_NONE = 0
STOP_USER = 1
STOP_LINK_TIMEOUT = 2
STOP_TARGET_TIMEOUT = 3
STOP_ESTOP = 4
STOP_FAULT = 5
STOP_SESSION_RESET = 6
STOP_NAMES = {
    STOP_NONE: "NONE",
    STOP_USER: "USER_STOP",
    STOP_LINK_TIMEOUT: "LINK_TIMEOUT",
    STOP_TARGET_TIMEOUT: "TARGET_TIMEOUT",
    STOP_ESTOP: "ESTOP",
    STOP_FAULT: "FAULT",
    STOP_SESSION_RESET: "SESSION_RESET",
}

# fault bits
FAULT_WATCHDOG = 1 << 0
FAULT_ESTOP = 1 << 1
FAULT_OFFLINE = 1 << 2
FAULT_SEQ = 1 << 3
FAULT_OVERLOAD = 1 << 4
FAULT_OVERTEMP = 1 << 5
FAULT_CRC = 1 << 6
FAULT_PROTOCOL = 1 << 7
FAULT_NAMES = {
    FAULT_WATCHDOG: "watchdog",
    FAULT_ESTOP: "estop",
    FAULT_OFFLINE: "offline",
    FAULT_SEQ: "seq",
    FAULT_OVERLOAD: "overload",
    FAULT_OVERTEMP: "overtemp",
    FAULT_CRC: "crc",
    FAULT_PROTOCOL: "protocol",
}

# operation types
OP_RESET = 1
OP_CALIBRATE = 2
OP_APPLY_CONFIG = 3
OP_NAMES = {OP_RESET: "RESET", OP_CALIBRATE: "CALIBRATE", OP_APPLY_CONFIG: "APPLY_CONFIG"}

# accept status
ACCEPT_OK = 0
ACCEPT_VERSION_MISMATCH = 1
ACCEPT_PROFILE_MISMATCH = 2
ACCEPT_BUSY = 3
ACCEPT_STATUS_NAMES = {
    ACCEPT_OK: "OK",
    ACCEPT_VERSION_MISMATCH: "VERSION_MISMATCH",
    ACCEPT_PROFILE_MISMATCH: "PROFILE_MISMATCH",
    ACCEPT_BUSY: "BUSY",
}

SERVO_COUNT = 8

# health flags
HEALTH_FLAG_TORQUE = 1 << 0
HEALTH_FLAG_STOPPED = 1 << 1
HEALTH_FLAG_SESSION_OK = 1 << 2

_HELLO_FMT = "<B B I H H"
_ACCEPT_FMT = "<B B I H B H"
_HEARTBEAT_FMT = "<I I"
_EXECUTION_FMT = "<B 8h 8h 8H 8H 8H H"
_ACTUAL_FMT = "<8h 8h 8b B I H 8B"
_HEALTH_FMT = "<B B H B B 4H"
_OPERATION_HDR_FMT = "<I B B"
_INFO_FMT = "<B B B B H I"

HELLO_SIZE = struct.calcsize(_HELLO_FMT)
ACCEPT_SIZE = struct.calcsize(_ACCEPT_FMT)
HEARTBEAT_SIZE = struct.calcsize(_HEARTBEAT_FMT)
EXECUTION_SIZE = struct.calcsize(_EXECUTION_FMT)
ACTUAL_SIZE = struct.calcsize(_ACTUAL_FMT)
HEALTH_SIZE = struct.calcsize(_HEALTH_FMT)
OPERATION_HDR_SIZE = struct.calcsize(_OPERATION_HDR_FMT)
INFO_SIZE = struct.calcsize(_INFO_FMT)


class ProtocolError(ValueError):
    """Bad frame / payload / version."""


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


@dataclass(frozen=True)
class Frame:
    type: int
    seq: int
    payload: bytes


def encode_frame(msg_type: int, seq: int, payload: bytes = b"") -> bytes:
    if not 0 <= msg_type <= 0xFF:
        raise ProtocolError(f"bad type {msg_type}")
    if not 0 <= seq <= 0xFFFF:
        raise ProtocolError(f"bad seq {seq}")
    if len(payload) > MAX_PAYLOAD:
        raise ProtocolError(f"payload too long: {len(payload)}")
    body = bytes([VERSION, msg_type]) + struct.pack("<H", seq) + bytes([len(payload)]) + payload
    crc = crc16_ccitt(body)
    return SOF + body + struct.pack("<H", crc)


def decode_frame(raw: bytes) -> Frame:
    if len(raw) < 9 or raw[0:2] != SOF:
        raise ProtocolError("bad SOF/length")
    if raw[2] != VERSION:
        raise ProtocolError(f"bad version {raw[2]}")
    msg_type = raw[3]
    seq = struct.unpack_from("<H", raw, 4)[0]
    length = raw[6]
    if length > MAX_PAYLOAD:
        raise ProtocolError(f"payload too long: {length}")
    total = length + 9
    if len(raw) != total:
        raise ProtocolError(f"frame length {len(raw)} != {total}")
    body = raw[2:7 + length]
    got = struct.unpack_from("<H", raw, 7 + length)[0]
    if crc16_ccitt(body) != got:
        raise ProtocolError("bad CRC")
    return Frame(type=msg_type, seq=seq, payload=raw[7:7 + length])


class FrameParser:
    """Streaming parser tolerant of fragmentation, leading garbage and bad frames."""

    def __init__(self) -> None:
        self._buf = bytearray()

    def feed(self, data: bytes) -> list[Frame]:
        self._buf += data
        out: list[Frame] = []
        while True:
            idx = self._buf.find(SOF)
            if idx < 0:
                if len(self._buf) > 1:
                    del self._buf[:-1]
                break
            if idx:
                del self._buf[:idx]
            if len(self._buf) < 7:
                break
            length = self._buf[6]
            if length > MAX_PAYLOAD:
                del self._buf[:2]
                continue
            total = length + 9
            if len(self._buf) < total:
                break
            raw = bytes(self._buf[:total])
            try:
                out.append(decode_frame(raw))
            except ProtocolError:
                del self._buf[:2]
                continue
            del self._buf[:total]
        return out


# ---- typed payloads ----

@dataclass(frozen=True)
class Hello:
    proto: int
    node_type: int
    session_id: int
    profile_rev: int
    caps: int


@dataclass(frozen=True)
class Accept:
    proto: int
    status: int
    session_id: int
    fw_version: int
    servo_count: int
    caps: int


@dataclass(frozen=True)
class Heartbeat:
    session_id: int
    uptime_ms: int


@dataclass(frozen=True)
class Execution:
    mode: int
    target: tuple[int, ...]
    close: tuple[int, ...]
    load_threshold: tuple[int, ...]
    speed: tuple[int, ...]
    torque_limit: tuple[int, ...]
    target_timeout_ms: int


@dataclass(frozen=True)
class Actual:
    raw: tuple[int, ...]
    load: tuple[int, ...]
    temp_c: tuple[int, ...]
    online_mask: int
    sample_time_ms: int
    age_ms: int
    error_bits: tuple[int, ...]

    @property
    def online(self) -> tuple[bool, ...]:
        return tuple(bool(self.online_mask & (1 << i)) for i in range(SERVO_COUNT))


@dataclass(frozen=True)
class Health:
    phase: int
    stop_reason: int
    fault_bits: int
    watchdog_state: int
    flags: int
    counters: tuple[int, ...]


@dataclass(frozen=True)
class Operation:
    op_id: int
    op_type: int
    status: int
    data: bytes = b""


@dataclass(frozen=True)
class Info:
    proto: int
    fw_major: int
    fw_minor: int
    servo_count: int
    caps: int
    build_id: int


# ---- builders ----

def build_hello(seq: int, session_id: int, profile_rev: int = 1, caps: int = 0) -> bytes:
    return encode_frame(MSG_HELLO, seq, struct.pack(_HELLO_FMT, VERSION, NODE_PI, session_id, profile_rev, caps))


def build_accept(seq: int, status: int, session_id: int, fw_version: int = 0x0001,
                 servo_count: int = SERVO_COUNT, caps: int = 0) -> bytes:
    return encode_frame(MSG_ACCEPT, seq, struct.pack(_ACCEPT_FMT, VERSION, status, session_id, fw_version, servo_count, caps))


def build_heartbeat(seq: int, session_id: int, uptime_ms: int) -> bytes:
    return encode_frame(MSG_HEARTBEAT, seq, struct.pack(_HEARTBEAT_FMT, session_id, uptime_ms))


def build_execution(seq: int, mode: int, target: tuple[int, ...], close: tuple[int, ...],
                    load_threshold: tuple[int, ...], speed: tuple[int, ...],
                    torque_limit: tuple[int, ...], target_timeout_ms: int) -> bytes:
    _check_i16_8(target)
    _check_i16_8(close)
    _check_u16_8(load_threshold)
    _check_u16_8(speed)
    _check_u16_8(torque_limit)
    if not 0 <= target_timeout_ms <= 0xFFFF:
        raise ProtocolError("target_timeout_ms out of range")
    return encode_frame(MSG_EXECUTION, seq, struct.pack(_EXECUTION_FMT, mode, *target, *close,
                                                       *load_threshold, *speed, *torque_limit,
                                                       target_timeout_ms))


def build_actual(seq: int, raw: tuple[int, ...], load: tuple[int, ...], temp_c: tuple[int, ...],
                 online_mask: int, sample_time_ms: int, age_ms: int, error_bits: tuple[int, ...]) -> bytes:
    _check_i16_8(raw)
    _check_i16_8(load)
    if len(temp_c) != SERVO_COUNT or any(not -128 <= v <= 127 for v in temp_c):
        raise ProtocolError("temp_c must be 8 x i8")
    if len(error_bits) != SERVO_COUNT:
        raise ProtocolError("error_bits must be 8")
    return encode_frame(MSG_ACTUAL, seq, struct.pack(_ACTUAL_FMT, *raw, *load, *temp_c,
                                                      online_mask, sample_time_ms, age_ms, *error_bits))


def build_health(seq: int, phase: int, stop_reason: int, fault_bits: int,
                 watchdog_state: int, flags: int, counters: tuple[int, ...]) -> bytes:
    if len(counters) != 4:
        raise ProtocolError("counters must be 4")
    return encode_frame(MSG_HEALTH, seq, struct.pack(_HEALTH_FMT, phase, stop_reason,
                                                     fault_bits, watchdog_state, flags, *counters))


def build_operation(seq: int, op_id: int, op_type: int, data: bytes = b"", status: int = 0) -> bytes:
    if len(data) > MAX_PAYLOAD - OPERATION_HDR_SIZE:
        raise ProtocolError("operation data too long")
    return encode_frame(MSG_OPERATION, seq, struct.pack(_OPERATION_HDR_FMT, op_id, op_type, status) + data)


def build_info(seq: int, fw_major: int = 0, fw_minor: int = 1,
               servo_count: int = SERVO_COUNT, caps: int = 0, build_id: int = 0) -> bytes:
    return encode_frame(MSG_INFO, seq, struct.pack(_INFO_FMT, VERSION, fw_major, fw_minor, servo_count, caps, build_id))


# ---- parsers ----

def parse_hello(payload: bytes) -> Hello:
    _expect(payload, HELLO_SIZE, "HELLO")
    return Hello(*struct.unpack(_HELLO_FMT, payload))


def parse_accept(payload: bytes) -> Accept:
    _expect(payload, ACCEPT_SIZE, "ACCEPT")
    return Accept(*struct.unpack(_ACCEPT_FMT, payload))


def parse_heartbeat(payload: bytes) -> Heartbeat:
    _expect(payload, HEARTBEAT_SIZE, "HEARTBEAT")
    return Heartbeat(*struct.unpack(_HEARTBEAT_FMT, payload))


def parse_execution(payload: bytes) -> Execution:
    _expect(payload, EXECUTION_SIZE, "EXECUTION")
    v = struct.unpack(_EXECUTION_FMT, payload)
    return Execution(mode=v[0], target=tuple(v[1:9]), close=tuple(v[9:17]),
                     load_threshold=tuple(v[17:25]), speed=tuple(v[25:33]),
                     torque_limit=tuple(v[33:41]), target_timeout_ms=v[41])


def parse_actual(payload: bytes) -> Actual:
    _expect(payload, ACTUAL_SIZE, "ACTUAL")
    v = struct.unpack(_ACTUAL_FMT, payload)
    return Actual(raw=tuple(v[0:8]), load=tuple(v[8:16]), temp_c=tuple(v[16:24]),
                  online_mask=v[24], sample_time_ms=v[25], age_ms=v[26],
                  error_bits=tuple(v[27:35]))


def parse_health(payload: bytes) -> Health:
    _expect(payload, HEALTH_SIZE, "HEALTH")
    v = struct.unpack(_HEALTH_FMT, payload)
    return Health(phase=v[0], stop_reason=v[1], fault_bits=v[2],
                  watchdog_state=v[3], flags=v[4], counters=tuple(v[5:9]))


def parse_operation(payload: bytes) -> Operation:
    if len(payload) < OPERATION_HDR_SIZE:
        raise ProtocolError("OPERATION payload too short")
    op_id, op_type, status = struct.unpack_from(_OPERATION_HDR_FMT, payload, 0)
    return Operation(op_id=op_id, op_type=op_type, status=status,
                     data=bytes(payload[OPERATION_HDR_SIZE:]))


def parse_info(payload: bytes) -> Info:
    _expect(payload, INFO_SIZE, "INFO")
    return Info(*struct.unpack(_INFO_FMT, payload))


def _expect(payload: bytes, size: int, name: str) -> None:
    if len(payload) != size:
        raise ProtocolError(f"{name} payload {len(payload)} != {size}")


def _check_i16_8(values: tuple[int, ...]) -> None:
    if len(values) != SERVO_COUNT or any(not -32768 <= v <= 32767 for v in values):
        raise ProtocolError("expected 8 x i16")


def _check_u16_8(values: tuple[int, ...]) -> None:
    if len(values) != SERVO_COUNT or any(not 0 <= v <= 0xFFFF for v in values):
        raise ProtocolError("expected 8 x u16")
