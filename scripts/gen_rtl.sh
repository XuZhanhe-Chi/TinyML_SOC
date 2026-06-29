#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

mkdir -p "$repo_root/build/rtl"

(
  cd "$repo_root/hw/spinal"
  sbt -batch "runMain top.VenusCoreRVTopGw5a50Gen"
)

rtl="$repo_root/build/rtl/VenusCoreRVTop.v"
python3 "$repo_root/scripts/patch_gw5a_sram_init.py" "$rtl"

if ! grep -q "assign flash_offset_u32 = 32'h00400000;" "$rtl"; then
  echo "[ERROR] unexpected QSPI flash offset in $rtl" >&2
  exit 1
fi

echo "[OK] generated $rtl"
