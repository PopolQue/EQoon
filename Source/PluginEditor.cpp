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
        
        auto updateChain = [&](MonoChain& chain, const ChannelSettings& channelSettings)
        {
            auto peakCoefficients1 = makePeakFilter(channelSettings, audioProcessor.getSampleRate(), 1);
            auto peakCoefficients2 = makePeakFilter(channelSettings, audioProcessor.getSampleRate(), 2);
            auto peakCoefficients3 = makePeakFilter(channelSettings, audioProcessor.getSampleRate(), 3);

            updateCoefficients(chain.get<ChainPositions::Peak1>().coefficients, peakCoefficients1);
            updateCoefficients(chain.get<ChainPositions::Peak2>().coefficients, peakCoefficients2);
            updateCoefficients(chain.get<ChainPositions::Peak3>().coefficients, peakCoefficients3);

            auto lowCutCoefficients = makeLowCutFilter(channelSettings, audioProcessor.getSampleRate());
            auto highCutCoefficients = makeHighCutFilter(channelSettings, audioProcessor.getSampleRate());
            
            updateCutFilter(chain.get<ChainPositions::LowCut>(), lowCutCoefficients, channelSettings.lowCutSlope);
            updateCutFilter(chain.get<ChainPositions::HighCut>(), highCutCoefficients, channelSettings.highCutSlope);
            
            auto lowShelfCoefficients = makeLowShelfFilter(channelSettings, audioProcessor.getSampleRate());
            auto highShelfCoefficients = makeHighShelfFilter(channelSettings, audioProcessor.getSampleRate());

            updateCoefficients(chain.get<ChainPositions::LowShelf>().coefficients, lowShelfCoefficients);
            updateCoefficients(chain.get<ChainPositions::HighShelf>().coefficients, highShelfCoefficients);
        };

        updateChain(leftChain, chainSettings.left);
        updateChain(rightChain, chainSettings.right);
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

void ResponseCurveComponent::mouseMove(const juce::MouseEvent& event)
{
    mouseOver = true;
    mouseX = event.getPosition().getX();
    repaint();
}

void ResponseCurveComponent::mouseEnter(const juce::MouseEvent& event)
{
    mouseOver = true;
    mouseX = event.getPosition().getX();
    repaint();
}

void ResponseCurveComponent::mouseExit(const juce::MouseEvent& event)
{
    mouseOver = false;
    repaint();
}

float ResponseCurveComponent::dbRescale(float x, float scale) noexcept
{
    x = juce::jlimit(0.0f, 1.0f, x);
    if (scale < 0.0f)
        return std::pow(x, std::pow(0.5f, -scale / 100.0f));
    else
        return std::pow(x, std::pow(2.0f, scale / 100.0f));
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
    const float w = (float) bounds.getWidth();
    const float h = (float) bounds.getHeight();
    const float top = 1.0f;
    const float bottom = h - 1.0f;

    Path fftPath;
    constexpr float rescaleDb = 18.0f;

    for (int i = 0; i < displayPoints; ++i)
    {
        const float normalisedX = (float) i / (float) juce::jmax(1, displayPoints - 1);
        const float x = normalisedX * w;
        const float magnitude = dbRescale(juce::jlimit(0.0f, 1.0f, fftData[(size_t) i]), rescaleDb);
        const float y = juce::jmap(magnitude, 0.0f, 1.0f, bottom, top);

        if (i == 0)
            fftPath.startNewSubPath(x, y);
        else
            fftPath.lineTo(x, y);
    }

    Path filledPath(fftPath);
    filledPath.lineTo(w, bottom);
    filledPath.lineTo(0.0f, bottom);
    filledPath.closeSubPath();

    g.setGradientFill(ColourGradient(fillColour, 0.0f, bottom * 0.8f,
                                     Colours::black, 0.0f, bottom,
                                     false));
    g.fillPath(filledPath);

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
                     Colours::orange.withAlpha(0.85f),
                     Colours::orange.withAlpha(0.75f),
                     1.0f);

    drawSpectrumPath(g2,
                     bounds.withPosition(0, 0),
                     postEQFFTData,
                     Colours::cyan.withAlpha(0.95f),
                     Colours::cyan.withAlpha(0.85f),
                     1.2f);

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
    constexpr float gridAlpha = 0.06f;
    constexpr float labelAlpha = 0.35f;
    
    // Horizontal grid lines (dB scale)
    for (float db = -24.0f; db <= 24.0f; db += 6.0f)
    {
        float y = jmap(db, -24.0f, 24.0f, (float)bounds.getBottom(), (float)bounds.getY());
        g.setColour(Colours::white.withAlpha(gridAlpha));
        g.drawHorizontalLine((int)y, (float)bounds.getX(), (float)bounds.getRight());
        
        g.setColour(Colours::white.withAlpha(labelAlpha));
        g.drawText(juce::String(db, 0) + " dB", bounds.getX() + 5, (int)y - 10, 50, 20, juce::Justification::left);
    }
    
    // Vertical grid lines (frequency scale)
    const float freqs[] = { 30.0f, 40.0f, 50.0f, 60.0f, 80.0f, 100.0f,
                            200.0f, 300.0f, 400.0f, 500.0f, 600.0f, 800.0f, 1000.0f,
                            2000.0f, 3000.0f, 4000.0f, 5000.0f, 6000.0f, 8000.0f, 10000.0f, 20000.0f };

    const float majorFreqs[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };
    
    const float logMinFreq = std::log10(20.0f);
    const float logMaxFreq = std::log10(20000.0f);
    const float logFreqRange = logMaxFreq - logMinFreq;

    float lastLabelEnd = -100.0f;
    
    for (float freq : freqs)
    {
        float logFreq = std::log10(freq);
        float x = bounds.getX() + bounds.getWidth() * (logFreq - logMinFreq) / logFreqRange;
        
        if (x >= bounds.getX() && x <= bounds.getRight())
        {
            g.setColour(Colours::white.withAlpha(gridAlpha));
            g.drawVerticalLine((int)x, (float)bounds.getY(), (float)bounds.getBottom());

            bool isMajor = false;
            for (float mf : majorFreqs)
            {
                if (std::abs(freq - mf) < 0.1f) { isMajor = true; break; }
            }
            
            if (isMajor)
            {
                juce::String freqText;
                if (freq < 1000.0f)
                    freqText = juce::String((int) freq);
                else
                    freqText = juce::String(freq / 1000.0f, freq < 10000.0f ? 1 : 0) + "k";

                const float labelWidth = 40.0f;
                const float labelX = x - labelWidth * 0.5f;
                
                if (labelX > lastLabelEnd + 4.0f)
                {
                    g.setColour(Colours::white.withAlpha(0.45f));
                    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
                    g.drawText(freqText,
                              (int) labelX,
                              bounds.getBottom() - 18,
                              (int) labelWidth, 16,
                              juce::Justification::centred);
                    lastLabelEnd = labelX + labelWidth;
                }
            }
        }
    }
    
    // Draw the response curves on top
    auto responseArea = bounds;
    auto w = responseArea.getWidth();
    auto sampleRate = audioProcessor.getSampleRate();
    const auto chainSettings = getChainSettings(audioProcessor.apvts);
    const auto summingMode = chainSettings.summingMode;

    auto calculateMags = [&](MonoChain& chain, std::vector<double>& mags)
    {
        auto& lowcut = chain.get<ChainPositions::LowCut>();
        auto& lowShelf = chain.get<ChainPositions::LowShelf>();
        auto& peak1 = chain.get<ChainPositions::Peak1>();
        auto& peak2 = chain.get<ChainPositions::Peak2>();
        auto& peak3 = chain.get<ChainPositions::Peak3>();
        auto& highShelf = chain.get<ChainPositions::HighShelf>();
        auto& highcut = chain.get<ChainPositions::HighCut>();

        for (int i = 0; i < w; ++i)
        {
            auto freq = mapToLog10(double(i) / double(w), 20.0, 20000.0);

            double cutMag = 1.0;
            if (!lowcut.isBypassed<0>()) cutMag *= lowcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
            if (!lowcut.isBypassed<1>()) cutMag *= lowcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
            if (!lowcut.isBypassed<2>()) cutMag *= lowcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
            if (!lowcut.isBypassed<3>()) cutMag *= lowcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);

            if (!highcut.isBypassed<0>()) cutMag *= highcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
            if (!highcut.isBypassed<1>()) cutMag *= highcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
            if (!highcut.isBypassed<2>()) cutMag *= highcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
            if (!highcut.isBypassed<3>()) cutMag *= highcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);

            double bandMag;
            if (summingMode == Summing_Classic)
            {
                bandMag = 1.0;
                auto multiplyMag = [&](const Coefficients& coeffs) {
                    if (coeffs != nullptr) bandMag *= coeffs->getMagnitudeForFrequency(freq, sampleRate);
                };
                if (!chain.isBypassed<ChainPositions::LowShelf>()) multiplyMag(lowShelf.coefficients);
                if (!chain.isBypassed<ChainPositions::Peak1>())    multiplyMag(peak1.coefficients);
                if (!chain.isBypassed<ChainPositions::Peak2>())    multiplyMag(peak2.coefficients);
                if (!chain.isBypassed<ChainPositions::Peak3>())    multiplyMag(peak3.coefficients);
                if (!chain.isBypassed<ChainPositions::HighShelf>()) multiplyMag(highShelf.coefficients);
            }
            else if (summingMode == Summing_Maximum)
            {
                bandMag = 0.0;
                auto updateMax = [&](const Coefficients& coeffs) {
                    if (coeffs != nullptr) bandMag = juce::jmax(bandMag, coeffs->getMagnitudeForFrequency(freq, sampleRate));
                };
                if (!chain.isBypassed<ChainPositions::LowShelf>()) updateMax(lowShelf.coefficients);
                if (!chain.isBypassed<ChainPositions::Peak1>())    updateMax(peak1.coefficients);
                if (!chain.isBypassed<ChainPositions::Peak2>())    updateMax(peak2.coefficients);
                if (!chain.isBypassed<ChainPositions::Peak3>())    updateMax(peak3.coefficients);
                if (!chain.isBypassed<ChainPositions::HighShelf>()) updateMax(highShelf.coefficients);
            }
            else
            {
                std::complex<double> parallelSum(0.0, 0.0);
                auto addResponse = [&](const Coefficients& coeffs) {
                    if (coeffs != nullptr) parallelSum += getComplexResponse(coeffs, freq, sampleRate);
                };
                if (!chain.isBypassed<ChainPositions::LowShelf>()) addResponse(lowShelf.coefficients);
                if (!chain.isBypassed<ChainPositions::Peak1>())    addResponse(peak1.coefficients);
                if (!chain.isBypassed<ChainPositions::Peak2>())    addResponse(peak2.coefficients);
                if (!chain.isBypassed<ChainPositions::Peak3>())    addResponse(peak3.coefficients);
                if (!chain.isBypassed<ChainPositions::HighShelf>()) addResponse(highShelf.coefficients);

                if (summingMode == Summing_Average) parallelSum /= 5.0;
                bandMag = std::abs(parallelSum);
            }

            mags[i] = Decibels::gainToDecibels(cutMag * bandMag);
        }
    };

    std::vector<double> leftMags(w), rightMags(w);
    calculateMags(leftChain, leftMags);
    calculateMags(rightChain, rightMags);

    juce::Path responseCurve;
    auto drawCurve = [&](const std::vector<double>& mags, juce::Colour colour, float thickness, float alpha, juce::Path& path)
    {
        path.clear();
        const double outputMin = responseArea.getBottom();
        const double outputMax = responseArea.getY();
        auto map = [outputMin, outputMax](double input) {
            return jmap(input, -24.0, 24.0, outputMin, outputMax);
        };

        path.startNewSubPath(responseArea.getX(), map(mags.front()));
        for (size_t i = 1; i < mags.size(); ++i) {
            path.lineTo(responseArea.getX() + i, map(mags[i]));
        }

        g.setColour(colour.withAlpha(alpha));
        g.strokePath(path, PathStrokeType(thickness));
    };

    // Use a path for drawing
    juce::Path leftPath, rightPath;
    drawCurve(leftMags, juce::Colours::white, 2.5f, 1.0f, leftPath);
    drawCurve(rightMags, juce::Colours::yellow, 1.5f, 0.6f, rightPath);

    // Draw the response curve with a glow effect
    g.setColour(Colours::aqua);
    g.drawRoundedRectangle(responseArea.toFloat(), 4.f, 1.f);
    
    // Use the combined or individual path
    g.setColour(Colours::white);
    g.strokePath(leftPath, PathStrokeType(2.5f));
    
    g.setColour(Colours::cyan.withAlpha(0.3f));
    g.strokePath(leftPath, PathStrokeType(4.0f));
    
    g.setColour(Colours::white.withAlpha(0.8f));
    g.strokePath(leftPath, PathStrokeType(1.0f));
    
    // Mouse tracking line and frequency caption
    if (mouseOver)
    {
        g.setColour(Colours::white.withAlpha(0.15f));
        g.drawVerticalLine(mouseX, 0.0f, (float) bounds.getBottom());
        
        const float normalisedX = (float) mouseX / (float) w;
        const float freq = std::pow(10.0f, std::log10(20.0f) + normalisedX * (std::log10(20000.0f) - std::log10(20.0f)));
        
        juce::String freqText;
        if (freq < 1000.0f)
            freqText = juce::String((int) freq) + " Hz";
        else
            freqText = juce::String(freq / 1000.0f, 1) + " kHz";

        const int hoverBin = juce::jlimit(0, (int) preEQFFTData.size() - 1,
                                          (int) (normalisedX * preEQFFTData.size()));
        const float preDb = preEQFFTData.empty() ? -100.0f : juce::jmap(preEQFFTData[(size_t) hoverBin], 0.0f, 1.0f, -100.0f, 0.0f);
        const float postDb = postEQFFTData.empty() ? -100.0f : juce::jmap(postEQFFTData[(size_t) hoverBin], 0.0f, 1.0f, -100.0f, 0.0f);
        
        juce::String dbText;
        if (preDb > -100.0f && postDb > -100.0f)
            dbText = "Pre: " + juce::String(preDb, 1) + " dB  Post: " + juce::String(postDb, 1) + " dB";
        else if (preDb > -100.0f)
            dbText = juce::String(preDb, 1) + " dB";

        auto captionBox = juce::Rectangle<int>(mouseX - 70, 8, 140, 36);
        g.setColour(Colours::black.withAlpha(0.7f));
        g.fillRect(captionBox);
        g.setColour(Colours::white.withAlpha(0.35f));
        g.drawRect(captionBox, 1);
        
        g.setColour(Colours::white);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f).withStyle("Bold")));
        g.drawText(freqText, captionBox.removeFromTop(18), juce::Justification::centred);
        
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
        g.setColour(Colours::white.withAlpha(0.6f));
        g.drawText(dbText, captionBox, juce::Justification::centred);
    }
}

EQoonAudioProcessorEditor::EQoonAudioProcessorEditor(EQoonAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p),
      responseCurveComponent(audioProcessor)
{
    // Initialize attachments AFTER buttons are initialized
    makeupGainSliderAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "Makeup Gain", makeupGainSlider);
    summingModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(audioProcessor.apvts, "Summing Mode", summingModeCombo);
    processingModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(audioProcessor.apvts, "Processing Mode", processingModeCombo);
    stereoLinkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.apvts, "Stereo Link", linkButton);

    leftMidButton.setButtonText("Left / Mid");
    rightSideButton.setButtonText("Right / Side");
    linkButton.setButtonText("Link");
    
    // Add buttons to a group
    leftMidButton.setRadioGroupId(1);
    rightSideButton.setRadioGroupId(1);

    leftMidButton.onClick = [this] { currentChannelView = Left_Mid; updateAttachments(); updateButtonStates(); };
    rightSideButton.onClick = [this] { currentChannelView = Right_Side; updateAttachments(); updateButtonStates(); };

    summingModeCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colours::black);
    summingModeCombo.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    summingModeCombo.setColour(juce::ComboBox::outlineColourId, juce::Colours::white.withAlpha(0.3f));
    summingModeCombo.setColour(juce::ComboBox::buttonColourId, juce::Colours::white.withAlpha(0.5f));
    summingModeCombo.setColour(juce::ComboBox::arrowColourId, juce::Colours::white.withAlpha(0.6f));
    summingModeCombo.setJustificationType(juce::Justification::centred);
    if (summingModeCombo.getNumItems() < 4)
    {
        summingModeCombo.clear();
        summingModeCombo.addItemList({ "Classic", "Average", "Sum", "Maximum" }, 1);
    }
    summingModeCombo.setSelectedItemIndex(static_cast<int>(audioProcessor.apvts.getRawParameterValue("Summing Mode")->load()), juce::dontSendNotification);

    processingModeCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colours::black);
    processingModeCombo.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    processingModeCombo.setColour(juce::ComboBox::outlineColourId, juce::Colours::white.withAlpha(0.3f));
    processingModeCombo.setColour(juce::ComboBox::buttonColourId, juce::Colours::white.withAlpha(0.5f));
    processingModeCombo.setColour(juce::ComboBox::arrowColourId, juce::Colours::white.withAlpha(0.6f));
    processingModeCombo.setJustificationType(juce::Justification::centred);
    if (processingModeCombo.getNumItems() < 2)
    {
        processingModeCombo.clear();
        processingModeCombo.addItemList({ "Left/Right", "Mid/Side" }, 1);
    }
    processingModeCombo.setSelectedItemIndex(static_cast<int>(audioProcessor.apvts.getRawParameterValue("Processing Mode")->load()), juce::dontSendNotification);
    
    processingModeCombo.onChange = [this] { updateButtonStates(); };

    for (auto* comp : getComps())
    {
        addAndMakeVisible(comp);
    }
    
    updateAttachments();
    updateButtonStates();

    setWantsKeyboardFocus(true);
    setSize(1000, 650);
}

void EQoonAudioProcessorEditor::updateAttachments()
{
    juce::String prefix = (currentChannelView == Left_Mid) ? "L " : "R ";
    
    auto& apvts = audioProcessor.apvts;
    
    lowCutFreqSliderAttachment = std::make_unique<Attachment>(apvts, prefix + "LowCut Freq", lowCutFreqSlider);
    lowCutSlopeSliderAttachment = std::make_unique<Attachment>(apvts, prefix + "LowCut Slope", lowCutSlopeSlider);
    lowCutQualitySliderAttachment = std::make_unique<Attachment>(apvts, prefix + "LowCut Quality", lowCutQualitySlider);
    
    lowShelfFreqSliderAttachment = std::make_unique<Attachment>(apvts, prefix + "LowShelf Freq", lowShelfFreqSlider);
    lowShelfGainSliderAttachment = std::make_unique<Attachment>(apvts, prefix + "LowShelf Gain", lowShelfGainSlider);
    lowShelfQualitySliderAttachment = std::make_unique<Attachment>(apvts, prefix + "LowShelf Quality", lowShelfQualitySlider);
    
    peakFreqSlider1Attachment = std::make_unique<Attachment>(apvts, prefix + "Peak1 Freq", peakFreq1Slider);
    peakGainSlider1Attachment = std::make_unique<Attachment>(apvts, prefix + "Peak1 Gain", peakGain1Slider);
    peakQualitySlider1Attachment = std::make_unique<Attachment>(apvts, prefix + "Peak1 Quality", peakQuality1Slider);
    
    peakFreqSlider2Attachment = std::make_unique<Attachment>(apvts, prefix + "Peak2 Freq", peakFreq2Slider);
    peakGainSlider2Attachment = std::make_unique<Attachment>(apvts, prefix + "Peak2 Gain", peakGain2Slider);
    peakQualitySlider2Attachment = std::make_unique<Attachment>(apvts, prefix + "Peak2 Quality", peakQuality2Slider);
    
    peakFreqSlider3Attachment = std::make_unique<Attachment>(apvts, prefix + "Peak3 Freq", peakFreq3Slider);
    peakGainSlider3Attachment = std::make_unique<Attachment>(apvts, prefix + "Peak3 Gain", peakGain3Slider);
    peakQualitySlider3Attachment = std::make_unique<Attachment>(apvts, prefix + "Peak3 Quality", peakQuality3Slider);
    
    highShelfFreqSliderAttachment = std::make_unique<Attachment>(apvts, prefix + "HighShelf Freq", highShelfFreqSlider);
    highShelfGainSliderAttachment = std::make_unique<Attachment>(apvts, prefix + "HighShelf Gain", highShelfGainSlider);
    highShelfQualitySliderAttachment = std::make_unique<Attachment>(apvts, prefix + "HighShelf Quality", highShelfQualitySlider);
    
    highCutFreqSliderAttachment = std::make_unique<Attachment>(apvts, prefix + "HighCut Freq", highCutFreqSlider);
    highCutSlopeSliderAttachment = std::make_unique<Attachment>(apvts, prefix + "HighCut Slope", highCutSlopeSlider);
    highCutQualitySliderAttachment = std::make_unique<Attachment>(apvts, prefix + "HighCut Quality", highCutQualitySlider);
}

void EQoonAudioProcessorEditor::updateButtonStates()
{
    bool isMS = processingModeCombo.getSelectedItemIndex() == 1;
    
    leftMidButton.setButtonText(isMS ? "MID" : "LEFT");
    rightSideButton.setButtonText(isMS ? "SIDE" : "RIGHT");
    
    leftMidButton.setToggleState(currentChannelView == Left_Mid, juce::dontSendNotification);
    rightSideButton.setToggleState(currentChannelView == Right_Side, juce::dontSendNotification);
    
    leftMidButton.setColour(juce::TextButton::buttonColourId, currentChannelView == Left_Mid ? juce::Colours::aqua : juce::Colours::black);
    rightSideButton.setColour(juce::TextButton::buttonColourId, currentChannelView == Right_Side ? juce::Colours::aqua : juce::Colours::black);
}

EQoonAudioProcessorEditor::~EQoonAudioProcessorEditor()
{
}

bool EQoonAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress('t', juce::ModifierKeys::commandModifier, 0))
    {
        DBG("Running EQoon unit tests...");
        juce::UnitTestRunner runner;
        runner.setAssertOnFailure(false);
        runner.runAllTests();
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
                                              juce::Label& nameLabel,
                                              bool isGainActuallySlope)
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
    
    if (isGainActuallySlope)
    {
        auto updateSlopeValue = [&gainValue, &gainSlider] {
            int slopeValue = 12 + (static_cast<int>(gainSlider.getValue()) * 12);
            gainValue.setText(juce::String(slopeValue) + " dB/oct", juce::dontSendNotification);
        };
        gainSlider.onValueChange = updateSlopeValue;
        updateSlopeValue();
    }
    else
    {
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
                   lowCutNameLabel,
                   true);

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
                   highCutNameLabel,
                   true);

    makeupRowBounds = makeupRow;

    auto positionMakeupRow = [&]()
    {
        int nameWidth = 80;
        int labelWidth = 40;
        int valueWidth = 60;
        int padding = 1;

        auto area = makeupRow;
        
        auto buttonArea = area.removeFromLeft(nameWidth + labelWidth);
        auto modeArea = buttonArea.removeFromTop(buttonArea.getHeight() / 2);
        summingModeCombo.setBounds(modeArea.removeFromLeft(modeArea.getWidth() / 2).reduced(2, 2));
        processingModeCombo.setBounds(modeArea.reduced(2, 2));
        
        auto selectorArea = buttonArea;
        leftMidButton.setBounds(selectorArea.removeFromLeft(selectorArea.getWidth() / 3).reduced(2, 2));
        rightSideButton.setBounds(selectorArea.removeFromLeft(selectorArea.getWidth() / 2).reduced(2, 2));
        linkButton.setBounds(selectorArea.reduced(2, 2));

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
        
        &summingModeCombo, &processingModeCombo,
        &leftMidButton, &rightSideButton, &linkButton,
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
