#include "FFTAnalyzer.h"

FFTAnalyzer::FFTAnalyzer()
    : forwardFFT(fftOrder)
    , window(fftSize, juce::dsp::WindowingFunction<float>::hann)
    , fftComplex(fftSize)
    , lastFFTData(scopeSize, 0.1f)
{
    std::fill(fifo.begin(), fifo.end(), 0.0f);
    std::fill(fftData.begin(), fftData.end(), 0.0f);
}

void FFTAnalyzer::prepare(double sampleRate)
{
    std::lock_guard<std::mutex> lock(fftMutex);
    std::fill(fifo.begin(), fifo.end(), 0.0f);
    std::fill(fftData.begin(), fftData.end(), 0.0f);
    std::fill(fftComplex.begin(), fftComplex.end(), std::complex<float>(0.0f, 0.0f));
    std::fill(lastFFTData.begin(), lastFFTData.end(), 0.1f);
    
    fifoIndex = 0;
    nextFFTBlockReady = false;
}

void FFTAnalyzer::processSample(float sample)
{
    // Add the sample to the FIFO buffer
    if (fifoIndex < fftSize)
    {
        fifo[fifoIndex] = sample;
        fifoIndex++;
    }
    
    // If we've filled the buffer, process it
    if (fifoIndex == fftSize)
    {
        processFFTFrame();
        fifoIndex = 0;
    }
}

void FFTAnalyzer::processFFTFrame()
{
    std::lock_guard<std::mutex> lock(fftMutex);
    
    // Copy the time domain data to the FFT buffer
    std::copy(fifo.begin(), fifo.end(), fftData.begin());
    
    // Apply windowing function to the time domain data
    window.multiplyWithWindowingTable(fftData.data(), fftSize);
    
    // Copy real data to complex buffer (imaginary part is zero)
    for (int i = 0; i < fftSize; ++i) {
        fftComplex[i] = std::complex<float>(fftData[i], 0.0f);
    }
    
    // Perform the FFT
    forwardFFT.perform(fftComplex.data(), nullptr, false);
    
    // Convert to magnitude spectrum (first half is all we need)
    for (int i = 0; i < fftSize / 2; ++i) {
        // Calculate magnitude (sqrt(re^2 + im^2))
        float re = fftComplex[i].real();
        float im = fftComplex[i].imag();
        fftData[i] = std::sqrt(re * re + im * im);
    }
    
    nextFFTBlockReady = true;
}

void FFTAnalyzer::getFFTData(juce::Rectangle<float> bounds, float* fftDataOut, int numBins, float sampleRate)
{
    if (!nextFFTBlockReady) {
        std::fill_n(fftDataOut, numBins, 0.0f);
        return;
    }
    
    std::lock_guard<std::mutex> lock(fftMutex);
    
    // Frequency range for display (20Hz to 20kHz)
    const float minFreq = 20.0f;
    const float maxFreq = 20000.0f;
    
    // Pre-calculate log frequency mapping
    const float logMin = std::log10(minFreq);
    const float logMax = std::log10(maxFreq);
    const float logRange = logMax - logMin;
    
    // Calculate the frequency resolution (width of each FFT bin in Hz)
    const float binWidth = sampleRate / fftSize;
    
    // Process each output bin
    for (int i = 0; i < numBins; ++i)
    {
        // Map bin index to frequency on a logarithmic scale
        float normalizedPos = static_cast<float>(i) / (numBins - 1);
        float freq = minFreq * std::pow(10.0f, normalizedPos * logRange);
        
        // Convert frequency to FFT bin index
        float binF = freq / binWidth;
        int bin = static_cast<int>(binF);
        float binFrac = binF - bin;
        
        // Make sure we're within bounds
        if (bin >= 0 && bin < (fftSize / 2 - 1)) {
            // Linear interpolation between bins
            float mag1 = fftData[bin];
            float mag2 = fftData[bin + 1];
            float magnitude = mag1 + (mag2 - mag1) * binFrac;
            
            // Convert to dB
            float db = juce::Decibels::gainToDecibels(magnitude, -160.0f);
            
            // Map to [0, 1] range
            const float minDB = -80.0f;
            const float maxDB = 0.0f;
            float normalizedLevel = juce::jmap(db, minDB, maxDB, 0.0f, 1.0f);
            
            // Apply a slight curve to make the visualization more musical
            normalizedLevel = std::pow(normalizedLevel, 0.7f);
            
            // Store the result
            fftDataOut[i] = juce::jlimit(0.0f, 1.0f, normalizedLevel);
        } else {
            fftDataOut[i] = 0.0f;
        }
    }
    
    // Apply frequency-dependent smoothing
    for (int i = 0; i < numBins; ++i)
    {
        // Calculate frequency for this bin
        float normalizedPos = static_cast<float>(i) / (numBins - 1);
        float freq = minFreq * std::pow(10.0f, normalizedPos * logRange);
        
        // Apply frequency weighting
        float freqWeight = 1.0f;
        if (freq < 1000.0f) {
            // Gentle boost to low-mids
            freqWeight = 0.7f + 0.3f * std::sqrt(freq / 1000.0f);
        } else if (freq > 5000.0f) {
            // Slight roll-off of very high frequencies
            freqWeight = 1.0f - 0.3f * ((freq - 5000.0f) / 15000.0f);
        }
        
        // Apply smoothing
        float alpha = 0.3f + 0.5f * (static_cast<float>(i) / numBins);
        fftDataOut[i] = alpha * fftDataOut[i] * freqWeight + 
                        (1.0f - alpha) * lastFFTData[i];
        lastFFTData[i] = fftDataOut[i];
        
        // Ensure the value is within bounds
        fftDataOut[i] = juce::jlimit(0.0f, 1.0f, fftDataOut[i]);
    }
    
    nextFFTBlockReady = false;
}
