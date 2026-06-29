#!/usr/bin/env python3
import argparse
import subprocess
from pathlib import Path


def run_cmd(cmd: list[str], cwd: Path | None = None) -> None:
    print("[CMD]", " ".join(cmd))
    subprocess.check_call(cmd, cwd=str(cwd) if cwd else None)


def main() -> int:
    ap = argparse.ArgumentParser(description="Run KWS XIP RT TB on speech_commands list")
    ap.add_argument("--list", required=True, help="list file: wav_path label_index")
    ap.add_argument("--max", type=int, default=0, help="max samples to run")
    ap.add_argument("--samples", type=int, default=16000, help="samples per wav")
    ap.add_argument("--build", action="store_true", help="build simv before running")
    args = ap.parse_args()

    app_root = Path(__file__).resolve().parent.parent
    repo_root = app_root.parents[2]
    list_path = Path(args.list).resolve()
    if not list_path.exists():
        raise SystemExit(f"list not found: {list_path}")

    if args.build:
        run_cmd(["make", "sim-iverilog-build", "sim-pcm-fw"], cwd=repo_root)

    simv = repo_root / "build" / "sim" / "iverilog" / "simv"
    if not simv.exists():
        raise SystemExit("simv not found; run 'make sim-iverilog-build sim-pcm-fw' first")

    flash_hex = repo_root / "build" / "sim" / "fw_pcm" / "kws_xip_rt_flash.hex"
    if not flash_hex.exists():
        raise SystemExit("PCM simulation firmware not found; run 'make sim-pcm-fw' first")

    pcm_dir = repo_root / "build" / "sim" / "pcm_batch"
    pcm_dir.mkdir(parents=True, exist_ok=True)

    wav_to_hex = app_root / "tools" / "wav_to_pcm_hex.py"

    entries = [line.strip() for line in list_path.read_text(encoding="utf-8").splitlines() if line.strip()]
    if args.max > 0:
        entries = entries[: args.max]

    for idx, line in enumerate(entries):
        parts = line.rsplit(maxsplit=1)
        if len(parts) != 2:
            continue
        wav_path = Path(parts[0]).resolve()
        label = parts[1]
        if not wav_path.exists():
            print(f"[WARN] wav missing: {wav_path}")
            continue

        pcm_hex = pcm_dir / f"pcm_{idx:05d}.hex"
        run_cmd([
            "python3",
            str(wav_to_hex),
            "--in-wav",
            str(wav_path),
            "--out-hex",
            str(pcm_hex),
            "--samples",
            str(args.samples),
        ], cwd=repo_root)

        run_cmd([
            "vvp",
            str(simv),
            f"+FLASH_HEX={flash_hex}",
            "+FLASH_LOAD_OFFSET=400000",
            "+FAST_QSPI_AHB=1",
            "+UART_DIV=432",
            f"+PCM_HEX={pcm_hex}",
            f"+PCM_SAMPLES={args.samples}",
            f"+EXPECTED_LABEL={label}",
            "+STOP_ON_KWS=1",
            "+TB_TIMEOUT_NS=5000000000",
        ], cwd=repo_root)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
