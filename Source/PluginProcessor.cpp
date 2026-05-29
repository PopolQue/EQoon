#include "PluginProcessor.h"
#ifndef EQOON_TEST_BUILD
#include "PluginEditor.h"
#endif

void updateCoefficients(Coefficients& old, const Coefficients& replacements)
{
    if (old != replacements)
        old = replacements;
}



#ifndef JucePlugin_PreferredChannelConfigurations
EQoonAudioProcessor::EQoonAudioProcessor()
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
        apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    preEQAnalyzer = std::make_unique<FFTAnalyzer>();
    postEQAnalyzer = std::make_unique<FFTAnalyzer>();

    for (auto* param : getParameters())
        param->addListener(this);
}

EQoonAudioProcessor::~EQoonAudioProcessor()
{
    for (auto* param : getParameters())
        param->removeListener(this);
}

const juce::String EQoonAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool EQoonAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool EQoonAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool EQoonAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double EQoonAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int EQoonAudioProcessor::getNumPrograms()
{
    return 1;
}

int EQoonAudioProcessor::getCurrentProgram()
{
    return 0;
}

void EQoonAudioProcessor::setCurrentProgram (int)
{
}

const juce::String EQoonAudioProcessor::getProgramName (int)
{
    return {};
}

void EQoonAudioProcessor::changeProgramName (int, const juce::String&)
{
}

void EQoonAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.maximumBlockSize = samplesPerBlock;
    spec.sampleRate = sampleRate;
    spec.numChannels = 1;
    
    preEQAnalyzer->prepare(sampleRate);
    postEQAnalyzer->prepare(sampleRate);

    leftLowCutChain.prepare(spec);
    rightLowCutChain.prepare(spec);
    leftHighCutChain.prepare(spec);
    rightHighCutChain.prepare(spec);

    leftLowShelf.prepare(spec);
    rightLowShelf.prepare(spec);
    leftPeak1.prepare(spec);
    rightPeak1.prepare(spec);
    leftPeak2.prepare(spec);
    rightPeak2.prepare(spec);
    leftPeak3.prepare(spec);
    rightPeak3.prepare(spec);
    leftHighShelf.prepare(spec);
    rightHighShelf.prepare(spec);

    juce::dsp::ProcessSpec stereoSpec;
    stereoSpec.maximumBlockSize = samplesPerBlock;
    stereoSpec.sampleRate = sampleRate;
    stereoSpec.numChannels = 2;
    makeupGain.prepare(stereoSpec);

    scratchRefBuf.setSize(1, samplesPerBlock, false, false, false);
    scratchTempBuf.setSize(1, samplesPerBlock, false, false, false);

    updateFilters();
}

void EQoonAudioProcessor::parameterValueChanged(int, float)
{
    parametersChanged.store(true, std::memory_order_release);
}

void EQoonAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool EQoonAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void EQoonAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    if (totalNumInputChannels > 0 && preEQAnalyzer != nullptr)
        preEQAnalyzer->processSamples(buffer, totalNumInputChannels);

    if (parametersChanged.exchange(false, std::memory_order_acquire))
        updateFilters();

    auto block = juce::dsp::AudioBlock<float>(buffer);
    auto numSamples = static_cast<int>(block.getNumSamples());

    if (numSamples > scratchRefBuf.getNumSamples())
    {
        scratchRefBuf.setSize(1, numSamples, false, false, false);
        scratchTempBuf.setSize(1, numSamples, false, false, false);
    }

    auto chainSettings = getChainSettings(apvts);

    if (chainSettings.processingMode == Processing_MS && totalNumInputChannels == 2)
    {
        auto* leftCh = buffer.getWritePointer(0);
        auto* rightCh = buffer.getWritePointer(1);
        for (int s = 0; s < numSamples; ++s)
        {
            float l = leftCh[s];
            float r = rightCh[s];
            leftCh[s] = (l + r) * 0.70710678f; // Mid
            rightCh[s] = (l - r) * 0.70710678f; // Side
        }
        DBG("MS Mode active");
    }

    for (int ch = 0; ch < totalNumInputChannels; ++ch)
    {
        auto chBlock = block.getSingleChannelBlock(ch);
        auto* data = chBlock.getChannelPointer(0);

        auto& lowCutChain = (ch == 0) ? leftLowCutChain : rightLowCutChain;
        auto& highCutChain = (ch == 0) ? leftHighCutChain : rightHighCutChain;
        auto& lowShelf = (ch == 0) ? leftLowShelf : rightLowShelf;
        auto& peak1 = (ch == 0) ? leftPeak1 : rightPeak1;
        auto& peak2 = (ch == 0) ? leftPeak2 : rightPeak2;
        auto& peak3 = (ch == 0) ? leftPeak3 : rightPeak3;
        auto& highShelf = (ch == 0) ? leftHighShelf : rightHighShelf;

        // 1. Process LowCut (series, first in chain)
        {
            auto ctx = juce::dsp::ProcessContextReplacing<float>(chBlock);
            lowCutChain.process(ctx);
        }

        if (chainSettings.summingMode == Summing_Classic)
        {
            // Classic cascaded: LowCut → LowShelf → Peak1 → Peak2 → Peak3 → HighShelf → HighCut
            // Each filter processes the previous one's output in-place.
            {
                auto ctx = juce::dsp::ProcessContextReplacing<float>(chBlock);
                lowShelf.process(ctx);
            }
            {
                auto ctx = juce::dsp::ProcessContextReplacing<float>(chBlock);
                peak1.process(ctx);
            }
            {
                auto ctx = juce::dsp::ProcessContextReplacing<float>(chBlock);
                peak2.process(ctx);
            }
            {
                auto ctx = juce::dsp::ProcessContextReplacing<float>(chBlock);
                peak3.process(ctx);
            }
            {
                auto ctx = juce::dsp::ProcessContextReplacing<float>(chBlock);
                highShelf.process(ctx);
            }
        }
        else
        {
            // 2. Save post-LowCut as reference for parallel bands
            scratchRefBuf.copyFrom(0, 0, data, numSamples);

            // 3. Clear output — we'll reconstruct from parallel bands
            juce::FloatVectorOperations::fill(data, 0.0f, numSamples);

            // 4. Process parallel bands according to summing mode
            switch (chainSettings.summingMode)
            {
                case Summing_Average:
                case Summing_Sum:
            {
                auto processAndSum = [&](juce::dsp::IIR::Filter<float>& filter)
                {
                    scratchTempBuf.copyFrom(0, 0, scratchRefBuf.getReadPointer(0), numSamples);
                    auto tempBlock = juce::dsp::AudioBlock<float>(scratchTempBuf);
                    filter.process(juce::dsp::ProcessContextReplacing<float>(tempBlock));
                    juce::FloatVectorOperations::add(data, scratchTempBuf.getReadPointer(0), numSamples);
                };

                processAndSum(lowShelf);
                processAndSum(peak1);
                processAndSum(peak2);
                processAndSum(peak3);
                processAndSum(highShelf);

                if (chainSettings.summingMode == Summing_Average)
                    juce::FloatVectorOperations::multiply(data, 1.0f / 5.0f, numSamples);
                break;
            }

            case Summing_Maximum:
            {
                auto processAndMax = [&](juce::dsp::IIR::Filter<float>& filter)
                {
                    scratchTempBuf.copyFrom(0, 0, scratchRefBuf.getReadPointer(0), numSamples);
                    auto tempBlock = juce::dsp::AudioBlock<float>(scratchTempBuf);
                    filter.process(juce::dsp::ProcessContextReplacing<float>(tempBlock));
                    for (int s = 0; s < numSamples; ++s)
                        data[s] = juce::jmax(data[s], scratchTempBuf.getSample(0, s));
                };

                scratchTempBuf.copyFrom(0, 0, scratchRefBuf.getReadPointer(0), numSamples);
                {
                    auto tb = juce::dsp::AudioBlock<float>(scratchTempBuf);
                    lowShelf.process(juce::dsp::ProcessContextReplacing<float>(tb));
                }
                juce::FloatVectorOperations::copy(data, scratchTempBuf.getReadPointer(0), numSamples);
                processAndMax(peak1);
                processAndMax(peak2);
                processAndMax(peak3);
                processAndMax(highShelf);
                break;
            }
        }
        }   // close else block (parallel branches)

        // 5. Process HighCut (series, last in chain)
        {
            auto ctx = juce::dsp::ProcessContextReplacing<float>(chBlock);
            highCutChain.process(ctx);
        }
    }

    if (chainSettings.processingMode == Processing_MS && totalNumInputChannels == 2)
    {
        auto* midCh = buffer.getWritePointer(0);
        auto* sideCh = buffer.getWritePointer(1);
        for (int s = 0; s < numSamples; ++s)
        {
            float m = midCh[s];
            float s_sig = sideCh[s];
            midCh[s] = (m + s_sig) * 0.70710678f; // Left
            sideCh[s] = (m - s_sig) * 0.70710678f; // Right
        }
    }

    // 6. Apply makeup gain to the full stereo result
    makeupGain.process(juce::dsp::ProcessContextReplacing<float>(block));

    if (totalNumInputChannels > 0 && postEQAnalyzer != nullptr)
        postEQAnalyzer->processSamples(buffer, totalNumInputChannels);
}

bool EQoonAudioProcessor::hasEditor() const
{
    return true;
}

#ifndef EQOON_TEST_BUILD
juce::AudioProcessorEditor* EQoonAudioProcessor::createEditor()
{
    return new EQoonAudioProcessorEditor (*this);
}
#else
juce::AudioProcessorEditor* EQoonAudioProcessor::createEditor() { return nullptr; }
#endif

void EQoonAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream mos(destData, true);
    apvts.state.writeToStream(mos);
}

void EQoonAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto tree = juce::ValueTree::readFromData(data, sizeInBytes);
    if (tree.isValid())
    {
        apvts.replaceState(tree);
        updateFilters();
    }
}

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts)
{
    ChainSettings settings;
    settings.lowCutFreq = apvts.getRawParameterValue("LowCut Freq")->load();
    settings.lowCutSlope = static_cast<Slope>(apvts.getRawParameterValue("LowCut Slope")->load());
    settings.lowCutQuality = apvts.getRawParameterValue("LowCut Quality")->load();
    settings.lowShelfFreq = apvts.getRawParameterValue("LowShelf Freq")->load();
    settings.lowShelfGainInDecibels = apvts.getRawParameterValue("LowShelf Gain")->load();
    settings.lowShelfQuality = apvts.getRawParameterValue("LowShelf Quality")->load();
    settings.peakFreq1 = apvts.getRawParameterValue("Peak1 Freq")->load();
    settings.peakGainInDecibels1 = apvts.getRawParameterValue("Peak1 Gain")->load();
    settings.peakQuality1 = apvts.getRawParameterValue("Peak1 Quality")->load();
    settings.peakFreq2 = apvts.getRawParameterValue("Peak2 Freq")->load();
    settings.peakGainInDecibels2 = apvts.getRawParameterValue("Peak2 Gain")->load();
    settings.peakQuality2 = apvts.getRawParameterValue("Peak2 Quality")->load();
    settings.peakFreq3 = apvts.getRawParameterValue("Peak3 Freq")->load();
    settings.peakGainInDecibels3 = apvts.getRawParameterValue("Peak3 Gain")->load();
    settings.peakQuality3 = apvts.getRawParameterValue("Peak3 Quality")->load();
    settings.highShelfFreq = apvts.getRawParameterValue("HighShelf Freq")->load();
    settings.highShelfGainInDecibels = apvts.getRawParameterValue("HighShelf Gain")->load();
    settings.highShelfQuality = apvts.getRawParameterValue("HighShelf Quality")->load();
    settings.highCutFreq = apvts.getRawParameterValue("HighCut Freq")->load();
    settings.highCutSlope = static_cast<Slope>(apvts.getRawParameterValue("HighCut Slope")->load());
    settings.highCutQuality = apvts.getRawParameterValue("HighCut Quality")->load();
    settings.makeupGainDb = apvts.getRawParameterValue("Makeup Gain")->load();
    {
        const int summingIndex = juce::jlimit(0, 3, static_cast<int>(apvts.getRawParameterValue("Summing Mode")->load()));
        settings.summingMode = static_cast<SummingMode>(summingIndex);
    }
    {
        const int processingIndex = juce::jlimit(0, 1, static_cast<int>(apvts.getRawParameterValue("Processing Mode")->load()));
        settings.processingMode = static_cast<ProcessingMode>(processingIndex);
    }
    return settings;
}

Coefficients makePeakFilter(const ChainSettings& chainSettings, double sampleRate, int peakIndex)
{
    switch (peakIndex)
    {
        case 1:
            return juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                sampleRate, chainSettings.peakFreq1, chainSettings.peakQuality1, juce::Decibels::decibelsToGain(chainSettings.peakGainInDecibels1));
        case 2:
            return juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                sampleRate, chainSettings.peakFreq2, chainSettings.peakQuality2, juce::Decibels::decibelsToGain(chainSettings.peakGainInDecibels2));
        case 3:
            return juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                sampleRate, chainSettings.peakFreq3, chainSettings.peakQuality3, juce::Decibels::decibelsToGain(chainSettings.peakGainInDecibels3));
        default:
            jassertfalse;
            return juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, 1000.0f, 1.0f, 1.0f);
    }
}

Coefficients makeLowShelfFilter(const ChainSettings& chainSettings, double sampleRate)
{
    return juce::dsp::IIR::Coefficients<float>::makeLowShelf(sampleRate,
                                                             chainSettings.lowShelfFreq,
                                                             chainSettings.lowShelfQuality,
                                                             juce::Decibels::decibelsToGain(chainSettings.lowShelfGainInDecibels));
}

Coefficients makeHighShelfFilter(const ChainSettings& chainSettings, double sampleRate)
{
    return juce::dsp::IIR::Coefficients<float>::makeHighShelf(sampleRate,
                                                              chainSettings.highShelfFreq,
                                                              chainSettings.highShelfQuality,
                                                              juce::Decibels::decibelsToGain(chainSettings.highShelfGainInDecibels));
}

void EQoonAudioProcessor::updatePeakFilters(const ChainSettings& chainSettings)
{
    auto peakCoefficients1 = makePeakFilter(chainSettings, getSampleRate(), 1);
    updateCoefficients(leftPeak1.coefficients, peakCoefficients1);
    updateCoefficients(rightPeak1.coefficients, peakCoefficients1);

    auto peakCoefficients2 = makePeakFilter(chainSettings, getSampleRate(), 2);
    updateCoefficients(leftPeak2.coefficients, peakCoefficients2);
    updateCoefficients(rightPeak2.coefficients, peakCoefficients2);

    auto peakCoefficients3 = makePeakFilter(chainSettings, getSampleRate(), 3);
    updateCoefficients(leftPeak3.coefficients, peakCoefficients3);
    updateCoefficients(rightPeak3.coefficients, peakCoefficients3);
}

void EQoonAudioProcessor::updateLowShelfFilter(const ChainSettings& chainSettings)
{
    auto lowShelfCoefficients = makeLowShelfFilter(chainSettings, getSampleRate());
    updateCoefficients(leftLowShelf.coefficients, lowShelfCoefficients);
    updateCoefficients(rightLowShelf.coefficients, lowShelfCoefficients);
}

void EQoonAudioProcessor::updateHighShelfFilter(const ChainSettings& chainSettings)
{
    auto highShelfCoefficients = makeHighShelfFilter(chainSettings, getSampleRate());
    updateCoefficients(leftHighShelf.coefficients, highShelfCoefficients);
    updateCoefficients(rightHighShelf.coefficients, highShelfCoefficients);
}

void EQoonAudioProcessor::updateLowCutFilters(const ChainSettings& chainSettings)
{
    auto lowCutCoefficients = makeLowCutFilter(chainSettings, getSampleRate());
    updateCutFilter(leftLowCutChain, lowCutCoefficients, chainSettings.lowCutSlope);
    updateCutFilter(rightLowCutChain, lowCutCoefficients, chainSettings.lowCutSlope);
}

void EQoonAudioProcessor::updateHighCutFilters(const ChainSettings& chainSettings)
{
    auto highCutCoefficients = makeHighCutFilter(chainSettings, getSampleRate());
    updateCutFilter(leftHighCutChain, highCutCoefficients, chainSettings.highCutSlope);
    updateCutFilter(rightHighCutChain, highCutCoefficients, chainSettings.highCutSlope);
}

void EQoonAudioProcessor::updateFilters()
{
    auto chainSettings = getChainSettings(apvts);
    updateLowCutFilters(chainSettings);
    updatePeakFilters(chainSettings);
    updateLowShelfFilter(chainSettings);
    updateHighShelfFilter(chainSettings);
    updateHighCutFilters(chainSettings);
    makeupGain.setGainDecibels(chainSettings.makeupGainDb);
}


juce::AudioProcessorValueTreeState::ParameterLayout EQoonAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    juce::StringArray slopeSteep;
    for (int i = 0; i < 4; ++i)
    {
        juce::String str;
        str << (12 + i * 12);
        str << " dB/oct";
        slopeSteep.add(str);
    }

    layout.add(std::make_unique<juce::AudioParameterFloat>("LowCut Freq",
                                                           "LowCut Freq",
                                                           juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.3f),
                                                           20.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("LowCut Quality",
                                                           "LowCut Quality",
                                                           juce::NormalisableRange<float>(0.1f, 10.f, 0.05f, 1.f),
                                                           1.f));
    layout.add(std::make_unique<juce::AudioParameterChoice>("LowCut Slope", "LowCut Slope", slopeSteep, 0));

    layout.add(std::make_unique<juce::AudioParameterFloat>("LowShelf Freq",
                                                           "LowShelf Freq",
                                                           juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.3f),
                                                           200.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("LowShelf Gain",
                                                           "LowShelf Gain",
                                                           juce::NormalisableRange<float>(-24.f, 24.f, 0.5f, 1.f),
                                                           0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("LowShelf Quality",
                                                           "LowShelf Quality",
                                                           juce::NormalisableRange<float>(0.1f, 10.f, 0.05f, 1.f),
                                                           1.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak1 Freq",
                                                           "Peak1 Freq",
                                                           juce::NormalisableRange<float>(20.0f, 20000.0f, 1.f, 0.3f),
                                                           750.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak1 Gain",
                                                           "Peak1 Gain",
                                                           -24.0f, 24.0f, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak1 Quality",
                                                           "Peak1 Quality",
                                                           0.1f, 10.0f, 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak2 Freq",
                                                           "Peak2 Freq",
                                                           juce::NormalisableRange<float>(20.0f, 20000.0f, 1.f, 0.3f),
                                                           1500.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak2 Gain",
                                                           "Peak2 Gain",
                                                           -24.0f, 24.0f, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak2 Quality",
                                                           "Peak2 Quality",
                                                           0.1f, 10.0f, 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak3 Freq",
                                                           "Peak3 Freq",
                                                           juce::NormalisableRange<float>(20.0f, 20000.0f, 1.f, 0.3f),
                                                           3000.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak3 Gain",
                                                           "Peak3 Gain",
                                                           -24.0f, 24.0f, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak3 Quality",
                                                           "Peak3 Quality",
                                                           0.1f, 10.0f, 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("HighShelf Freq",
                                                           "HighShelf Freq",
                                                           juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.3f),
                                                           10000.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("HighShelf Gain",
                                                           "HighShelf Gain",
                                                           juce::NormalisableRange<float>(-24.f, 24.f, 0.5f, 1.f),
                                                           0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("HighShelf Quality",
                                                           "HighShelf Quality",
                                                           juce::NormalisableRange<float>(0.1f, 10.f, 0.05f, 1.f),
                                                           1.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("HighCut Freq",
                                                           "HighCut Freq",
                                                           juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.3f),
                                                           20000.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("HighCut Quality",
                                                           "HighCut Quality",
                                                           juce::NormalisableRange<float>(0.1f, 10.f, 0.05f, 1.f),
                                                           1.f));
    layout.add(std::make_unique<juce::AudioParameterChoice>("HighCut Slope", "HighCut Slope", slopeSteep, 0));

    juce::StringArray summingModes = { "Classic", "Average", "Sum", "Maximum" };
    layout.add(std::make_unique<juce::AudioParameterChoice>("Summing Mode", "Summing Mode", summingModes, 0));

    juce::StringArray processingModes = { "Left/Right", "Mid/Side" };
    layout.add(std::make_unique<juce::AudioParameterChoice>("Processing Mode", "Processing Mode", processingModes, 0));

    layout.add(std::make_unique<juce::AudioParameterFloat>("Makeup Gain",
                                                           "Makeup Gain",
                                                           juce::NormalisableRange<float>(-12.f, 12.f, 0.1f, 1.f),
                                                           0.0f));

    return layout;
}

// FFT analysis is now handled by the FFTAnalyzer class

//==============================================================================
// This creates new instances of the plugin..
#ifndef EQOON_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EQoonAudioProcessor();
}
#endif

#endif // JucePlugin_PreferredChannelConfigurations

