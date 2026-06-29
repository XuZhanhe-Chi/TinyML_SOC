#!/usr/bin/env python3
import argparse
import os
import struct
import wave


def resample_linear(samples, src_rate, dst_rate, dst_len):
    if src_rate == dst_rate:
        if len(samples) >= dst_len:
            return samples[:dst_len]
        return samples + [0] * (dst_len - len(samples))
    ratio = src_rate / float(dst_rate)
    out = []
    for i in range(dst_len):
        pos = i * ratio
        idx = int(pos)
        frac = pos - idx
        if idx >= len(samples) - 1:
            s0 = samples[-1]
            s1 = samples[-1]
        else:
            s0 = samples[idx]
            s1 = samples[idx + 1]
        v = s0 * (1.0 - frac) + s1 * frac
        if v > 32767:
            v = 32767
        if v < -32768:
            v = -32768
        out.append(int(round(v)))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="WAV -> 16-bit PCM hex (one sample per line)")
    ap.add_argument("--in-wav", required=True, help="input wav path")
    ap.add_argument("--out-hex", required=True, help="output hex path")
    ap.add_argument("--samples", type=int, default=16000, help="output sample count")
    ap.add_argument("--rate", type=int, default=16000, help="target sample rate")
    args = ap.parse_args()

    in_wav = args.in_wav
    out_hex = args.out_hex
    out_samples = args.samples
    target_rate = args.rate

    if not os.path.exists(in_wav):
        raise SystemExit(f"input wav not found: {in_wav}")

    with wave.open(in_wav, "rb") as wf:
        nch = wf.getnchannels()
        sampwidth = wf.getsampwidth()
        rate = wf.getframerate()
        nframes = wf.getnframes()
        raw = wf.readframes(nframes)

    if nch != 1:
        raise SystemExit(f"only mono wav supported, got channels={nch}")
    if sampwidth != 2:
        raise SystemExit(f"only 16-bit wav supported, got sampwidth={sampwidth}")

    samples = list(struct.unpack("<%dh" % (len(raw) // 2), raw))
    samples = resample_linear(samples, rate, target_rate, out_samples)

    with open(out_hex, "w", encoding="utf-8") as f:
        for s in samples:
            f.write(f"{s & 0xFFFF:04X}\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
