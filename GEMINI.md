# EQoon Project Instructions

EQoon is a JUCE-based equalizer audio plugin (VST3, AU, Standalone) featuring real-time response curve visualization and spectrum analysis.

## Project Overview

- **Purpose**: A multi-band equalizer with various filter types and summing modes.
- **Key Technologies**: C++20, JUCE Framework (dsp, audio_processors, gui_basics, core), FFT for spectrum analysis.
- **Architecture**:
  - `EQoonAudioProcessor`: Main DSP logic, parameter management (APVTS), and filter chain orchestration.
  - `EQoonAudioProcessorEditor`: GUI management, custom sliders, and interaction handling.
  - `ResponseCurveComponent`: Dedicated component for drawing the EQ response curve and pre/post-EQ spectrum.
  - `FFTAnalyzer`: Handles the FFT processing for spectral visualization.
  - `ChainSettings`: Data structure for current EQ parameters.

## Building and Running

### Plugin Build (Standard JUCE Workflow)

1. Open `EQoon.jucer` in **Projucer**.
2. Save the project to generate IDE-specific files (e.g., Xcode, Visual Studio).
3. Open the generated project in `Builds/` and build the desired target (VST3, AU, or Standalone).

### Unit Tests Build (CMake Workflow)

The project includes a console-based test runner.

1. Create a build directory: `mkdir build && cd build`
2. Configure with CMake: `cmake ..`
3. Build the test runner: `cmake --build . --target EQoonTestRunner`
4. Run the tests: `./EQoonTestRunner` (or `ctest`)

## Development Conventions

- **JUCE Framework**: Follow standard JUCE conventions. Always use the `juce::` namespace explicitly.
- **Parameters**: All parameters must be managed via `juce::AudioProcessorValueTreeState (apvts)` in `EQoonAudioProcessor`.
- **DSP**: Prefer `juce::dsp` modules for audio processing. The main signal chain is defined in `PluginProcessor.h` using `juce::dsp::ProcessorChain`.
- **Testing**: New features or bug fixes should be accompanied by tests in `Source/Tests.cpp`. Use the JUCE `UnitTest` class.
- **Real-time Safety**: Ensure that no allocations, file I/O, or blocking operations occur in `processBlock`.

## Key Files

- `Source/PluginProcessor.h/cpp`: Core audio processing logic and parameter layout.
- `Source/PluginEditor.h/cpp`: UI implementation and parameter attachments.
- `Source/FFTAnalyzer.h/cpp`: Spectral analysis implementation.
- `Source/Tests.cpp`: Unit test definitions.
- `CMakeLists.txt`: Build configuration for the test runner.
- `EQoon.jucer`: Main Projucer project configuration.
- `JuceLibraryCode/`: The JUCE sourcecode. Your reference point for the JUCE documentation, check the Headerfiles.
