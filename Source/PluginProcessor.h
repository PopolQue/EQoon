#pragma once

#include <JuceHeader.h>
#include "FFTAnalyzer.h"
#include <array>

// Helper function to limit values between min and max
template <typename T>
T jlimit(T minValue, T maxValue, T valueToConstrain) noexcept
{
    return juce::jlimit(minValue, maxValue, valueToConstrain);
}

enum Slope
{
    Slope_12,
    Slope_24,
    Slope_36,
    Slope_48
};

struct ChainSettings
{
    float peakFreq1 { 0 }, peakGainInDecibels1 { 0 }, peakQuality1 {1.f};
    float peakFreq2 { 0 }, peakGainInDecibels2 { 0 }, peakQuality2 {1.f};
    float peakFreq3 { 0 }, peakGainInDecibels3 { 0 }, peakQuality3 {1.f};
    float lowCutFreq { 0 }, highCutFreq { 0 }, lowCutQuality {1.f}, highCutQuality {1.f};
    float lowShelfFreq { 0 }, lowShelfGainInDecibels { 0 }, lowShelfQuality {1.f};
    float highShelfFreq { 0 }, highShelfGainInDecibels { 0 }, highShelfQuality {1.f};
    Slope lowCutSlope { Slope::Slope_12 }, highCutSlope { Slope::Slope_12 };
};

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts);

using Filter = juce::dsp::IIR::Filter<float>;
using CutFilter = juce::dsp::ProcessorChain<Filter, Filter, Filter, Filter>;
using MonoChain = juce::dsp::ProcessorChain<CutFilter, Filter, Filter, Filter, Filter, Filter, CutFilter>;
using Coefficients = Filter::CoefficientsPtr;
using CoefficientsArray = juce::ReferenceCountedArray<juce::dsp::IIR::Coefficients<float>>;

enum ChainPositions
{
    LowCut,
    LowShelf,
    Peak1,
    Peak2,
    Peak3,
    HighShelf,
    HighCut
};
Coefficients makePeakFilter(const ChainSettings& chainSettings, double sampleRate, int peakIndex);
Coefficients makeLowShelfFilter(const ChainSettings& chainSettings, double sampleRate);
Coefficients makeHighShelfFilter(const ChainSettings& chainSettings, double sampleRate);
CoefficientsArray makeLowCutFilter(const ChainSettings& chainSettings, double sampleRate);
CoefficientsArray makeHighCutFilter(const ChainSettings& chainSettings, double sampleRate);

void updateCoefficients(Coefficients& old, const Coefficients& replacements);

template<int Index, typename ChainType>
void update(ChainType& chain, const Coefficients& coefficients)
{
    auto& filter = chain.template get<Index>();
    updateCoefficients(filter.coefficients, coefficients);
    chain.template setBypassed<Index>(false);
}

template<typename ChainType, size_t... Is>
void updateCutFilterImpl(ChainType& chain, const CoefficientsArray& coefficientsArray, int numFiltersToEnable, std::index_sequence<Is...>)
{
    // Bypass all filters first
    (chain.template setBypassed<Is>(true), ...);

    // Apply the coefficients to each filter in the chain
    int i = 0;
    auto updateFilter = [&](auto index) {
        if (i < numFiltersToEnable && i < coefficientsArray.size()) {
            updateCoefficients(chain.template get<index>().coefficients, coefficientsArray[i]);
            chain.template setBypassed<index>(false);
            ++i;
        }
    };
    
    (updateFilter(std::integral_constant<size_t, Is>{}), ...);
}

template<typename ChainType>
void updateCutFilter(ChainType& chain, const CoefficientsArray& coefficientsArray, Slope slope)
{
    // Determine how many filters we need to enable based on slope
    int numFiltersToEnable = 0;
    switch (slope)
    {
        case Slope_48: numFiltersToEnable = 4; break;
        case Slope_36: numFiltersToEnable = 3; break;
        case Slope_24: numFiltersToEnable = 2; break;
        case Slope_12: numFiltersToEnable = 1; break;
    }
    
    updateCutFilterImpl(chain, coefficientsArray, numFiltersToEnable, std::make_index_sequence<4>{});
}

inline CoefficientsArray makeLowCutFilter(const ChainSettings& chainSettings, double sampleRate)
{
    const float q = jlimit(0.1f, 10.0f, chainSettings.lowCutQuality);
    const int order = 2 * (static_cast<int>(chainSettings.lowCutSlope) + 1);
    
    // Create a vector to hold all the filter coefficients
    CoefficientsArray allCoeffs;
    
    // Calculate how many biquad sections we need (each provides 12dB/octave)
    const int numSections = (order + 1) / 2;
    
    for (int i = 0; i < numSections; ++i)
    {
        // For each section, create a high-pass filter with the specified Q
        // We'll adjust the Q for each section to maintain the overall Q
        const float sectionQ = (i == numSections - 1) ? q : 0.7071f; // Only apply Q to the last section
        
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(
            sampleRate,
            chainSettings.lowCutFreq,
            sectionQ);
            
        allCoeffs.add(coeffs);
    }
    
    return allCoeffs;
}

inline CoefficientsArray makeHighCutFilter(const ChainSettings& chainSettings, double sampleRate)
{
    const float q = jlimit(0.1f, 10.0f, chainSettings.highCutQuality);
    const int order = 2 * (static_cast<int>(chainSettings.highCutSlope) + 1);
    
    // Create a vector to hold all the filter coefficients
    CoefficientsArray allCoeffs;
    
    // Calculate how many biquad sections we need (each provides 12dB/octave)
    const int numSections = (order + 1) / 2;
    
    for (int i = 0; i < numSections; ++i)
    {
        // For each section, create a low-pass filter with the specified Q
        // We'll adjust the Q for each section to maintain the overall Q
        const float sectionQ = (i == 0) ? q : 0.7071f; // Only apply Q to the first section
        
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(
            sampleRate,
            chainSettings.highCutFreq,
            sectionQ);
            
        allCoeffs.add(coeffs);
    }
    
    return allCoeffs;
}

class EQoonAudioProcessor  : public juce::AudioProcessor
{
public:
    EQoonAudioProcessor();
    ~EQoonAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

private:
    MonoChain leftChain, rightChain;
    
    // FFT Analyzer
    std::unique_ptr<FFTAnalyzer> fftAnalyzer;
    std::atomic<int> testCounter{0};
    std::mutex fftMutex;
    float maxFFT = 0.0f;  // For tracking maximum FFT value
    
    void updatePeakFilters(const ChainSettings& chainSettings);
    void updateLowShelfFilter(const ChainSettings& chainSettings);
    void updateHighShelfFilter(const ChainSettings& chainSettings);
    void updateLowCutFilters(const ChainSettings& chainSettings);
    void updateHighCutFilters(const ChainSettings& chainSettings);
    void updateFilters();
    void pushNextSampleIntoFifo(float sample) noexcept;
    
public:
    // For FFT analysis
    void getFFTData(juce::Rectangle<float> fftBounds, float* fftData, float fftSize, float sampleRate) {
        if (fftAnalyzer) {
            fftAnalyzer->getFFTData(fftBounds, fftData, static_cast<int>(fftSize), sampleRate);
        } else {
            std::fill_n(fftData, static_cast<int>(fftSize), 0.0f);
        }
    }
    
    bool isFFTDataReady() const { return fftAnalyzer ? fftAnalyzer->isDataReady() : false; }
    int getTestCounter() const { return testCounter.load(); }
    
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQoonAudioProcessor)
};
