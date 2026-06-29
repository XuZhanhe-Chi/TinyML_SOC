#!/usr/bin/env bash
set -euo pipefail

check_cmd() {
  local name="$1"
  local cmd="$2"
  if command -v "$cmd" >/dev/null 2>&1; then
    echo "OK   $name: $(command -v "$cmd")"
  else
    echo "MISS $name: $cmd"
  fi
}

check_path() {
  local name="$1"
  local path="$2"
  if [[ -n "$path" && -e "$path" ]]; then
    echo "OK   $name: $path"
  else
    echo "MISS $name: set the related environment variable"
  fi
}

check_cmd "python3" python3
check_cmd "sbt" sbt
check_cmd "iverilog" iverilog
check_cmd "vvp" vvp
check_cmd "VCS (optional)" vcs

if [[ -n "${RISCV_TOOLCHAIN_PREFIX:-}" ]]; then
  check_path "riscv gcc" "${RISCV_TOOLCHAIN_PREFIX}gcc"
else
  check_cmd "riscv gcc" riscv-none-elf-gcc
fi

if [[ -n "${GOWIN_IDE_BIN:-}" ]]; then
  check_path "Gowin gw_sh" "$GOWIN_IDE_BIN/gw_sh"
else
  check_cmd "Gowin gw_sh" gw_sh
fi

if [[ -n "${GOWIN_PROGRAMMER:-}" ]]; then
  check_path "Gowin programmer" "$GOWIN_PROGRAMMER"
elif [[ -n "${GOWIN_IDE_BIN:-}" ]]; then
  check_path "Gowin programmer" "$(cd "$GOWIN_IDE_BIN/../.." 2>/dev/null && pwd)/Programmer/bin/programmer_cli"
else
  check_cmd "Gowin programmer" programmer_cli
fi

python3 - <<'PY'
try:
    import serial  # noqa: F401
    print("OK   pyserial")
except Exception:
    print("MISS pyserial: install pyserial for monitor targets")

try:
    import pyftdi  # noqa: F401
    print("OK   pyftdi")
except Exception:
    print("MISS pyftdi: required for direct FT2232 UART access")
PY
