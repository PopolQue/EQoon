#!/bin/bash
# Scenario 3: 12 dB cut at 500 Hz, Q=2.828 (one octave)
# 5th panel: zoomed spectrum around 500 Hz
set -euo pipefail
cd "$(dirname "$0")/.."
SCENARIO="cut"
REF="tests/reference/sweep.wav"
OUTPUT="tests/plots/${SCENARIO}_output.wav"

echo "== Scenario: $SCENARIO =="
build/EQoonHeadless_artefacts/EQoonHeadless \
  --input "$REF" --output "$OUTPUT" --duration 1.0 \
  --set "L Peak1 Freq" 500 --set "L Peak1 Gain" -12 --set "L Peak1 Quality" 2.828 \
  --set "R Peak1 Freq" 500 --set "R Peak1 Gain" -12 --set "R Peak1 Quality" 2.828

.venv/bin/python3 tests/plot_results.py \
  --scenario "$SCENARIO" --input "$REF" --output "$OUTPUT" \
  --panel5 zoomed_spectrum

echo "Done."
