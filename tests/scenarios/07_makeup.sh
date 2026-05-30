#!/bin/bash
# Scenario 7: Makeup gain — process pink noise with +6dB peak, test makeup
# 5th panel: RMS bar chart showing input RMS vs output RMS
set -euo pipefail
cd "$(dirname "$0")/.."
SCENARIO="makeup"
REF="tests/reference/pink_noise.wav"
OUTDIR="tests/plots"

echo "== Scenario: $SCENARIO =="

# With a 6dB boost and -6dB makeup: output RMS should be close to input
WITH_MAKEUP="$OUTDIR/${SCENARIO}_makeup.wav"
build/EQoonHeadless_artefacts/EQoonHeadless \
  --input "$REF" --output "$WITH_MAKEUP" --duration 1.0 \
  --set "L Peak1 Gain" 6 --set "R Peak1 Gain" 6 \
  --set "Makeup Gain" -6

.venv/bin/python3 tests/plot_results.py \
  --scenario "$SCENARIO" --input "$REF" --output "$WITH_MAKEUP" \
  --panel5 rms_bar

echo "Done."
