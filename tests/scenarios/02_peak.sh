#!/bin/bash
# Scenario 2: 6 dB peak boost at 1 kHz, Q=2
# 5th panel: zoomed spectrum around 1 kHz
set -euo pipefail
cd "$(dirname "$0")/.."
SCENARIO="peak"
REF="tests/reference/sine_1k.wav"
OUTPUT="tests/plots/${SCENARIO}_output.wav"

echo "== Scenario: $SCENARIO =="
build/EQoonHeadless_artefacts/EQoonHeadless \
  --input "$REF" --output "$OUTPUT" --duration 1.0 \
  --set "L Peak1 Freq" 1000 --set "L Peak1 Gain" 6 --set "L Peak1 Quality" 2 \
  --set "R Peak1 Freq" 1000 --set "R Peak1 Gain" 6 --set "R Peak1 Quality" 2

.venv/bin/python3 tests/plot_results.py \
  --scenario "$SCENARIO" --input "$REF" --output "$OUTPUT" \
  --panel5 zoomed_spectrum

echo "Done."
