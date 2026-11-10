"""Run all host-side smoke tests (no hardware).

Usage: python tools/run_smoke.py
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PYTHON = sys.executable
CC = os.environ.get("CC", "gcc")
EXE = ".exe" if os.name == "nt" else ""

PY_TESTS = [
    "host/tests/test_stm32_protocol_v2.py",
    "host/tests/test_controller.py",
    "host/tests/test_standin_e2e.py",
    "host/tests/test_serial_transport.py",
    "host/tests/test_stm32_protocol.py",
    "host/tests/test_stm32_backend.py",
    "host/tests/test_mcu_link.py",
    "host/tests/smoke_test_control.py",
    "host/tests/smoke_test_backend.py",
    "host/tests/test_calibration.py",
]

C_TESTS = [
    {
        "name": "test_ah_protocol",
        "sources": ["firmware/stm32/Core/Src/ah_protocol.c",
                    "firmware/stm32/tests/test_ah_protocol.c"],
    },
    {
        "name": "test_ah_protocol_v2",
        "sources": ["firmware/stm32/Core/Src/ah_protocol_v2.c",
                    "firmware/stm32/tests/test_ah_protocol_v2.c"],
    },
    {
        "name": "test_ah_scs",
        "sources": ["firmware/stm32/Core/Src/ah_scs.c",
                    "firmware/stm32/tests/fake_hal.c",
                    "firmware/stm32/tests/test_ah_scs.c"],
    },
    {
        "name": "test_ah_device",
        "sources": ["firmware/stm32/Core/Src/ah_scs.c",
                    "firmware/stm32/Core/Src/ah_device.c",
                    "firmware/stm32/tests/fake_hal.c",
                    "firmware/stm32/tests/test_ah_device.c"],
    },
    {
        "name": "test_ah_app",
        "sources": ["firmware/stm32/Core/Src/ah_protocol_v2.c",
                    "firmware/stm32/Core/Src/ah_scs.c",
                    "firmware/stm32/Core/Src/ah_device.c",
                    "firmware/stm32/Core/Src/ah_app.c",
                    "firmware/stm32/tests/fake_hal.c",
                    "firmware/stm32/tests/test_ah_app.c"],
    },
]


def run(cmd: list[str], cwd: Path, env: dict[str, str] | None = None) -> None:
    print("$", " ".join(cmd))
    subprocess.run(cmd, cwd=cwd, env=env, check=True)


def main() -> int:
    tmp = ROOT / "tmp" / "gcc"
    tmp.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["PYTHONPATH"] = str(ROOT / "host") + os.pathsep + env.get("PYTHONPATH", "")
    env["TEMP"] = str(tmp)
    env["TMP"] = str(tmp)

    for test in PY_TESTS:
        run([PYTHON, test], ROOT, env)

    includes = ["-I", str(ROOT / "firmware/stm32/Core/Inc"),
                "-I", str(ROOT / "firmware/stm32/tests")]
    for test in C_TESTS:
        exe = tmp / (test["name"] + EXE)
        sources = [str(ROOT / s) for s in test["sources"]]
        run([CC, "-std=c11", "-Wall", "-Wextra", "-Werror", *includes,
             *sources, "-o", str(exe)], ROOT, env)
        run([str(exe)], ROOT, env)

    print("SMOKE OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
