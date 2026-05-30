#!/bin/bash
# Scenario 6: Save/restore state through XML — load 6dB peak boost,
# process sweep, verify it matches direct --set invocation.
# 5th panel: diff spectrogram between direct and restored
set -euo pipefail
cd "$(dirname "$0")/.."
SCENARIO="save_state"
REF="tests/reference/sweep.wav"
OUTDIR="tests/plots"

echo "== Scenario: $SCENARIO =="

# 1. Process with direct --set
DIRECT_OUT="$OUTDIR/${SCENARIO}_direct.wav"
build/EQoonHeadless_artefacts/EQoonHeadless \
  --input "$REF" --output "$DIRECT_OUT" --duration 1.0 \
  --set "L Peak1 Freq" 1000 --set "L Peak1 Gain" 6 --set "L Peak1 Quality" 2 \
  --set "R Peak1 Freq" 1000 --set "R Peak1 Gain" 6 --set "R Peak1 Quality" 2 \
  --save-state "$OUTDIR/${SCENARIO}_state.xml"

echo "State saved to $OUTDIR/${SCENARIO}_state.xml"

# 2. Process with loaded state
RESTORE_OUT="$OUTDIR/${SCENARIO}_restore.wav"
build/EQoonHeadless_artefacts/EQoonHeadless \
  --input "$REF" --output "$RESTORE_OUT" --duration 1.0 \
  --params "$OUTDIR/${SCENARIO}_state.xml"

echo "State restored from $OUTDIR/${SCENARIO}_state.xml"

# 3. Diff plot
.venv/bin/python3 tests/plot_results.py \
  --scenario "$SCENARIO" --input "$DIRECT_OUT" --output "$RESTORE_OUT" \
  --panel5 diff_spectrogram --ref-wav "$DIRECT_OUT"

echo "Done."
