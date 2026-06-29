#!/usr/bin/env bash
set -euo pipefail

command -v espeak-ng >/dev/null 2>&1 || {
  echo "[ERROR] espeak-ng is required for reproducible KWS playback" >&2
  exit 2
}
command -v paplay >/dev/null 2>&1 || {
  echo "[ERROR] paplay is required for audio playback" >&2
  exit 2
}

words="${KWS_TEST_WORDS:-one two up down}"
repeat="${KWS_TEST_REPEAT:-2}"
gap="${KWS_TEST_GAP_SEC:-1.2}"
voice="${KWS_TEST_VOICE:-en-us}"
speed="${KWS_TEST_SPEED:-125}"
amplitude="${KWS_TEST_AMPLITUDE:-180}"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

for ((round = 1; round <= repeat; round++)); do
  for word in $words; do
    wav="$tmp_dir/${round}_${word}.wav"
    echo "[AUDIO] round=$round word=$word"
    espeak-ng -v "$voice" -s "$speed" -a "$amplitude" -w "$wav" "$word"
    paplay "$wav"
    sleep "$gap"
  done
done
