#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote


LINK_RE = re.compile(r"\[[^\]]+\]\(([^)]+)\)")


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    docs = [root / "README.md", *sorted((root / "docs").glob("*.md"))]
    missing: list[str] = []

    for document in docs:
        text = document.read_text(encoding="utf-8")
        for target in LINK_RE.findall(text):
            target = target.strip().strip("<>")
            if not target or target.startswith(("#", "http://", "https://", "mailto:")):
                continue
            path_text = unquote(target.split("#", 1)[0])
            resolved = (document.parent / path_text).resolve()
            if not resolved.exists():
                missing.append(f"{document.relative_to(root)} -> {target}")

    if missing:
        for item in missing:
            print(f"[ERROR] broken documentation link: {item}", file=sys.stderr)
        return 1

    print(f"[CHECK] documentation links: {len(docs)} files PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
