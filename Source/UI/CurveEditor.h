#pragma once

#include "../PluginProcessor.h"
#include "AccessibleSlider.h"

class CurveEditor final : public juce::Component
{
public:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    CurveEditor (SidechainerAudioProcessor::APVTS& state,
                 juce::String pointParameterPrefix,
                 juce::String editorTitle);

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void showPresetMenu();
    void saveCurrentCurve();
    bool saveCurveToFile (juce::File file);
    bool loadCurveFromFile (const juce::File& file);
    juce::File getPresetDirectory() const;
    juce::String getDefaultPresetName() const;
    void updatePointDescriptions();
    juce::String curvePointId (int index) const;

    SidechainerAudioProcessor::APVTS& parameters;
    juce::String pointPrefix;
    juce::String title;
    juce::TextButton presetButton { "Curve Presets" };
    juce::Array<juce::File> presetFiles;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::OwnedArray<AccessibleSlider> pointSliders;
    std::vector<std::unique_ptr<SliderAttachment>> attachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveEditor)
};
