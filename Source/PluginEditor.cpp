#include "PluginEditor.h"

namespace
{
class GroupAccessibilityHandler final : public juce::AccessibilityHandler
{
public:
    GroupAccessibilityHandler (juce::Component& component, juce::String groupTitle)
        : AccessibilityHandler (component, juce::AccessibilityRole::group), title (std::move (groupTitle)) {}

    juce::String getTitle() const override { return title; }

private:
    juce::String title;
};
}

SidechainerAudioProcessorEditor::Group::Group (juce::String groupTitle)
    : title (std::move (groupTitle))
{
    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
    setTitle (title);
}

void SidechainerAudioProcessorEditor::Group::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xffffffff));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (juce::Colour (0xffd8d8d0));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (juce::Colour (0xff181816));
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (title, getLocalBounds().reduced (12).removeFromTop (22), juce::Justification::centredLeft);
}

std::unique_ptr<juce::AccessibilityHandler> SidechainerAudioProcessorEditor::Group::createAccessibilityHandler()
{
    return std::make_unique<GroupAccessibilityHandler> (*this, title);
}

SidechainerAudioProcessorEditor::SidechainerAudioProcessorEditor (SidechainerAudioProcessor& p)
    : AudioProcessorEditor (&p),
      parameters (p.getValueTreeState()),
      curveEditor (parameters, "curvePoint", "Main / Low Curve Editor",
                   "Sixteen focusable curve points for the full-band curve, or the low band when Band is enabled"),
      highCurveEditor (parameters, "highCurvePoint", "High Curve Editor",
                       "Sixteen focusable curve points for the high band above the cutoff frequency")
{
    setSize (820, 940);
    setWantsKeyboardFocus (true);
    setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
    setTitle ("BP-Trigger");
    setDescription ("Volume shaping plugin with host sync and sidechain triggering");
    setHelpText ("Press comma or period to move between major sections. Tab moves through controls.");

    triggerModeBox.addItemList ({ "Grid", "Sidechain", "Grid and Sidechain" }, 1);
    divisionBox.addItemList ({ "1 bar", "1/2", "1/2 triplet", "1/4", "1/4 triplet",
                               "1/8", "1/8 triplet", "1/16" },
                             1);
    configureCombo (triggerModeBox, "Trigger Mode",
                    "Choose whether the curve runs on the host grid, the sidechain input, or both", 1);
    configureCombo (divisionBox, "Grid Division",
                    "Choose the rhythmic division for synced ducking and sidechain envelope length", 2);
    sidechainSoloButton.setWantsKeyboardFocus (true);
    sidechainSoloButton.setTitle ("Sidechain Solo");
    sidechainSoloButton.setDescription ("Monitor input channels 3/4 through the plugin output; audio is heard only while it reaches the Sidechain Threshold");
    sidechainSoloButton.setHelpText ("Press Space to toggle thresholded sidechain monitoring on or off");
    sidechainSoloButton.setExplicitFocusOrder (6);

    ringmodEnabledButton.setWantsKeyboardFocus (true);
    ringmodEnabledButton.setTitle ("Ringmod Sidechain");
    ringmodEnabledButton.setDescription ("Use input channels 3/4 as a ringmod source before the low and high curve shapers");
    ringmodEnabledButton.setHelpText ("Press Space to toggle ringmod sidechain on or off");
    ringmodEnabledButton.setExplicitFocusOrder (7);

    configureSlider (speedSlider, speedLabel, "Speed",
                     "Scale the curve speed in synced and sidechain modes", "x", 3);
    configureSlider (rampDownSlider, rampDownLabel, "Ramp Down",
                     "Fade down into the first curve point before the trigger, with matching latency compensation", " ms", 4);
    configureSlider (rampShapeSlider, rampShapeLabel, "Ramp Shape",
                     "Adjust the curve shape of the ramp down before the first curve point", "x", 5);
    configureSlider (preduckSlider, preduckLabel, "Pre-duck",
                     "Duck before sidechain hits by delaying the main signal up to ten milliseconds", " ms", 9);
    configureSlider (thresholdSlider, thresholdLabel, "Sidechain Threshold",
                     "Set the sidechain detector threshold from minus forty decibels to zero decibels", "dB", 10);
    configureSlider (sensitivitySlider, sensitivityLabel, "Detector Release",
                     "Set how quickly the sidechain detector resets after a trigger", "releaseMs", 11);
    configureSlider (ringmodIntensitySlider, ringmodIntensityLabel, "Ringmod Intensity",
                     "Set how strongly input channels 3/4 modulate the shaped path", "%", 8);
    bandEnabledButton.setWantsKeyboardFocus (true);
    bandEnabledButton.setTitle ("Band");
    bandEnabledButton.setDescription ("Split the signal at the cutoff so the main curve shapes the low band");
    bandEnabledButton.setHelpText ("Press Space to toggle band splitting on or off");
    bandEnabledButton.setExplicitFocusOrder (13);
    highCurveEnabledButton.setWantsKeyboardFocus (true);
    highCurveEnabledButton.setTitle ("High Curve");
    highCurveEnabledButton.setDescription ("Enable an independent curve for the high band above the cutoff frequency");
    highCurveEnabledButton.setHelpText ("Press Space to toggle the high band curve editor on or off");
    highCurveEnabledButton.setExplicitFocusOrder (14);
    configureSlider (bandCutoffSlider, bandCutoffLabel, "Band Cutoff",
                     "Set the frequency that separates the low and high bands", "Hz", 15);
    configureSlider (depthSlider, depthLabel, "Depth",
                     "Set the full-band depth, or the low band depth when Band is enabled", "%", 16);
    configureSlider (highDepthSlider, highDepthLabel, "High Depth",
                     "Set the amount of gain reduction for the high band curve", "%", 17);
    configureSlider (mixSlider, mixLabel, "Mix",
                     "Blend between dry audio and shaped audio", "%", 18);

    triggerModeAttachment = std::make_unique<ComboBoxAttachment> (parameters, "triggerMode", triggerModeBox);
    divisionAttachment = std::make_unique<ComboBoxAttachment> (parameters, "division", divisionBox);
    sidechainSoloAttachment = std::make_unique<ButtonAttachment> (parameters, "sidechainSolo", sidechainSoloButton);
    ringmodEnabledAttachment = std::make_unique<ButtonAttachment> (parameters, "ringmodEnabled", ringmodEnabledButton);
    bandEnabledAttachment = std::make_unique<ButtonAttachment> (parameters, "bandEnabled", bandEnabledButton);
    highCurveEnabledAttachment = std::make_unique<ButtonAttachment> (parameters, "highCurveEnabled", highCurveEnabledButton);
    speedAttachment = std::make_unique<SliderAttachment> (parameters, "speed", speedSlider);
    rampDownAttachment = std::make_unique<SliderAttachment> (parameters, "rampDown", rampDownSlider);
    rampShapeAttachment = std::make_unique<SliderAttachment> (parameters, "rampShape", rampShapeSlider);
    preduckAttachment = std::make_unique<SliderAttachment> (parameters, "preduck", preduckSlider);
    thresholdAttachment = std::make_unique<SliderAttachment> (parameters, "threshold", thresholdSlider);
    sensitivityAttachment = std::make_unique<SliderAttachment> (parameters, "sensitivity", sensitivitySlider);
    ringmodIntensityAttachment = std::make_unique<SliderAttachment> (parameters, "ringmodIntensity", ringmodIntensitySlider);
    bandCutoffAttachment = std::make_unique<SliderAttachment> (parameters, "bandCutoff", bandCutoffSlider);
    depthAttachment = std::make_unique<SliderAttachment> (parameters, "depth", depthSlider);
    highDepthAttachment = std::make_unique<SliderAttachment> (parameters, "highDepth", highDepthSlider);
    mixAttachment = std::make_unique<SliderAttachment> (parameters, "mix", mixSlider);

    addAndMakeVisible (timingGroup);
    addAndMakeVisible (sidechainGroup);
    addAndMakeVisible (outputGroup);
    addAndMakeVisible (curveEditor);
    addChildComponent (highCurveEditor);

    for (auto* child : { static_cast<juce::Component*> (&triggerModeBox), static_cast<juce::Component*> (&divisionBox),
                         static_cast<juce::Component*> (&speedSlider), static_cast<juce::Component*> (&speedLabel),
                         static_cast<juce::Component*> (&rampDownSlider), static_cast<juce::Component*> (&rampDownLabel),
                         static_cast<juce::Component*> (&rampShapeSlider), static_cast<juce::Component*> (&rampShapeLabel) })
        timingGroup.addAndMakeVisible (child);

    for (auto* child : { static_cast<juce::Component*> (&sidechainSoloButton),
                         static_cast<juce::Component*> (&ringmodEnabledButton),
                         static_cast<juce::Component*> (&ringmodIntensitySlider),
                         static_cast<juce::Component*> (&ringmodIntensityLabel),
                         static_cast<juce::Component*> (&preduckSlider), static_cast<juce::Component*> (&preduckLabel),
                         static_cast<juce::Component*> (&thresholdSlider), static_cast<juce::Component*> (&thresholdLabel),
                         static_cast<juce::Component*> (&sensitivitySlider), static_cast<juce::Component*> (&sensitivityLabel) })
        sidechainGroup.addAndMakeVisible (child);

    for (auto* child : { static_cast<juce::Component*> (&bandEnabledButton),
                         static_cast<juce::Component*> (&highCurveEnabledButton),
                         static_cast<juce::Component*> (&bandCutoffSlider), static_cast<juce::Component*> (&bandCutoffLabel),
                         static_cast<juce::Component*> (&depthSlider), static_cast<juce::Component*> (&depthLabel),
                         static_cast<juce::Component*> (&highDepthSlider), static_cast<juce::Component*> (&highDepthLabel),
                         static_cast<juce::Component*> (&mixSlider), static_cast<juce::Component*> (&mixLabel) })
        outputGroup.addAndMakeVisible (child);

    highCurveEnabledButton.onStateChange = [this] { updateHighCurveVisibility(); };
    updateHighCurveVisibility();

    focusGroups = { &timingGroup, &sidechainGroup, &outputGroup, &curveEditor, &highCurveEditor };
}

void SidechainerAudioProcessorEditor::configureSlider (AccessibleSlider& slider,
                                                       juce::Label& label,
                                                       const juce::String& title,
                                                       const juce::String& description,
                                                       const juce::String& suffix,
                                                       int focusOrder)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 22);

    if (suffix == "%")
    {
        slider.setKeyboardSteps (0.01, 0.01);
        slider.setTextValueSuffix ({});
        slider.textFromValueFunction = [] (double value)
        {
            return juce::String (juce::roundToInt (value * 100.0)) + "%";
        };
        slider.valueFromTextFunction = [] (const juce::String& text)
        {
            return juce::jlimit (0.0, 1.0, text.retainCharacters ("0123456789.-").getDoubleValue() / 100.0);
        };
    }
    else if (suffix == "dB")
    {
        slider.setTextValueSuffix ({});
        slider.textFromValueFunction = [] (double value)
        {
            return juce::String (juce::Decibels::gainToDecibels (juce::jmax (0.000001, value)), 1) + " dB";
        };
        slider.valueFromTextFunction = [] (const juce::String& text)
        {
            const auto dB = juce::jlimit (-40.0, 0.0,
                                          text.retainCharacters ("0123456789.-").getDoubleValue());
            return juce::jlimit (0.01, 1.0, static_cast<double> (juce::Decibels::decibelsToGain (static_cast<float> (dB))));
        };
        slider.setCustomStepFunction ([] (double value, bool fine, int direction)
        {
            const auto step = fine ? 0.1 : 1.0;
            const auto currentDb = juce::Decibels::gainToDecibels (juce::jmax (0.000001, value));
            const auto nextDb = juce::jlimit (-40.0, 0.0, currentDb + static_cast<double> (direction) * step);
            return juce::jlimit (0.01, 1.0, static_cast<double> (juce::Decibels::decibelsToGain (static_cast<float> (nextDb))));
        });
    }
    else if (suffix == "releaseMs")
    {
        slider.setTextValueSuffix ({});
        slider.textFromValueFunction = [] (double value)
        {
            const auto releaseMs = juce::jmap (juce::jlimit (0.0, 1.0, value), 100.0, 1.0);
            return juce::String (juce::roundToInt (releaseMs)) + " ms";
        };
        slider.valueFromTextFunction = [] (const juce::String& text)
        {
            const auto releaseMs = juce::jlimit (1.0, 100.0,
                                                 text.retainCharacters ("0123456789.-").getDoubleValue());
            return juce::jlimit (0.0, 1.0, (100.0 - releaseMs) / 99.0);
        };
        slider.setCustomStepFunction ([] (double value, bool fine, int direction)
        {
            const auto step = fine ? 0.1 : 1.0;
            const auto currentMs = juce::jmap (juce::jlimit (0.0, 1.0, value), 100.0, 1.0);
            const auto nextMs = juce::jlimit (1.0, 100.0, currentMs + static_cast<double> (direction) * step);
            return juce::jlimit (0.0, 1.0, (100.0 - nextMs) / 99.0);
        });
    }
    else if (suffix == "Hz")
    {
        slider.setTextValueSuffix ({});
        slider.textFromValueFunction = [] (double value)
        {
            if (value >= 1000.0)
                return juce::String (value / 1000.0, value >= 10000.0 ? 1 : 2) + " kHz";

            return juce::String (juce::roundToInt (value)) + " Hz";
        };
        slider.valueFromTextFunction = [] (const juce::String& text)
        {
            const auto lower = text.toLowerCase();
            auto value = lower.retainCharacters ("0123456789.-").getDoubleValue();

            if (lower.contains ("khz"))
                value *= 1000.0;

            return juce::jlimit (20.0, 20000.0, value);
        };
        slider.setCustomStepFunction ([] (double value, bool fine, int direction)
        {
            const auto step = fine ? 10.0 : 100.0;
            return juce::jlimit (20.0, 20000.0, value + static_cast<double> (direction) * step);
        });
    }
    else
    {
        slider.setTextValueSuffix (suffix);
    }

    slider.setTitle (title);
    slider.setDescription (description);
    if (suffix == "%")
        slider.setHelpText ("Use arrow keys to adjust by one percent.");
    else if (suffix == "dB")
        slider.setHelpText ("Use arrow keys to adjust by one decibel. Hold Shift with arrow keys for one tenth of a decibel.");
    else if (suffix == "releaseMs")
        slider.setHelpText ("Use arrow keys to adjust by one millisecond. Hold Shift with arrow keys for one tenth of a millisecond.");
    else if (suffix == "Hz")
        slider.setHelpText ("Use arrow keys to adjust by one hundred hertz. Hold Shift with arrow keys for ten hertz.");
    else
        slider.setHelpText ("Use arrow keys to adjust. Hold Shift with arrow keys for fine adjustment.");
    slider.setExplicitFocusOrder (focusOrder);

    label.setText (title, juce::dontSendNotification);
    label.attachToComponent (&slider, true);
    label.setJustificationType (juce::Justification::centredRight);
}

void SidechainerAudioProcessorEditor::configureCombo (juce::ComboBox& combo,
                                                      const juce::String& title,
                                                      const juce::String& description,
                                                      int focusOrder)
{
    combo.setWantsKeyboardFocus (true);
    combo.setTitle (title);
    combo.setDescription (description);
    combo.setHelpText ("Use arrow keys to choose an option");
    combo.setExplicitFocusOrder (focusOrder);
}

void SidechainerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xfff0f2ef));
    g.setColour (juce::Colour (0xff181816));
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("BP-Trigger", 22, 14, getWidth() - 44, 30, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (14.0f));
    g.setColour (juce::Colour (0xff555a57));
    g.drawText ("Grid and sidechain volume shaper", 22, 42, getWidth() - 44, 22, juce::Justification::centredLeft);
}

void SidechainerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (22);
    area.removeFromTop (54);

    auto top = area.removeFromTop (380);
    auto timing = top.removeFromLeft (250);
    top.removeFromLeft (12);
    auto sidechain = top.removeFromLeft (306);
    top.removeFromLeft (12);

    timingGroup.setBounds (timing);
    sidechainGroup.setBounds (sidechain);
    outputGroup.setBounds (top);

    auto layoutCombo = [] (juce::ComboBox& combo, juce::Rectangle<int>& groupArea)
    {
        combo.setBounds (groupArea.removeFromTop (28).withTrimmedLeft (112));
        groupArea.removeFromTop (8);
    };

    auto layoutSlider = [] (juce::Slider& slider, juce::Rectangle<int>& groupArea)
    {
        slider.setBounds (groupArea.removeFromTop (34).withTrimmedLeft (82));
        groupArea.removeFromTop (8);
    };

    auto timingArea = timingGroup.getLocalBounds().reduced (12).withTrimmedTop (34);
    layoutCombo (triggerModeBox, timingArea);
    layoutCombo (divisionBox, timingArea);
    layoutSlider (speedSlider, timingArea);
    layoutSlider (rampDownSlider, timingArea);
    layoutSlider (rampShapeSlider, timingArea);

    auto sidechainArea = sidechainGroup.getLocalBounds().reduced (12).withTrimmedTop (34);
    sidechainSoloButton.setBounds (sidechainArea.removeFromTop (26).withTrimmedLeft (112));
    sidechainArea.removeFromTop (8);
    ringmodEnabledButton.setBounds (sidechainArea.removeFromTop (26).withTrimmedLeft (112));
    sidechainArea.removeFromTop (8);
    layoutSlider (ringmodIntensitySlider, sidechainArea);
    layoutSlider (preduckSlider, sidechainArea);
    layoutSlider (thresholdSlider, sidechainArea);
    layoutSlider (sensitivitySlider, sidechainArea);

    auto outputArea = outputGroup.getLocalBounds().reduced (12).withTrimmedTop (34);
    bandEnabledButton.setBounds (outputArea.removeFromTop (26).withTrimmedLeft (82));
    outputArea.removeFromTop (8);
    highCurveEnabledButton.setBounds (outputArea.removeFromTop (26).withTrimmedLeft (82));
    outputArea.removeFromTop (8);
    layoutSlider (bandCutoffSlider, outputArea);
    layoutSlider (depthSlider, outputArea);
    if (highDepthSlider.isVisible())
        layoutSlider (highDepthSlider, outputArea);
    layoutSlider (mixSlider, outputArea);

    area.removeFromTop (14);
    if (highCurveEditor.isVisible())
    {
        auto lowCurveArea = area.removeFromTop ((area.getHeight() - 12) / 2);
        curveEditor.setBounds (lowCurveArea);
        area.removeFromTop (12);
        highCurveEditor.setBounds (area);
    }
    else
    {
        curveEditor.setBounds (area);
        highCurveEditor.setBounds ({});
    }
}

bool SidechainerAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == ',' || key.getTextCharacter() == ',')
    {
        focusGroup (-1);
        return true;
    }

    if (key.getKeyCode() == '.' || key.getTextCharacter() == '.')
    {
        focusGroup (1);
        return true;
    }

    return juce::AudioProcessorEditor::keyPressed (key);
}

void SidechainerAudioProcessorEditor::updateHighCurveVisibility()
{
    const auto shouldShow = highCurveEnabledButton.getToggleState();
    highCurveEditor.setVisible (shouldShow);
    highCurveEditor.setEnabled (shouldShow);
    highDepthSlider.setVisible (shouldShow);
    highDepthSlider.setEnabled (shouldShow);
    highDepthLabel.setVisible (shouldShow);
    highDepthLabel.setEnabled (shouldShow);
    resized();
}

void SidechainerAudioProcessorEditor::focusGroup (int delta)
{
    for (size_t attempt = 0; attempt < focusGroups.size(); ++attempt)
    {
        focusedGroup = (focusedGroup + delta + static_cast<int> (focusGroups.size())) % static_cast<int> (focusGroups.size());

        if (auto* group = focusGroups[(size_t) focusedGroup])
        {
            if (! group->isVisible())
                continue;

            group->grabKeyboardFocus();
            juce::AccessibilityHandler::postAnnouncement (group->getTitle(),
                                                          juce::AccessibilityHandler::AnnouncementPriority::medium);
            return;
        }
    }
}
