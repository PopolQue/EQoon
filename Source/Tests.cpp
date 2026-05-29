#include "PluginProcessor.h"
#include <JuceHeader.h>

class EQoonProcessorTest : public juce::UnitTest
{
public:
    EQoonProcessorTest() : juce::UnitTest("EQoon Processor", "EQoon") {}

    void runTest() override
    {
        runFilterCoefficientTests();
        runFlatResponseTests();
        runBandGainTests();
        runSummingModeTests();
        runCutFilterTests();
        runProcessingModeTests();
        runStereoLinkTests();
    }

private:
    static std::unique_ptr<EQoonAudioProcessor> createProcessor(double sampleRate = 44100.0, int blockSize = 512)
    {
        auto proc = std::make_unique<EQoonAudioProcessor>();
        proc->setRateAndBufferSizeDetails(sampleRate, blockSize);
        proc->prepareToPlay(sampleRate, blockSize);
        return proc;
    }

    static void setParameter(juce::AudioProcessor& proc, const juce::String& paramID, float value)
    {
        auto& eqoonProc = static_cast<EQoonAudioProcessor&>(proc);
        if (auto* param = eqoonProc.apvts.getParameter(paramID))
            param->setValueNotifyingHost(param->convertTo0to1(value));
        else if (auto* paramL = eqoonProc.apvts.getParameter("L " + paramID))
        {
            paramL->setValueNotifyingHost(paramL->convertTo0to1(value));
        }
    }

    static void processImpulse(EQoonAudioProcessor& proc, juce::AudioBuffer<float>& buffer)
    {
        juce::MidiBuffer midi;
        proc.processBlock(buffer, midi);
    }

    static juce::AudioBuffer<float> makeStereoBuffer()
    {
        juce::AudioBuffer<float> buf(2, 512);
        buf.clear();
        return buf;
    }

    static void setImpulse(juce::AudioBuffer<float>& buf)
    {
        buf.setSample(0, 0, 1.0f);
        buf.setSample(1, 0, 1.0f);
    }

    static float getFlatReference(EQoonAudioProcessor& proc)
    {
        auto buf = makeStereoBuffer();
        setImpulse(buf);
        processImpulse(proc, buf);
        return buf.getSample(0, 0);
    }

    void runFilterCoefficientTests()
    {
        beginTest("Peak Filter Coefficients");
        {
            ChannelSettings settings;
            settings.peakFreq1 = 1000.0f;
            settings.peakGainInDecibels1 = 6.0f;
            settings.peakQuality1 = 1.0f;
            
            auto coeffs = makePeakFilter(settings, 44100.0, 1);
            expect(coeffs != nullptr);
            
            // At resonance freq, magnitude should be approx 2.0 (6dB)
            double mag = coeffs->getMagnitudeForFrequency(1000.0, 44100.0);
            expectWithinAbsoluteError(mag, 1.995, 0.01, "Peak filter magnitude should be approx 2.0 (6dB) at 1000Hz");
        }

        beginTest("LowShelf Filter Coefficients");
        {
            ChannelSettings settings;
            settings.lowShelfFreq = 100.0f;
            settings.lowShelfGainInDecibels = 12.0f;
            settings.lowShelfQuality = 0.707f;
            
            auto coeffs = makeLowShelfFilter(settings, 44100.0);
            expect(coeffs != nullptr, "LowShelf coefficients should not be null");
            
            // Low freq magnitude should be approx 3.98 (12dB)
            double mag = coeffs->getMagnitudeForFrequency(20.0, 44100.0);
            expectWithinAbsoluteError(mag, 3.98, 0.05, "LowShelf magnitude should be approx 3.98 (12dB) at 20Hz");
        }

        beginTest("HighShelf Filter Coefficients");
        {
            ChannelSettings settings;
            settings.highShelfFreq = 10000.0f;
            settings.highShelfGainInDecibels = -12.0f;
            settings.highShelfQuality = 0.707f;
            
            auto coeffs = makeHighShelfFilter(settings, 44100.0);
            expect(coeffs != nullptr, "HighShelf coefficients should not be null");
            
            // High freq magnitude should be approx 0.25 (-12dB)
            double mag = coeffs->getMagnitudeForFrequency(15000.0, 44100.0);
            expectWithinAbsoluteError(mag, 0.251, 0.05, "HighShelf magnitude should be approx 0.25 (-12dB) at 15000Hz");
        }

        beginTest("LowCut Filter Coefficients");
        {
            ChannelSettings settings;
            settings.lowCutFreq = 100.0f;
            settings.lowCutQuality = 0.707f;
            settings.lowCutSlope = Slope::Slope_12;
            
            auto coeffsArray = makeLowCutFilter(settings, 44100.0);
            expect(coeffsArray.size() > 0, "LowCut coefficients array should not be empty");
            
            // At cutoff, magnitude should be approx 0.707 (-3dB)
            double mag = coeffsArray[0]->getMagnitudeForFrequency(100.0, 44100.0);
            expectWithinAbsoluteError(mag, 0.707, 0.05, "LowCut magnitude should be approx 0.707 (-3dB) at 100Hz");
        }

        beginTest("HighCut Filter Coefficients");
        {
            ChannelSettings settings;
            settings.highCutFreq = 5000.0f;
            settings.highCutQuality = 0.707f;
            settings.highCutSlope = Slope::Slope_12;
            
            auto coeffsArray = makeHighCutFilter(settings, 44100.0);
            expect(coeffsArray.size() > 0, "HighCut coefficients array should not be empty");
            
            // At cutoff, magnitude should be approx 0.707 (-3dB)
            double mag = coeffsArray[0]->getMagnitudeForFrequency(5000.0, 44100.0);
            expectWithinAbsoluteError(mag, 0.707, 0.05, "HighCut magnitude should be approx 0.707 (-3dB) at 5000Hz");
        }
    }

    void runFlatResponseTests()
    {
        beginTest("Classic mode — flat parameters pass impulse unchanged");
        {
            auto proc = createProcessor();
            setParameter(*proc, "Summing Mode", 0.0f);

            auto buffer = makeStereoBuffer();
            setImpulse(buffer);

            processImpulse(*proc, buffer);

            // Classic cascaded: each filter at 0dB gain, only high-cut at 20000Hz
            // near Nyquist attenuates the first sample to ~0.854.
            expectWithinAbsoluteError(buffer.getSample(0, 0), 0.854f, 0.01f);
        }

        beginTest("Average mode — flat parameters pass impulse unchanged");
        {
            auto proc = createProcessor();
            setParameter(*proc, "Summing Mode", 1.0f);

            auto buffer = makeStereoBuffer();
            setImpulse(buffer);

            processImpulse(*proc, buffer);

            expectWithinAbsoluteError(buffer.getSample(0, 0), 0.854f, 0.01f);
        }

        beginTest("Sum mode — flat parameters pass impulse x5");
        {
            auto proc = createProcessor();
            setParameter(*proc, "Summing Mode", 2.0f);

            auto buffer = makeStereoBuffer();
            setImpulse(buffer);

            processImpulse(*proc, buffer);

            expectWithinAbsoluteError(buffer.getSample(0, 0), 4.27f, 0.02f);
        }

        beginTest("Maximum mode — flat parameters pass impulse unchanged");
        {
            auto proc = createProcessor();
            setParameter(*proc, "Summing Mode", 3.0f);

            auto buffer = makeStereoBuffer();
            setImpulse(buffer);

            processImpulse(*proc, buffer);

            expectWithinAbsoluteError(buffer.getSample(0, 0), 0.854f, 0.01f);
        }
    }

    void runBandGainTests()
    {
        beginTest("Boosting Peak1 by 6dB increases output in Sum mode");
        {
            auto refProc = createProcessor();
            setParameter(*refProc, "Summing Mode", 2.0f);
            float flatOutput = getFlatReference(*refProc);

            auto proc = createProcessor();
            setParameter(*proc, "Summing Mode", 2.0f);
            setParameter(*proc, "Peak1 Gain", 6.0f);
            auto buffer = makeStereoBuffer();
            setImpulse(buffer);
            processImpulse(*proc, buffer);

            expect(buffer.getSample(0, 0) > flatOutput + 0.001f);
        }

        beginTest("Cutting Peak1 by -24dB reduces output in Sum mode");
        {
            auto refProc = createProcessor();
            setParameter(*refProc, "Summing Mode", 2.0f);
            float flatOutput = getFlatReference(*refProc);

            auto proc = createProcessor();
            setParameter(*proc, "Summing Mode", 2.0f);
            setParameter(*proc, "Peak1 Gain", -24.0f);
            auto buffer = makeStereoBuffer();
            setImpulse(buffer);
            processImpulse(*proc, buffer);

            expect(buffer.getSample(0, 0) < flatOutput - 0.001f);
        }
    }

    void runSummingModeTests()
    {
        beginTest("Sum mode output > Average mode output with boost");
        {
            auto proc = createProcessor();
            setParameter(*proc, "Peak1 Gain", 0.5f);

            setParameter(*proc, "Summing Mode", 2.0f);
            auto sumBuf = makeStereoBuffer();
            setImpulse(sumBuf);
            processImpulse(*proc, sumBuf);
            float sumOutput = sumBuf.getSample(0, 0);

            // Create fresh processor for Average measurement
            auto avgProc = createProcessor();
            setParameter(*avgProc, "Peak1 Gain", 0.5f);
            setParameter(*avgProc, "Summing Mode", 1.0f);
            auto avgBuf = makeStereoBuffer();
            setImpulse(avgBuf);
            processImpulse(*avgProc, avgBuf);
            float avgOutput = avgBuf.getSample(0, 0);

            expect(sumOutput > avgOutput);
        }

        beginTest("Maximum mode output > Average mode output with boost");
        {
            auto proc = createProcessor();
            setParameter(*proc, "Peak1 Gain", 0.5f);
            setParameter(*proc, "Peak3 Gain", 0.75f);
            setParameter(*proc, "Summing Mode", 3.0f);
            auto maxBuf = makeStereoBuffer();
            setImpulse(maxBuf);
            processImpulse(*proc, maxBuf);
            float maxOutput = maxBuf.getSample(0, 0);

            auto refProc = createProcessor();
            setParameter(*refProc, "Peak1 Gain", 0.5f);
            setParameter(*refProc, "Peak3 Gain", 0.75f);
            setParameter(*refProc, "Summing Mode", 1.0f);
            auto avgBuf = makeStereoBuffer();
            setImpulse(avgBuf);
            processImpulse(*refProc, avgBuf);
            float avgOutput = avgBuf.getSample(0, 0);

            expect(maxOutput > avgOutput + 0.001f);
        }
    }

    void runCutFilterTests()
    {
        beginTest("HighCut at 20Hz with 48dB slope silences most energy");
        {
            auto proc = createProcessor();
            setParameter(*proc, "HighCut Freq", 20.0f);
            setParameter(*proc, "HighCut Slope", 3.0f);

            auto buffer = makeStereoBuffer();
            buffer.setSample(0, 0, 1.0f);
            buffer.setSample(1, 0, 1.0f);

            processImpulse(*proc, buffer);

            float sumAbs = 0.0f;
            for (int s = 0; s < 512; ++s)
                sumAbs += std::abs(buffer.getSample(0, s));

            expect(sumAbs < 0.05f);
        }

        beginTest("LowCut at 20000Hz with 48dB slope silences most energy");
        {
            auto proc = createProcessor();
            setParameter(*proc, "LowCut Freq", 20000.0f);
            setParameter(*proc, "LowCut Slope", 3.0f);

            auto buffer = makeStereoBuffer();
            buffer.setSample(0, 0, 1.0f);
            buffer.setSample(1, 0, 1.0f);

            processImpulse(*proc, buffer);

            float sumAbs = 0.0f;
            for (int s = 0; s < 512; ++s)
                sumAbs += std::abs(buffer.getSample(0, s));

            expect(sumAbs < 1.0f);
        }
    }

    void runProcessingModeTests()
    {
        beginTest("Mid/Side mode conversion - Discrete Mid");
        {
            auto proc = createProcessor();
            setParameter(*proc, "Processing Mode", 1.0f); // MS Mode
            setParameter(*proc, "Stereo Link", 0.0f);
            
            // Heavy cut on "Side" (Right in MS)
            setParameter(*proc, "R Peak1 Gain", -24.0f);
            setParameter(*proc, "R Peak2 Gain", -24.0f);
            setParameter(*proc, "R Peak3 Gain", -24.0f);
            
            auto buffer = makeStereoBuffer();
            // L=1, R=1 -> Mid=1.414, Side=0.
            buffer.setSample(0, 0, 1.0f);
            buffer.setSample(1, 0, 1.0f);
            
            processImpulse(*proc, buffer);
            
            // Side was 0, so result should be approx original.
            expectWithinAbsoluteError(buffer.getSample(0, 0), 0.854f, 0.05f);
            expectWithinAbsoluteError(buffer.getSample(1, 0), 0.854f, 0.05f);
        }

        beginTest("Mid/Side mode conversion - Discrete Side");
        {
            auto proc = createProcessor();
            setParameter(*proc, "Processing Mode", 1.0f); // MS Mode
            setParameter(*proc, "Stereo Link", 0.0f);
            
            // Heavy cut on "Mid" (Left in MS)
            setParameter(*proc, "L Peak1 Gain", -24.0f);
            setParameter(*proc, "L Peak2 Gain", -24.0f);
            setParameter(*proc, "L Peak3 Gain", -24.0f);
            
            auto buffer = makeStereoBuffer();
            // L=1, R=-1 -> Mid=0, Side=1.414.
            buffer.setSample(0, 0, 1.0f);
            buffer.setSample(1, 0, -1.0f);
            
            processImpulse(*proc, buffer);
            
            // Mid was 0, result should be approx original.
            expectWithinAbsoluteError(buffer.getSample(0, 0), 0.854f, 0.05f);
            expectWithinAbsoluteError(buffer.getSample(1, 0), -0.854f, 0.05f);
        }
    }

    void runStereoLinkTests()
    {
        beginTest("Stereo Link propagates parameter changes");
        {
            auto proc = createProcessor();
            setParameter(*proc, "Stereo Link", 1.0f);
            
            setParameter(*proc, "L Peak1 Freq", 500.0f);
            
            auto* rightParam = proc->apvts.getParameter("R Peak1 Freq");
            expectWithinAbsoluteError(rightParam->getNormalisableRange().convertFrom0to1(rightParam->getValue()), 500.0f, 0.1f);
            
            setParameter(*proc, "R Peak1 Gain", -10.0f);
            
            auto* leftParam = proc->apvts.getParameter("L Peak1 Gain");
            expectWithinAbsoluteError(leftParam->getNormalisableRange().convertFrom0to1(leftParam->getValue()), -10.0f, 0.1f);
        }

        beginTest("Stereo Link disabled does not propagate changes");
        {
            auto proc = createProcessor();
            setParameter(*proc, "Stereo Link", 0.0f);
            
            setParameter(*proc, "L Peak1 Freq", 800.0f);
            
            auto* rightParam = proc->apvts.getParameter("R Peak1 Freq");
            expect(std::abs(rightParam->getNormalisableRange().convertFrom0to1(rightParam->getValue()) - 800.0f) > 10.0f);
        }
    }
};

static EQoonProcessorTest eqoonTest;
