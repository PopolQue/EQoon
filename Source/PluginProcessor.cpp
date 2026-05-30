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

void EQoonAudioProcessor::parameterValueChanged(int parameterIndex, float newValue)
{
    parametersChanged.store(true, std::memory_order_release);

    if (isUpdating.exchange(true)) return;

    if (auto* linkParam = apvts.getRawParameterValue("Stereo Link"))
    {
        if (linkParam->load() > 0.5f)
        {
            const auto& params = getParameters();
            if (parameterIndex >= 0 && parameterIndex < params.size())
            {
                if (auto* param = dynamic_cast<juce::AudioProcessorParameterWithID*>(params[parameterIndex]))
                {
                    juce::String id = param->paramID;
                    juce::String otherId;

                    if (id.startsWith("L "))
                        otherId = "R " + id.substring(2);
                    else if (id.startsWith("R "))
                        otherId = "L " + id.substring(2);

                    if (otherId.isNotEmpty())
                    {
                        if (auto* otherParam = apvts.getParameter(otherId))
                        {
                            if (std::abs(otherParam->getValue() - newValue) > 0.0001f)
                            {
                                otherParam->setValueNotifyingHost(newValue);
                            }
                        }
                    }
                }
            }
        }
    }
    isUpdating.store(false);
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
    
    if (chainSettings.bypass)
    {
        if (totalNumInputChannels > 0 && postEQAnalyzer != nullptr)
            postEQAnalyzer->processSamples(buffer, totalNumInputChannels);
        return;
    }
    
    // Debug: Calculate total signal energy in the buffer
    float totalEnergy = 0.0f;
    for(int ch=0; ch < buffer.getNumChannels(); ++ch)
        for(int s=0; s < buffer.getNumSamples(); ++s)
            totalEnergy += std::abs(buffer.getSample(ch, s));
    DBG("processBlock energy: " << totalEnergy);

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

            // 3. Initialize output with dry signal for parallel summing
            juce::FloatVectorOperations::copy(data, scratchRefBuf.getReadPointer(0), numSamples);

            // 4. Process parallel bands according to summing mode
            switch (chainSettings.summingMode)
            {
                case Summing_Average:
                case Summing_Sum:
            {
                auto processAndSum = [&](juce::dsp::IIR::Filter<float>& filter, float gainDb)
                {
                    // Only process if not flat (0dB)
                    if (std::abs(gainDb) >= 0.001f)
                    {
                        scratchTempBuf.copyFrom(0, 0, scratchRefBuf.getReadPointer(0), numSamples);
                        auto tempBlock = juce::dsp::AudioBlock<float>(scratchTempBuf);
                        filter.process(juce::dsp::ProcessContextReplacing<float>(tempBlock));
                        juce::FloatVectorOperations::add(data, scratchTempBuf.getReadPointer(0), numSamples);
                    }
                };

                processAndSum(lowShelf, (ch == 0) ? chainSettings.left.lowShelfGainInDecibels : chainSettings.right.lowShelfGainInDecibels);
                processAndSum(peak1, (ch == 0) ? chainSettings.left.peakGainInDecibels1 : chainSettings.right.peakGainInDecibels1);
                processAndSum(peak2, (ch == 0) ? chainSettings.left.peakGainInDecibels2 : chainSettings.right.peakGainInDecibels2);
                processAndSum(peak3, (ch == 0) ? chainSettings.left.peakGainInDecibels3 : chainSettings.right.peakGainInDecibels3);
                processAndSum(highShelf, (ch == 0) ? chainSettings.left.highShelfGainInDecibels : chainSettings.right.highShelfGainInDecibels);

                if (chainSettings.summingMode == Summing_Average)
                    juce::FloatVectorOperations::multiply(data, 1.0f / 6.0f, numSamples); // Divide by 6 (Dry + 5 bands)
                break;
            }

            case Summing_Maximum:
            {
                auto processAndMax = [&](juce::dsp::IIR::Filter<float>& filter, float gainDb)
                {
                    // Only process if not flat (0dB)
                    if (std::abs(gainDb) >= 0.001f)
                    {
                        scratchTempBuf.copyFrom(0, 0, scratchRefBuf.getReadPointer(0), numSamples);
                        auto tempBlock = juce::dsp::AudioBlock<float>(scratchTempBuf);
                        filter.process(juce::dsp::ProcessContextReplacing<float>(tempBlock));
                        
                        for (int s = 0; s < numSamples; ++s)
                            data[s] = juce::jmax(data[s], scratchTempBuf.getSample(0, s));
                    }
                };

                auto& lowShelf = (ch == 0) ? leftLowShelf : rightLowShelf;
                auto& peak1 = (ch == 0) ? leftPeak1 : rightPeak1;
                auto& peak2 = (ch == 0) ? leftPeak2 : rightPeak2;
                auto& peak3 = (ch == 0) ? leftPeak3 : rightPeak3;
                auto& highShelf = (ch == 0) ? leftHighShelf : rightHighShelf;

                processAndMax(lowShelf, (ch == 0) ? chainSettings.left.lowShelfGainInDecibels : chainSettings.right.lowShelfGainInDecibels);
                processAndMax(peak1, (ch == 0) ? chainSettings.left.peakGainInDecibels1 : chainSettings.right.peakGainInDecibels1);
                processAndMax(peak2, (ch == 0) ? chainSettings.left.peakGainInDecibels2 : chainSettings.right.peakGainInDecibels2);
                processAndMax(peak3, (ch == 0) ? chainSettings.left.peakGainInDecibels3 : chainSettings.right.peakGainInDecibels3);
                processAndMax(highShelf, (ch == 0) ? chainSettings.left.highShelfGainInDecibels : chainSettings.right.highShelfGainInDecibels);
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

ChannelSettings getChannelSettings(juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix)
{
    ChannelSettings settings;
    settings.lowCutFreq = apvts.getRawParameterValue(prefix + "LowCut Freq")->load();
    settings.lowCutSlope = static_cast<Slope>(apvts.getRawParameterValue(prefix + "LowCut Slope")->load());
    settings.lowCutQuality = apvts.getRawParameterValue(prefix + "LowCut Quality")->load();
    settings.lowShelfFreq = apvts.getRawParameterValue(prefix + "LowShelf Freq")->load();
    settings.lowShelfGainInDecibels = apvts.getRawParameterValue(prefix + "LowShelf Gain")->load();
    settings.lowShelfQuality = apvts.getRawParameterValue(prefix + "LowShelf Quality")->load();
    settings.peakFreq1 = apvts.getRawParameterValue(prefix + "Peak1 Freq")->load();
    settings.peakGainInDecibels1 = apvts.getRawParameterValue(prefix + "Peak1 Gain")->load();
    settings.peakQuality1 = apvts.getRawParameterValue(prefix + "Peak1 Quality")->load();
    settings.peakFreq2 = apvts.getRawParameterValue(prefix + "Peak2 Freq")->load();
    settings.peakGainInDecibels2 = apvts.getRawParameterValue(prefix + "Peak2 Gain")->load();
    settings.peakQuality2 = apvts.getRawParameterValue(prefix + "Peak2 Quality")->load();
    settings.peakFreq3 = apvts.getRawParameterValue(prefix + "Peak3 Freq")->load();
    settings.peakGainInDecibels3 = apvts.getRawParameterValue(prefix + "Peak3 Gain")->load();
    settings.peakQuality3 = apvts.getRawParameterValue(prefix + "Peak3 Quality")->load();
    settings.highShelfFreq = apvts.getRawParameterValue(prefix + "HighShelf Freq")->load();
    settings.highShelfGainInDecibels = apvts.getRawParameterValue(prefix + "HighShelf Gain")->load();
    settings.highShelfQuality = apvts.getRawParameterValue(prefix + "HighShelf Quality")->load();
    settings.highCutFreq = apvts.getRawParameterValue(prefix + "HighCut Freq")->load();
    settings.highCutSlope = static_cast<Slope>(apvts.getRawParameterValue(prefix + "HighCut Slope")->load());
    settings.highCutQuality = apvts.getRawParameterValue(prefix + "HighCut Quality")->load();
    return settings;
}

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts)
{
    ChainSettings settings;
    settings.left = getChannelSettings(apvts, "L ");
    settings.right = getChannelSettings(apvts, "R ");
    DBG("getChainSettings: Peak1 Freq L=" << settings.left.peakFreq1 << ", Gain L=" << settings.left.peakGainInDecibels1);
    settings.makeupGainDb = apvts.getRawParameterValue("Makeup Gain")->load();
    settings.summingMode = static_cast<SummingMode>(juce::jlimit(0, 3, static_cast<int>(apvts.getRawParameterValue("Summing Mode")->load())));
    settings.processingMode = static_cast<ProcessingMode>(juce::jlimit(0, 1, static_cast<int>(apvts.getRawParameterValue("Processing Mode")->load())));
    settings.stereoLink = apvts.getRawParameterValue("Stereo Link")->load() > 0.5f;
    settings.bypass = apvts.getRawParameterValue("Bypass")->load() > 0.5f;
    return settings;
}

Coefficients makePeakFilter(const ChannelSettings& channelSettings, double sampleRate, int peakIndex)
{
    float freq, quality, gainDb;
    switch (peakIndex)
    {
        case 1: freq = channelSettings.peakFreq1; quality = channelSettings.peakQuality1; gainDb = channelSettings.peakGainInDecibels1; break;
        case 2: freq = channelSettings.peakFreq2; quality = channelSettings.peakQuality2; gainDb = channelSettings.peakGainInDecibels2; break;
        case 3: freq = channelSettings.peakFreq3; quality = channelSettings.peakQuality3; gainDb = channelSettings.peakGainInDecibels3; break;
        default: jassertfalse; return juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, 1000.0f, 1.0f, 1.0f);
    }
    
    // Sanitize
    freq = juce::jlimit(20.0f, 20000.0f, freq);
    quality = juce::jlimit(0.1f, 10.0f, quality);
    gainDb = juce::jlimit(-24.0f, 24.0f, gainDb);
    
    return juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate, freq, quality, juce::Decibels::decibelsToGain(gainDb));
}

Coefficients makeLowShelfFilter(const ChannelSettings& channelSettings, double sampleRate)
{
    float freq = juce::jlimit(20.0f, 20000.0f, channelSettings.lowShelfFreq);
    float quality = juce::jlimit(0.1f, 10.0f, channelSettings.lowShelfQuality);
    float gainDb = juce::jlimit(-24.0f, 24.0f, channelSettings.lowShelfGainInDecibels);
    
    return juce::dsp::IIR::Coefficients<float>::makeLowShelf(sampleRate, freq, quality, juce::Decibels::decibelsToGain(gainDb));
}

Coefficients makeHighShelfFilter(const ChannelSettings& channelSettings, double sampleRate)
{
    float freq = juce::jlimit(20.0f, 20000.0f, channelSettings.highShelfFreq);
    float quality = juce::jlimit(0.1f, 10.0f, channelSettings.highShelfQuality);
    float gainDb = juce::jlimit(-24.0f, 24.0f, channelSettings.highShelfGainInDecibels);
    
    return juce::dsp::IIR::Coefficients<float>::makeHighShelf(sampleRate, freq, quality, juce::Decibels::decibelsToGain(gainDb));
}

void EQoonAudioProcessor::updatePeakFilters(const ChannelSettings& leftSettings, const ChannelSettings& rightSettings)
{
    updateCoefficients(leftPeak1.coefficients, makePeakFilter(leftSettings, getSampleRate(), 1));
    updateCoefficients(rightPeak1.coefficients, makePeakFilter(rightSettings, getSampleRate(), 1));

    updateCoefficients(leftPeak2.coefficients, makePeakFilter(leftSettings, getSampleRate(), 2));
    updateCoefficients(rightPeak2.coefficients, makePeakFilter(rightSettings, getSampleRate(), 2));

    updateCoefficients(leftPeak3.coefficients, makePeakFilter(leftSettings, getSampleRate(), 3));
    updateCoefficients(rightPeak3.coefficients, makePeakFilter(rightSettings, getSampleRate(), 3));
}

void EQoonAudioProcessor::updateLowShelfFilter(const ChannelSettings& leftSettings, const ChannelSettings& rightSettings)
{
    updateCoefficients(leftLowShelf.coefficients, makeLowShelfFilter(leftSettings, getSampleRate()));
    updateCoefficients(rightLowShelf.coefficients, makeLowShelfFilter(rightSettings, getSampleRate()));
}

void EQoonAudioProcessor::updateHighShelfFilter(const ChannelSettings& leftSettings, const ChannelSettings& rightSettings)
{
    updateCoefficients(leftHighShelf.coefficients, makeHighShelfFilter(leftSettings, getSampleRate()));
    updateCoefficients(rightHighShelf.coefficients, makeHighShelfFilter(rightSettings, getSampleRate()));
}

void EQoonAudioProcessor::updateLowCutFilters(const ChannelSettings& leftSettings, const ChannelSettings& rightSettings)
{
    updateCutFilter(leftLowCutChain, makeLowCutFilter(leftSettings, getSampleRate()), leftSettings.lowCutSlope);
    updateCutFilter(rightLowCutChain, makeLowCutFilter(rightSettings, getSampleRate()), rightSettings.lowCutSlope);
}

void EQoonAudioProcessor::updateHighCutFilters(const ChannelSettings& leftSettings, const ChannelSettings& rightSettings)
{
    updateCutFilter(leftHighCutChain, makeHighCutFilter(leftSettings, getSampleRate()), leftSettings.highCutSlope);
    updateCutFilter(rightHighCutChain, makeHighCutFilter(rightSettings, getSampleRate()), rightSettings.highCutSlope);
}

void EQoonAudioProcessor::updateFilters()
{
    auto chainSettings = getChainSettings(apvts);
    DBG("Updating filters. Peak1 Freq: " << chainSettings.left.peakFreq1 << ", Gain: " << chainSettings.left.peakGainInDecibels1);
    updateLowCutFilters(chainSettings.left, chainSettings.right);
    updatePeakFilters(chainSettings.left, chainSettings.right);
    updateLowShelfFilter(chainSettings.left, chainSettings.right);
    updateHighShelfFilter(chainSettings.left, chainSettings.right);
    updateHighCutFilters(chainSettings.left, chainSettings.right);
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

    auto addParams = [&](const juce::String& prefix)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "LowCut Freq", prefix + "LowCut Freq", juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.3f), 20.f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "LowCut Quality", prefix + "LowCut Quality", juce::NormalisableRange<float>(0.1f, 10.f, 0.05f, 1.f), 1.f));
        layout.add(std::make_unique<juce::AudioParameterChoice>(prefix + "LowCut Slope", prefix + "LowCut Slope", slopeSteep, 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "LowShelf Freq", prefix + "LowShelf Freq", juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.3f), 200.f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "LowShelf Gain", prefix + "LowShelf Gain", juce::NormalisableRange<float>(-24.f, 24.f, 0.5f, 1.f), 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "LowShelf Quality", prefix + "LowShelf Quality", juce::NormalisableRange<float>(0.1f, 10.f, 0.05f, 1.f), 1.f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Peak1 Freq", prefix + "Peak1 Freq", juce::NormalisableRange<float>(20.0f, 20000.0f, 1.f, 0.3f), 750.f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Peak1 Gain", prefix + "Peak1 Gain", -24.0f, 24.0f, 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Peak1 Quality", prefix + "Peak1 Quality", 0.1f, 10.0f, 1.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Peak2 Freq", prefix + "Peak2 Freq", juce::NormalisableRange<float>(20.0f, 20000.0f, 1.f, 0.3f), 1500.f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Peak2 Gain", prefix + "Peak2 Gain", -24.0f, 24.0f, 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Peak2 Quality", prefix + "Peak2 Quality", 0.1f, 10.0f, 1.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Peak3 Freq", prefix + "Peak3 Freq", juce::NormalisableRange<float>(20.0f, 20000.0f, 1.f, 0.3f), 3000.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Peak3 Gain", prefix + "Peak3 Gain", -24.0f, 24.0f, 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "Peak3 Quality", prefix + "Peak3 Quality", 0.1f, 10.0f, 1.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "HighShelf Freq", prefix + "HighShelf Freq", juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.3f), 10000.f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "HighShelf Gain", prefix + "HighShelf Gain", juce::NormalisableRange<float>(-24.f, 24.f, 0.5f, 1.f), 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "HighShelf Quality", prefix + "HighShelf Quality", juce::NormalisableRange<float>(0.1f, 10.f, 0.05f, 1.f), 1.f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "HighCut Freq", prefix + "HighCut Freq", juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.3f), 20000.f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(prefix + "HighCut Quality", prefix + "HighCut Quality", juce::NormalisableRange<float>(0.1f, 10.f, 0.05f, 1.f), 1.f));
        layout.add(std::make_unique<juce::AudioParameterChoice>(prefix + "HighCut Slope", prefix + "HighCut Slope", slopeSteep, 0));
    };

    addParams("L ");
    addParams("R ");

    layout.add(std::make_unique<juce::AudioParameterBool>("Bypass", "Bypass", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>("Summing Mode", "Summing Mode", juce::StringArray{ "Classic", "Average", "Sum", "Maximum" }, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("Processing Mode", "Processing Mode", juce::StringArray{ "Left/Right", "Mid/Side" }, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>("Stereo Link", "Stereo Link", true));
    layout.add(std::make_unique<juce::AudioParameterFloat>("Makeup Gain", "Makeup Gain", juce::NormalisableRange<float>(-12.f, 12.f, 0.1f, 1.f), 0.0f));

    return layout;
}

// FFT analysis is now handled by the FFTAnalyzer class

//==============================================================================
// This creates new instances of the plugin..
void EQoonAudioProcessor::reset()
{
    leftLowShelf.reset(); rightLowShelf.reset();
    leftPeak1.reset(); rightPeak1.reset();
    leftPeak2.reset(); rightPeak2.reset();
    leftPeak3.reset(); rightPeak3.reset();
    leftHighShelf.reset(); rightHighShelf.reset();
    leftLowCutChain.reset(); rightLowCutChain.reset();
    leftHighCutChain.reset(); rightHighCutChain.reset();
}
//...
#ifndef EQOON_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EQoonAudioProcessor();
}
#endif

#endif // JucePlugin_PreferredChannelConfigurations

