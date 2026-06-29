#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

echo "[CHECK] large files"
while IFS= read -r -d '' file; do
  [[ "$file" == third_party/* ]] && continue
  if (( $(stat -c '%s' "$file") > 10 * 1024 * 1024 )); then
    echo "[ERROR] large tracked/public file: $file" >&2
    exit 1
  fi
done < <(git ls-files --cached --others --exclude-standard -z)

echo "[CHECK] generated artifacts"
artifact_pattern='(^|/)(build|out|dist|target|\.metals|\.scala-build|\.vscode)/|(^|/)(simv[^/]*|csrc|ucli\.key)$|\.(fsdb|vcd|fst|vpd|wdb|bit|fs|bin|elf|xsa|wav|ogg|mp3|flac)$'
if git ls-files --cached --others --exclude-standard | grep -E "$artifact_pattern" | grep -v '^third_party/'; then
  echo "[ERROR] generated artifacts are present in public tree" >&2
  exit 1
fi

echo "[CHECK] private paths and contact data"
private_pattern="/(home|Users)/[^/[:space:]\"']+/|[[:alnum:]._%+-]+@[[:alnum:].-]+\\.[[:alpha:]]{2,}"
while IFS= read -r -d '' file; do
  [[ "$file" == third_party/* || ! -f "$file" ]] && continue
  if rg -n -- "$private_pattern" "$file"; then
    echo "[ERROR] private path or contact data found: $file" >&2
    exit 1
  fi
  if [[ -n "${PUBLIC_DENY_REGEX:-}" ]] && rg -n -- "$PUBLIC_DENY_REGEX" "$file"; then
    echo "[ERROR] project-specific private data found: $file" >&2
    exit 1
  fi
done < <(git ls-files --cached --others --exclude-standard -z)

echo "[CHECK] .gitmodules URL hygiene"
if [[ -f .gitmodules ]]; then
  while IFS= read -r url; do
    case "$url" in
      ../TinyML_NPU.git | \
      https://github.com/FreeRTOS/FreeRTOS-Kernel.git | \
      https://github.com/SpinalHDL/VexRiscv.git)
        ;;
      *)
        echo "[ERROR] unapproved submodule URL: $url" >&2
        exit 1
        ;;
    esac
  done < <(git config --file .gitmodules --get-regexp '\.url$' | awk '{print $2}')
fi

echo "[CHECK] PASS"
