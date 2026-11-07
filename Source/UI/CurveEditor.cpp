#include "CurveEditor.h"
#include "ScreenReader.h"

namespace
{
class GroupHandler final : public juce::AccessibilityHandler
{
public:
    GroupHandler (juce::Component& component, std::function<juce::String()> titleToRead,
                  std::function<juce::String()> descriptionToRead = {})
        : AccessibilityHandler (component, juce::AccessibilityRole::group),
          readTitle (std::move (titleToRead)),
          readDescription (std::move (descriptionToRead)) {}

    juce::String getTitle() const override         { return readTitle(); }
    juce::String getDescription() const override   { return readDescription ? readDescription() : juce::String(); }

private:
    std::function<juce::String()> readTitle, readDescription;
};

void say (juce::Component& source, const juce::String& text)
{
    ScreenReader::announce (source, text, juce::AccessibilityHandler::AnnouncementPriority::high);
}

constexpr auto presetFormat = "BP-Trigger";
constexpr auto presetVersion = 2;
constexpr auto titleHeight = 28;
constexpr auto buttonHeight = 32;
}

CurveEditor::CurveEditor (SidechainerAudioProcessor& p, bool highCurve)
    : processor (p), high (highCurve)
{
    // A section for the screen reader that Tab passes through, as the editor's other sections.
    setFocusContainerType (juce::Component::FocusContainerType::focusContainer);

    shape.setWantsKeyboardFocus (true);
    shape.setExplicitFocusOrder (1);
    addAndMakeVisible (shape);

    presetButton.setWantsKeyboardFocus (true);
    presetButton.setButtonText (high ? "High Curve Presets" : "Curve Presets");
    presetButton.setTitle (presetButton.getButtonText());
    presetButton.setExplicitFocusOrder (2);
    presetButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetButton);

    processor.addChangeListener (this);
    setTrigger (0);
}

CurveEditor::~CurveEditor()
{
    processor.removeChangeListener (this);
}

void CurveEditor::setTrigger (int newTrigger)
{
    trigger = newTrigger;
    title = "Trigger " + juce::String (trigger + 1) + (high ? " High Curve" : " Curve");
    setTitle (title);
    shape.setTitle (title);
    editing.curve = processor.getCurve (slot());
    repaint();
}

void CurveEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    editing.curve = processor.getCurve (slot());
    shape.repaint();
}

void CurveEditor::apply (const CurveEditing::Result& result)
{
    if (result.changed)
        processor.setCurve (slot(), editing.curve);

    shape.repaint();
    say (shape, result.text);
}

void CurveEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xfff7f7f4));
    g.setColour (juce::Colour (0xffd8d8d0));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);
    g.setColour (juce::Colour (0xff181816));
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText (title, getLocalBounds().reduced (14).removeFromTop (titleHeight - 4), juce::Justification::centredLeft);
}

void CurveEditor::resized()
{
    auto area = getLocalBounds().reduced (14);
    area.removeFromTop (titleHeight);
    presetButton.setBounds (area.removeFromBottom (buttonHeight).removeFromLeft (170));
    area.removeFromBottom (10);
    shape.setBounds (area);
}

std::unique_ptr<juce::AccessibilityHandler> CurveEditor::createAccessibilityHandler()
{
    return std::make_unique<GroupHandler> (*this, [this] { return title; });
}

CurveEditor::Shape::Shape (CurveEditor& editorToEdit) : owner (editorToEdit) {}

std::unique_ptr<juce::AccessibilityHandler> CurveEditor::Shape::createAccessibilityHandler()
{
    // A group whose description is the point or value at the cursor, as Tempus's pattern editor.
    return std::make_unique<GroupHandler> (*this,
                                           [this] { return owner.title; },
                                           [this] { return owner.editing.cursorText(); });
}

bool CurveEditor::Shape::keyPressed (const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();
    const auto code = key.getKeyCode();
    const auto character = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    const auto shift = modifiers.isShiftDown();
    // Some keyboards, such as Italian ones, type the brackets with Option.
    const auto lowerCurve = character == '[' || character == '{';
    const auto raiseCurve = character == ']' || character == '}';

    if (modifiers.isCommandDown() || modifiers.isCtrlDown() || (modifiers.isAltDown() && ! lowerCurve && ! raiseCurve))
        return false;

    auto& model = owner.editing;

    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
        owner.apply (model.moveCursor (code == juce::KeyPress::rightKey ? 1 : -1, shift ? 1 : 5));
    else if (code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey)
    {
        const auto direction = code == juce::KeyPress::pageUpKey ? 1 : -1;
        owner.apply (shift ? model.changeValue (direction, 10) : model.moveCursor (direction, 10));
    }
    else if (code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey)
        owner.apply (model.moveCursorTo (code == juce::KeyPress::homeKey ? 0 : 100));
    else if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        owner.apply (model.changeValue (code == juce::KeyPress::upKey ? 1 : -1, shift ? 1 : 5));
    else if (lowerCurve || raiseCurve)
        owner.apply (model.changeTension (raiseCurve ? 1 : -1, shift || character == '{' || character == '}' ? 1 : 5));
    else if (code == juce::KeyPress::backspaceKey || code == juce::KeyPress::deleteKey)
        owner.apply (model.clear());
    else if (character == 'a')
        owner.apply (model.addPoint());
    else if (character == 'e')
        owner.apply (model.removePoint());
    else if (character == ',' || character == '.')
        owner.apply (model.jumpToPoint (character == '.' ? 1 : -1));
    else
        return false;

    return true;
}

void CurveEditor::Shape::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xffecece6));
    g.fillRoundedRectangle (area, 6.0f);

    const auto graph = area.reduced (8.0f);
    const auto x = [&graph] (double position) { return graph.getX() + graph.getWidth() * static_cast<float> (position); };
    const auto y = [&graph] (double value) { return graph.getBottom() - graph.getHeight() * static_cast<float> (value); };

    g.setColour (juce::Colour (0xffbfc5c8));
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine (juce::roundToInt (y (i / 4.0)), graph.getX(), graph.getRight());

    const auto& curve = owner.editing.curve;
    const auto steps = juce::jmax (2, juce::roundToInt (graph.getWidth()));
    juce::Path line;

    for (int i = 0; i <= steps; ++i)
    {
        const auto position = static_cast<double> (i) / steps;
        const juce::Point<float> at { x (position), y (curve.valueAt (position)) };
        i == 0 ? line.startNewSubPath (at) : line.lineTo (at);
    }

    g.setColour (juce::Colour (0xff235b8f));
    g.strokePath (line, juce::PathStrokeType (3.0f));

    for (const auto& point : curve.getPoints())
        g.fillEllipse (x (point.position / 100.0) - 4.0f, y (point.value / 1000.0) - 4.0f, 8.0f, 8.0f);

    if (hasKeyboardFocus (false))
    {
        g.setColour (juce::Colour (0xffc0392b));
        g.drawVerticalLine (juce::roundToInt (x (owner.editing.cursor / 100.0)), graph.getY(), graph.getBottom());
        g.drawRoundedRectangle (area, 6.0f, 2.0f);
    }
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
        menu.addItem (2, "No saved curves", false, false);

    for (int i = 0; i < presetFiles.size(); ++i)
        menu.addItem (firstPresetItemId + i, presetFiles.getReference (i).getFileNameWithoutExtension());

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
                            if (! juce::isPositiveAndBelow (presetIndex, safeThis->presetFiles.size()))
                                return;

                            const auto& file = safeThis->presetFiles.getReference (presetIndex);
                            say (*safeThis, safeThis->loadCurveFromFile (file) ? file.getFileNameWithoutExtension() + " loaded"
                                                                               : "Couldn't load " + file.getFileNameWithoutExtension());
                        });
}

void CurveEditor::saveCurrentCurve()
{
    const auto directory = getPresetDirectory();
    directory.createDirectory();

    const auto name = title.replace (" ", "-");
    fileChooser = std::make_unique<juce::FileChooser> ("Save Curve", directory.getChildFile (name + ".triggercurve"), "*.triggercurve");
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

                                  say (*this, saveCurveToFile (file) ? "Curve saved" : "Couldn't save the curve");
                              });
}

bool CurveEditor::saveCurveToFile (const juce::File& file) const
{
    auto xml = editing.curve.toXml ("TriggerCurve");
    xml.setAttribute ("format", presetFormat);
    xml.setAttribute ("version", presetVersion);
    return file.replaceWithText (xml.toString());
}

bool CurveEditor::loadCurveFromFile (const juce::File& file)
{
    const auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName ("TriggerCurve"))
        return false;

    auto curve = Curve::makeDefault();

    if (xml->getIntAttribute ("version", 1) >= 2)
    {
        if (! curve.loadFromXml (*xml))
            return false;
    }
    else
    {
        // Version 1 saved the 16 even points of the curves of the time, by index.
        std::vector<float> values ((size_t) juce::jmax (0, xml->getIntAttribute ("pointCount")), -1.0f);

        for (auto* point : xml->getChildWithTagNameIterator ("Point"))
        {
            const auto index = point->getIntAttribute ("index", -1);
            if (juce::isPositiveAndBelow (index, (int) values.size()))
                values[(size_t) index] = juce::jlimit (0.0f, 1.0f, static_cast<float> (point->getDoubleAttribute ("value", 1.0)));
        }

        if (values.size() < 2 || std::any_of (values.begin(), values.end(), [] (float value) { return value < 0.0f; }))
            return false;

        curve = Curve::fromEvenPoints (values);
    }

    editing.curve = curve;
    processor.setCurve (slot(), curve);
    shape.repaint();
    return true;
}

juce::File CurveEditor::getPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("BLIND PRODUCERS PLUGINS")
        .getChildFile ("BP-Trigger");
}
