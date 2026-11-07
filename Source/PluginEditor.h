#pragma once

#include "PluginProcessor.h"
#include "UI/AccessibleComboBox.h"
#include "UI/AccessibleSlider.h"
#include "UI/AccessibleToggleButton.h"
#include "UI/CurveEditor.h"

class SidechainerAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::ChangeListener
{
public:
    explicit SidechainerAudioProcessorEditor (SidechainerAudioProcessor&);
    ~SidechainerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void focusGained (FocusChangeType) override;
    void focusOfChildComponentChanged (FocusChangeType) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    std::unique_ptr<juce::ComponentTraverser> createKeyboardFocusTraverser() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Group final : public juce::Component
    {
        explicit Group (juce::String groupTitle);
        void paint (juce::Graphics& g) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        juce::String title;
    };

    // A list for a choice parameter whose item IDs are the choice numbers plus 1, so it can leave choices out.
    class ChoiceAttachment final
    {
    public:
        ChoiceAttachment (juce::RangedAudioParameter& parameter, juce::ComboBox& box);
        ~ChoiceAttachment();

    private:
        juce::ComboBox& comboBox;
        juce::ParameterAttachment attachment;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void configureSlider (AccessibleSlider& slider, juce::Label& label, const juce::String& title, int focusOrder);
    void configureCombo (juce::ComboBox& combo, juce::Label& label, const juce::String& title, int focusOrder);
    void configureToggle (juce::Button& button, int focusOrder);
    void bindTrigger (int trigger);
    void updateHighCurveVisibility();
    void focusGroup (int delta);

    SidechainerAudioProcessor& triggerProcessor;
    juce::AudioProcessorValueTreeState& parameters;
    int boundTrigger = -1;

    Group triggerGroup { "Trigger" };
    Group rampGroup { "Ramp" };
    Group effectsGroup { "Sidechain Effects" };
    Group outputGroup { "Output" };
    CurveEditor curveEditor;
    CurveEditor highCurveEditor;

    AccessibleComboBox editTriggerBox;
    AccessibleComboBox triggerModeBox;
    AccessibleComboBox divisionBox;
    AccessibleComboBox sourceBox;
    AccessibleToggleButton sidechainSoloButton { "Sidechain Solo" };
    AccessibleToggleButton ringmodEnabledButton { "Ringmod Sidechain" };
    AccessibleToggleButton ringmodDuckedButton { "Duck Ringmod" };
    AccessibleToggleButton amplitudeModulationButton { "Amplitude Modulation" };
    AccessibleToggleButton bandEnabledButton { "Band" };
    AccessibleToggleButton highCurveEnabledButton { "High Curve" };
    // Normal and fine steps, in the unit each value is shown in; percentages step in fractions (0.01 is 1%).
    AccessibleSlider speedSlider { 0.1, 0.01 };
    AccessibleSlider thresholdSlider { 1.0, 0.1 };
    AccessibleSlider sensitivitySlider { 1.0, 0.1 };
    AccessibleSlider depthSlider { 0.01, 0.001 };
    AccessibleSlider highDepthSlider { 0.01, 0.001 };
    AccessibleSlider rampDownSlider { 1.0, 0.1 };
    AccessibleSlider rampShapeSlider { 0.1, 0.01 };
    AccessibleSlider preduckSlider { 1.0, 0.1 };
    AccessibleSlider ringmodIntensitySlider { 0.01, 0.001 };
    AccessibleSlider amplitudeModulationDepthSlider { 0.01, 0.001 };
    AccessibleSlider bandCutoffSlider { 100.0, 10.0 };
    AccessibleSlider mixSlider { 0.01, 0.001 };

    juce::Label editTriggerLabel, triggerModeLabel, divisionLabel, sourceLabel;
    juce::Label speedLabel, thresholdLabel, sensitivityLabel, depthLabel, highDepthLabel;
    juce::Label rampDownLabel, rampShapeLabel, preduckLabel, ringmodIntensityLabel, amplitudeModulationDepthLabel;
    juce::Label bandCutoffLabel, mixLabel;

    // The trigger shown by Edit Trigger.
    std::unique_ptr<ChoiceAttachment> triggerModeAttachment, divisionAttachment, sourceAttachment;
    std::unique_ptr<ButtonAttachment> sidechainSoloAttachment, bandEnabledAttachment, highCurveEnabledAttachment;
    std::unique_ptr<SliderAttachment> speedAttachment, thresholdAttachment, sensitivityAttachment, depthAttachment, highDepthAttachment;

    ButtonAttachment ringmodEnabledAttachment { parameters, "ringmodEnabled", ringmodEnabledButton };
    ButtonAttachment ringmodDuckedAttachment { parameters, "ringmodDucked", ringmodDuckedButton };
    ButtonAttachment amplitudeModulationAttachment { parameters, "amplitudeModulationEnabled", amplitudeModulationButton };
    SliderAttachment rampDownAttachment { parameters, "rampDown", rampDownSlider };
    SliderAttachment rampShapeAttachment { parameters, "rampShape", rampShapeSlider };
    SliderAttachment preduckAttachment { parameters, "preduck", preduckSlider };
    SliderAttachment ringmodIntensityAttachment { parameters, "ringmodIntensity", ringmodIntensitySlider };
    SliderAttachment amplitudeModulationDepthAttachment { parameters, "amplitudeModulationDepth", amplitudeModulationDepthSlider };
    SliderAttachment bandCutoffAttachment { parameters, "bandCutoff", bandCutoffSlider };
    SliderAttachment mixAttachment { parameters, "mix", mixSlider };

    std::array<juce::Component*, 6> focusGroups {};
    juce::Component::SafePointer<juce::Component> lastFocusedControl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SidechainerAudioProcessorEditor)
};
