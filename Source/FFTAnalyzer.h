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
    void processSample(float sample);
    void getFFTData(juce::Rectangle<float> bounds, float* fftData, int numBins, float sampleRate);
    bool isDataReady() const { return nextFFTBlockReady; }
    
private:
    juce::dsp::FFT forwardFFT;
    juce::dsp::WindowingFunction<float> window;
    
    std::array<float, fftSize> fifo;
    std::array<float, fftSize * 2> fftData;
    std::vector<std::complex<float>> fftComplex;
    std::vector<float> lastFFTData;
    
    int fifoIndex = 0;
    bool nextFFTBlockReady = false;
    std::mutex fftMutex;
    
    void processFFTFrame();
};
