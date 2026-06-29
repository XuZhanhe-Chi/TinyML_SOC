#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Generate FPGA-friendly KWS testvector header for kws_xip_rt firmware.

It converts a float32 NCHW feature tensor into int8 NCHWc4-packed u32 words,
and records an expected top1 index computed from a reference logits .npy.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np


def _find_scale(debug_ir: dict, tensor_name: str) -> float | None:
    t = debug_ir.get("tensors", {}).get(tensor_name)
    if isinstance(t, dict):
        s = t.get("scale")
        if isinstance(s, (int, float)) and float(s) > 0.0:
            return float(s)
    return None


def _find_output_scale(debug_ir: dict) -> float | None:
    # Common names in exported QDQ graphs.
    for name in ("logits", "output", "/head/Conv_output_0"):
        s = _find_scale(debug_ir, name)
        if s is not None:
            return s

    # Fallback: pick the first int8 tensor with shape [1,12,1,1].
    for name, t in (debug_ir.get("tensors", {}) or {}).items():
        if not isinstance(t, dict):
            continue
        if t.get("dtype") != "int8":
            continue
        if t.get("shape") != [1, 12, 1, 1]:
            continue
        s = t.get("scale")
        if isinstance(s, (int, float)) and float(s) > 0.0:
            return float(s)
    return None


def _quant_i8(x: np.ndarray, scale: float) -> np.ndarray:
    if scale <= 0.0:
        raise ValueError(f"Invalid scale: {scale}")
    q = np.round(x / scale)
    q = np.clip(q, -128, 127).astype(np.int8)
    return q


def _pack_nchw_c4_words_u8x4_le(q_nchw: np.ndarray) -> list[int]:
    if q_nchw.ndim != 4:
        raise ValueError(f"Expected NCHW 4D, got shape={q_nchw.shape}")
    n, c, h, w = map(int, q_nchw.shape)
    if n != 1:
        raise ValueError("Only N=1 supported for FPGA testvector header")
    c4 = (c + 3) // 4

    words: list[int] = []

    for c4_idx in range(c4):
        base_c = c4_idx * 4
        for hi in range(h):
            for wi in range(w):
                b = [0, 0, 0, 0]
                for lane in range(4):
                    ci = base_c + lane
                    if ci < c:
                        b[lane] = int(np.uint8(q_nchw[0, ci, hi, wi]))
                word = (b[0] & 0xFF) | ((b[1] & 0xFF) << 8) | ((b[2] & 0xFF) << 16) | (
                    (b[3] & 0xFF) << 24
                )
                words.append(word)
    return words


def _format_u32_array(words: list[int], per_line: int = 8) -> str:
    lines = []
    for i in range(0, len(words), per_line):
        chunk = words[i : i + per_line]
        lines.append("    " + ", ".join(f"0x{w:08X}u" for w in chunk) + ",")
    if lines:
        lines[-1] = lines[-1].rstrip(",")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--debug-ir", required=True, type=Path, help="debug_ir.json (for quant scales)")
    ap.add_argument("--input-npy", required=True, type=Path, help="input_f32.npy (float32 NCHW)")
    ap.add_argument("--expected-npy", required=True, type=Path, help="expected logits npy (float32, used for top1)")
    ap.add_argument("--meta-json", type=Path, default=None, help="meta.json (optional, for label)")
    ap.add_argument("--sample-idx", type=int, default=0)
    ap.add_argument("--out-header", required=True, type=Path)
    args = ap.parse_args()

    debug_ir = json.loads(args.debug_ir.read_text(encoding="utf-8"))
    input_scale = _find_scale(debug_ir, "input")
    if input_scale is None:
        raise SystemExit("Cannot find input scale in debug_ir.json (tensors.input.scale)")
    output_scale = _find_output_scale(debug_ir) or 0.0

    x = np.load(args.input_npy).astype(np.float32)
    if x.ndim != 4:
        raise SystemExit(f"Expected input_npy as NCHW, got shape={x.shape}")
    if args.sample_idx < 0 or args.sample_idx >= x.shape[0]:
        raise SystemExit(f"sample_idx out of range: {args.sample_idx} (count={x.shape[0]})")
    x0 = x[args.sample_idx : args.sample_idx + 1]

    q = _quant_i8(x0, input_scale)
    words = _pack_nchw_c4_words_u8x4_le(q)

    logits = np.load(args.expected_npy).astype(np.float32)
    if logits.ndim == 4 and logits.shape[0] == x.shape[0]:
        # Some pipelines keep NCHW.
        logits = logits.reshape(logits.shape[0], -1)
    if logits.ndim != 2:
        raise SystemExit(f"Expected expected_npy as [N, C], got shape={logits.shape}")
    if logits.shape[0] != x.shape[0]:
        raise SystemExit(f"N mismatch: input={x.shape[0]} expected={logits.shape[0]}")
    top1_f32 = int(np.argmax(logits[args.sample_idx]))

    meta_label = 0
    if args.meta_json and args.meta_json.exists():
        meta = json.loads(args.meta_json.read_text(encoding="utf-8"))
        samples = meta.get("samples", [])
        if isinstance(samples, list) and args.sample_idx < len(samples):
            rec = samples[args.sample_idx]
            if isinstance(rec, dict) and isinstance(rec.get("label"), int):
                meta_label = int(rec["label"])

    n, c, h, w = map(int, x0.shape)
    c4 = (c + 3) // 4
    out_c = int(logits.shape[1])
    out_c4 = (out_c + 3) // 4

    guard = "VENUSCORE_KWS_TESTVECTOR_FPGA_H"
    src_lines = [
        "/*",
        " * Auto-generated FPGA testvector header (kws_cnn_lite).",
        " *",
        f" * Source:",
        f" *   - {args.input_npy.as_posix()} (float32 NCHW)",
        f" *   - {args.expected_npy.as_posix()} (float32 logits, reference=behavioral plan sim)",
        f" * Quantization scales:",
        f" *   - {args.debug_ir.as_posix()}",
        " */",
    ]

    text = "\n".join(
        [
            f"#ifndef {guard}",
            f"#define {guard}",
            "",
            "#include <stdint.h>",
            "",
            *src_lines,
            "",
            f"#define VC_KWS_SAMPLE_INDEX {args.sample_idx}",
            "",
            f"#define VC_KWS_INPUT_N {n}",
            f"#define VC_KWS_INPUT_C {c}",
            f"#define VC_KWS_INPUT_H {h}",
            f"#define VC_KWS_INPUT_W {w}",
            f"#define VC_KWS_INPUT_C4 {c4}",
            f"#define VC_KWS_INPUT_WORDS (VC_KWS_INPUT_C4 * VC_KWS_INPUT_H * VC_KWS_INPUT_W)",
            f"#define VC_KWS_INPUT_BYTES (VC_KWS_INPUT_WORDS * 4u)",
            "",
            f"#define VC_KWS_OUTPUT_N 1",
            f"#define VC_KWS_OUTPUT_C {out_c}",
            f"#define VC_KWS_OUTPUT_H 1",
            f"#define VC_KWS_OUTPUT_W 1",
            f"#define VC_KWS_OUTPUT_C4 {out_c4}",
            f"#define VC_KWS_OUTPUT_WORDS (VC_KWS_OUTPUT_C4 * VC_KWS_OUTPUT_H * VC_KWS_OUTPUT_W)",
            f"#define VC_KWS_OUTPUT_BYTES (VC_KWS_OUTPUT_WORDS * 4u)",
            "",
            f"static const float VC_KWS_INPUT_SCALE = {input_scale:.10g}f;",
            f"static const float VC_KWS_OUTPUT_SCALE = {output_scale:.10g}f;",
            "",
            f"static const uint32_t VC_KWS_META_LABEL = {meta_label}u;",
            f"static const uint32_t VC_KWS_EXPECTED_TOP1_F32 = {top1_f32}u;",
            f"static const uint32_t VC_KWS_EXPECTED_TOP1_I8  = {top1_f32}u;",
            "",
            "/* Packed NCHWc4 int8 input bytes as little-endian u32 words (lane order: ch0,ch1,ch2,ch3). */",
            "static const uint32_t VC_KWS_INPUT_WORDS_U8X4_LE[VC_KWS_INPUT_WORDS] = {",
            _format_u32_array(words, per_line=8),
            "};",
            "",
            f"#endif // {guard}",
            "",
        ]
    )

    args.out_header.parent.mkdir(parents=True, exist_ok=True)
    args.out_header.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
