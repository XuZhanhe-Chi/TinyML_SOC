#!/usr/bin/env python3
import argparse


def main() -> int:
    ap = argparse.ArgumentParser(description="Convert binary to $readmemh 8-bit hex (one byte per line).")
    ap.add_argument("--in-bin", required=True)
    ap.add_argument("--out-hex", required=True)
    ap.add_argument("--skip", default="0", help="skip leading bytes before dumping (supports 0x...)")
    args = ap.parse_args()

    with open(args.in_bin, "rb") as f:
        data = f.read()
    skip = int(args.skip, 0)
    if skip < 0:
        raise SystemExit("skip must be >= 0")
    if skip > len(data):
        raise SystemExit(f"skip exceeds file size: skip={skip} size={len(data)}")
    if skip:
        data = data[skip:]

    with open(args.out_hex, "w", encoding="utf-8") as f:
        for b in data:
            f.write(f"{b:02x}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
