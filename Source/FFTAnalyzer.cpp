#include "FFTAnalyzer.h"

FFTAnalyzer::FFTAnalyzer()
    : forwardFFT(fftOrder)
    , window(fftSize, windowType)
    , smoothedDisplayData(scopeSize, 0.0f)
{
    std::fill(fifo.begin(), fifo.end(), 0.0f);
    std::fill(analysisBuffer.begin(), analysisBuffer.end(), 0.0f);
    std::fill(fftData.begin(), fftData.end(), 0.0f);
}

void FFTAnalyzer::prepare(double)
{
    sampleFifo.reset();
    std::fill(fifo.begin(), fifo.end(), 0.0f);
    std::fill(analysisBuffer.begin(), analysisBuffer.end(), 0.0f);
    std::fill(fftData.begin(), fftData.end(), 0.0f);
    std::fill(smoothedDisplayData.begin(), smoothedDisplayData.end(), 0.0f);

    analysisBufferIndex = 0;
    hasSpectrumData.store(false);
}

void FFTAnalyzer::processSamples(const float* samples, int numSamples) noexcept
{
    if (samples == nullptr || numSamples <= 0)
        return;

    const auto freeSpace = sampleFifo.getFreeSpace();
    if (numSamples > freeSpace)
    {
        const auto samplesToDrop = juce::jmin(numSamples - freeSpace, sampleFifo.getNumReady());
        const auto dropScope = sampleFifo.read(samplesToDrop);
        juce::ignoreUnused(dropScope);
    }

    const auto writeScope = sampleFifo.write(numSamples);

    if (writeScope.blockSize1 > 0)
        std::copy(samples,
                  samples + writeScope.blockSize1,
                  fifo.begin() + writeScope.startIndex1);

    if (writeScope.blockSize2 > 0)
        std::copy(samples + writeScope.blockSize1,
                  samples + writeScope.blockSize1 + writeScope.blockSize2,
                  fifo.begin() + writeScope.startIndex2);
}

void FFTAnalyzer::processSamples(const juce::AudioBuffer<float>& buffer, int numChannels) noexcept
{
    const auto numSamples = buffer.getNumSamples();
    const auto channelsToAnalyze = juce::jmin(numChannels, buffer.getNumChannels());

    if (numSamples <= 0 || channelsToAnalyze <= 0)
        return;

    const auto freeSpace = sampleFifo.getFreeSpace();
    if (numSamples > freeSpace)
    {
        const auto samplesToDrop = juce::jmin(numSamples - freeSpace, sampleFifo.getNumReady());
        const auto dropScope = sampleFifo.read(samplesToDrop);
        juce::ignoreUnused(dropScope);
    }

    const auto writeScope = sampleFifo.write(numSamples);
    auto writeMixedSamples = [this, &buffer, channelsToAnalyze](int startIndex, int blockSize, int sourceOffset)
    {
        for (int sample = 0; sample < blockSize; ++sample)
        {
            float sum = 0.0f;

            for (int channel = 0; channel < channelsToAnalyze; ++channel)
                sum += buffer.getSample(channel, sourceOffset + sample);

            fifo[(size_t) (startIndex + sample)] = sum / (float) channelsToAnalyze;
        }
    };

    if (writeScope.blockSize1 > 0)
        writeMixedSamples(writeScope.startIndex1, writeScope.blockSize1, 0);

    if (writeScope.blockSize2 > 0)
        writeMixedSamples(writeScope.startIndex2, writeScope.blockSize2, writeScope.blockSize1);
}

void FFTAnalyzer::decayDisplayData()
{
    for (auto& value : smoothedDisplayData)
        value *= idleDecayCoefficient;
}

void FFTAnalyzer::processAvailableSamples(float sampleRate)
{
    constexpr int maxFramesPerUpdate = 4;
    int framesProcessed = 0;

    while (sampleFifo.getNumReady() > 0 && framesProcessed < maxFramesPerUpdate)
    {
        const auto samplesNeeded = fftSize - analysisBufferIndex;
        const auto samplesToRead = juce::jmin(sampleFifo.getNumReady(), samplesNeeded);

        const auto readScope = sampleFifo.read(samplesToRead);

        if (readScope.blockSize1 > 0)
        {
            std::copy(fifo.begin() + readScope.startIndex1,
                      fifo.begin() + readScope.startIndex1 + readScope.blockSize1,
                      analysisBuffer.begin() + analysisBufferIndex);
            analysisBufferIndex += readScope.blockSize1;
        }

        if (readScope.blockSize2 > 0)
        {
            std::copy(fifo.begin() + readScope.startIndex2,
                      fifo.begin() + readScope.startIndex2 + readScope.blockSize2,
                      analysisBuffer.begin() + analysisBufferIndex);
            analysisBufferIndex += readScope.blockSize2;
        }

        if (analysisBufferIndex == fftSize)
        {
            processFFTFrame(sampleRate);
            analysisBufferIndex = 0;
            ++framesProcessed;
        }
    }

    if (framesProcessed == 0)
        decayDisplayData();
}

void FFTAnalyzer::processFFTFrame(float sampleRate)
{
    std::fill(fftData.begin(), fftData.end(), 0.0f);
    std::copy(analysisBuffer.begin(), analysisBuffer.end(), fftData.begin());

    window.multiplyWithWindowingTable(fftData.data(), fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform(fftData.data(), true);

    const auto maxDisplayBins = (int) smoothedDisplayData.size();
    const auto nyquist = sampleRate * 0.5f;
    const auto minFreq = minDisplayFrequency;
    const auto maxFreq = juce::jmin(maxDisplayFrequency, nyquist);
    const auto logMin = std::log10(minFreq);
    const auto logRange = std::log10(maxFreq) - logMin;

    for (int i = 0; i < maxDisplayBins; ++i)
    {
        const auto normalisedX = (float) i / (float) juce::jmax(1, maxDisplayBins - 1);
        const auto freq = std::pow(10.0f, logMin + normalisedX * logRange);
        const auto exactBin = freq * (float) fftSize / sampleRate;
        const auto binLow = juce::jlimit(0, fftSize / 2 - 1, (int) std::floor(exactBin));
        const auto binHigh = juce::jlimit(0, fftSize / 2 - 1, binLow + 1);
        const auto binFraction = exactBin - (float) binLow;

        const auto magnitude = fftData[(size_t) binLow]
                             + (fftData[(size_t) binHigh] - fftData[(size_t) binLow]) * binFraction;
        const auto db = juce::Decibels::gainToDecibels(magnitude / (float) fftSize, -100.0f);
        const auto target = juce::jlimit(0.0f, 1.0f, juce::jmap(db, minDisplayDecibels, maxDisplayDecibels, 0.0f, 1.0f));
        const auto coeff = target > smoothedDisplayData[(size_t) i] ? attackCoefficient : releaseCoefficient;

        smoothedDisplayData[(size_t) i] += (target - smoothedDisplayData[(size_t) i]) * coeff;
    }

    hasSpectrumData.store(true);
}

void FFTAnalyzer::getFFTData(float* fftDataOut, int numBins, float sampleRate)
{
    if (fftDataOut == nullptr || numBins <= 0)
        return;

    if (sampleRate <= 0.0f)
    {
        std::fill_n(fftDataOut, numBins, 0.0f);
        return;
    }

    if ((int) smoothedDisplayData.size() != numBins)
        smoothedDisplayData.assign((size_t) numBins, 0.0f);

    processAvailableSamples(sampleRate);

    std::copy(smoothedDisplayData.begin(), smoothedDisplayData.end(), fftDataOut);
}
