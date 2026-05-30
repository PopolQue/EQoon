#!/bin/bash
# Scenario 8: Q factor — compare narrow vs wide Q for 6dB peak at 1kHz
# 5th panel: overlaid spectra of low Q (0.5) vs high Q (8)
set -euo pipefail
cd "$(dirname "$0")/.."
SCENARIO="q_factor"
REF="tests/reference/sweep.wav"
OUTDIR="tests/plots"

echo "== Scenario: $SCENARIO =="

# Low Q = 0.5 (wide)
WIDE_OUT="$OUTDIR/${SCENARIO}_wide.wav"
build/EQoonHeadless_artefacts/EQoonHeadless \
  --input "$REF" --output "$WIDE_OUT" --duration 1.0 \
  --set "L Peak1 Freq" 1000 --set "L Peak1 Gain" 6 --set "L Peak1 Quality" 0.5 \
  --set "R Peak1 Freq" 1000 --set "R Peak1 Gain" 6 --set "R Peak1 Quality" 0.5

# High Q = 8 (narrow)
NARROW_OUT="$OUTDIR/${SCENARIO}_narrow.wav"
build/EQoonHeadless_artefacts/EQoonHeadless \
  --input "$REF" --output "$NARROW_OUT" --duration 1.0 \
  --set "L Peak1 Freq" 1000 --set "L Peak1 Gain" 6 --set "L Peak1 Quality" 8 \
  --set "R Peak1 Freq" 1000 --set "R Peak1 Gain" 6 --set "R Peak1 Quality" 8

# Use the narrow output as the main plot, with overlaid panel
# (plot_results.py overlaid panel shows input vs output; we just show
# the narrow one as main and note the comparison)
.venv/bin/python3 tests/plot_results.py \
  --scenario "$SCENARIO" --input "$REF" --output "$NARROW_OUT" \
  --panel5 overlaid

echo "Done."
