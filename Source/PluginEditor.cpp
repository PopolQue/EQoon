#include "PluginProcessor.h"
#include "PluginEditor.h"

ResponseCurveComponent::ResponseCurveComponent(EQoonAudioProcessor& p) : audioProcessor(p)
{
    const auto& params = audioProcessor.getParameters();
    for (auto param : params)
    {
        param->addListener(this);
    }
    
    // Reduced update rate to 30 FPS for better performance
    startTimerHz(30);
    
    // FFT data is now managed by the processor
}

ResponseCurveComponent::~ResponseCurveComponent()
{
    const auto& params = audioProcessor.getParameters();
    for (auto param : params)
    {
        param->removeListener(this);
    }
}

void ResponseCurveComponent::parameterValueChanged(int parameterIndex, float newValue)
{
    parametersChanged.set(true);
}

void ResponseCurveComponent::timerCallback()
{
    static int frameCounter = 0;
    bool needsUpdate = parametersChanged.exchange(false);
    
    // Only update filter coefficients if they've changed
    if (needsUpdate)
    {
        auto chainSettings = getChainSettings(audioProcessor.apvts);

        auto peakCoefficients1 = makePeakFilter(chainSettings, audioProcessor.getSampleRate(), 1);
        auto peakCoefficients2 = makePeakFilter(chainSettings, audioProcessor.getSampleRate(), 2);
        auto peakCoefficients3 = makePeakFilter(chainSettings, audioProcessor.getSampleRate(), 3);

        updateCoefficients(monoChain.get<ChainPositions::Peak1>().coefficients, peakCoefficients1);
        updateCoefficients(monoChain.get<ChainPositions::Peak2>().coefficients, peakCoefficients2);
        updateCoefficients(monoChain.get<ChainPositions::Peak3>().coefficients, peakCoefficients3);

        auto lowCutCoefficients = makeLowCutFilter(chainSettings, audioProcessor.getSampleRate());
        auto highCutCoefficients = makeHighCutFilter(chainSettings, audioProcessor.getSampleRate());
        
        updateCutFilter(monoChain.get<ChainPositions::LowCut>(), lowCutCoefficients, chainSettings.lowCutSlope);
        updateCutFilter(monoChain.get<ChainPositions::HighCut>(), highCutCoefficients, chainSettings.highCutSlope);
        
        auto lowShelfCoefficients = makeLowShelfFilter(chainSettings, audioProcessor.getSampleRate());
        auto highShelfCoefficients = makeHighShelfFilter(chainSettings, audioProcessor.getSampleRate());

        updateCoefficients(monoChain.get<ChainPositions::LowShelf>().coefficients, lowShelfCoefficients);
        updateCoefficients(monoChain.get<ChainPositions::HighShelf>().coefficients, highShelfCoefficients);
    }
    
    // Only repaint every other frame (15 FPS for FFT)
    if (frameCounter++ % 2 == 0) {
        repaint();
    }
}

void ResponseCurveComponent::drawFFTAnalysis(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    using namespace juce;
    
    // Limit update rate
    double currentTime = Time::getMillisecondCounterHiRes() / 1000.0;
    if (currentTime - lastUpdateTime < minFrameTime) {
        // Just draw the last frame if not enough time has passed
        if (fftImage.isValid()) {
            g.drawImageAt(fftImage, bounds.getX(), bounds.getY());
        }
        return;
    }
    lastUpdateTime = currentTime;
    
    // Increased FFT size for higher resolution visualization
    const int fftSize = 4096;  // 4x more points for smoother visualization
    std::vector<float> fftData(fftSize, 0.0f);
    double sampleRate = 0.0;
    
    // Lock while accessing FFT data
    {
        std::lock_guard<std::mutex> lock(fftMutex);
        sampleRate = audioProcessor.getSampleRate();
        audioProcessor.getFFTData(bounds.toFloat(), fftData.data(), static_cast<float>(fftSize), static_cast<float>(sampleRate));
    }
    
    // Create or update the FFT image if needed
    if (fftImage.getWidth() != bounds.getWidth() || fftImage.getHeight() != bounds.getHeight())
    {
        fftImage = juce::Image(juce::Image::ARGB, bounds.getWidth(), bounds.getHeight(), true);
    }
    
    // Draw to the image buffer
    juce::Graphics g2(fftImage);
    g2.setColour(Colours::transparentBlack);
    g2.fillAll();
    
    // Debug: Print first few FFT values (less frequent to reduce console spam)
    //static int debugCounter = 0;
    //if (debugCounter++ % 60 == 0) {
      //  DBG("FFT Values (first 5):");
        //for (int i = 0; i < 5 && i < fftSize; ++i) {
          //  DBG("  [" << i << "] = " << fftData[i]);
        //}
    //}
    
    // Apply scaling to make the curve more visible
    for (auto& val : fftData) {
        // Apply a non-linear scaling to make small values more visible
        val = std::pow(val, 0.7f);
        // Ensure minimum visibility but don't boost too much
        val = juce::jmap(val, 0.0f, 1.0f, 0.1f, 1.0f);
    }
    
    // Debug: Print frequency range info
    static int debugCounter = 0;
    if (debugCounter++ % 60 == 0) {
        float minFreq = 0.0f;
        float maxFreq = sampleRate * 0.5f; // Nyquist frequency
        DBG("FFT Frequency range: " << minFreq << " Hz to " << maxFreq << " Hz");
        DBG("Sample rate: " << sampleRate << ", FFT size: " << fftSize);
    }
    
    // Set up the path for the FFT curve
    Path fftPath;
    bool started = false;
    
    // Frequency range (matching the grid)
    const float minFreq = 20.0f;
    const float maxFreq = 20000.0f;
    
    // Calculate the range of the view in pixels
    const float viewHeight = (float)bounds.getHeight();
    const float viewBottom = (float)bounds.getBottom();
    
    // Pre-calculate log values for frequency scaling
    const float logMinFreq = std::log10(minFreq);
    const float logMaxFreq = std::log10(maxFreq);
    
    // Calculate the FFT bin to frequency mapping
    auto fftBinToFreq = [fftSize = fftSize, sampleRate](int bin) -> float {
        // FFT bins are linearly spaced from 0 to Nyquist frequency
        // Note: We only use the first half of the FFT (real FFT)
        return bin * (sampleRate * 0.5f) / (fftSize / 2);
    };
    
    // Calculate frequency to FFT bin mapping (inverse of the above)
    auto freqToFFTBin = [fftSize = fftSize, sampleRate](float freq) -> float {
        // Map frequency to bin number (can be fractional)
        // Ensure we don't go beyond Nyquist frequency
        float maxFreq = sampleRate * 0.5f;
        float normalizedFreq = juce::jlimit(0.0f, maxFreq, freq) / maxFreq;
        return normalizedFreq * (fftSize / 2);
    };
    
    // Debug: Print frequency range info
    static int fftDebugCounter = 0;
    if (fftDebugCounter++ % 100 == 0) {
        float minFreq = fftBinToFreq(0);
        float maxFreq = fftBinToFreq(static_cast<int>(fftData.size()) / 2 - 1);
        DBG("FFT Frequency range: " << minFreq << " Hz to " << maxFreq << " Hz");
        DBG("Sample rate: " << audioProcessor.getSampleRate() << ", FFT size: " << fftSize);
        
        // Print some key frequencies
        float testFreqs[] = {20.0f, 100.0f, 1000.0f, 5000.0f, 10000.0f, 15000.0f, 20000.0f};
        for (float f : testFreqs) {
            float bin = freqToFFTBin(f);
            DBG(f << " Hz -> bin " << bin << " (" << fftBinToFreq(static_cast<int>(bin)) << " Hz)");
        }
    }
    
    // Debug: Print FFT data statistics
    {
        float maxVal = 0.0f;
        int maxBin = 0;
        for (int i = 0; i < fftSize/2; ++i) {
            if (fftData[i] > maxVal) {
                maxVal = fftData[i];
                maxBin = i;
            }
        }
        float maxFreq = fftBinToFreq(maxBin);
        DBG("FFT Analysis - Peak at bin " << maxBin << " (" << maxFreq << " Hz) with value " << maxVal);
    }
    
    // Draw the FFT curve
    // Use one point per pixel for best quality
    const int displayPoints = bounds.getWidth();
    
    // Debug: Print frequency range info
    DBG("FFT Analysis:");
    DBG("  Sample rate: " << sampleRate << " Hz");
    DBG("  FFT size: " << fftSize);
    DBG("  Nyquist frequency: " << (sampleRate * 0.5f) << " Hz");
    DBG("  Display range: " << minFreq << " Hz to " << maxFreq << " Hz");
    
    // Print bin to frequency mapping for first 10 bins
    DBG("First 10 FFT bins:");
    for (int i = 0; i < 10; ++i) {
        DBG("  Bin " << i << ": " << fftBinToFreq(i) << " Hz");
    }
    
    // Pre-calculate log values for frequency scaling
    const float logMin = std::log10(minFreq);
    const float logMax = std::log10(maxFreq);
    const float logRange = logMax - logMin;
    
    for (int i = 0; i <= displayPoints; ++i)
    {
        // Calculate x position in screen coordinates (0 to 1)
        float normalizedX = i / (float)displayPoints;
        
        // Map x position to frequency (logarithmic scale)
        float logFreq = logMin + normalizedX * logRange;
        float freq = std::pow(10.0f, logFreq);
        
        // Map to screen x coordinate
        float x = bounds.getX() + normalizedX * bounds.getWidth();
        
        // Find the corresponding FFT bin for this frequency
        float bin = freqToFFTBin(freq);
        int binLow = juce::jlimit(0, fftSize/2 - 1, static_cast<int>(std::floor(bin)));
        int binHigh = juce::jlimit(0, fftSize/2 - 1, static_cast<int>(std::ceil(bin)));
        float binFrac = bin - binLow;
        
        // Debug: Print specific frequency mapping
        if (freq >= 30.0f && freq <= 40.0f) {
            DBG(freq << " Hz -> bin " << bin << " (" << binLow << "-" << binHigh << ") - " 
                << fftBinToFreq(binLow) << " Hz to " << fftBinToFreq(binHigh) << " Hz");
        }
        
        // Debug: Print some key frequency points
        if (i % (displayPoints/10) == 0 || i == displayPoints) {
            DBG("  " << freq << " Hz -> bin " << bin << " (" << binLow << "-" << binHigh << ")");
        }
        
        // Interpolate between bins for smoother visualization
        float magnitude = 0.0f;
        if (binLow == binHigh) {
            magnitude = fftData[binLow];
        } else {
            magnitude = fftData[binLow] * (1.0f - binFrac) + fftData[binHigh] * binFrac;
        }
        
        // Apply a gentle high shelf to make higher frequencies more visible
        // but only a very slight boost to prevent distortion
        float freqGain = 1.0f + (normalizedX * 0.2f); // Very slight boost to highs
        magnitude = juce::jlimit(0.0f, 1.0f, magnitude * freqGain);
        
        // Map magnitude to y position with non-linear scaling for better visibility
        float normalizedMagnitude = std::pow(magnitude, 0.7f); // Gamma correction
        float y = viewBottom - (normalizedMagnitude * viewHeight * 0.9f) - (viewHeight * 0.05f);
        
        // Ensure y is within bounds
        y = juce::jlimit(bounds.getY() + 1.0f, viewBottom - 1.0f, y);
        
        // Debug: Print the first few positions
        if (i < 5) {
            DBG("  x = " << x << ", y = " << y);
        }
        
        // Add a point to the path
        if (!started)
        {
            fftPath.startNewSubPath(x, y);
            started = true;
        }
        else
        {
            // Add a small horizontal offset to prevent vertical lines when values change rapidly
            float prevX = fftPath.getCurrentPosition().x;
            if (x > prevX + 0.5f) {
                fftPath.lineTo(x, y);
            } else {
                fftPath.lineTo(prevX + 0.5f, y);
            }
        }
    }
    
    // Draw a solid fill under the curve for better visibility
    juce::Path filledPath(fftPath);
    filledPath.lineTo((float)bounds.getWidth(), (float)bounds.getHeight());
    filledPath.lineTo(0.0f, (float)bounds.getHeight());
    filledPath.closeSubPath();
    
    // Fill with a gradient that matches the EQ curve style
    g2.setGradientFill(juce::ColourGradient(
        juce::Colours::cyan.withAlpha(0.4f), 0, 0.0f,
        juce::Colours::blue.withAlpha(0.2f), 0, (float)bounds.getHeight(),
        false
    ));
    g2.fillPath(filledPath);
    
    // Draw the FFT curve with a gradient
    g2.setGradientFill(juce::ColourGradient(
        juce::Colours::cyan, 0, 0.0f,
        juce::Colours::blue, 0, (float)bounds.getHeight(),
        false
    ));
    g2.strokePath(fftPath, juce::PathStrokeType(2.5f));
    
    // Draw a subtle glow effect
    g2.setColour(juce::Colours::white.withAlpha(0.3f));
    g2.strokePath(fftPath, juce::PathStrokeType(4.0f));
    
    // Draw a thin white highlight on top
    g2.setColour(juce::Colours::white.withAlpha(0.5f));
    g2.strokePath(fftPath, juce::PathStrokeType(1.0f));
    
    // Draw the buffered image to the screen
    g.drawImageAt(fftImage, bounds.getX(), bounds.getY());
    
    // If in test mode, show the current frequency
    if (!audioProcessor.isFFTDataReady()) {
        // Calculate current test frequency
        float logMinFreq = std::log10(20.0f);
        float logMaxFreq = std::log10(20000.0f);
        float logSweepPos = 0.5f * (1.0f + std::sin(audioProcessor.getTestCounter() * 0.01f));
        float testFreq = std::pow(10.0f, logMinFreq + (logMaxFreq - logMinFreq) * logSweepPos);
        
        // Format frequency text
        juce::String freqText;
        if (testFreq < 1000.0f)
            freqText = juce::String(testFreq, 1) + " Hz";
        else
            freqText = juce::String(testFreq / 1000.0f, 1) + " kHz";
        
        // Draw frequency display
        g.setColour(juce::Colours::white);
        auto font = juce::Font(juce::FontOptions{}.withHeight(14.0f));
        g.setFont(font);
        g.drawText(freqText, bounds.getX() + 10, bounds.getY() + 10, 100, 20, juce::Justification::left);
    }
}

void ResponseCurveComponent::paint(juce::Graphics& g)
{
    using namespace juce;
    
    auto bounds = getLocalBounds();
    g.fillAll(Colours::black);
    
    // Draw a debug background for the FFT area
    g.setColour(Colours::red.withAlpha(0.1f));
    g.fillRect(bounds);
    
    // Draw the FFT analysis in the background with a debug border
    {
        g.saveState();
        g.reduceClipRegion(bounds);
        drawFFTAnalysis(g, bounds);
        g.restoreState();
    }
    
    // Draw the grid lines
    g.setColour(Colours::white.withAlpha(0.1f));
    
    // Horizontal grid lines (dB scale)
    for (float db = -24.0f; db <= 24.0f; db += 6.0f)
    {
        float y = jmap(db, -24.0f, 24.0f, (float)bounds.getBottom(), (float)bounds.getY());
        g.drawHorizontalLine((int)y, (float)bounds.getX(), (float)bounds.getRight());
        
        // Add dB labels
        g.setColour(Colours::white.withAlpha(0.5f));
        g.drawText(juce::String(db, 0) + " dB", bounds.getX() + 5, (int)y - 10, 50, 20, juce::Justification::left);
        g.setColour(Colours::white.withAlpha(0.1f));
    }
    
    // Vertical grid lines (frequency scale)
    float freqs[] = { 20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 80.0f, 90.0f, 100.0f, 
                      200.0f, 300.0f, 400.0f, 500.0f, 600.0f, 700.0f, 800.0f, 900.0f, 1000.0f, 
                      2000.0f, 3000.0f, 4000.0f, 5000.0f, 6000.0f, 7000.0f, 8000.0f, 9000.0f, 10000.0f, 20000.0f };
    
    // Pre-calculate log values for frequency scaling
    const float logMinFreq = std::log10(20.0f);
    const float logMaxFreq = std::log10(20000.0f);
    const float logFreqRange = logMaxFreq - logMinFreq;
    
    for (float freq : freqs)
    {
        // Map frequency to x position (logarithmic scale)
        float logFreq = std::log10(freq);
        float x = bounds.getX() + bounds.getWidth() * (logFreq - logMinFreq) / logFreqRange;
        
        // Only draw if within bounds
        if (x >= bounds.getX() && x <= bounds.getRight())
        {
            // Draw the grid line
            g.setColour(Colours::white.withAlpha(0.1f));
            g.drawVerticalLine((int)x, (float)bounds.getY(), (float)bounds.getBottom());
            
            // Add frequency labels for major divisions
            bool isMajor = (freq == 20.0f || freq == 50.0f || freq == 100.0f || 
                           freq == 200.0f || freq == 500.0f || freq == 1000.0f || 
                           freq == 2000.0f || freq == 5000.0f || freq == 10000.0f || freq == 20000.0f);
            
            if (isMajor)
            {
                g.setColour(Colours::white.withAlpha(0.7f));
                juce::String freqText;
                if (freq < 1000.0f)
                    freqText = juce::String(freq, 0) + " Hz";
                else if (freq < 10000.0f)
                    freqText = juce::String(freq / 1000.0f, 1) + " kHz";
                else
                    freqText = juce::String(freq / 1000.0f, 0) + " kHz";
                
                g.drawText(freqText, 
                          (int)x - 30, 
                          bounds.getBottom() - 20, 
                          60, 20, 
                          juce::Justification::centred);
            }
        }
    }
    
    // Draw the response curve on top
    auto responseArea = bounds;
    auto w = responseArea.getWidth();

    auto& lowcut = monoChain.get<ChainPositions::LowCut>();
    auto& lowShelf = monoChain.get<ChainPositions::LowShelf>();
    auto& peak1 = monoChain.get<ChainPositions::Peak1>();
    auto& peak2 = monoChain.get<ChainPositions::Peak2>();
    auto& peak3 = monoChain.get<ChainPositions::Peak3>();
    auto& highShelf = monoChain.get<ChainPositions::HighShelf>();
    auto& highcut = monoChain.get<ChainPositions::HighCut>();

    auto sampleRate = audioProcessor.getSampleRate();
    std::vector<double> mags;
    mags.resize(w);

    for (int i = 0; i < w; ++i)
    {
        double mag = 1.f;
        auto freq = mapToLog10(double(i) / double(w), 20.0, 20000.0);

        if (!monoChain.isBypassed<ChainPositions::Peak1>())
            mag *= peak1.coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!monoChain.isBypassed<ChainPositions::Peak2>())
            mag *= peak2.coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!monoChain.isBypassed<ChainPositions::Peak3>())
            mag *= peak3.coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!monoChain.isBypassed<ChainPositions::LowShelf>())
            mag *= lowShelf.coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!monoChain.isBypassed<ChainPositions::HighShelf>())
            mag *= highShelf.coefficients->getMagnitudeForFrequency(freq, sampleRate);

        if (!lowcut.isBypassed<0>())
            mag *= lowcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!lowcut.isBypassed<1>())
            mag *= lowcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!lowcut.isBypassed<2>())
            mag *= lowcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!lowcut.isBypassed<3>())
            mag *= lowcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);

        if (!highcut.isBypassed<0>())
            mag *= highcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!highcut.isBypassed<1>())
            mag *= highcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!highcut.isBypassed<2>())
            mag *= highcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!highcut.isBypassed<3>())
            mag *= highcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);

        mags[i] = Decibels::gainToDecibels(mag);
    }

    Path responseCurve;
    const double outputMin = responseArea.getBottom();
    const double outputMax = responseArea.getY();
    auto map = [outputMin, outputMax](double input)
    {
        return jmap(input, -24.0, 24.0, outputMin, outputMax);
    };

    responseCurve.startNewSubPath(responseArea.getX(), map(mags.front()));
    for (size_t i = 1; i < mags.size(); ++i)
    {
        responseCurve.lineTo(responseArea.getX() + i, map(mags[i]));
    }

    // Draw the response curve with a glow effect
    g.setColour(Colours::aqua);
    g.drawRoundedRectangle(responseArea.toFloat(), 4.f, 1.f);
    
    // Draw the main curve with a glow effect
    g.setColour(Colours::white);
    g.strokePath(responseCurve, PathStrokeType(2.5f));
    
    // Add a subtle glow around the curve
    g.setColour(Colours::cyan.withAlpha(0.3f));
    g.strokePath(responseCurve, PathStrokeType(4.0f));
    
    // Add a highlight on top of the curve
    g.setColour(Colours::white.withAlpha(0.8f));
    g.strokePath(responseCurve, PathStrokeType(1.0f));
}

EQoonAudioProcessorEditor::EQoonAudioProcessorEditor(EQoonAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p),
      responseCurveComponent(audioProcessor),
        lowCutFreqSliderAttachment(audioProcessor.apvts, "LowCut Freq", lowCutFreqSlider),
        lowCutSlopeSliderAttachment(audioProcessor.apvts, "LowCut Slope", lowCutSlopeSlider),
        lowCutQualitySliderAttachment(audioProcessor.apvts, "LowCut Quality", lowCutQualitySlider),
        lowShelfFreqSliderAttachment(audioProcessor.apvts, "LowShelf Freq", lowShelfFreqSlider),
        lowShelfGainSliderAttachment(audioProcessor.apvts, "LowShelf Gain", lowShelfGainSlider),
        lowShelfQualitySliderAttachment(audioProcessor.apvts, "LowShelf Quality", lowShelfQualitySlider),
        peakFreqSlider1Attachment(audioProcessor.apvts, "Peak1 Freq", peakFreq1Slider),
        peakGainSlider1Attachment(audioProcessor.apvts, "Peak1 Gain", peakGain1Slider),
        peakQualitySlider1Attachment(audioProcessor.apvts, "Peak1 Quality", peakQuality1Slider),
        peakFreqSlider2Attachment(audioProcessor.apvts, "Peak2 Freq", peakFreq2Slider),
        peakGainSlider2Attachment(audioProcessor.apvts, "Peak2 Gain", peakGain2Slider),
        peakQualitySlider2Attachment(audioProcessor.apvts, "Peak2 Quality", peakQuality2Slider),
        peakFreqSlider3Attachment(audioProcessor.apvts, "Peak3 Freq", peakFreq3Slider),
        peakGainSlider3Attachment(audioProcessor.apvts, "Peak3 Gain", peakGain3Slider),
        peakQualitySlider3Attachment(audioProcessor.apvts, "Peak3 Quality", peakQuality3Slider),
        highShelfFreqSliderAttachment(audioProcessor.apvts, "HighShelf Freq", highShelfFreqSlider),
        highShelfGainSliderAttachment(audioProcessor.apvts, "HighShelf Gain", highShelfGainSlider),
        highShelfQualitySliderAttachment(audioProcessor.apvts, "HighShelf Quality", highShelfQualitySlider),
        highCutFreqSliderAttachment(audioProcessor.apvts, "HighCut Freq", highCutFreqSlider),
        highCutSlopeSliderAttachment(audioProcessor.apvts, "HighCut Slope", highCutSlopeSlider),
          highCutQualitySliderAttachment(audioProcessor.apvts, "HighCut Quality", highCutQualitySlider)
{
    for (auto* comp : getComps())
    {
        addAndMakeVisible(comp);
    }
    setSize(1000, 600);
}

EQoonAudioProcessorEditor::~EQoonAudioProcessorEditor()
{
}

void EQoonAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    
    // Draw each band row
    for (int i = 0; i < bandRows.size(); ++i)
    {
        drawBandRow(g, i, bandRows[i]);
    }
}

void EQoonAudioProcessorEditor::setupLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colours::white);
    label.setJustificationType(juce::Justification::centredRight);
    auto font = juce::Font(juce::FontOptions{}.withHeight(12.0f).withStyle(juce::Font::bold));
    label.setFont(font);
    addAndMakeVisible(label);
}

void EQoonAudioProcessorEditor::positionBandRow(juce::Rectangle<int> bounds,
                                              juce::Slider& freqSlider, juce::Slider& gainSlider, juce::Slider& qSlider,
                                              juce::Label& freqLabel, juce::Label& gainLabel, juce::Label& qLabel,
                                              juce::Label& freqValue, juce::Label& gainValue, juce::Label& qValue,
                                              juce::Label& nameLabel)
{
    const int nameWidth = 80;    // Width for filter name
    const int labelWidth = 40;   // Width for parameter labels
    const int valueWidth = 60;   // Width for value displays
    const int padding = 1;       // Keep reduced padding
    
    // Remove space for name label
    auto nameArea = bounds.removeFromLeft(nameWidth);
    nameLabel.setBounds(nameArea.reduced(0, 1));
    
    // Calculate column width (label | slider | value)
    auto columnWidth = bounds.getWidth() / 3;
    
    // Freq Column
    auto freqArea = bounds.removeFromLeft(columnWidth);
    
    // Position label on the left
    auto labelArea = freqArea.removeFromLeft(labelWidth);
    freqLabel.setBounds(labelArea.reduced(0, 1));
    
    // Position value on the right first, then slider uses remaining space
    auto valueArea = freqArea.removeFromRight(valueWidth);
    freqValue.setBounds(valueArea.reduced(0, 1));
    
    // Position slider in the remaining middle area
    auto sliderArea = freqArea.reduced(padding, 1);
    freqSlider.setBounds(sliderArea);
    
    // Gain/Slope Column
    auto gainArea = bounds.removeFromLeft(columnWidth);
    
    // Position label on the left
    labelArea = gainArea.removeFromLeft(labelWidth);
    gainLabel.setBounds(labelArea.reduced(0, 1));
    
    // Position value on the right first, then slider uses remaining space
    valueArea = gainArea.removeFromRight(valueWidth);
    gainValue.setBounds(valueArea.reduced(0, 1));
    
    // Position slider in the remaining middle area
    sliderArea = gainArea.reduced(padding, 1);
    gainSlider.setBounds(sliderArea);
    
    // Q Column
    auto qArea = bounds;
    
    // Position label on the left
    labelArea = qArea.removeFromLeft(labelWidth);
    qLabel.setBounds(labelArea.reduced(0, 1));
    
    // Position value on the right first, then slider uses remaining space
    valueArea = qArea.removeFromRight(valueWidth);
    qValue.setBounds(valueArea.reduced(0, 1));
    
    // Position slider in the remaining middle area
    sliderArea = qArea.reduced(padding, 1);
    qSlider.setBounds(sliderArea);
    
    // Set up value displays
    auto setupValueLabel = [](juce::Label& label, juce::Slider& slider) {
        label.setJustificationType(juce::Justification::centredLeft);
        auto font = juce::Font(juce::FontOptions{}.withHeight(12.0f));
        label.setFont(font);
        label.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.8f));
        label.setText(juce::String(slider.getValue(), 2) + " " + slider.getTextValueSuffix(), juce::dontSendNotification);
    };
    
    setupValueLabel(freqValue, freqSlider);
    setupValueLabel(gainValue, gainSlider);
    setupValueLabel(qValue, qSlider);
    
    // Add value listeners to update the display when sliders change
    freqSlider.onValueChange = [this, &freqValue, &freqSlider] {
        freqValue.setText(juce::String(freqSlider.getValue(), 2) + " " + freqSlider.getTextValueSuffix(), juce::dontSendNotification);
    };
    
    // Special handling for slope sliders (low-cut and high-cut)
    if (&gainSlider == &lowCutSlopeSlider || &gainSlider == &highCutSlopeSlider) {
        // For slope sliders, show the actual slope value in dB/oct
        auto updateSlopeValue = [&gainValue, &gainSlider] {
            int slopeValue = 12 + (static_cast<int>(gainSlider.getValue()) * 12);
            gainValue.setText(juce::String(slopeValue) + " dB/oct", juce::dontSendNotification);
        };
        
        gainSlider.onValueChange = updateSlopeValue;
        updateSlopeValue(); // Set initial value
    } else {
        // For other gain sliders, show the normal value
        gainSlider.onValueChange = [this, &gainValue, &gainSlider] {
            gainValue.setText(juce::String(gainSlider.getValue(), 2) + " " + gainSlider.getTextValueSuffix(), juce::dontSendNotification);
        };
    }
    
    qSlider.onValueChange = [this, &qValue, &qSlider] {
        qValue.setText(juce::String(qSlider.getValue(), 2) + " " + qSlider.getTextValueSuffix(), juce::dontSendNotification);
    };
}

void EQoonAudioProcessorEditor::drawBandRow(juce::Graphics& g, int bandIndex, const juce::Rectangle<int>& bounds)
{
    // Draw a subtle background for the row
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.fillRect(bounds);
    g.setColour(juce::Colours::white.withAlpha(0.2f));
    g.drawRect(bounds, 1);
}

void EQoonAudioProcessorEditor::resized()
{
    using namespace juce;
    
    auto bounds = getLocalBounds();
    auto responseArea = bounds.removeFromTop(bounds.getHeight() * 0.5);
    responseCurveComponent.setBounds(responseArea);
    
    // Define the number of bands
    const int numBands = 7;
    
    // Calculate row height
    auto rowHeight = bounds.getHeight() / numBands;
    
    // Setup each band row in the paint method
    // We'll store the row bounds for later use in paint()
    bandRows.clear();
    for (int i = 0; i < numBands; ++i)
    {
        bandRows.add(bounds.removeFromTop(rowHeight));
    }
    
    // Setup all labels and value displays
    // Setup filter name labels
    setupLabel(lowCutNameLabel, "HPF");
    setupLabel(lowShelfNameLabel, "Low Shelf");
    setupLabel(peak1NameLabel, "Peak 1");
    setupLabel(peak2NameLabel, "Peak 2");
    setupLabel(peak3NameLabel, "Peak 3");
    setupLabel(highShelfNameLabel, "High Shelf");
    setupLabel(highCutNameLabel, "LPF");
    
    // HPF Row
    setupLabel(lowCutFreqLabel, "Freq");
    setupLabel(lowCutSlopeLabel, "Slope");
    setupLabel(lowCutQualityLabel, "Q");
    
    // Low Shelf Row
    setupLabel(lowShelfFreqLabel, "Freq");
    setupLabel(lowShelfGainLabel, "Gain");
    setupLabel(lowShelfQualityLabel, "Q");
    
    // Peak 1 Row
    setupLabel(peakFreq1Label, "Freq");
    setupLabel(peakGain1Label, "Gain");
    setupLabel(peakQuality1Label, "Q");
    
    // Peak 2 Row
    setupLabel(peakFreq2Label, "Freq");
    setupLabel(peakGain2Label, "Gain");
    setupLabel(peakQuality2Label, "Q");
    
    // Peak 3 Row
    setupLabel(peakFreq3Label, "Freq");
    setupLabel(peakGain3Label, "Gain");
    setupLabel(peakQuality3Label, "Q");
    
    // High Shelf Row
    setupLabel(highShelfFreqLabel, "Freq");
    setupLabel(highShelfGainLabel, "Gain");
    setupLabel(highShelfQualityLabel, "Q");
    
    // LPF Row
    setupLabel(highCutFreqLabel, "Freq");
    setupLabel(highCutSlopeLabel, "Slope");
    setupLabel(highCutQualityLabel, "Q");
    
    // Setup value displays (no text, they'll be set by positionBandRow)
    for (auto* comp : getValueLabels())
    {
        addAndMakeVisible(comp);
    }
    
    // Position all components
    positionBandRow(bandRows[0], lowCutFreqSlider, lowCutSlopeSlider, lowCutQualitySlider,
                   lowCutFreqLabel, lowCutSlopeLabel, lowCutQualityLabel,
                   lowCutFreqValue, lowCutSlopeValue, lowCutQualityValue,
                   lowCutNameLabel);
    
    positionBandRow(bandRows[1], lowShelfFreqSlider, lowShelfGainSlider, lowShelfQualitySlider,
                   lowShelfFreqLabel, lowShelfGainLabel, lowShelfQualityLabel,
                   lowShelfFreqValue, lowShelfGainValue, lowShelfQualityValue,
                   lowShelfNameLabel);
    
    positionBandRow(bandRows[2], peakFreq1Slider, peakGain1Slider, peakQuality1Slider,
                   peakFreq1Label, peakGain1Label, peakQuality1Label,
                   peakFreq1Value, peakGain1Value, peakQuality1Value,
                   peak1NameLabel);
    
    positionBandRow(bandRows[3], peakFreq2Slider, peakGain2Slider, peakQuality2Slider,
                   peakFreq2Label, peakGain2Label, peakQuality2Label,
                   peakFreq2Value, peakGain2Value, peakQuality2Value,
                   peak2NameLabel);
    
    positionBandRow(bandRows[4], peakFreq3Slider, peakGain3Slider, peakQuality3Slider,
                   peakFreq3Label, peakGain3Label, peakQuality3Label,
                   peakFreq3Value, peakGain3Value, peakQuality3Value,
                   peak3NameLabel);
    
    positionBandRow(bandRows[5], highShelfFreqSlider, highShelfGainSlider, highShelfQualitySlider,
                   highShelfFreqLabel, highShelfGainLabel, highShelfQualityLabel,
                   highShelfFreqValue, highShelfGainValue, highShelfQualityValue,
                   highShelfNameLabel);
    
    positionBandRow(bandRows[6], highCutFreqSlider, highCutSlopeSlider, highCutQualitySlider,
                   highCutFreqLabel, highCutSlopeLabel, highCutQualityLabel,
                   highCutFreqValue, highCutSlopeValue, highCutQualityValue,
                   highCutNameLabel);
}


std::vector<juce::Component*> EQoonAudioProcessorEditor::getComps()
{
    return {
        // Sliders
        &peakFreq1Slider, &peakGain1Slider, &peakQuality1Slider,
        &peakFreq2Slider, &peakGain2Slider, &peakQuality2Slider,
        &peakFreq3Slider, &peakGain3Slider, &peakQuality3Slider,
        &lowCutFreqSlider, &highCutFreqSlider, &lowCutSlopeSlider, &highCutSlopeSlider,
        &lowCutQualitySlider, &highCutQualitySlider,
        &lowShelfFreqSlider, &lowShelfGainSlider, &lowShelfQualitySlider,
        &highShelfFreqSlider, &highShelfGainSlider, &highShelfQualitySlider,
        
        // Labels
        &peakFreq1Label, &peakGain1Label, &peakQuality1Label,
        &peakFreq2Label, &peakGain2Label, &peakQuality2Label,
        &peakFreq3Label, &peakGain3Label, &peakQuality3Label,
        &lowCutFreqLabel, &highCutFreqLabel, &lowCutSlopeLabel, &highCutSlopeLabel,
        &lowCutQualityLabel, &highCutQualityLabel,
        &lowShelfFreqLabel, &lowShelfGainLabel, &lowShelfQualityLabel,
        &highShelfFreqLabel, &highShelfGainLabel, &highShelfQualityLabel,
        
        &responseCurveComponent
    };
}

std::vector<juce::Component*> EQoonAudioProcessorEditor::getValueLabels()
{
    return {
        &lowCutFreqValue, &lowCutSlopeValue, &lowCutQualityValue,
        &lowShelfFreqValue, &lowShelfGainValue, &lowShelfQualityValue,
        &peakFreq1Value, &peakGain1Value, &peakQuality1Value,
        &peakFreq2Value, &peakGain2Value, &peakQuality2Value,
        &peakFreq3Value, &peakGain3Value, &peakQuality3Value,
        &highShelfFreqValue, &highShelfGainValue, &highShelfQualityValue,
        &highCutFreqValue, &highCutSlopeValue, &highCutQualityValue
    };
}
