#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cmd="${1:-}"

programmer="${GOWIN_PROGRAMMER:-}"
if [[ -z "$programmer" && -n "${GOWIN_IDE_BIN:-}" ]]; then
  programmer="$(cd "$GOWIN_IDE_BIN/../.." 2>/dev/null && pwd)/Programmer/bin/programmer_cli"
fi
if [[ -z "$programmer" ]]; then
  programmer="$(command -v programmer_cli || true)"
fi
if [[ -z "$programmer" || ! -x "$programmer" ]]; then
  echo "[ERROR] programmer_cli not found. Set GOWIN_PROGRAMMER or GOWIN_IDE_BIN." >&2
  exit 2
fi

device="${GOWIN_DEVICE:-GW5A-25A}"
program_timeout="${GOWIN_PROGRAM_TIMEOUT_SEC:-300}"
short_timeout="${GOWIN_SHORT_TIMEOUT_SEC:-15}"
cable_args=()

lock_file="${TMPDIR:-/tmp}/tinyml_soc_gowin_program.lock"
exec 9>"$lock_file"
if ! flock -n 9; then
  echo "[ERROR] another Gowin Programmer operation is running" >&2
  exit 3
fi

run_checked() {
  local timeout_seconds="$1"
  shift
  local output_file rc attempt retryable
  output_file="$(mktemp)"
  for attempt in 1 2; do
    : >"$output_file"
    set +e
    timeout --foreground --kill-after=2 "$timeout_seconds" "$@" 2>&1 | tee "$output_file"
    rc="${PIPESTATUS[0]}"
    set -e
    if (( rc == 0 )) && ! grep -Eq '^Error:' "$output_file"; then
      rm -f "$output_file"
      return 0
    fi
    retryable=0
    if (( rc == 124 || rc == 137 )) || grep -q 'Cable failed to open' "$output_file"; then
      retryable=1
    fi
    if (( attempt == 1 && retryable == 1 )); then
      echo "[PROGRAM] transient cable failure; retrying once" >&2
      sleep 1
      continue
    fi
    rm -f "$output_file"
    if (( rc == 0 )); then
      rc=1
    fi
    return "$rc"
  done
  rm -f "$output_file"
}

select_cable() {
  local scan_output cable_index
  if [[ -n "${GOWIN_CABLE_INDEX:-}" ]]; then
    cable_args+=(--cable-index "$GOWIN_CABLE_INDEX")
  else
    scan_output="$(timeout --foreground --kill-after=2 10 "$programmer" --scan-cables 2>&1 || true)"
    cable_index=""
    case "$scan_output" in
      *"USB Debugger A"*) cable_index=4 ;;
      *"Gowin USB Cable(GWU2X)"*) cable_index=0 ;;
      *"Gowin USB Cable(FT2CH)"*) cable_index=1 ;;
      *"Digilent USB Device"*) cable_index=3 ;;
      *"Gowin USB Cable(WINUSB)"*) cable_index=5 ;;
    esac
    if [[ -n "$cable_index" ]]; then
      cable_args+=(--cable-index "$cable_index")
      echo "[PROGRAM] auto-selected cable-index=$cable_index"
      if [[ "$cable_index" == "4" && -z "${GOWIN_CHANNEL:-}" ]]; then
        cable_args+=(--channel 0)
      fi
    else
      echo "[ERROR] no supported Gowin download cable found" >&2
      printf '%s\n' "$scan_output" >&2
      exit 4
    fi
  fi
  if [[ -n "${GOWIN_CHANNEL:-}" ]]; then
    cable_args+=(--channel "$GOWIN_CHANNEL")
  fi
}

case "$cmd" in
  scan)
    run_checked "$short_timeout" "$programmer" --scan-cables
    ;;
  probe)
    select_cable
    run_checked "$short_timeout" "$programmer" --device "$device" --operation_index 0 "${cable_args[@]}"
    ;;
  detect-flash)
    select_cable
    run_checked "$short_timeout" "$programmer" --device "$device" --operation_index 51 "${cable_args[@]}"
    ;;
  flash-bitstream)
    select_cable
    fs_file="${FS_FILE:-$repo_root/build/gowin/tinyml_soc_gw5a/impl/pnr/tinyml_soc_gw5a.fs}"
    [[ -f "$fs_file" ]] || { echo "[ERROR] missing FS file: $fs_file" >&2; exit 2; }
    run_checked "$program_timeout" "$programmer" --device "$device" --operation_index 54 --fsFile "$fs_file" "${cable_args[@]}"
    ;;
  flash-kws)
    select_cable
    fw_file="${FW_FILE:-$repo_root/build/fw/kws_xip_rt_flash.bin}"
    offset="${FLASH_FW_OFFSET:-0x400000}"
    [[ -f "$fw_file" ]] || { echo "[ERROR] missing firmware image: $fw_file" >&2; exit 2; }
    run_checked "$program_timeout" "$programmer" --device "$device" --operation_index 56 --mcuFile "$fw_file" --spiaddr "$offset" "${cable_args[@]}"
    ;;
  reboot)
    select_cable
    run_checked "$short_timeout" "$programmer" --device "$device" --operation_index 1 "${cable_args[@]}"
    ;;
  *)
    echo "Usage: $0 scan|probe|detect-flash|flash-bitstream|flash-kws|reboot" >&2
    exit 2
    ;;
esac
