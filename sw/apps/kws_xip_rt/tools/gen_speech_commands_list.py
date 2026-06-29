#!/usr/bin/env python3
import argparse
from pathlib import Path

COMMANDS = [
    "one", "two", "three", "four", "five", "six",
    "seven", "eight", "up", "down", "noise", "silence",
]
CMD2ID = {c: i for i, c in enumerate(COMMANDS)}


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate speech_commands wav list")
    ap.add_argument("--root", default="sw/kws_cnn_lite/speech_commands", help="speech_commands root")
    ap.add_argument("--out", required=True, help="output list file")
    ap.add_argument("--max-per-label", type=int, default=0, help="limit per label (0=all)")
    args = ap.parse_args()

    root = Path(args.root)
    if not root.exists():
        raise SystemExit(f"root not found: {root}")

    lines = []
    for label in COMMANDS:
        label_dir = root / label
        if not label_dir.exists():
            continue
        wavs = sorted(label_dir.glob("*.wav"))
        if args.max_per_label > 0:
            wavs = wavs[: args.max_per_label]
        for wav in wavs:
            lines.append(f"{wav} {CMD2ID[label]}")

    Path(args.out).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"write {len(lines)} entries -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
