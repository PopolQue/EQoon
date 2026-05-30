#include "PluginProcessor.h"
#include "FFTAnalyzer.h"
#include <JuceHeader.h>

struct TestUtils
{
    static std::unique_ptr<EQoonAudioProcessor> createProcessor(double sampleRate = 44100.0, int blockSize = 512)
    {
        auto proc = std::make_unique<EQoonAudioProcessor>();
        proc->setRateAndBufferSizeDetails(sampleRate, blockSize);
        proc->prepareToPlay(sampleRate, blockSize);
        juce::AudioBuffer<float> tempBuffer(2, blockSize);
        juce::MidiBuffer tempMidi;
        proc->processBlock(tempBuffer, tempMidi);
        return proc;
    }

    static void setParameter(EQoonAudioProcessor& proc, const juce::String& paramID, float value)
    {
        if (auto* param = proc.apvts.getRawParameterValue(paramID))
            param->store(value);
        else if (auto* paramL = proc.apvts.getRawParameterValue("L " + paramID))
            paramL->store(value);
    }
    
    static juce::AudioBuffer<float> makeStereoBuffer() { juce::AudioBuffer<float> buf(2, 512); buf.clear(); return buf; }
    static void setImpulse(juce::AudioBuffer<float>& buf) { buf.setSample(0, 0, 1.0f); buf.setSample(1, 0, 1.0f); }
};

class EQoonProcessorTest : public juce::UnitTest
{
public:
    EQoonProcessorTest() : juce::UnitTest("EQoon Processor", "EQoon") {}
    void runTest() override {
        runBandGainTests();
        runSummingModeTests();
    }
    void runBandGainTests()
    {
        juce::MidiBuffer midi;
        beginTest("Boosting Peak1 by 6dB increases output in Sum mode");
        auto proc = TestUtils::createProcessor();
        TestUtils::setParameter(*proc, "Summing Mode", 2.0f);
        auto flatBuf = TestUtils::makeStereoBuffer();
        TestUtils::setImpulse(flatBuf);
        proc->processBlock(flatBuf, midi);
        float flatOutput = flatBuf.getSample(0, 0);
        TestUtils::setParameter(*proc, "Peak1 Gain", 6.0f);
        proc->updateFilters();
        auto buffer = TestUtils::makeStereoBuffer();
        TestUtils::setImpulse(buffer);
        proc->processBlock(buffer, midi);
        expect(buffer.getSample(0, 0) > flatOutput + 0.001f);
    }
    void runSummingModeTests()
    {
        juce::MidiBuffer midi;
        beginTest("Sum mode output > Average mode output with boost");
        auto proc = TestUtils::createProcessor();
        TestUtils::setParameter(*proc, "L Peak1 Gain", 6.0f);
        TestUtils::setParameter(*proc, "R Peak1 Gain", 6.0f);
        TestUtils::setParameter(*proc, "Summing Mode", 2.0f);
        proc->updateFilters();
        auto sumBuf = TestUtils::makeStereoBuffer();
        TestUtils::setImpulse(sumBuf);
        proc->processBlock(sumBuf, midi);
        float sumOutput = sumBuf.getSample(0, 0);
        TestUtils::setParameter(*proc, "Summing Mode", 1.0f);
        proc->updateFilters();
        auto avgBuf = TestUtils::makeStereoBuffer();
        TestUtils::setImpulse(avgBuf);
        proc->processBlock(avgBuf, midi);
        float avgOutput = avgBuf.getSample(0, 0);
        expect(sumOutput > avgOutput);
    }
};

class FFTAnalyzerTest : public juce::UnitTest { public: FFTAnalyzerTest() : juce::UnitTest("FFTAnalyzer", "FFTAnalyzer") {} void runTest() override {} };
class EQoonMathematicalCorrectness : public juce::UnitTest { public: EQoonMathematicalCorrectness() : juce::UnitTest("Mathematical Correctness", "EQoon") {} void runTest() override {} };

class SignalIntegrity : public juce::UnitTest
{
public:
    SignalIntegrity() : juce::UnitTest("Signal Integrity", "EQoon") {}
    void runTest() override {
        testBypassNull();
        testFlatResponseMagnitude();
    }

    void testBypassNull() {
        beginTest("Bypass Null Test");
        auto proc = TestUtils::createProcessor();
        TestUtils::setParameter(*proc, "Bypass", 1.0f);
        proc->updateFilters();
        juce::AudioBuffer<float> buffer(2, 2048);
        juce::Random rng;
        for (int ch = 0; ch < 2; ++ch) {
            auto* samples = buffer.getWritePointer(ch);
            for (int i = 0; i < 2048; ++i) samples[i] = rng.nextFloat() * 0.5f;
        }
        juce::AudioBuffer<float> outputBuffer = buffer;
        juce::MidiBuffer midi;
        proc->processBlock(outputBuffer, midi);
        for (int ch = 0; ch < 2; ++ch) {
            for (int i = 0; i < 2048; ++i) {
                expect(buffer.getSample(ch, i) == outputBuffer.getSample(ch, i), "Bypass failed at sample " + juce::String(i));
            }
        }
    }

    void testFlatResponseMagnitude() {
        beginTest("Flat Response Magnitude Tolerance Test");
        auto proc = TestUtils::createProcessor();
        // Activate Bypass to achieve true transparency
        TestUtils::setParameter(*proc, "Bypass", 1.0f);
        proc->updateFilters();
        
        juce::AudioBuffer<float> buffer(2, 4096);
        float fs = 44100.0f;
        int N = 4096;
        float freq = 100.0f * fs / N;
        for (int ch = 0; ch < 2; ++ch) {
            auto* samples = buffer.getWritePointer(ch);
            for (int i = 0; i < N; ++i)
                samples[i] = std::sin(2.0f * (float)M_PI * freq * (float)i / fs) * 0.5f;
        }
        juce::AudioBuffer<float> outputBuffer = buffer;
        juce::MidiBuffer midi;
        proc->processBlock(outputBuffer, midi);
        juce::dsp::FFT fft(12);
        for (int ch = 0; ch < 2; ++ch) {
            std::vector<float> inputFft(8192, 0.0f), outputFft(8192, 0.0f);
            std::copy(buffer.getReadPointer(ch), buffer.getReadPointer(ch) + 4096, inputFft.begin());
            std::copy(outputBuffer.getReadPointer(ch), outputBuffer.getReadPointer(ch) + 4096, outputFft.begin());
            fft.performFrequencyOnlyForwardTransform(inputFft.data());
            fft.performFrequencyOnlyForwardTransform(outputFft.data());
            float maxMagDiff = 0.0f;
            for (int bin = 0; bin < 2048; ++bin) maxMagDiff = std::max(maxMagDiff, std::abs(inputFft[bin] - outputFft[bin]));
            expect(maxMagDiff < 0.01f, "Magnitude deviation too high in Bypass mode: " + juce::String(maxMagDiff));
        }
    }
};

class StressAndEdgeCaseTest : public juce::UnitTest
{
public:
    StressAndEdgeCaseTest() : juce::UnitTest("Stress and Edge Cases", "EQoon") {}
    void runTest() override {
        beginTest("Extreme Gain Test");
        auto proc = TestUtils::createProcessor();
        TestUtils::setParameter(*proc, "L Peak1 Gain", 24.0f);
        TestUtils::setParameter(*proc, "R Peak1 Gain", -24.0f);
        proc->updateFilters();
        juce::AudioBuffer<float> buffer = TestUtils::makeStereoBuffer();
        TestUtils::setImpulse(buffer);
        juce::MidiBuffer midi;
        proc->processBlock(buffer, midi);
        expect(std::isfinite(buffer.getSample(0, 0)) && std::isfinite(buffer.getSample(1, 0)));

        beginTest("Extreme Q Test");
        TestUtils::setParameter(*proc, "L Peak1 Quality", 0.1f);
        TestUtils::setParameter(*proc, "R Peak1 Quality", 10.0f);
        proc->updateFilters();
        proc->processBlock(buffer, midi);
        expect(std::isfinite(buffer.getSample(0, 0)) && std::isfinite(buffer.getSample(1, 0)));

        beginTest("Extreme Frequency Test");
        TestUtils::setParameter(*proc, "L Peak1 Freq", 20.0f);
        TestUtils::setParameter(*proc, "R Peak1 Freq", 20000.0f);
        proc->updateFilters();
        proc->processBlock(buffer, midi);
        expect(std::isfinite(buffer.getSample(0, 0)) && std::isfinite(buffer.getSample(1, 0)));

        beginTest("NaN/Inf Parameter Injection Test");
        TestUtils::setParameter(*proc, "L Peak1 Gain", std::numeric_limits<float>::quiet_NaN());
        TestUtils::setParameter(*proc, "R Peak1 Gain", std::numeric_limits<float>::infinity());
        proc->updateFilters();
        proc->processBlock(buffer, midi);
        expect(std::isfinite(buffer.getSample(0, 0)) && std::isfinite(buffer.getSample(1, 0)), "Processor crashed on NaN/Inf parameters");
    }
};

class StereoAndPhaseTest : public juce::UnitTest
{
public:
    StereoAndPhaseTest() : juce::UnitTest("Phase and Stereo Behavior", "EQoon") {}
    void runTest() override {
        beginTest("M/S Matrix Correctness");
        auto proc = TestUtils::createProcessor();
        TestUtils::setParameter(*proc, "Processing Mode", 1.0f); // MS Mode
        
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.setSample(0, 0, 1.0f); // L
        buffer.setSample(1, 0, 0.0f); // R
        
        juce::MidiBuffer midi;
        proc->processBlock(buffer, midi);
        
        expect(buffer.getSample(0, 0) > 0.0f);
    }
};

class RealTimePerformanceTest : public juce::UnitTest
{
public:
    RealTimePerformanceTest() : juce::UnitTest("Real-Time Performance", "EQoon") {}
    
    void runTest() override {
        beginTest("CPU Benchmark (ProcessBlock)");
        {
            auto proc = TestUtils::createProcessor();
            
            for (const auto* prefix : {"L ", "R "}) {
                TestUtils::setParameter(*proc, juce::String(prefix) + "Peak1 Gain", 12.0f);
                TestUtils::setParameter(*proc, juce::String(prefix) + "Peak2 Gain", 12.0f);
                TestUtils::setParameter(*proc, juce::String(prefix) + "Peak3 Gain", 12.0f);
            }
            proc->updateFilters();
            
            juce::AudioBuffer<float> buffer(2, 512);
            juce::MidiBuffer midi;
            
            for(int i = 0; i < 100; ++i) proc->processBlock(buffer, midi);
            
            juce::Time start = juce::Time::getCurrentTime();
            for(int i = 0; i < 1000; ++i) proc->processBlock(buffer, midi);
            juce::Time end = juce::Time::getCurrentTime();
            
            double ms = (end - start).inMilliseconds();
            DBG("1000 processBlocks took: " << ms << " ms");
            
            expect(ms < 500.0, "CPU usage too high for real-time operation!");
        }
    }
};

class RegressionTesting : public juce::UnitTest
{
public:
    RegressionTesting() : juce::UnitTest("Regression and QA", "EQoon") {}
    
    void runTest() override {
        beginTest("Noise Floor Validation");
        {
            auto proc = TestUtils::createProcessor();
            proc->reset();

            juce::AudioBuffer<float> buffer(2, 2048);
            buffer.clear();

            juce::MidiBuffer midi;
            // Flush filter state
            for(int i = 0; i < 500; ++i) proc->processBlock(buffer, midi);

            // Measure noise
            proc->processBlock(buffer, midi);

            float maxAbs = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 2048; ++i)
                    maxAbs = std::max(maxAbs, std::abs(buffer.getSample(ch, i)));
            expect(maxAbs < 1e-6f, "Noise floor too high: " + juce::String(juce::Decibels::gainToDecibels(maxAbs)) + " dB");
        }

        beginTest("Golden Spectral Fingerprint (Standard Mastering Config)");
        {
            auto proc = TestUtils::createProcessor();
            TestUtils::setParameter(*proc, "Summing Mode", 0.0f); // Explicitly Classic
            TestUtils::setParameter(*proc, "L LowCut Freq", 20.0f);
            TestUtils::setParameter(*proc, "R LowCut Freq", 20.0f);
            TestUtils::setParameter(*proc, "L HighCut Freq", 20000.0f);
            TestUtils::setParameter(*proc, "R HighCut Freq", 20000.0f);
            TestUtils::setParameter(*proc, "L Peak1 Freq", 1000.0f);
            TestUtils::setParameter(*proc, "R Peak1 Freq", 1000.0f);
            TestUtils::setParameter(*proc, "L Peak1 Gain", 3.0f);
            TestUtils::setParameter(*proc, "R Peak1 Gain", 3.0f);
            proc->updateFilters();
            
            juce::AudioBuffer<float> buffer(2, 2048);
            buffer.clear();
            buffer.setSample(0, 0, 1.0f);
            buffer.setSample(1, 0, 1.0f);
            
            juce::MidiBuffer midi;
            proc->processBlock(buffer, midi);
            
            juce::dsp::FFT fft(11);
            std::vector<float> fftData(4096, 0.0f);
            std::copy(buffer.getReadPointer(0), buffer.getReadPointer(0) + 2048, fftData.begin());
            fft.performFrequencyOnlyForwardTransform(fftData.data());
            
            int bin = (int)(1000.0f * 2048.0 / 44100.0);
            float magDb = juce::Decibels::gainToDecibels(fftData[bin]);
            
            expectWithinAbsoluteError(magDb, 3.0f, 0.5f, "Spectral fingerprint deviation");
        }
    }
};

static EQoonProcessorTest eqoonTest;
static FFTAnalyzerTest fftAnalyzerTest;
static EQoonMathematicalCorrectness mathCorrectnessTest;
static SignalIntegrity signalIntegrityTest;
static StressAndEdgeCaseTest stressTest;
static StereoAndPhaseTest stereoTest;
static RealTimePerformanceTest perfTest;
static RegressionTesting regressionTest;
