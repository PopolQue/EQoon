#pragma once

#include <JuceHeader.h>

class FFTAnalyzer
{
public:
    enum
    {
        fftOrder = 11,             // 2^11 = 2048 point FFT
        fftSize = 1 << fftOrder,    // 2048
        scopeSize = 1024            // Number of frequency bins to display
    };
    
    FFTAnalyzer();
    ~FFTAnalyzer() = default;
    
    void prepare(double sampleRate);
    void processSamples(const float* samples, int numSamples) noexcept;
    void processSamples(const juce::AudioBuffer<float>& buffer, int numChannels) noexcept;
    void getFFTData(float* fftData, int numBins, float sampleRate);
    bool isDataReady() const { return hasSpectrumData.load(); }
    
private:
    static constexpr int fifoCapacity = fftSize * 8;
    static constexpr float minDisplayFrequency = 20.0f;
    static constexpr float maxDisplayFrequency = 20000.0f;
    static constexpr float minDisplayDecibels = -100.0f;
    static constexpr float maxDisplayDecibels = 0.0f;
    static constexpr float attackCoefficient = 0.78f;
    static constexpr float releaseCoefficient = 0.62f;
    static constexpr float idleDecayCoefficient = 0.70f;
    static constexpr auto windowType = juce::dsp::WindowingFunction<float>::blackmanHarris;

    juce::dsp::FFT forwardFFT;
    juce::dsp::WindowingFunction<float> window;
    
    juce::AbstractFifo sampleFifo { fifoCapacity };
    std::array<float, fifoCapacity> fifo;
    std::array<float, fftSize> analysisBuffer;
    std::array<float, fftSize * 2> fftData;
    std::vector<float> smoothedDisplayData;
    
    int analysisBufferIndex = 0;
    std::atomic<bool> hasSpectrumData { false };
    
    void decayDisplayData();
    void processAvailableSamples(float sampleRate);
    void processFFTFrame(float sampleRate);
};
