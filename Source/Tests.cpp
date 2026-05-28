#include "PluginProcessor.h"
#include <JuceHeader.h>

class EQoonProcessorTest : public juce::UnitTest
{
public:
    EQoonProcessorTest() : juce::UnitTest("EQoon Processor", "EQoon") {}

    void runTest() override
    {
        runFlatResponseTests();
        runBandGainTests();
        runSummingModeTests();
        runCutFilterTests();
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
};

static EQoonProcessorTest eqoonTest;
