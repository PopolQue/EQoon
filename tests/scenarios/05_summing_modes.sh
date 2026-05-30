#!/bin/bash
# Scenario 5: Summing modes — process pink noise through all 4 summing modes
# 5th panel: overlaid spectra for comparison
set -euo pipefail
cd "$(dirname "$0")/.."
SCENARIO="summing_modes"
REF="tests/reference/pink_noise.wav"
OUTDIR="tests/plots"

echo "== Scenario: $SCENARIO =="
REF_OUT="$OUTDIR/${SCENARIO}_ref.wav"
build/EQoonHeadless_artefacts/EQoonHeadless \
  --input "$REF" --output "$REF_OUT" --duration 1.0 --bypass

declare -A MODE_NAMES
MODE_NAMES[0]="Classic"
MODE_NAMES[1]="Average"
MODE_NAMES[2]="Sum"
MODE_NAMES[3]="Maximum"

# We'll build the overlaid plot from this scenario
SPECTRA_JSON="$OUTDIR/${SCENARIO}_spectra.json"
echo "[" > "$SPECTRA_JSON"
first=true

for mode in 0 1 2 3; do
  name="${MODE_NAMES[$mode]}"
  out="$OUTDIR/${SCENARIO}_${name}.wav"
  build/EQoonHeadless_artefacts/EQoonHeadless \
    --input "$REF" --output "$out" --duration 1.0 \
    --set "Summing Mode" "$mode" \
    --set "L Peak1 Gain" 6 --set "R Peak1 Gain" 6

  if [ "$first" = true ]; then first=false; else echo "," >> "$SPECTRA_JSON"; fi
  echo "\"${name}\"" >> "$SPECTRA_JSON"
done
echo "]" >> "$SPECTRA_JSON"

# Use overlaid panel with bypass as reference
.venv/bin/python3 tests/plot_results.py \
  --scenario "$SCENARIO" --input "$REF" --output "$REF_OUT" \
  --panel5 overlaid

echo "Done."
