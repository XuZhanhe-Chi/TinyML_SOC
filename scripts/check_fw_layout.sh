#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fw_bin="$repo_root/build/fw/kws_xip_rt_flash.bin"
rtl="$repo_root/build/rtl/VenusCoreRVTop.v"
bsp_map="$repo_root/sw/bsp/core/soc.h"
soc_config="$repo_root/hw/spinal/src/main/scala/config/VenusCoreRVTopConfig.scala"

if [[ ! -f "$fw_bin" ]]; then
  echo "[ERROR] missing firmware flash image: $fw_bin" >&2
  exit 2
fi
if [[ ! -f "$rtl" ]]; then
  echo "[ERROR] missing generated RTL: $rtl" >&2
  exit 2
fi

size="$(stat -c '%s' "$fw_bin")"
if (( size > 4 * 1024 * 1024 )); then
  echo "[ERROR] firmware image is larger than the 4 MiB XIP window: $size" >&2
  exit 1
fi

for lane in 0 1 2 3; do
  if [[ ! -f "$repo_root/build/fw/kws_xip_rt_boot_${lane}.memb" ]]; then
    echo "[ERROR] missing boot initmem lane $lane" >&2
    exit 1
  fi
done

grep -q "assign flash_offset_u32 = 32'h00400000;" "$rtl"
grep -Fq '#define SOC_SRAM_SIZE (64u * 1024u)' "$bsp_map"
grep -Fq 's0SramSizeBytes: Long = 0x00010000L' "$soc_config"
echo "[OK] firmware size=${size} bytes, flash offset=0x00400000"
