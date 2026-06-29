#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path


def print_resource_summary(text: str) -> None:
    m = re.search(r"3\. Resource Usage Summary(.*?)=+\s*\n", text, flags=re.S)
    if not m:
        return
    print("[SUMMARY] Resource Usage Summary:")
    for line in m.group(1).splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith(("Resources", "-", "|")):
            continue
        if any(k in line for k in ("Logic", "Register", "BSRAM", "DSP", "I/O Port", "CLS")):
            print("  " + line)


def print_timing_summary(html: str) -> None:
    txt = re.sub(r"<[^>]+>", " ", html)
    txt = re.sub(r"\s+", " ", txt)
    m = re.search(
        r"Clock Name\s+Constraint\s+Actual Fmax\s+Logic Level\s+Entity\s+\d+\s+clk\s+([0-9.]+)\(MHz\)\s+([0-9.]+)\(MHz\)",
        txt,
        flags=re.I,
    )
    if m:
        print(f"[SUMMARY] clk Fmax: actual={m.group(2)}MHz constraint={m.group(1)}MHz")
    m = re.search(
        r"Total Negative Slack Summary\s+Clock Name\s+Analysis Type\s+Endpoints TNS\s+Number of Endpoints\s+clk\s+Setup\s+([-0-9.]+)\s+(\d+)",
        txt,
        flags=re.I,
    )
    if m:
        print(f"[SUMMARY] clk TNS(setup)={m.group(1)}ns endpoints={m.group(2)}")
    m = re.search(
        r"Setup Paths Table\s+Path Number\s+Path Slack\s+From Node\s+To Node\s+From Clock\s+To Clock\s+Relation\s+Clock Skew\s+Data Delay\s+1\s+([-0-9.]+)\s+([^\s]+)\s+([^\s]+)",
        txt,
        flags=re.I,
    )
    if m:
        print(f"[SUMMARY] Worst setup slack={m.group(1)}ns")
        print(f"[SUMMARY] Worst path: {m.group(2)} -> {m.group(3)}")


def main() -> int:
    rpt_txt = Path(sys.argv[1]) if len(sys.argv) > 1 else Path()
    tr_html = Path(sys.argv[2]) if len(sys.argv) > 2 else Path()
    if rpt_txt.exists():
        print_resource_summary(rpt_txt.read_text(errors="ignore"))
    else:
        print(f"[SUMMARY] missing report: {rpt_txt}")
    if tr_html.exists():
        print_timing_summary(tr_html.read_text(errors="ignore"))
    else:
        print(f"[SUMMARY] missing timing report: {tr_html}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
