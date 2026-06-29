#!/usr/bin/env python3
"""Convert a raw binary into binary text files for $readmemb."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--in-bin", required=True, type=Path)
    ap.add_argument("--out-memb", type=Path)
    ap.add_argument(
        "--out-lane-prefix",
        type=Path,
        help="Write 8-bit lane files as <prefix>_0.memb ... <prefix>_3.memb.",
    )
    ap.add_argument("--depth-words", type=int, default=16384)
    args = ap.parse_args()

    data = args.in_bin.read_bytes()
    padded_len = max(args.depth_words * 4, len(data))
    if padded_len % 4:
        padded_len += 4 - (padded_len % 4)
    data = data + b"\x00" * (padded_len - len(data))

    if args.out_memb:
        lines = []
        for i in range(0, len(data), 4):
            word = int.from_bytes(data[i : i + 4], "little", signed=False)
            lines.append(f"{word:032b}")
        args.out_memb.write_text("\n".join(lines) + "\n", encoding="utf-8")

    if args.out_lane_prefix:
        for lane in range(4):
            lane_path = args.out_lane_prefix.with_name(f"{args.out_lane_prefix.name}_{lane}.memb")
            lane_lines = [f"{data[i + lane]:08b}" for i in range(0, len(data), 4)]
            lane_path.write_text("\n".join(lane_lines) + "\n", encoding="utf-8")

    if not args.out_memb and not args.out_lane_prefix:
        raise SystemExit("Specify --out-memb and/or --out-lane-prefix.")


if __name__ == "__main__":
    main()
