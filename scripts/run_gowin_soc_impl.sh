#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="${REPO_ROOT:-"$(cd "$script_dir/.." && pwd)"}"

if [[ ! -f "$repo_root/fpga/gw5a/filelist.f" ]]; then
  echo "[ERROR] REPO_ROOT does not look like TinyML_SOC: $repo_root" >&2
  exit 2
fi

if [[ -n "${GOWIN_GWSH:-}" ]]; then
  gw_sh="$GOWIN_GWSH"
elif [[ -n "${GOWIN_IDE_BIN:-}" ]]; then
  gw_sh="$GOWIN_IDE_BIN/gw_sh"
else
  gw_sh="$(command -v gw_sh || true)"
fi

if [[ -z "$gw_sh" || ! -x "$gw_sh" ]]; then
  echo "[ERROR] gw_sh not found. Set GOWIN_IDE_BIN or GOWIN_GWSH." >&2
  exit 2
fi

part="${PART:-GW5A-25A-MBGA121N-1}"
device_version="${DEVICE_VERSION:-A}"
out_root="${OUT_ROOT:-$repo_root/build/gowin}"
proj_name="${PROJ_NAME:-tinyml_soc_gw5a}"
top_module="${TOP_MODULE:-Top}"
filelist="${FILELIST:-$repo_root/fpga/gw5a/filelist.f}"
cst_file="${CST_FILE:-$repo_root/fpga/gw5a/src/fpga_project.cst}"
sdc_file="${SDC_FILE:-$repo_root/fpga/gw5a/src/fpga_project.sdc}"
tcl="$repo_root/scripts/gowin_soc_impl.tcl"
log="$out_root/gowin_soc_impl.log"

mkdir -p "$out_root" "/tmp/runtime-${USER:-user}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/runtime-${USER:-user}}"

if [[ "${GOWIN_PRELOAD_SYSTEM_FREETYPE:-1}" != "0" ]]; then
  for freetype_so in /lib64/libfreetype.so.6 /usr/lib/x86_64-linux-gnu/libfreetype.so.6; do
    if [[ -f "$freetype_so" ]]; then
      export LD_PRELOAD="$freetype_so${LD_PRELOAD:+:$LD_PRELOAD}"
      break
    fi
  done
fi

export REPO_ROOT="$repo_root"
export PART="$part"
export DEVICE_VERSION="$device_version"
export OUT_ROOT="$out_root"
export PROJ_NAME="$proj_name"
export TOP_MODULE="$top_module"
export FILELIST="$filelist"
export CST_FILE="$cst_file"
export SDC_FILE="$sdc_file"

if [[ ! -f "$repo_root/build/rtl/VenusCoreRVTop.v" ]]; then
  echo "[ERROR] missing generated RTL: build/rtl/VenusCoreRVTop.v" >&2
  echo "Run: make soc-rtl" >&2
  exit 2
fi

set -x
"$gw_sh" "$tcl" | tee "$log"
set +x

proj_dir="$out_root/$proj_name/impl/pnr"
rpt_txt="$proj_dir/${proj_name}.rpt.txt"
tr_html="$proj_dir/${proj_name}_tr_content.html"
bit_fs="$proj_dir/${proj_name}.fs"
bit_bin="$proj_dir/${proj_name}.bin"

echo "=================================================================="
echo "[GOWIN] LOG     = $log"
echo "[GOWIN] FS      = $bit_fs"
echo "[GOWIN] BIN     = $bit_bin"
echo "[GOWIN] PNR_RPT = $rpt_txt"
echo "[GOWIN] TIMING  = $tr_html"
echo "=================================================================="

python3 "$repo_root/scripts/summarize_gowin_reports.py" "$rpt_txt" "$tr_html"
