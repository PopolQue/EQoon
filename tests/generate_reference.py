#!/usr/bin/env python3
"""Generate reference WAV files for EQoon headless testing."""
import numpy as np
from scipy.io import wavfile
from pathlib import Path

SR = 48000
DUR = 2.0
N = int(SR * DUR)
ref_dir = Path(__file__).parent / "reference"
ref_dir.mkdir(parents=True, exist_ok=True)

t = np.linspace(0, DUR, N, endpoint=False)

# 1. Sine 1 kHz @ -6 dBFS
sine = 0.5 * np.sin(2 * np.pi * 1000 * t)
wavfile.write(str(ref_dir / "sine_1k.wav"), SR, (sine * 32767).astype(np.int16))

# 2. Log sweep 20 Hz – 20 kHz, -6 dBFS
f0, f1 = 20.0, 20000.0
sweep = 0.5 * np.sin(2 * np.pi * f0 * (f1 / f0) ** (t / DUR) / np.log(f1 / f0) * DUR * ((t / DUR)))
wavfile.write(str(ref_dir / "sweep.wav"), SR, (sweep * 32767).astype(np.int16))

# 3. Impulse (single sample at -6 dBFS, then zero)
impulse = np.zeros(N, dtype=np.int16)
impulse[0] = 16384  # -6 dBFS ≈ 0.5 * 32768
wavfile.write(str(ref_dir / "impulse.wav"), SR, impulse)

# 4. Pink noise at -12 dBFS RMS
white = np.random.randn(N)
# 3 dB/oct rolloff via cumulative mean
pink = np.cumsum(white)
pink = pink / np.sqrt(np.mean(pink ** 2))  # normalise RMS
pink = 0.25 * pink  # -12 dBFS RMS
np.clip(pink, -1.0, 1.0, out=pink)
wavfile.write(str(ref_dir / "pink_noise.wav"), SR, (pink * 32767).astype(np.int16))

print(f"Generated 4 reference WAVs in {ref_dir}")
