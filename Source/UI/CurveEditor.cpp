#include "CurveEditor.h"
#include "ScreenReader.h"

namespace
{
class GroupHandler final : public juce::AccessibilityHandler
{
public:
    GroupHandler (juce::Component& component, juce::String groupTitle)
        : AccessibilityHandler (component, juce::AccessibilityRole::group),
          title (std::move (groupTitle)) {}

    juce::String getTitle() const override { return title; }

private:
    juce::String title;
};

juce::String sliderPercentText (double value)
{
    return juce::String (juce::roundToInt (value * 100.0)) + "%";
}

double sliderPercentValue (const juce::String& text)
{
    return juce::jlimit (0.0, 1.0, text.retainCharacters ("0123456789.-").getDoubleValue() / 100.0);
}

double snapPercent (double value)
{
    return juce::jlimit (0.0, 1.0, std::round (value * 100.0) / 100.0);
}

int graphHeightFor (int componentHeight)
{
    return juce::jlimit (56, 96, componentHeight / 3);
}
}

CurveEditor::CurveEditor (SidechainerAudioProcessor::APVTS& state,
                          juce::String pointParameterPrefix,
                          juce::String editorTitle)
    : parameters (state),
      pointPrefix (std::move (pointParameterPrefix)),
      title (std::move (editorTitle))
{
    setWantsKeyboardFocus (true);
    // A group for the screen reader that Tab passes through, as the editor's other sections.
    setFocusContainerType (juce::Component::FocusContainerType::focusContainer);
    setTitle (title);

    presetButton.setWantsKeyboardFocus (true);
    presetButton.setTitle ("Curve Presets");
    presetButton.setExplicitFocusOrder (100);
    presetButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetButton);

    for (int i = 0; i < SidechainerAudioProcessor::curvePointCount; ++i)
    {
        auto* slider = new AccessibleSlider (0.01, 0.01);
        slider->setSliderStyle (juce::Slider::LinearVertical);
        slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 20);
        slider->setRange (0.0, 1.0, 0.01);
        slider->setTitle (title + " point " + juce::String (i + 1));
        slider->setExplicitFocusOrder (i + 1);
        slider->textFromValueFunction = sliderPercentText;
        slider->valueFromTextFunction = sliderPercentValue;

        addAndMakeVisible (slider);
        pointSliders.add (slider);
        attachments.push_back (std::make_unique<SliderAttachment> (parameters, curvePointId (i), *slider));
    }

    updatePointDescriptions();
}

void CurveEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll (juce::Colour (0xfff7f7f4));
    g.setColour (juce::Colour (0xffd8d8d0));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    auto graphArea = getLocalBounds().reduced (14);
    graphArea.removeFromTop (28);
    auto graph = graphArea.removeFromTop (graphHeightFor (getHeight())).toFloat();
    g.setColour (juce::Colour (0xffecece6));
    g.fillRoundedRectangle (graph, 6.0f);
    g.setColour (juce::Colour (0xffbfc5c8));
    for (int i = 1; i < 4; ++i)
    {
        const auto y = graph.getY() + graph.getHeight() * static_cast<float> (i) / 4.0f;
        g.drawHorizontalLine (juce::roundToInt (y), graph.getX(), graph.getRight());
    }

    juce::Path curvePath;
    for (int i = 0; i < pointSliders.size(); ++i)
    {
        const auto x = juce::jmap (static_cast<float> (i), 0.0f, static_cast<float> (pointSliders.size() - 1),
                                  graph.getX() + 8.0f, graph.getRight() - 8.0f);
        const auto y = juce::jmap (static_cast<float> (pointSliders[i]->getValue()), 0.0f, 1.0f,
                                  graph.getBottom() - 8.0f, graph.getY() + 8.0f);

        if (i == 0)
            curvePath.startNewSubPath (x, y);
        else
            curvePath.lineTo (x, y);
    }

    g.setColour (juce::Colour (0xff235b8f));
    g.strokePath (curvePath, juce::PathStrokeType (3.0f));

    g.setColour (juce::Colour (0xff181816));
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText (title, getLocalBounds().reduced (14).removeFromTop (24), juce::Justification::centredLeft);
}

void CurveEditor::resized()
{
    auto area = getLocalBounds().reduced (14);
    area.removeFromTop (28 + graphHeightFor (getHeight()) + 8);

    auto buttonRow = area.removeFromBottom (32);
    presetButton.setBounds (buttonRow.removeFromLeft (150));

    area.removeFromBottom (10);
    auto sliderArea = area;
    const auto gap = 4;
    const auto width = juce::jmax (36, (sliderArea.getWidth() - gap * (pointSliders.size() - 1)) / pointSliders.size());

    for (auto* slider : pointSliders)
    {
        slider->setBounds (sliderArea.removeFromLeft (width));
        sliderArea.removeFromLeft (gap);
    }
}

bool CurveEditor::keyPressed (const juce::KeyPress& key)
{
    return juce::Component::keyPressed (key);
}

void CurveEditor::showPresetMenu()
{
    auto directory = getPresetDirectory();
    directory.createDirectory();

    presetFiles.clear();
    directory.findChildFiles (presetFiles, juce::File::findFiles, false, "*.triggercurve");
    presetFiles.sort();

    juce::PopupMenu menu;
    static constexpr int saveItemId = 1;
    static constexpr int firstPresetItemId = 1000;

    menu.addItem (saveItemId, "Save Current Curve...");
    menu.addSeparator();

    if (presetFiles.isEmpty())
    {
        menu.addItem (2, "No saved curves found", false, false);
    }
    else
    {
        for (int i = 0; i < presetFiles.size(); ++i)
            menu.addItem (firstPresetItemId + i, presetFiles.getReference (i).getFileNameWithoutExtension());
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                        [safeThis = juce::Component::SafePointer<CurveEditor> (this)] (int result)
                        {
                            if (safeThis == nullptr)
                                return;

                            if (result == saveItemId)
                            {
                                safeThis->saveCurrentCurve();
                                return;
                            }

                            ScreenReader::restoreFocusAfterMenu();

                            const auto presetIndex = result - firstPresetItemId;
                            if (juce::isPositiveAndBelow (presetIndex, safeThis->presetFiles.size())
                                && safeThis->loadCurveFromFile (safeThis->presetFiles.getReference (presetIndex)))
                            {
                                ScreenReader::announce (*safeThis, safeThis->title + " curve loaded",
                                                        juce::AccessibilityHandler::AnnouncementPriority::high);
                            }
                        });
}

void CurveEditor::saveCurrentCurve()
{
    const auto directory = getPresetDirectory();
    directory.createDirectory();

    fileChooser = std::make_unique<juce::FileChooser> ("Save trigger curve",
                                                       directory.getChildFile (getDefaultPresetName() + ".triggercurve"),
                                                       "*.triggercurve");
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  auto file = chooser.getResult();
                                  if (file == juce::File())
                                      return;

                                  if (! file.hasFileExtension ("triggercurve"))
                                      file = file.withFileExtension (".triggercurve");

                                  if (saveCurveToFile (file))
                                      ScreenReader::announce (*this, title + " curve saved",
                                                              juce::AccessibilityHandler::AnnouncementPriority::high);
                              });
}

bool CurveEditor::saveCurveToFile (juce::File file)
{
    juce::XmlElement xml ("TriggerCurve");
    xml.setAttribute ("format", "BP-Trigger");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("pointCount", SidechainerAudioProcessor::curvePointCount);

    for (int i = 0; i < pointSliders.size(); ++i)
    {
        auto* point = xml.createNewChildElement ("Point");
        point->setAttribute ("index", i);
        point->setAttribute ("value", juce::String (pointSliders[i]->getValue(), 4));
    }

    return file.replaceWithText (xml.toString());
}

bool CurveEditor::loadCurveFromFile (const juce::File& file)
{
    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName ("TriggerCurve"))
        return false;

    std::array<double, SidechainerAudioProcessor::curvePointCount> values {};
    auto loadedPoints = 0;

    for (auto* point = xml->getFirstChildElement(); point != nullptr; point = point->getNextElement())
    {
        if (! point->hasTagName ("Point"))
            continue;

        const auto index = point->getIntAttribute ("index", loadedPoints);
        if (juce::isPositiveAndBelow (index, SidechainerAudioProcessor::curvePointCount))
        {
            values[(size_t) index] = snapPercent (point->getDoubleAttribute ("value", 1.0));
            ++loadedPoints;
        }
    }

    if (loadedPoints != SidechainerAudioProcessor::curvePointCount)
        return false;

    for (int i = 0; i < pointSliders.size(); ++i)
        pointSliders[i]->setValue (values[(size_t) i], juce::sendNotificationSync);

    repaint();
    return true;
}

juce::File CurveEditor::getPresetDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("BLIND PRODUCERS PLUGINS")
        .getChildFile ("BP-Trigger");
}

juce::String CurveEditor::getDefaultPresetName() const
{
    auto name = title.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -_");
    name = name.trim().replace (" ", "-");

    if (name.isEmpty())
        name = "Trigger-Curve";

    return name;
}

void CurveEditor::updatePointDescriptions()
{
    for (int i = 0; i < pointSliders.size(); ++i)
        pointSliders[i]->onValueChange = [this] { repaint(); };
}

juce::String CurveEditor::curvePointId (int index) const
{
    return pointPrefix + juce::String (index).paddedLeft ('0', 2);
}

std::unique_ptr<juce::AccessibilityHandler> CurveEditor::createAccessibilityHandler()
{
    return std::make_unique<GroupHandler> (*this, title);
}
