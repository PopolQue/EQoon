# EQoon

5-band parametric EQ with variable-slope high/low cut filters, real-time response curve + FFT visualization, and bit-transparent bypass. Built with JUCE.

## Features (v0.3)

- **5 EQ bands**: Low Shelf, 3x Peak, High Shelf
- **Cut filters**: LowCut + HighCut with 12/24/36/48 dB/oct slopes and configurable Q
- **Bypass mode**: Bit-transparent bypass
- **4 summing modes**: Classic (series), Average, Sum, Maximum (parallel)
- **2 processing modes**: Left/Right, Mid/Side
- **Stereo Link** — mirrors L↔R parameter changes
- **Makeup Gain** — ±12 dB post-processing trim
- **FFT analyzer** — 2048-point, pre-EQ (orange) + post-EQ (cyan) overlay, 30 FPS
- **Log-frequency response curve** with grid lines (6 dB, standard freqs)
- **Mouse hover** — vertical line + frequency + dB readout
- **Channel view** — toggle between Left/Mid and Right/Side parameter sets
- **Real-time value readouts** on all slider parameters

## Build

```
# Plugin (Projucer → Xcode)
Open EQoon.jucer in Projucer, save → open Builds/MacOSX/EQoon.xcodeproj

# Test runner (CMake)
mkdir build && cd build && cmake .. && cmake --build . && ./EQoonTestRunner
```

## Install

```sh
ditto Builds/MacOSX/build/Debug/EQoon.vst3 ~/Library/Audio/Plug-Ins/VST3/
```

## Project structure

| Path | Purpose |
| ---- | ------- |
| `Source/PluginProcessor.h/.cpp` | DSP: filter chain, APVTS, processBlock, parameter sanitization |
| `Source/PluginEditor.h/.cpp` | UI: sliders, response curve, FFT overlay |
| `Source/FFTAnalyzer.h/.cpp` | 2048-point FFT with Blackman-Harris window |
| `Source/Tests.cpp` | Comprehensive suite: Math correctness, Signal integrity, Stress, Stereo/Phase, and Regression tests |
| `CMakeLists.txt` | Standalone test runner build |
| `EQoon.jucer` | Projucer project file |
