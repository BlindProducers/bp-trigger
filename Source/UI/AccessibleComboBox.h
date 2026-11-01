#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The keys move through the choices as the list reads, top to bottom, as in Atlas:
// arrows one at a time, Page Up and Page Down ten, Home the first and End the last.
class AccessibleComboBox final : public juce::ComboBox
{
public:
    AccessibleComboBox()
    {
        setWantsKeyboardFocus (true);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto modifiers = key.getModifiers();
        const auto keyCode = key.getKeyCode();

        if (modifiers.isCtrlDown() || modifiers.isAltDown() || modifiers.isCommandDown())
            return juce::ComboBox::keyPressed (key);

        const auto isListKey = keyCode == juce::KeyPress::upKey || keyCode == juce::KeyPress::downKey
                               || keyCode == juce::KeyPress::leftKey || keyCode == juce::KeyPress::rightKey
                               || keyCode == juce::KeyPress::pageUpKey || keyCode == juce::KeyPress::pageDownKey
                               || keyCode == juce::KeyPress::homeKey || keyCode == juce::KeyPress::endKey;

        // The open list passes Left and Right back here; they must not change the choice behind it.
        if (isPopupActive())
            return isListKey;

        auto index = getSelectedItemIndex();

        if (keyCode == juce::KeyPress::homeKey)                                                 index = 0;
        else if (keyCode == juce::KeyPress::endKey)                                             index = getNumItems() - 1;
        else if (keyCode == juce::KeyPress::downKey || keyCode == juce::KeyPress::rightKey)     ++index;
        else if (keyCode == juce::KeyPress::upKey || keyCode == juce::KeyPress::leftKey)        --index;
        else if (keyCode == juce::KeyPress::pageDownKey)                                        index += 10;
        else if (keyCode == juce::KeyPress::pageUpKey)                                          index -= 10;
        else                                                                                    return juce::ComboBox::keyPressed (key);

        setSelectedItemIndex (juce::jlimit (0, getNumItems() - 1, index), juce::sendNotificationSync);
        return true;
    }
};
