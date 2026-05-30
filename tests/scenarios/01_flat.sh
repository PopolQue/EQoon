#!/bin/bash
# Scenario 1: Flat EQ (no processing, sweep passes through)
# Verifies that the default state produces a flat response within tolerances.
set -euo pipefail
cd "$(dirname "$0")/.."
SCENARIO="flat"
REF="tests/reference/sweep.wav"
OUTPUT="tests/plots/${SCENARIO}_output.wav"

echo "== Scenario: $SCENARIO =="
build/EQoonHeadless_artefacts/EQoonHeadless \
  --input "$REF" --output "$OUTPUT" --duration 1.0

.venv/bin/python3 tests/plot_results.py \
  --scenario "$SCENARIO" --input "$REF" --output "$OUTPUT" \
  --panel5 overlaid

echo "Done."
