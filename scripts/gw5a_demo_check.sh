#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

serial_port="${SERIAL_PORT:?set SERIAL_PORT}"
serial_baud="${SERIAL_BAUD:-115200}"
timeout_sec="${DEMO_TIMEOUT_SEC:-120}"
ready_timeout_sec="${DEMO_READY_TIMEOUT_SEC:-30}"
reboot="${DEMO_REBOOT:-1}"
play_audio="${DEMO_PLAY_AUDIO:-1}"
log_dir="${DEMO_LOG_DIR:-$repo_root/build/board}"
log_file="$log_dir/gw5a_demo.log"
min_detects="${DEMO_MIN_DETECTS:-1}"
expected_labels="${DEMO_EXPECT_LABELS:-}"
sync_boot=0

mkdir -p "$log_dir"
: >"$log_file"

if [[ "$reboot" == "1" && "$serial_port" == ftdi://* ]]; then
  bash scripts/gw5a_program.sh probe
  bash scripts/gw5a_program.sh reboot
  sleep 0.2
  reboot=0
  sync_boot=1
fi

monitor_args=(
  --port "$serial_port"
  --baud "$serial_baud"
  --timeout "$timeout_sec"
  --require-ready
  --require-detect
  --fail-on-error
  --min-detects "$min_detects"
)
for label in $expected_labels; do
  monitor_args+=(--expect-label "$label")
done
if [[ "$sync_boot" == "1" ]]; then
  monitor_args+=(--require-boot --sync-last-boot)
fi

python3 -u scripts/monitor_serial.py \
  "${monitor_args[@]}" 2>&1 | tee "$log_file" &
monitor_pid=$!

cleanup() {
  if kill -0 "$monitor_pid" 2>/dev/null; then
    kill "$monitor_pid" 2>/dev/null || true
    wait "$monitor_pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

sleep 1
if ! kill -0 "$monitor_pid" 2>/dev/null; then
  wait "$monitor_pid"
  exit $?
fi
if [[ "$reboot" == "1" ]]; then
  bash scripts/gw5a_program.sh reboot
fi

ready_deadline=$((SECONDS + ready_timeout_sec))
while ! grep -q '^\[TinyML_SOC\] ready mic_sr=' "$log_file"; do
  if ! kill -0 "$monitor_pid" 2>/dev/null; then
    wait "$monitor_pid"
    exit $?
  fi
  if (( SECONDS >= ready_deadline )); then
    echo "[ERROR] ready line not observed within ${ready_timeout_sec}s" >&2
    exit 1
  fi
  sleep 0.2
done

if [[ "$play_audio" == "1" ]]; then
  bash scripts/play_kws_words.sh | tee -a "$log_file"
fi

wait "$monitor_pid"
trap - EXIT INT TERM
echo "[DEMO] PASS log=$log_file"
