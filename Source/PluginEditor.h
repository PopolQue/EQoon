#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <vector>

struct CustomRotarySlider : juce::Slider
{
    CustomRotarySlider() : juce::Slider(juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag,
                                        juce::Slider::TextEntryBoxPosition::NoTextBox)
    {
    }
};

struct ResponseCurveComponent : juce::Component,
                                juce::AudioProcessorParameter::Listener,
                                juce::Timer
{
    ResponseCurveComponent(EQoonAudioProcessor&);
    ~ResponseCurveComponent();

    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override {}
    void timerCallback() override;
    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    
    // For FFT analysis
    void drawFFTAnalysis(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawSpectrumPath(juce::Graphics& g,
                          juce::Rectangle<int> bounds,
                          const std::vector<float>& fftData,
                          juce::Colour lineColour,
                          juce::Colour fillColour,
                          float strokeWidth);

    static float dbRescale(float x, float scale) noexcept;

private:

    juce::Image fftImage;
    std::vector<float> preEQFFTData;
    std::vector<float> postEQFFTData;
    EQoonAudioProcessor& audioProcessor;
    juce::Atomic<bool> parametersChanged{false};
    MonoChain leftChain, rightChain;

    bool mouseOver { false };
    int mouseX { 0 };
    juce::Path responseCurve;
};

class EQoonAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    EQoonAudioProcessorEditor(EQoonAudioProcessor&);
    ~EQoonAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    EQoonAudioProcessor& audioProcessor;
    
    // Sliders
    CustomRotarySlider lowCutFreqSlider, lowCutSlopeSlider, lowCutQualitySlider;
    CustomRotarySlider lowShelfFreqSlider, lowShelfGainSlider, lowShelfQualitySlider;
    CustomRotarySlider peakFreq1Slider, peakGain1Slider, peakQuality1Slider;
    CustomRotarySlider peakFreq2Slider, peakGain2Slider, peakQuality2Slider;
    CustomRotarySlider peakFreq3Slider, peakGain3Slider, peakQuality3Slider;
    CustomRotarySlider highShelfFreqSlider, highShelfGainSlider, highShelfQualitySlider;
    CustomRotarySlider highCutFreqSlider, highCutSlopeSlider, highCutQualitySlider;
    CustomRotarySlider makeupGainSlider;
    juce::ComboBox summingModeCombo;
    juce::ComboBox processingModeCombo;
    
    // Labels for sliders
    // Filter name labels
    juce::Label lowCutNameLabel, lowShelfNameLabel, peak1NameLabel, peak2NameLabel, 
                peak3NameLabel, highShelfNameLabel, highCutNameLabel, makeupNameLabel;
    
    // Parameter labels (on the left)
    juce::Label lowCutFreqLabel, lowCutSlopeLabel, lowCutQualityLabel;
    juce::Label lowShelfFreqLabel, lowShelfGainLabel, lowShelfQualityLabel;
    juce::Label peakFreq1Label, peakGain1Label, peakQuality1Label;
    juce::Label peakFreq2Label, peakGain2Label, peakQuality2Label;
    juce::Label peakFreq3Label, peakGain3Label, peakQuality3Label;
    juce::Label highShelfFreqLabel, highShelfGainLabel, highShelfQualityLabel;
    juce::Label highCutFreqLabel, highCutSlopeLabel, highCutQualityLabel;
    juce::Label makeupGainLabel;
    
    // Value display labels (on the right of sliders)
    juce::Label lowCutFreqValue, lowCutSlopeValue, lowCutQualityValue;
    juce::Label lowShelfFreqValue, lowShelfGainValue, lowShelfQualityValue;
    juce::Label peakFreq1Value, peakGain1Value, peakQuality1Value;
    juce::Label peakFreq2Value, peakGain2Value, peakQuality2Value;
    juce::Label peakFreq3Value, peakGain3Value, peakQuality3Value;
    juce::Label highShelfFreqValue, highShelfGainValue, highShelfQualityValue;
    juce::Label highCutFreqValue, highCutSlopeValue, highCutQualityValue;
    juce::Label makeupGainValue;
    
    // Band names
    static constexpr int numBands = 7;
    const juce::String bandNames[numBands] = {"HPF", "Low Shelf", "Peak 1", "Peak 2", "Peak 3", "High Shelf", "LPF"};
    
    // Row bounds for drawing
    juce::Array<juce::Rectangle<int>> bandRows;
    juce::Rectangle<int> makeupRowBounds;
    
    // Helper functions
    void positionBandRow(juce::Rectangle<int> bounds,
                       juce::Slider& freqSlider, juce::Slider& gainSlider, juce::Slider& qSlider,
                       juce::Label& freqLabel, juce::Label& gainLabel, juce::Label& qLabel,
                       juce::Label& freqValue, juce::Label& gainValue, juce::Label& qValue,
                       juce::Label& nameLabel,
                       bool isGainActuallySlope = false);
    
    void setupLabel(juce::Label& label, const juce::String& text);
    
    void drawBandRow(juce::Graphics& g, int bandIndex, const juce::Rectangle<int>& bounds);
    
    enum ChannelView { Left_Mid, Right_Side };
    ChannelView currentChannelView { Left_Mid };
    void updateAttachments();
    void updateButtonStates();

    ResponseCurveComponent responseCurveComponent;

    using APVTS = juce::AudioProcessorValueTreeState;
    using Attachment = APVTS::SliderAttachment;
    
    std::unique_ptr<Attachment> lowCutFreqSliderAttachment, lowCutSlopeSliderAttachment, lowCutQualitySliderAttachment;
    std::unique_ptr<Attachment> lowShelfFreqSliderAttachment, lowShelfGainSliderAttachment, lowShelfQualitySliderAttachment;
    std::unique_ptr<Attachment> peakFreqSlider1Attachment, peakGainSlider1Attachment, peakQualitySlider1Attachment;
    std::unique_ptr<Attachment> peakFreqSlider2Attachment, peakGainSlider2Attachment, peakQualitySlider2Attachment;
    std::unique_ptr<Attachment> peakFreqSlider3Attachment, peakGainSlider3Attachment, peakQualitySlider3Attachment;
    std::unique_ptr<Attachment> highShelfFreqSliderAttachment, highShelfGainSliderAttachment, highShelfQualitySliderAttachment;
    std::unique_ptr<Attachment> highCutFreqSliderAttachment, highCutSlopeSliderAttachment, highCutQualitySliderAttachment;
    
    std::unique_ptr<Attachment> makeupGainSliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> summingModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> processingModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> stereoLinkAttachment;

    juce::TextButton leftMidButton, rightSideButton;
    juce::ToggleButton linkButton;

    std::vector<juce::Component*> getComps();
    std::vector<juce::Component*> getValueLabels();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQoonAudioProcessorEditor)
};
