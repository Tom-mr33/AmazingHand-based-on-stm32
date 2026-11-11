"""Manual CLI for the Pi controller (no vision/voice)."""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from app.controller.controller import Controller  # noqa: E402
from app.controller.fake_transport import FakeTransport  # noqa: E402
from app.controller.serial_transport import SerialTransport  # noqa: E402


def build_transport(name: str, port: str):
    if name == "fake":
        return FakeTransport(), True
    if name == "serial":
        return SerialTransport(port=port), False
    raise ValueError(f"unknown transport: {name}")


def simulate(transport, controller: Controller, ms: int) -> None:
    for _ in range(max(1, ms // 100)):
        transport.advance(100)
        controller.step()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="AmazingHand Pi controller CLI")
    parser.add_argument("--transport", choices=["fake", "serial"], default="fake")
    parser.add_argument("--port", default="/dev/serial0")
    parser.add_argument("--script", default="", help="comma-separated commands, e.g. open,grasp,release")
    args = parser.parse_args(argv)

    transport, is_fake = build_transport(args.transport, args.port)
    clock = (lambda: transport.now_ms / 1000.0) if is_fake else time.monotonic
    controller = Controller(transport, clock=clock)
    controller.connect()
    commands = [c.strip() for c in args.script.split(",") if c.strip()]

    if commands:
        for command in commands:
            if is_fake:
                simulate(transport, controller, 300)
            controller.step()
            if command == "open":
                controller.open()
            elif command == "grasp":
                controller.grasp()
            elif command == "hold":
                controller.hold()
            elif command == "release":
                controller.release()
            elif command == "stop":
                controller.stop()
            elif command == "reset":
                controller.reset()
            elif command == "status":
                pass
            else:
                print(f"unknown command: {command}", file=sys.stderr)
                return 2
            if is_fake:
                simulate(transport, controller, 600)
            controller.step()
            print(controller.status())
        return 0

    print("commands: open grasp hold release stop reset status quit")
    try:
        while True:
            line = input("> ").strip()
            if not line:
                continue
            if line in ("quit", "exit"):
                break
            if is_fake:
                simulate(transport, controller, 100)
            else:
                controller.step()
            if line == "open":
                controller.open()
            elif line == "grasp":
                controller.grasp()
            elif line == "hold":
                controller.hold()
            elif line == "release":
                controller.release()
            elif line == "stop":
                controller.stop()
            elif line == "reset":
                controller.reset()
            elif line != "status":
                print(f"unknown command: {line}", file=sys.stderr)
                continue
            if is_fake:
                simulate(transport, controller, 500)
            else:
                controller.step()
            print(controller.status())
    finally:
        controller.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
