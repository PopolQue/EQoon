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

    void runFlatResponseTests()
    {
        beginTest("Average mode — flat parameters pass impulse unchanged");
        {
            auto proc = createProcessor();
            setParameter(*proc, "Summing Mode", 0.0f);

            juce::AudioBuffer<float> buffer(1, 512);
            buffer.clear();
            buffer.setSample(0, 0, 1.0f);

            processImpulse(*proc, buffer);

            expectWithinAbsoluteError(buffer.getSample(0, 0), 1.0f, 0.001f);
        }

        beginTest("Sum mode — flat parameters pass impulse x5");
        {
            auto proc = createProcessor();
            setParameter(*proc, "Summing Mode", 1.0f);

            juce::AudioBuffer<float> buffer(1, 512);
            buffer.clear();
            buffer.setSample(0, 0, 1.0f);

            processImpulse(*proc, buffer);

            expectWithinAbsoluteError(buffer.getSample(0, 0), 5.0f, 0.001f);
        }

        beginTest("Maximum mode — flat parameters pass impulse unchanged");
        {
            auto proc = createProcessor();
            setParameter(*proc, "Summing Mode", 2.0f);

            juce::AudioBuffer<float> buffer(1, 512);
            buffer.clear();
            buffer.setSample(0, 0, 1.0f);

            processImpulse(*proc, buffer);

            expectWithinAbsoluteError(buffer.getSample(0, 0), 1.0f, 0.001f);
        }
    }

    void runBandGainTests()
    {
        beginTest("Boosting Peak1 by 6dB increases output in Sum mode");
        {
            auto proc = createProcessor();
            setParameter(*proc, "Summing Mode", 1.0f);
            setParameter(*proc, "Peak1 Gain", 6.0f);

            juce::AudioBuffer<float> buffer(1, 512);
            buffer.clear();
            buffer.setSample(0, 0, 1.0f);

            processImpulse(*proc, buffer);

            float flatOutput = 5.0f;
            expect(buffer.getSample(0, 0) > flatOutput + 0.001f);
        }

        beginTest("Cutting Peak1 by -24dB reduces output in Sum mode");
        {
            auto proc = createProcessor();
            setParameter(*proc, "Summing Mode", 1.0f);

            // Flat baseline — all bands at 0 dB, Sum = 5.0
            juce::AudioBuffer<float> baseline(1, 512);
            baseline.clear();
            baseline.setSample(0, 0, 1.0f);
            processImpulse(*proc, baseline);
            float flatOutput = baseline.getSample(0, 0);

            // Deep cut on Peak1
            setParameter(*proc, "Peak1 Gain", -24.0f);
            juce::AudioBuffer<float> buffer(1, 512);
            buffer.clear();
            buffer.setSample(0, 0, 1.0f);
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

            setParameter(*proc, "Summing Mode", 1.0f);
            juce::AudioBuffer<float> sumBuf(1, 512);
            sumBuf.clear();
            sumBuf.setSample(0, 0, 1.0f);
            processImpulse(*proc, sumBuf);
            float sumOutput = sumBuf.getSample(0, 0);

            setParameter(*proc, "Summing Mode", 0.0f);
            juce::AudioBuffer<float> avgBuf(1, 512);
            avgBuf.clear();
            avgBuf.setSample(0, 0, 1.0f);
            processImpulse(*proc, avgBuf);
            float avgOutput = avgBuf.getSample(0, 0);

            expect(sumOutput > avgOutput);
        }

        beginTest("Maximum mode output >= each individual band output");
        {
            auto proc = createProcessor();
            setParameter(*proc, "Peak1 Gain", 0.5f);
            setParameter(*proc, "Peak3 Gain", 0.75f);

            setParameter(*proc, "Summing Mode", 2.0f);
            juce::AudioBuffer<float> maxBuf(1, 512);
            maxBuf.clear();
            maxBuf.setSample(0, 0, 1.0f);
            processImpulse(*proc, maxBuf);
            float maxOutput = maxBuf.getSample(0, 0);

            setParameter(*proc, "Summing Mode", 1.0f);
            setParameter(*proc, "Peak1 Gain", 0.0f);
            setParameter(*proc, "Peak2 Gain", 0.0f);
            setParameter(*proc, "Peak3 Gain", 0.0f);
            setParameter(*proc, "LowShelf Gain", 0.0f);
            setParameter(*proc, "HighShelf Gain", 0.0f);
            juce::AudioBuffer<float> flatBuf(1, 512);
            flatBuf.clear();
            flatBuf.setSample(0, 0, 1.0f);
            processImpulse(*proc, flatBuf);
            float flatOutput = flatBuf.getSample(0, 0);

            expect(maxOutput > flatOutput);
        }
    }

    void runCutFilterTests()
    {
        beginTest("HighCut at 20Hz with 48dB slope silences most energy");
        {
            auto proc = createProcessor();
            setParameter(*proc, "HighCut Freq", 20.0f);
            // Slope index 3 = "48 dB/oct"
            setParameter(*proc, "HighCut Slope", 3.0f);

            juce::AudioBuffer<float> buffer(1, 512);
            for (int s = 0; s < 512; ++s)
                buffer.setSample(0, s, 0.0f);
            buffer.setSample(0, 0, 1.0f);

            processImpulse(*proc, buffer);

            float sumAbs = 0.0f;
            for (int s = 0; s < 512; ++s)
                sumAbs += std::abs(buffer.getSample(0, s));

            expect(sumAbs < 1.0f);
        }

        beginTest("LowCut at 20000Hz with 48dB slope silences most energy");
        {
            auto proc = createProcessor();
            setParameter(*proc, "LowCut Freq", 20000.0f);
            setParameter(*proc, "LowCut Slope", 3.0f);

            juce::AudioBuffer<float> buffer(1, 512);
            for (int s = 0; s < 512; ++s)
                buffer.setSample(0, s, 0.0f);
            buffer.setSample(0, 0, 1.0f);

            processImpulse(*proc, buffer);

            float sumAbs = 0.0f;
            for (int s = 0; s < 512; ++s)
                sumAbs += std::abs(buffer.getSample(0, s));

            expect(sumAbs < 1.0f);
        }
    }
};

static EQoonProcessorTest eqoonProcessorTest;

class EQoonUnitTestRunner
{
public:
    static void runAll();
};

void EQoonUnitTestRunner::runAll()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();
}
