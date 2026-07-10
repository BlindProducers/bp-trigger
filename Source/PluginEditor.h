#pragma once

#include "PluginProcessor.h"
#include "UI/AccessibleSlider.h"
#include "UI/CurveEditor.h"

class SidechainerAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SidechainerAudioProcessorEditor (SidechainerAudioProcessor&);
    ~SidechainerAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Group final : public juce::Component
    {
        explicit Group (juce::String groupTitle);
        void paint (juce::Graphics& g) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        juce::String title;
    };

    void configureSlider (AccessibleSlider& slider,
                          juce::Label& label,
                          const juce::String& title,
                          const juce::String& description,
                          const juce::String& suffix,
                          int focusOrder);

    void configureCombo (juce::ComboBox& combo,
                         const juce::String& title,
                         const juce::String& description,
                         int focusOrder);

    void updateHighCurveVisibility();
    void focusGroup (int delta);

    juce::AudioProcessorValueTreeState& parameters;

    Group timingGroup { "Timing" };
    Group sidechainGroup { "Sidechain" };
    Group outputGroup { "Output" };
    CurveEditor curveEditor;
    CurveEditor highCurveEditor;

    juce::ComboBox triggerModeBox;
    juce::ComboBox divisionBox;
    juce::ToggleButton sidechainSoloButton { "Sidechain Solo" };
    juce::ToggleButton ringmodEnabledButton { "Ringmod Sidechain" };
    juce::ToggleButton bandEnabledButton { "Band" };
    juce::ToggleButton highCurveEnabledButton { "High Curve" };
    AccessibleSlider speedSlider { 0.01, 0.001 };
    AccessibleSlider rampDownSlider { 0.1, 0.01 };
    AccessibleSlider rampShapeSlider { 0.01, 0.001 };
    AccessibleSlider preduckSlider { 0.1, 0.01 };
    AccessibleSlider thresholdSlider { 0.01, 0.001 };
    AccessibleSlider sensitivitySlider { 0.01, 0.001 };
    AccessibleSlider ringmodIntensitySlider { 0.01, 0.001 };
    AccessibleSlider bandCutoffSlider { 100.0, 10.0 };
    AccessibleSlider depthSlider { 0.01, 0.001 };
    AccessibleSlider highDepthSlider { 0.01, 0.001 };
    AccessibleSlider mixSlider { 0.01, 0.001 };

    juce::Label speedLabel, rampDownLabel, rampShapeLabel, preduckLabel, thresholdLabel, sensitivityLabel;
    juce::Label ringmodIntensityLabel;
    juce::Label bandCutoffLabel, depthLabel, highDepthLabel, mixLabel;

    std::unique_ptr<ComboBoxAttachment> triggerModeAttachment;
    std::unique_ptr<ComboBoxAttachment> divisionAttachment;
    std::unique_ptr<ButtonAttachment> sidechainSoloAttachment;
    std::unique_ptr<ButtonAttachment> ringmodEnabledAttachment;
    std::unique_ptr<ButtonAttachment> bandEnabledAttachment;
    std::unique_ptr<ButtonAttachment> highCurveEnabledAttachment;
    std::unique_ptr<SliderAttachment> speedAttachment;
    std::unique_ptr<SliderAttachment> rampDownAttachment;
    std::unique_ptr<SliderAttachment> rampShapeAttachment;
    std::unique_ptr<SliderAttachment> preduckAttachment;
    std::unique_ptr<SliderAttachment> thresholdAttachment;
    std::unique_ptr<SliderAttachment> sensitivityAttachment;
    std::unique_ptr<SliderAttachment> ringmodIntensityAttachment;
    std::unique_ptr<SliderAttachment> bandCutoffAttachment;
    std::unique_ptr<SliderAttachment> depthAttachment;
    std::unique_ptr<SliderAttachment> highDepthAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;

    std::array<juce::Component*, 5> focusGroups {};
    int focusedGroup = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SidechainerAudioProcessorEditor)
};
