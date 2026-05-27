#include "PluginProcessor.h"
#include "PluginEditor.h"

class EQoonUnitTestRunner
{
public:
    static void runAll();
};

ResponseCurveComponent::ResponseCurveComponent(EQoonAudioProcessor& p) : audioProcessor(p)
{
    const auto& params = audioProcessor.getParameters();
    for (auto param : params)
    {
        param->addListener(this);
    }
    
    // Reduced update rate to 30 FPS for better performance
    startTimerHz(30);
    parametersChanged.set(true);
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
    bool needsUpdate = parametersChanged.exchange(false);
    
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

    const int displayPoints = juce::jmax(2, getWidth());
    if ((int) preEQFFTData.size() != displayPoints)
        preEQFFTData.assign((size_t) displayPoints, 0.0f);

    if ((int) postEQFFTData.size() != displayPoints)
        postEQFFTData.assign((size_t) displayPoints, 0.0f);

    const auto sampleRate = static_cast<float>(audioProcessor.getSampleRate());
    audioProcessor.getPreEQFFTData(preEQFFTData.data(), displayPoints, sampleRate);
    audioProcessor.getPostEQFFTData(postEQFFTData.data(), displayPoints, sampleRate);
    
    repaint();
}

void ResponseCurveComponent::drawSpectrumPath(juce::Graphics& g,
                                              juce::Rectangle<int> bounds,
                                              const std::vector<float>& fftData,
                                              juce::Colour lineColour,
                                              juce::Colour fillColour,
                                              float strokeWidth)
{
    using namespace juce;

    if (fftData.empty())
        return;

    const int displayPoints = juce::jmin((int) fftData.size(), juce::jmax(2, bounds.getWidth()));

    Path fftPath;
    const float top = 1.0f;
    const float bottom = (float) bounds.getHeight() - 1.0f;
    const float height = bottom - top;

    for (int i = 0; i < displayPoints; ++i)
    {
        const float normalisedX = (float) i / (float) juce::jmax(1, displayPoints - 1);
        const float x = normalisedX * (float) bounds.getWidth();
        const float magnitude = std::pow(juce::jlimit(0.0f, 1.0f, fftData[(size_t) i]), 0.72f);
        const float y = juce::jmap(magnitude, 0.0f, 1.0f, bottom, top + height * 0.08f);

        if (i == 0)
            fftPath.startNewSubPath(x, y);
        else
            fftPath.lineTo(x, y);
    }

    Path filledPath(fftPath);
    filledPath.lineTo((float) bounds.getWidth(), (float) bounds.getHeight());
    filledPath.lineTo(0.0f, (float) bounds.getHeight());
    filledPath.closeSubPath();

    g.setGradientFill(ColourGradient(fillColour, 0.0f, 0.0f,
                                     fillColour.withAlpha(0.0f), 0.0f, (float) bounds.getHeight(),
                                     false));
    g.fillPath(filledPath);

    g.setColour(lineColour.withAlpha(0.22f));
    g.strokePath(fftPath, PathStrokeType(strokeWidth + 2.0f));
    g.setColour(lineColour);
    g.strokePath(fftPath, PathStrokeType(strokeWidth));
}

void ResponseCurveComponent::drawFFTAnalysis(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    using namespace juce;

    if (preEQFFTData.empty() && postEQFFTData.empty())
        return;

    if (fftImage.getWidth() != bounds.getWidth() || fftImage.getHeight() != bounds.getHeight())
        fftImage = Image(Image::ARGB, bounds.getWidth(), bounds.getHeight(), true);

    Graphics g2(fftImage);
    g2.fillAll(Colours::transparentBlack);

    drawSpectrumPath(g2,
                     bounds.withPosition(0, 0),
                     preEQFFTData,
                     Colours::orange.withAlpha(0.58f),
                     Colours::orange.withAlpha(0.12f),
                     1.3f);

    drawSpectrumPath(g2,
                     bounds.withPosition(0, 0),
                     postEQFFTData,
                     Colours::cyan.withAlpha(0.85f),
                     Colours::cyan.withAlpha(0.22f),
                     1.8f);

    auto legendBounds = juce::Rectangle<int>(bounds.getWidth() - 130, 8, 120, 36);
    g2.setFont(juce::Font(juce::FontOptions { 12.0f, juce::Font::bold }));
    g2.setColour(Colours::orange.withAlpha(0.75f));
    g2.drawText("PRE", legendBounds.removeFromTop(16), Justification::centredRight);
    g2.setColour(Colours::cyan.withAlpha(0.9f));
    g2.drawText("POST", legendBounds.removeFromTop(16), Justification::centredRight);

    g.drawImageAt(fftImage, bounds.getX(), bounds.getY());
}

void ResponseCurveComponent::paint(juce::Graphics& g)
{
    using namespace juce;
    
    auto bounds = getLocalBounds();
    g.fillAll(Colours::black);
    
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
        auto freq = mapToLog10(double(i) / double(w), 20.0, 20000.0);

        double cutMag = 1.0;
        if (!lowcut.isBypassed<0>())
            cutMag *= lowcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!lowcut.isBypassed<1>())
            cutMag *= lowcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!lowcut.isBypassed<2>())
            cutMag *= lowcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!lowcut.isBypassed<3>())
            cutMag *= lowcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);

        if (!highcut.isBypassed<0>())
            cutMag *= highcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!highcut.isBypassed<1>())
            cutMag *= highcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!highcut.isBypassed<2>())
            cutMag *= highcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!highcut.isBypassed<3>())
            cutMag *= highcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);

        // Display response: cascaded product of all bands (matches knob settings)
        // regardless of summing mode (which only affects the audio path)
        double bandMag = 1.0;
        auto collectAndMultiply = [&](const Coefficients& coeffs) {
            if (coeffs != nullptr)
                bandMag *= coeffs->getMagnitudeForFrequency(freq, sampleRate);
        };

        if (!monoChain.isBypassed<ChainPositions::LowShelf>())
            collectAndMultiply(lowShelf.coefficients);
        if (!monoChain.isBypassed<ChainPositions::Peak1>())
            collectAndMultiply(peak1.coefficients);
        if (!monoChain.isBypassed<ChainPositions::Peak2>())
            collectAndMultiply(peak2.coefficients);
        if (!monoChain.isBypassed<ChainPositions::Peak3>())
            collectAndMultiply(peak3.coefficients);
        if (!monoChain.isBypassed<ChainPositions::HighShelf>())
            collectAndMultiply(highShelf.coefficients);

        double mag = cutMag * bandMag;
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
          highCutQualitySliderAttachment(audioProcessor.apvts, "HighCut Quality", highCutQualitySlider),
        makeupGainSliderAttachment(audioProcessor.apvts, "Makeup Gain", makeupGainSlider),
        summingModeAttachment(audioProcessor.apvts, "Summing Mode", summingModeCombo)
{
    summingModeCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colours::black);
    summingModeCombo.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    summingModeCombo.setColour(juce::ComboBox::outlineColourId, juce::Colours::white.withAlpha(0.3f));
    summingModeCombo.setColour(juce::ComboBox::buttonColourId, juce::Colours::white.withAlpha(0.5f));
    summingModeCombo.setColour(juce::ComboBox::arrowColourId, juce::Colours::white.withAlpha(0.6f));
    summingModeCombo.setJustificationType(juce::Justification::centred);
    if (summingModeCombo.getNumItems() < 3)
    {
        summingModeCombo.clear();
        summingModeCombo.addItemList({ "Average", "Sum", "Maximum" }, 1);
    }
    summingModeCombo.setSelectedItemIndex(static_cast<int>(audioProcessor.apvts.getRawParameterValue("Summing Mode")->load()), juce::dontSendNotification);

    for (auto* comp : getComps())
    {
        addAndMakeVisible(comp);
    }
    setWantsKeyboardFocus(true);
    setSize(1000, 650);
}

EQoonAudioProcessorEditor::~EQoonAudioProcessorEditor()
{
}

bool EQoonAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress('t', juce::ModifierKeys::commandModifier, 0))
    {
        DBG("Running EQoon unit tests...");
        EQoonUnitTestRunner::runAll();
        DBG("Tests complete.");
        return true;
    }
    return false;
}

void EQoonAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    
    for (int i = 0; i < bandRows.size(); ++i)
        drawBandRow(g, i, bandRows[i]);

    if (!makeupRowBounds.isEmpty())
    {
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.fillRect(makeupRowBounds);
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.drawRect(makeupRowBounds, 1);
    }
}

void EQoonAudioProcessorEditor::setupLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colours::white);
    label.setJustificationType(juce::Justification::centredRight);
    auto font = juce::Font(juce::FontOptions{}.withHeight(12.0f).withStyle("Bold"));
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
    
    const int numBands = 7;
    
    auto rowHeight = bounds.getHeight() / (numBands + 1);
    
    bandRows.clear();
    for (int i = 0; i < numBands; ++i)
    {
        bandRows.add(bounds.removeFromTop(rowHeight));
    }
    auto makeupRow = bounds; // remaining space is the 8th row
    
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
    
    // Makeup Gain Row
    setupLabel(makeupGainLabel, "Gain");

    for (auto* comp : getValueLabels())
    {
        addAndMakeVisible(comp);
    }

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

    makeupRowBounds = makeupRow;

    auto positionMakeupRow = [&]()
    {
        int nameWidth = 80;
        int labelWidth = 40;
        int valueWidth = 60;
        int padding = 1;

        auto area = makeupRow;
        auto comboArea = area.removeFromLeft(nameWidth + labelWidth);
        summingModeCombo.setBounds(comboArea.reduced(2, 4));

        auto valueArea = area.removeFromRight(valueWidth);
        makeupGainValue.setBounds(valueArea.reduced(0, 1));

        makeupGainSlider.setBounds(area.reduced(padding, 1));

        makeupGainSlider.onValueChange = [this] {
            makeupGainValue.setText(
                juce::String(makeupGainSlider.getValue(), 1) + " dB",
                juce::dontSendNotification);
        };
        makeupGainValue.setText(
            juce::String(makeupGainSlider.getValue(), 1) + " dB",
            juce::dontSendNotification);
    };
    positionMakeupRow();
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
        &makeupGainSlider,
        
        // Labels
        &peakFreq1Label, &peakGain1Label, &peakQuality1Label,
        &peakFreq2Label, &peakGain2Label, &peakQuality2Label,
        &peakFreq3Label, &peakGain3Label, &peakQuality3Label,
        &lowCutFreqLabel, &highCutFreqLabel, &lowCutSlopeLabel, &highCutSlopeLabel,
        &lowCutQualityLabel, &highCutQualityLabel,
        &lowShelfFreqLabel, &lowShelfGainLabel, &lowShelfQualityLabel,
        &highShelfFreqLabel, &highShelfGainLabel, &highShelfQualityLabel,
        &makeupGainLabel,
        
        &summingModeCombo,
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
        &highCutFreqValue, &highCutSlopeValue, &highCutQualityValue,
        &makeupGainValue
    };
}
