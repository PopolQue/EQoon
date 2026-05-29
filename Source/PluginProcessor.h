#pragma once

#include <JuceHeader.h>
#include "FFTAnalyzer.h"
#include <array>
#include <complex>

enum Slope
{
    Slope_12,
    Slope_24,
    Slope_36,
    Slope_48
};

enum SummingMode
{
    Summing_Classic,
    Summing_Average,
    Summing_Sum,
    Summing_Maximum
};

enum ProcessingMode
{
    Processing_LR,
    Processing_MS
};

struct ChannelSettings
{
    float peakFreq1 { 750.f }, peakGainInDecibels1 { 0 }, peakQuality1 {1.f};
    float peakFreq2 { 1500.f }, peakGainInDecibels2 { 0 }, peakQuality2 {1.f};
    float peakFreq3 { 3000.f }, peakGainInDecibels3 { 0 }, peakQuality3 {1.f};
    float lowCutFreq { 0 }, highCutFreq { 0 }, lowCutQuality {1.f}, highCutQuality {1.f};
    float lowShelfFreq { 0 }, lowShelfGainInDecibels { 0 }, lowShelfQuality {1.f};
    float highShelfFreq { 0 }, highShelfGainInDecibels { 0 }, highShelfQuality {1.f};
    Slope lowCutSlope { Slope::Slope_12 }, highCutSlope { Slope::Slope_12 };
};

struct ChainSettings
{
    ChannelSettings left, right;
    float makeupGainDb { 0.0f };
    SummingMode summingMode { Summing_Classic };
    ProcessingMode processingMode { Processing_LR };
    bool stereoLink { true };
};

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts);

using Filter = juce::dsp::IIR::Filter<float>;
using CutFilter = juce::dsp::ProcessorChain<Filter, Filter, Filter, Filter>;
using MonoChain = juce::dsp::ProcessorChain<CutFilter, Filter, Filter, Filter, Filter, Filter, CutFilter>;
using Coefficients = Filter::CoefficientsPtr;
using CoefficientsArray = juce::ReferenceCountedArray<juce::dsp::IIR::Coefficients<float>>;
static constexpr float butterworthQ = 0.70710678118f;

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
Coefficients makePeakFilter(const ChannelSettings& channelSettings, double sampleRate, int peakIndex);
Coefficients makeLowShelfFilter(const ChannelSettings& channelSettings, double sampleRate);
Coefficients makeHighShelfFilter(const ChannelSettings& channelSettings, double sampleRate);
CoefficientsArray makeLowCutFilter(const ChannelSettings& channelSettings, double sampleRate);
CoefficientsArray makeHighCutFilter(const ChannelSettings& channelSettings, double sampleRate);
std::complex<double> getComplexResponse(const Coefficients& coeffs, double freq, double sampleRate);

void updateCoefficients(Coefficients& old, const Coefficients& replacements);

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

inline CoefficientsArray makeLowCutFilter(const ChannelSettings& channelSettings, double sampleRate)
{
    const float q = juce::jlimit(0.1f, 10.0f, channelSettings.lowCutQuality);
    const int order = 2 * (static_cast<int>(channelSettings.lowCutSlope) + 1);
    
    // Create a vector to hold all the filter coefficients
    CoefficientsArray allCoeffs;
    
    // Calculate how many biquad sections we need (each provides 12dB/octave)
    const int numSections = (order + 1) / 2;
    
    for (int i = 0; i < numSections; ++i)
    {
        // For each section, create a high-pass filter with the specified Q
        // We'll adjust the Q for each section to maintain the overall Q
        const float sectionQ = (i == numSections - 1) ? q : butterworthQ;
        
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(
            sampleRate,
            channelSettings.lowCutFreq,
            sectionQ);
            
        allCoeffs.add(coeffs);
    }
    
    return allCoeffs;
}

inline CoefficientsArray makeHighCutFilter(const ChannelSettings& channelSettings, double sampleRate)
{
    const float q = juce::jlimit(0.1f, 10.0f, channelSettings.highCutQuality);
    const int order = 2 * (static_cast<int>(channelSettings.highCutSlope) + 1);
    
    // Create a vector to hold all the filter coefficients
    CoefficientsArray allCoeffs;
    
    // Calculate how many biquad sections we need (each provides 12dB/octave)
    const int numSections = (order + 1) / 2;
    
    for (int i = 0; i < numSections; ++i)
    {
        // For each section, create a low-pass filter with the specified Q
        // We'll adjust the Q for each section to maintain the overall Q
        const float sectionQ = (i == 0) ? q : butterworthQ;
        
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(
            sampleRate,
            channelSettings.highCutFreq,
            sectionQ);
            
        allCoeffs.add(coeffs);
    }
    
    return allCoeffs;
}

inline std::complex<double> getComplexResponse(const Coefficients& coeffs, double freq, double sampleRate)
{
    if (coeffs == nullptr)
        return 1.0;

    const auto* c = coeffs->getRawCoefficients();
    const double w = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
    const double cos_w = std::cos(w);
    const double sin_w = std::sin(w);
    const double cos_2w = std::cos(2.0 * w);
    const double sin_2w = std::sin(2.0 * w);

    // H(z) = (b0 + b1*z^-1 + b2*z^-2) / (a0 + a1*z^-1 + a2*z^-2)
    // z = e^(jω): H(e^jω) = (b0 + b1*e^-jω + b2*e^-j2ω) / (a0 + a1*e^-jω + a2*e^-j2ω)
    // e^-jω = cos(ω) - j*sin(ω), e^-j2ω = cos(2ω) - j*sin(2ω)
    const double realNum = c[0] + c[1] * cos_w + c[2] * cos_2w;
    const double imagNum = -(c[1] * sin_w + c[2] * sin_2w);
    const double realDen = c[5] + c[3] * cos_w + c[4] * cos_2w;
    const double imagDen = -(c[3] * sin_w + c[4] * sin_2w);

    return std::complex<double>(realNum, imagNum) / std::complex<double>(realDen, imagDen);
}

class EQoonAudioProcessor  : public juce::AudioProcessor,
                             private juce::AudioProcessorParameter::Listener
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

    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int, bool) override {}

private:
    std::atomic<bool> parametersChanged{ false };
    std::atomic<bool> isUpdating{ false };

    // Cut filters (series, at the edges of the signal chain)
    CutFilter leftLowCutChain, rightLowCutChain;
    CutFilter leftHighCutChain, rightHighCutChain;

    // Parallel shelf/peak bands (each processes the full signal, outputs summed)
    Filter leftLowShelf, rightLowShelf;
    Filter leftPeak1, rightPeak1;
    Filter leftPeak2, rightPeak2;
    Filter leftPeak3, rightPeak3;
    Filter leftHighShelf, rightHighShelf;

    // Makeup gain applied after parallel sum
    juce::dsp::Gain<float> makeupGain;

    // FFT Analyzers
    std::unique_ptr<FFTAnalyzer> preEQAnalyzer;
    std::unique_ptr<FFTAnalyzer> postEQAnalyzer;

    // Pre-allocated scratch buffers for real-time processing
    juce::AudioBuffer<float> scratchRefBuf;
    juce::AudioBuffer<float> scratchTempBuf;

    void updatePeakFilters(const ChannelSettings& leftSettings, const ChannelSettings& rightSettings);
    void updateLowShelfFilter(const ChannelSettings& leftSettings, const ChannelSettings& rightSettings);
    void updateHighShelfFilter(const ChannelSettings& leftSettings, const ChannelSettings& rightSettings);
    void updateLowCutFilters(const ChannelSettings& leftSettings, const ChannelSettings& rightSettings);
    void updateHighCutFilters(const ChannelSettings& leftSettings, const ChannelSettings& rightSettings);
    void updateFilters();
    
public:
    // For FFT analysis
    void getPreEQFFTData(float* fftData, int numBins, float sampleRate) {
        if (preEQAnalyzer) {
            preEQAnalyzer->getFFTData(fftData, numBins, sampleRate);
        } else {
            std::fill_n(fftData, numBins, 0.0f);
        }
    }

    void getPostEQFFTData(float* fftData, int numBins, float sampleRate) {
        if (postEQAnalyzer) {
            postEQAnalyzer->getFFTData(fftData, numBins, sampleRate);
        } else {
            std::fill_n(fftData, numBins, 0.0f);
        }
    }
    
    bool isFFTDataReady() const {
        return (preEQAnalyzer && preEQAnalyzer->isDataReady())
            && (postEQAnalyzer && postEQAnalyzer->isDataReady());
    }
    
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQoonAudioProcessor)
};
