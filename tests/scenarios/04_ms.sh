#!/bin/bash
# Scenario 4: Mid/Side processing — boost Mid, cut Sides, verify goniometer
# 5th panel: goniometer showing M/S bias
set -euo pipefail
cd "$(dirname "$0")/.."
SCENARIO="ms"
REF="tests/reference/pink_noise.wav"
OUTPUT="tests/plots/${SCENARIO}_output.wav"

echo "== Scenario: $SCENARIO =="
# Enable Mid/Side mode. Mid = L+R, Side = L-R.
# Boost Peak1 on Mid (L+R), cut on Side (L-R):
# In MS mode, parameter IDs stay the same ("L Peak1" = Mid, "R Peak1" = Side).
build/EQoonHeadless_artefacts/EQoonHeadless \
  --input "$REF" --output "$OUTPUT" --duration 1.0 \
  --set "Processing Mode" 1 \
  --set "L Peak1 Gain" 6 \
  --set "R Peak1 Gain" -6

.venv/bin/python3 tests/plot_results.py \
  --scenario "$SCENARIO" --input "$REF" --output "$OUTPUT" \
  --panel5 goniometer

echo "Done."
