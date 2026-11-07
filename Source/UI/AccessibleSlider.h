#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <functional>

// Keys, as in Atlas: the arrows move a normal step, Shift with an arrow a fine one,
// Page Up and Page Down ten normal steps, Home the top and End the bottom,
// Backspace or Delete back to the default, and Return opens the value box.
// Steps are in the unit the value is shown in, and land on that step's grid.
class AccessibleSlider final : public juce::Slider,
                               private juce::Label::Listener
{
public:
    AccessibleSlider (double normalStepIn, double fineStepIn)
        : normalStep (normalStepIn), fineStep (fineStepIn)
    {
        setWantsKeyboardFocus (true);
    }

    ~AccessibleSlider() override
    {
        if (auto* box = findValueBox())
            box->removeListener (this);
    }

    // For a value shown in another unit than it is stored in, such as a gain shown in decibels.
    void setDisplayUnit (std::function<double (double)> toUnitFunction,
                         std::function<double (double)> fromUnitFunction)
    {
        toUnit = std::move (toUnitFunction);
        fromUnit = std::move (fromUnitFunction);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        // Compare key codes: KeyPress == keyCode is false whenever Shift is held.
        const auto modifiers = key.getModifiers();
        const auto keyCode = key.getKeyCode();

        if (modifiers.isCtrlDown() || modifiers.isAltDown() || modifiers.isCommandDown())
            return juce::Slider::keyPressed (key);

        if (keyCode == juce::KeyPress::leftKey || keyCode == juce::KeyPress::downKey)
        {
            stepBy (-1, modifiers.isShiftDown());
            return true;
        }

        if (keyCode == juce::KeyPress::rightKey || keyCode == juce::KeyPress::upKey)
        {
            stepBy (1, modifiers.isShiftDown());
            return true;
        }

        // As in Atlas: Page Up and Page Down move ten steps, Home goes to the top and End to the bottom.
        if (keyCode == juce::KeyPress::pageUpKey || keyCode == juce::KeyPress::pageDownKey)
        {
            stepBy (keyCode == juce::KeyPress::pageUpKey ? 10 : -10, false);
            return true;
        }

        if (keyCode == juce::KeyPress::homeKey || keyCode == juce::KeyPress::endKey)
        {
            stepBy (keyCode == juce::KeyPress::homeKey ? stepsPastEitherEnd : -stepsPastEitherEnd, false);
            return true;
        }

        if ((keyCode == juce::KeyPress::backspaceKey || keyCode == juce::KeyPress::deleteKey)
            && isDoubleClickReturnEnabled())
        {
            // The parameter attachment sets the double-click value to the parameter's default.
            setValueAndAnnounce (getDoubleClickReturnValue());
            return true;
        }

        // Return opens the value box. Return there sets the value, and Escape leaves it unchanged.
        if (keyCode == juce::KeyPress::returnKey && isTextBoxEditable())
        {
            if (auto* box = findValueBox())
                box->addListener (this); // Added once: JUCE ignores a listener already in the list.

            showTextBox();
            return true;
        }

        return juce::Slider::keyPressed (key);
    }

private:
    // The value box is the slider's only label child. JUCE may replace it, so it is looked up each time.
    juce::Label* findValueBox() const
    {
        for (auto* child : getChildren())
            if (auto* box = dynamic_cast<juce::Label*> (child))
                return box;

        return nullptr;
    }

    void labelTextChanged (juce::Label*) override {}

    void editorHidden (juce::Label*, juce::TextEditor&) override
    {
        // Called just before the box's editor closes, so the check waits until it has.
        // The focus comes back to the slider, unless Tab or a click moved it to another control.
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<AccessibleSlider> (this)]
        {
            if (safeThis == nullptr)
                return;

            auto* focused = juce::Component::getCurrentlyFocusedComponent();

            if (focused == nullptr || safeThis->isParentOf (focused))
                safeThis->grabKeyboardFocus();
        });
    }

    // More steps than any range holds, so Home and End reach the ends the arrows move towards
    // (Detector Release's top is its longest time, though the slider stores it lowest).
    static constexpr int stepsPastEitherEnd = 1000000;

    double toShown (double value) const     { return toUnit != nullptr ? toUnit (value) : value; }
    double fromShown (double value) const   { return fromUnit != nullptr ? fromUnit (value) : value; }

    void stepBy (int steps, bool fine)
    {
        const auto step = fine ? fineStep : normalStep;
        const auto bottom = toShown (getMinimum()), top = toShown (getMaximum());
        const auto low = std::min (bottom, top), high = std::max (bottom, top);

        // From the next line of the step's grid, so 1.05x goes up to 1.1x and down to 1.0x.
        // A value within half a fine step of a line shows as on it, so it counts as on it:
        // the default threshold of -26.02 dB, shown as -26.0 dB, goes up to -25.0 dB.
        const auto tolerance = 0.5 * fineStep / step;
        const auto lines = toShown (getValue()) / step;
        const auto from = steps > 0 ? std::floor (lines + tolerance) : std::ceil (lines - tolerance);

        setValueAndAnnounce (fromShown (juce::jlimit (low, high, (from + steps) * step)));
    }

    void setValueAndAnnounce (double newValue)
    {
        const juce::Slider::ScopedDragNotification gesture (*this);
        const auto previous = getValue();

        setValue (newValue, juce::sendNotificationSync);

        // A new value is announced by the slider itself; at either end nothing changes, so say it again.
        if (juce::exactlyEqual (getValue(), previous))
            if (auto* handler = getAccessibilityHandler())
                handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
    }

    double normalStep;
    double fineStep;
    std::function<double (double)> toUnit, fromUnit;
};
