#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import re
import sys
import time


DETECT_RE = re.compile(
    r"\[KWS\] detect idx=(?P<idx>\d+) label=(?P<label>\S+) score=(?P<score>-?\d+) "
    r"margin=(?P<margin>-?\d+) cycles=(?P<cycles>\d+) us=(?P<us>\d+)"
)


def open_serial(port: str, baud: int):
    if port.startswith("ftdi://"):
        try:
            from pyftdi.serialext import serial_for_url
        except Exception as exc:
            raise RuntimeError(f"pyftdi is required for {port}: {exc}") from exc
        return serial_for_url(port, baudrate=baud, timeout=0.2)

    import serial

    return serial.Serial(port, baud, timeout=0.2)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=os.environ.get("SERIAL_PORT", ""))
    ap.add_argument("--baud", type=int, default=int(os.environ.get("SERIAL_BAUD", "115200")))
    ap.add_argument("--timeout", type=float, default=0.0, help="0 means no timeout")
    ap.add_argument("--require-detect", action="store_true")
    ap.add_argument("--require-ready", action="store_true")
    ap.add_argument("--fail-on-error", action="store_true")
    ap.add_argument("--flush-input", action="store_true")
    ap.add_argument("--min-detects", type=int, default=1)
    ap.add_argument("--expect-label", action="append", default=[])
    ap.add_argument("--require-boot", action="store_true")
    ap.add_argument("--sync-last-boot", action="store_true")
    args = ap.parse_args()

    if not args.port:
        raise SystemExit("SERIAL_PORT is required")

    deadline = time.monotonic() + args.timeout if args.timeout > 0 else None
    saw_boot = False
    saw_ready = False
    detect_count = 0
    seen_labels: set[str] = set()

    try:
        ser_ctx = open_serial(args.port, args.baud)
    except Exception as exc:
        print(f"[ERROR] cannot open serial port {args.port}: {exc}", file=sys.stderr)
        return 2

    with ser_ctx as ser:
        if args.flush_input:
            ser.reset_input_buffer()
            return 0

        pending_lines: list[str] = []
        if args.sync_last_boot:
            initial = bytearray()
            quiet_deadline = time.monotonic() + 0.3
            max_deadline = time.monotonic() + 2.0
            while time.monotonic() < max_deadline:
                waiting = getattr(ser, "in_waiting", 0)
                chunk = ser.read(max(1, waiting))
                if chunk:
                    initial.extend(chunk)
                    quiet_deadline = time.monotonic() + 0.3
                elif time.monotonic() >= quiet_deadline:
                    break
            initial_lines = [
                line.lstrip("\x00\r")
                for line in initial.decode("utf-8", errors="replace").splitlines()
            ]
            boot_indices = [
                index
                for index, line in enumerate(initial_lines)
                if line.startswith("[TinyML_SOC] boot")
            ]
            if boot_indices:
                for line in initial_lines[: boot_indices[-1]]:
                    print(line, flush=True)
                pending_lines = initial_lines[boot_indices[-1] :]
            else:
                pending_lines = initial_lines

        while True:
            if deadline is not None and time.monotonic() >= deadline:
                break
            if pending_lines:
                line = pending_lines.pop(0)
            else:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").rstrip().lstrip("\x00\r")
            print(line, flush=True)
            if args.fail_on_error and line.startswith("[TinyML_SOC][ERR]"):
                print("[ERROR] target reported a runtime error", file=sys.stderr)
                return 1
            if line.startswith("[TinyML_SOC] boot"):
                saw_boot = True
                saw_ready = False
                detect_count = 0
                seen_labels.clear()
            if line.startswith("[TinyML_SOC] ready"):
                saw_ready = True
            match = DETECT_RE.search(line)
            if match:
                detect_count += 1
                seen_labels.add(match.group("label"))
                labels_ok = set(args.expect_label).issubset(seen_labels)
                if (
                    args.require_detect
                    and detect_count >= args.min_detects
                    and labels_ok
                    and (not args.require_boot or saw_boot)
                    and (not args.require_ready or saw_ready)
                ):
                    print(
                        f"[MONITOR] detects={detect_count} labels={','.join(sorted(seen_labels))}",
                        flush=True,
                    )
                    return 0

    if args.require_boot and not saw_boot:
        print("[ERROR] boot line not observed", file=sys.stderr)
        return 1
    if args.require_ready and not saw_ready:
        print("[ERROR] ready line not observed", file=sys.stderr)
        return 1
    if args.require_detect and detect_count < args.min_detects:
        print(
            f"[ERROR] detect count {detect_count} is below required {args.min_detects}",
            file=sys.stderr,
        )
        return 1
    missing_labels = set(args.expect_label) - seen_labels
    if missing_labels:
        print(f"[ERROR] expected labels not observed: {','.join(sorted(missing_labels))}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
