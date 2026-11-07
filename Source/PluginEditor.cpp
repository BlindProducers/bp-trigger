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

// Tab and Shift-Tab run through the editor's controls and wrap around, as they would
// in a keyboard focus container. The editor can't be one: screen readers would then
// see it as a group of its own inside the host's group (see createAccessibilityHandler).
class EditorKeyboardTraverser final : public juce::ComponentTraverser
{
public:
    explicit EditorKeyboardTraverser (juce::Component& editorToTraverse) : editor (editorToTraverse) {}

    juce::Component* getDefaultComponent (juce::Component* parent) override
    {
        const auto controls = getAllComponents (parent);
        return controls.empty() ? nullptr : controls.front();
    }

    juce::Component* getNextComponent (juce::Component* current) override       { return step (current, 1); }
    juce::Component* getPreviousComponent (juce::Component* current) override   { return step (current, -1); }

    std::vector<juce::Component*> getAllComponents (juce::Component* parent) override
    {
        auto controls = traverser.getAllComponents (scopeFor (parent));

        // JUCE makes an editable value box a Tab stop of its own; Return on the slider opens it instead.
        controls.erase (std::remove_if (controls.begin(), controls.end(), [] (const juce::Component* comp)
                                        {
                                            return dynamic_cast<const juce::Slider*> (comp->getParentComponent()) != nullptr;
                                        }),
                        controls.end());
        return controls;
    }

private:
    // JUCE asks about the host's wrapper when it wraps around, so anything above the editor means the editor.
    juce::Component* scopeFor (juce::Component* parent) const
    {
        return parent == nullptr || parent->isParentOf (&editor) ? &editor : parent;
    }

    // Returns nothing past either end, so JUCE wraps to the first or last control.
    // From inside a control, such as a slider's open value box, it steps from that control.
    juce::Component* step (juce::Component* current, int delta)
    {
        const auto controls = getAllComponents (&editor);
        auto found = controls.end();

        for (auto* comp = current; comp != nullptr && found == controls.end(); comp = comp->getParentComponent())
            found = std::find (controls.begin(), controls.end(), comp);

        if (found == controls.end())
            return nullptr;

        const auto index = std::distance (controls.begin(), found) + delta;
        return juce::isPositiveAndBelow (index, controls.size()) ? controls[(size_t) index] : nullptr;
    }

    juce::Component& editor;
    juce::KeyboardFocusTraverser traverser;
};
}

SidechainerAudioProcessorEditor::Group::Group (juce::String groupTitle)
    : title (std::move (groupTitle))
{
    // A screen reader enters the section as a named group, and Tab runs on through every section.
    setFocusContainerType (juce::Component::FocusContainerType::focusContainer);
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
      curveEditor (parameters, "curvePoint", "Main / Low Curve Editor"),
      highCurveEditor (parameters, "highCurvePoint", "High Curve Editor")
{
    setSize (820, 970);
    setWantsKeyboardFocus (true);
    setTitle ("BP-Trigger");

    triggerModeBox.addItemList ({ "Grid", "Sidechain", "Grid and Sidechain" }, 1);
    divisionBox.addItemList ({ "1 bar", "1/2", "1/2 triplet", "1/4", "1/4 triplet",
                               "1/8", "1/8 triplet", "1/16" },
                             1);
    configureCombo (triggerModeBox, "Trigger Mode", 1);
    configureCombo (divisionBox, "Grid Division", 2);
    sidechainSoloButton.setWantsKeyboardFocus (true);
    sidechainSoloButton.setTitle ("Sidechain Solo");
    sidechainSoloButton.setExplicitFocusOrder (6);

    ringmodEnabledButton.setWantsKeyboardFocus (true);
    ringmodEnabledButton.setTitle ("Ringmod Sidechain");
    ringmodEnabledButton.setExplicitFocusOrder (7);

    ringmodDuckedButton.setWantsKeyboardFocus (true);
    ringmodDuckedButton.setTitle ("Duck Ringmod");
    ringmodDuckedButton.setExplicitFocusOrder (8);

    amplitudeModulationButton.setWantsKeyboardFocus (true);
    amplitudeModulationButton.setTitle ("Amplitude Modulation");
    amplitudeModulationButton.setExplicitFocusOrder (10);

    configureSlider (speedSlider, speedLabel, "Speed", 3);
    configureSlider (rampDownSlider, rampDownLabel, "Ramp Down", 4);
    configureSlider (rampShapeSlider, rampShapeLabel, "Ramp Shape", 5);
    configureSlider (preduckSlider, preduckLabel, "Pre-duck", 12);
    configureSlider (thresholdSlider, thresholdLabel, "Sidechain Threshold", 13);
    configureSlider (sensitivitySlider, sensitivityLabel, "Detector Release", 14);
    configureSlider (ringmodIntensitySlider, ringmodIntensityLabel, "Ringmod Intensity", 9);
    configureSlider (amplitudeModulationDepthSlider, amplitudeModulationDepthLabel, "Amplitude Modulation Depth", 11);
    bandEnabledButton.setWantsKeyboardFocus (true);
    bandEnabledButton.setTitle ("Band");
    bandEnabledButton.setExplicitFocusOrder (15);
    highCurveEnabledButton.setWantsKeyboardFocus (true);
    highCurveEnabledButton.setTitle ("High Curve");
    highCurveEnabledButton.setExplicitFocusOrder (16);
    configureSlider (bandCutoffSlider, bandCutoffLabel, "Band Cutoff", 17);
    configureSlider (depthSlider, depthLabel, "Depth", 18);
    configureSlider (highDepthSlider, highDepthLabel, "High Depth", 19);
    configureSlider (mixSlider, mixLabel, "Mix", 20);

    // The arrows move these through the units they are shown in: decibels, and milliseconds of release.
    thresholdSlider.setDisplayUnit ([] (double gain) { return (double) juce::Decibels::gainToDecibels ((float) gain); },
                                    [] (double dB) { return (double) juce::Decibels::decibelsToGain ((float) dB); });
    sensitivitySlider.setDisplayUnit (SidechainerAudioProcessor::detectorReleaseMsFromSensitivity,
                                      SidechainerAudioProcessor::sensitivityFromDetectorReleaseMs);

    triggerModeAttachment = std::make_unique<ComboBoxAttachment> (parameters, "triggerMode", triggerModeBox);
    divisionAttachment = std::make_unique<ComboBoxAttachment> (parameters, "division", divisionBox);
    sidechainSoloAttachment = std::make_unique<ButtonAttachment> (parameters, "sidechainSolo", sidechainSoloButton);
    ringmodEnabledAttachment = std::make_unique<ButtonAttachment> (parameters, "ringmodEnabled", ringmodEnabledButton);
    ringmodDuckedAttachment = std::make_unique<ButtonAttachment> (parameters, "ringmodDucked", ringmodDuckedButton);
    amplitudeModulationAttachment = std::make_unique<ButtonAttachment> (parameters, "amplitudeModulationEnabled", amplitudeModulationButton);
    bandEnabledAttachment = std::make_unique<ButtonAttachment> (parameters, "bandEnabled", bandEnabledButton);
    highCurveEnabledAttachment = std::make_unique<ButtonAttachment> (parameters, "highCurveEnabled", highCurveEnabledButton);
    speedAttachment = std::make_unique<SliderAttachment> (parameters, "speed", speedSlider);
    rampDownAttachment = std::make_unique<SliderAttachment> (parameters, "rampDown", rampDownSlider);
    rampShapeAttachment = std::make_unique<SliderAttachment> (parameters, "rampShape", rampShapeSlider);
    preduckAttachment = std::make_unique<SliderAttachment> (parameters, "preduck", preduckSlider);
    thresholdAttachment = std::make_unique<SliderAttachment> (parameters, "threshold", thresholdSlider);
    sensitivityAttachment = std::make_unique<SliderAttachment> (parameters, "sensitivity", sensitivitySlider);
    ringmodIntensityAttachment = std::make_unique<SliderAttachment> (parameters, "ringmodIntensity", ringmodIntensitySlider);
    amplitudeModulationDepthAttachment = std::make_unique<SliderAttachment> (parameters, "amplitudeModulationDepth", amplitudeModulationDepthSlider);
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
                         static_cast<juce::Component*> (&ringmodDuckedButton),
                         static_cast<juce::Component*> (&ringmodIntensitySlider),
                         static_cast<juce::Component*> (&ringmodIntensityLabel),
                         static_cast<juce::Component*> (&amplitudeModulationButton),
                         static_cast<juce::Component*> (&amplitudeModulationDepthSlider),
                         static_cast<juce::Component*> (&amplitudeModulationDepthLabel),
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
                                                       int focusOrder)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 22);

    slider.setTitle (title);
    slider.setExplicitFocusOrder (focusOrder);

    // The slider already carries this name, so screen readers skip the visible label.
    label.setAccessible (false);
    label.setText (title, juce::dontSendNotification);
    label.attachToComponent (&slider, true);
    label.setJustificationType (juce::Justification::centredRight);
}

void SidechainerAudioProcessorEditor::configureCombo (juce::ComboBox& combo,
                                                      const juce::String& title,
                                                      int focusOrder)
{
    combo.setWantsKeyboardFocus (true);
    combo.setTitle (title);
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

    auto top = area.removeFromTop (410);
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
    ringmodDuckedButton.setBounds (sidechainArea.removeFromTop (26).withTrimmedLeft (112));
    sidechainArea.removeFromTop (8);
    layoutSlider (ringmodIntensitySlider, sidechainArea);
    amplitudeModulationButton.setBounds (sidechainArea.removeFromTop (26).withTrimmedLeft (112));
    sidechainArea.removeFromTop (8);
    layoutSlider (amplitudeModulationDepthSlider, sidechainArea);
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

void SidechainerAudioProcessorEditor::focusGained (FocusChangeType)
{
    // The host gives the focus to the plug-in's window, and the window to the editor:
    // hand it on to the control used last, or the first one, so Tab and the arrows work at once.
    auto* control = lastFocusedControl.getComponent();

    if (control == nullptr || ! control->isShowing() || ! control->getWantsKeyboardFocus())
        control = &triggerModeBox;

    control->grabKeyboardFocus();
}

std::unique_ptr<juce::AccessibilityHandler> SidechainerAudioProcessorEditor::createAccessibilityHandler()
{
    // The host's plug-in window is already a group named after the plug-in,
    // so the editor adds no second group and the sections sit directly inside it.
    // This works only because the editor is no focus container: JUCE lists an
    // element's children by focus traversal, which stops at focus containers.
    return createIgnoredAccessibilityHandler (*this);
}

std::unique_ptr<juce::ComponentTraverser> SidechainerAudioProcessorEditor::createKeyboardFocusTraverser()
{
    return std::make_unique<EditorKeyboardTraverser> (*this);
}

void SidechainerAudioProcessorEditor::focusOfChildComponentChanged (FocusChangeType)
{
    auto* focused = juce::Component::getCurrentlyFocusedComponent();

    if (focused != nullptr && focused != this && isParentOf (focused))
        lastFocusedControl = focused;
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
    const auto groupCount = static_cast<int> (focusGroups.size());

    // Move on from the section holding the focus, wherever Tab or a click left it.
    auto index = delta > 0 ? -1 : groupCount;

    for (int i = 0; i < groupCount; ++i)
        if (focusGroups[(size_t) i]->hasKeyboardFocus (true))
            index = i;

    for (int attempt = 0; attempt < groupCount; ++attempt)
    {
        index = (index + delta + groupCount) % groupCount;

        if (auto* group = focusGroups[(size_t) index])
        {
            if (! group->isVisible())
                continue;

            const auto focusWasInGroup = group->hasKeyboardFocus (true);
            group->grabKeyboardFocus();

            if (ScreenReader::readsGroupNameOnFocus && ! focusWasInGroup && group->hasKeyboardFocus (true))
                return;

            ScreenReader::announce (*this, group->getTitle(),
                                    juce::AccessibilityHandler::AnnouncementPriority::medium);
            return;
        }
    }
}
