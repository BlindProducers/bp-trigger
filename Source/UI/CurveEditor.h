#pragma once

#include "../PluginProcessor.h"

// A curve's section: the keyboard editor for the curve, and its presets.
class CurveEditor final : public juce::Component,
                          private juce::ChangeListener
{
public:
    CurveEditor (SidechainerAudioProcessor& processor, bool highCurve);
    ~CurveEditor() override;

    // Shows the curve of a trigger, 0 or 1.
    void setTrigger (int trigger);

    void paint (juce::Graphics& g) override;
    void resized() override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    // The curve drawn and edited from the keyboard; every key's result is spoken.
    class Shape final : public juce::Component
    {
    public:
        explicit Shape (CurveEditor& owner);

        void paint (juce::Graphics& g) override;
        bool keyPressed (const juce::KeyPress& key) override;
        void focusGained (FocusChangeType) override   { repaint(); }
        void focusLost (FocusChangeType) override     { repaint(); }
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    private:
        CurveEditor& owner;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void apply (const CurveEditing::Result& result);
    void showPresetMenu();
    void saveCurrentCurve();
    bool saveCurveToFile (const juce::File& file) const;
    bool loadCurveFromFile (const juce::File& file);
    static juce::File getPresetDirectory();
    int slot() const noexcept   { return SidechainerAudioProcessor::curveSlot (trigger, high); }

    SidechainerAudioProcessor& processor;
    const bool high;
    int trigger = 0;
    juce::String title;
    CurveEditing editing;
    Shape shape { *this };
    juce::TextButton presetButton;
    juce::Array<juce::File> presetFiles;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveEditor)
};
