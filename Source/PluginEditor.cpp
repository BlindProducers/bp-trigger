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

juce::StringArray choicesOf (juce::RangedAudioParameter& parameter)
{
    const auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&parameter);
    return choice != nullptr ? choice->choices : juce::StringArray();
}

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

SidechainerAudioProcessorEditor::ChoiceAttachment::ChoiceAttachment (juce::RangedAudioParameter& parameter, juce::ComboBox& box)
    : comboBox (box),
      attachment (parameter, [&box] (float choice) { box.setSelectedId (juce::roundToInt (choice) + 1, juce::dontSendNotification); })
{
    comboBox.onChange = [this]
    {
        if (comboBox.getSelectedId() > 0)
            attachment.setValueAsCompleteGesture (static_cast<float> (comboBox.getSelectedId() - 1));
    };

    attachment.sendInitialUpdate();
}

SidechainerAudioProcessorEditor::ChoiceAttachment::~ChoiceAttachment()
{
    comboBox.onChange = nullptr;
}

SidechainerAudioProcessorEditor::SidechainerAudioProcessorEditor (SidechainerAudioProcessor& p)
    : AudioProcessorEditor (&p),
      triggerProcessor (p),
      parameters (p.getValueTreeState()),
      curveEditor (p, false),
      highCurveEditor (p, true)
{
    setSize (960, 1040);
    setWantsKeyboardFocus (true);
    setTitle ("BP-Trigger");

    editTriggerBox.addItemList ({ "Trigger 1", "Trigger 2" }, 1);
    divisionBox.addItemList (choicesOf (*parameters.getParameter ("division")), 1);

    // Without a second sidechain, the list stops before Sidechain 2.
    auto sources = choicesOf (*parameters.getParameter ("source"));
    if (! triggerProcessor.hasSecondSidechain())
        sources.removeRange (3, sources.size() - 3);
    sourceBox.addItemList (sources, 1);

    configureCombo (editTriggerBox, editTriggerLabel, "Edit Trigger", 1);
    configureCombo (triggerModeBox, triggerModeLabel, "Trigger Mode", 2);
    configureCombo (divisionBox, divisionLabel, "Grid Division", 3);
    configureSlider (speedSlider, speedLabel, "Speed", 4);
    configureCombo (sourceBox, sourceLabel, "Source", 5);
    configureSlider (thresholdSlider, thresholdLabel, "Threshold", 6);
    configureSlider (sensitivitySlider, sensitivityLabel, "Detector Release", 7);
    configureToggle (sidechainSoloButton, 8);
    configureSlider (depthSlider, depthLabel, "Depth", 9);
    configureToggle (bandEnabledButton, 10);
    configureToggle (highCurveEnabledButton, 11);
    configureSlider (highDepthSlider, highDepthLabel, "High Depth", 12);

    configureSlider (rampDownSlider, rampDownLabel, "Ramp Down", 1);
    configureSlider (rampShapeSlider, rampShapeLabel, "Ramp Shape", 2);
    configureSlider (preduckSlider, preduckLabel, "Pre-duck", 3);

    configureToggle (ringmodEnabledButton, 1);
    configureToggle (ringmodDuckedButton, 2);
    configureSlider (ringmodIntensitySlider, ringmodIntensityLabel, "Ringmod Intensity", 3);
    configureToggle (amplitudeModulationButton, 4);
    configureSlider (amplitudeModulationDepthSlider, amplitudeModulationDepthLabel, "Amplitude Modulation Depth", 5);

    configureSlider (bandCutoffSlider, bandCutoffLabel, "Band Cutoff", 1);
    configureSlider (mixSlider, mixLabel, "Mix", 2);

    // The arrows move these through the units they are shown in: decibels, and milliseconds of release.
    thresholdSlider.setDisplayUnit ([] (double gain) { return (double) juce::Decibels::gainToDecibels ((float) gain); },
                                    [] (double dB) { return (double) juce::Decibels::decibelsToGain ((float) dB); });
    sensitivitySlider.setDisplayUnit (SidechainerAudioProcessor::detectorReleaseMsFromSensitivity,
                                      SidechainerAudioProcessor::sensitivityFromDetectorReleaseMs);

    const auto addTo = [] (Group& group, std::initializer_list<juce::Component*> children)
    {
        for (auto* child : children)
            group.addAndMakeVisible (child);
    };

    addTo (triggerGroup, { &editTriggerBox, &triggerModeBox, &divisionBox, &speedSlider, &sourceBox, &thresholdSlider,
                           &sensitivitySlider, &sidechainSoloButton, &depthSlider, &bandEnabledButton,
                           &highCurveEnabledButton, &highDepthSlider });
    addTo (rampGroup, { &rampDownSlider, &rampShapeSlider, &preduckSlider });
    addTo (effectsGroup, { &ringmodEnabledButton, &ringmodDuckedButton, &ringmodIntensitySlider,
                           &amplitudeModulationButton, &amplitudeModulationDepthSlider });
    addTo (outputGroup, { &bandCutoffSlider, &mixSlider });

    focusGroups = { &triggerGroup, &rampGroup, &effectsGroup, &outputGroup, &curveEditor, &highCurveEditor };

    for (size_t i = 0; i < focusGroups.size(); ++i)
    {
        focusGroups[i]->setExplicitFocusOrder (static_cast<int> (i) + 1);
        addAndMakeVisible (focusGroups[i]);
    }

    editTriggerBox.onChange = [this] { bindTrigger (editTriggerBox.getSelectedId() - 1); };
    bindTrigger (triggerProcessor.getEditTrigger());
    triggerProcessor.addChangeListener (this);

    highCurveEnabledButton.onStateChange = [this] { updateHighCurveVisibility(); };
}

SidechainerAudioProcessorEditor::~SidechainerAudioProcessorEditor()
{
    triggerProcessor.removeChangeListener (this);
}

void SidechainerAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // A project was loaded; it may show another trigger.
    if (triggerProcessor.getEditTrigger() != boundTrigger)
        bindTrigger (triggerProcessor.getEditTrigger());
}

void SidechainerAudioProcessorEditor::bindTrigger (int trigger)
{
    if (! juce::isPositiveAndBelow (trigger, SidechainerAudioProcessor::triggerCount))
        return;

    boundTrigger = trigger;
    triggerProcessor.setEditTrigger (trigger);
    editTriggerBox.setSelectedId (trigger + 1, juce::dontSendNotification);

    const auto parameter = [this, trigger] (const char* trigger1Id) -> juce::RangedAudioParameter&
    {
        return *parameters.getParameter (SidechainerAudioProcessor::triggerParameterId (trigger, trigger1Id));
    };
    const auto id = [trigger] (const char* trigger1Id) { return SidechainerAudioProcessor::triggerParameterId (trigger, trigger1Id); };

    // Each control lets go of the other trigger's parameter before taking this one's, so neither moves the other.
    triggerModeAttachment.reset();
    triggerModeBox.clear (juce::dontSendNotification);
    triggerModeBox.addItemList (choicesOf (parameter ("triggerMode")), 1);
    triggerModeAttachment = std::make_unique<ChoiceAttachment> (parameter ("triggerMode"), triggerModeBox);

    divisionAttachment.reset();
    divisionAttachment = std::make_unique<ChoiceAttachment> (parameter ("division"), divisionBox);
    sourceAttachment.reset();
    sourceAttachment = std::make_unique<ChoiceAttachment> (parameter ("source"), sourceBox);
    for (auto [attachment, button, trigger1Id] : { std::tuple { &sidechainSoloAttachment, &sidechainSoloButton, "sidechainSolo" },
                                                   std::tuple { &bandEnabledAttachment, &bandEnabledButton, "bandEnabled" },
                                                   std::tuple { &highCurveEnabledAttachment, &highCurveEnabledButton, "highCurveEnabled" } })
    {
        attachment->reset();
        *attachment = std::make_unique<ButtonAttachment> (parameters, id (trigger1Id), *button);
    }

    for (auto [attachment, slider, trigger1Id] : { std::tuple { &speedAttachment, &speedSlider, "speed" },
                                                   std::tuple { &thresholdAttachment, &thresholdSlider, "threshold" },
                                                   std::tuple { &sensitivityAttachment, &sensitivitySlider, "sensitivity" },
                                                   std::tuple { &depthAttachment, &depthSlider, "depth" },
                                                   std::tuple { &highDepthAttachment, &highDepthSlider, "highDepth" } })
    {
        attachment->reset();
        *attachment = std::make_unique<SliderAttachment> (parameters, id (trigger1Id), *slider);
    }

    curveEditor.setTrigger (trigger);
    highCurveEditor.setTrigger (trigger);
    updateHighCurveVisibility();
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
                                                      juce::Label& label,
                                                      const juce::String& title,
                                                      int focusOrder)
{
    combo.setWantsKeyboardFocus (true);
    combo.setTitle (title);
    combo.setExplicitFocusOrder (focusOrder);

    label.setAccessible (false);
    label.setText (title, juce::dontSendNotification);
    label.attachToComponent (&combo, true);
    label.setJustificationType (juce::Justification::centredRight);
}

void SidechainerAudioProcessorEditor::configureToggle (juce::Button& button, int focusOrder)
{
    button.setWantsKeyboardFocus (true);
    button.setTitle (button.getButtonText());
    button.setExplicitFocusOrder (focusOrder);
}

void SidechainerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xfff0f2ef));
    g.setColour (juce::Colour (0xff181816));
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("BP-Trigger", 22, 14, getWidth() - 44, 30, juce::Justification::centredLeft);
}

void SidechainerAudioProcessorEditor::resized()
{
    constexpr auto labelWidth = 110;
    constexpr auto gap = 12;

    auto area = getLocalBounds().reduced (22);
    area.removeFromTop (40);

    auto top = area.removeFromTop (524);
    triggerGroup.setBounds (top.removeFromLeft (330));
    top.removeFromLeft (gap);
    auto middle = top.removeFromLeft (300);
    rampGroup.setBounds (middle.removeFromTop (178));
    middle.removeFromTop (gap);
    effectsGroup.setBounds (middle.removeFromTop (250));
    top.removeFromLeft (gap);
    outputGroup.setBounds (top.removeFromTop (138));

    const auto contentOf = [] (Group& group) { return group.getLocalBounds().reduced (12).withTrimmedTop (34); };
    const auto place = [] (juce::Component& control, juce::Rectangle<int>& groupArea, int height, int indent)
    {
        control.setBounds (groupArea.removeFromTop (height).withTrimmedLeft (indent));
        groupArea.removeFromTop (8);
    };

    auto triggerArea = contentOf (triggerGroup);
    for (auto* combo : { &editTriggerBox, &triggerModeBox, &divisionBox })
        place (*combo, triggerArea, 28, labelWidth);
    place (speedSlider, triggerArea, 34, labelWidth);
    place (sourceBox, triggerArea, 28, labelWidth);
    place (thresholdSlider, triggerArea, 34, labelWidth);
    place (sensitivitySlider, triggerArea, 34, labelWidth);
    place (sidechainSoloButton, triggerArea, 26, labelWidth);
    place (depthSlider, triggerArea, 34, labelWidth);
    place (bandEnabledButton, triggerArea, 26, labelWidth);
    place (highCurveEnabledButton, triggerArea, 26, labelWidth);
    if (highDepthSlider.isVisible())
        place (highDepthSlider, triggerArea, 34, labelWidth);

    auto rampArea = contentOf (rampGroup);
    for (auto* slider : { &rampDownSlider, &rampShapeSlider, &preduckSlider })
        place (*slider, rampArea, 34, labelWidth);

    auto effectsArea = contentOf (effectsGroup);
    place (ringmodEnabledButton, effectsArea, 26, labelWidth);
    place (ringmodDuckedButton, effectsArea, 26, labelWidth);
    place (ringmodIntensitySlider, effectsArea, 34, labelWidth);
    place (amplitudeModulationButton, effectsArea, 26, labelWidth);
    place (amplitudeModulationDepthSlider, effectsArea, 34, labelWidth);

    auto outputArea = contentOf (outputGroup);
    place (bandCutoffSlider, outputArea, 34, 86);
    place (mixSlider, outputArea, 34, 86);

    area.removeFromTop (14);
    if (highCurveEditor.isVisible())
    {
        curveEditor.setBounds (area.removeFromTop ((area.getHeight() - gap) / 2));
        area.removeFromTop (gap);
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
        control = &editTriggerBox;

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

            // A curve's editor takes the focus and says its own name, the section's.
            if (dynamic_cast<CurveEditor*> (group) != nullptr)
                return;

            ScreenReader::announce (*this, group->getTitle(),
                                    juce::AccessibilityHandler::AnnouncementPriority::medium);
            return;
        }
    }
}
