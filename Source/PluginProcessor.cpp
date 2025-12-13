#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    // Initialize FFT analyzer
    fftAnalyzer = std::make_unique<FFTAnalyzer>();
}

EQoonAudioProcessor::~EQoonAudioProcessor()
{
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

void EQoonAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String EQoonAudioProcessor::getProgramName (int index)
{
    return {};
}

void EQoonAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

void EQoonAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    
    juce::dsp::ProcessSpec spec;
    spec.maximumBlockSize = samplesPerBlock;
    spec.sampleRate = sampleRate;
    spec.numChannels = 1;
    
    // Prepare FFT analyzer
    fftAnalyzer->prepare(sampleRate);

    leftChain.prepare(spec);
    rightChain.prepare(spec);

    updateFilters();
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

    // Clear any output channels that didn't contain input data
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Update filters if needed
    updateFilters();

    // Create audio blocks for processing
    auto block = juce::dsp::AudioBlock<float>(buffer);
    auto leftBlock = block.getSingleChannelBlock(0);
    auto rightBlock = totalNumInputChannels > 1 ? block.getSingleChannelBlock(1) : leftBlock;

    // Process left channel
    juce::dsp::ProcessContextReplacing<float> leftContext(leftBlock);
    leftChain.process(leftContext);

    // Process right channel if stereo
    if (totalNumInputChannels > 1) {
        juce::dsp::ProcessContextReplacing<float> rightContext(rightBlock);
        rightChain.process(rightContext);
    }
    
    // For FFT analysis (use left channel only)
    if (totalNumInputChannels > 0) {
        auto* channelData = buffer.getReadPointer(0);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            if (fftAnalyzer) {
                fftAnalyzer->processSample(channelData[i]);
            }
        }
    }
}

bool EQoonAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* EQoonAudioProcessor::createEditor()
{
    return new EQoonAudioProcessorEditor (*this);
}

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
            jassertfalse; // Invalid peak index
            return nullptr;
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
    update<ChainPositions::Peak1>(leftChain, peakCoefficients1);
    update<ChainPositions::Peak1>(rightChain, peakCoefficients1);

    auto peakCoefficients2 = makePeakFilter(chainSettings, getSampleRate(), 2);
    update<ChainPositions::Peak2>(leftChain, peakCoefficients2);
    update<ChainPositions::Peak2>(rightChain, peakCoefficients2);

    auto peakCoefficients3 = makePeakFilter(chainSettings, getSampleRate(), 3);
    update<ChainPositions::Peak3>(leftChain, peakCoefficients3);
    update<ChainPositions::Peak3>(rightChain, peakCoefficients3);
}

void EQoonAudioProcessor::updateLowShelfFilter(const ChainSettings& chainSettings)
{
    auto lowShelfCoefficients = makeLowShelfFilter(chainSettings, getSampleRate());
    updateCoefficients(leftChain.get<ChainPositions::LowShelf>().coefficients, lowShelfCoefficients);
    updateCoefficients(rightChain.get<ChainPositions::LowShelf>().coefficients, lowShelfCoefficients);
}

void EQoonAudioProcessor::updateHighShelfFilter(const ChainSettings& chainSettings)
{
    auto highShelfCoefficients = makeHighShelfFilter(chainSettings, getSampleRate());
    updateCoefficients(leftChain.get<ChainPositions::HighShelf>().coefficients, highShelfCoefficients);
    updateCoefficients(rightChain.get<ChainPositions::HighShelf>().coefficients, highShelfCoefficients);
}

void EQoonAudioProcessor::updateLowCutFilters(const ChainSettings& chainSettings)
{
    auto lowCutCoefficients = makeLowCutFilter(chainSettings, getSampleRate());
    updateCutFilter(leftChain.get<ChainPositions::LowCut>(), lowCutCoefficients, chainSettings.lowCutSlope);
    updateCutFilter(rightChain.get<ChainPositions::LowCut>(), lowCutCoefficients, chainSettings.lowCutSlope);
}


void EQoonAudioProcessor::updateHighCutFilters(const ChainSettings& chainSettings)
{
    auto highCutCoefficients = makeHighCutFilter(chainSettings, getSampleRate());
    auto& leftHighCut = leftChain.get<ChainPositions::HighCut>();
    updateCutFilter(leftHighCut, highCutCoefficients, chainSettings.highCutSlope);
    auto& rightHighCut = rightChain.get<ChainPositions::HighCut>();
    updateCutFilter(rightHighCut, highCutCoefficients, chainSettings.highCutSlope);
}

void EQoonAudioProcessor::updateFilters()
{
    auto chainSettings = getChainSettings(apvts);
    updateLowCutFilters(chainSettings);
    updatePeakFilters(chainSettings);
    updateLowShelfFilter(chainSettings);
    updateHighShelfFilter(chainSettings);
    updateHighCutFilters(chainSettings);
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

    return layout;
}

// FFT analysis is now handled by the FFTAnalyzer class

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EQoonAudioProcessor();
}

#endif // JucePlugin_PreferredChannelConfigurations
