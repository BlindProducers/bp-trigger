#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class AccessibleSlider final : public juce::Slider
{
public:
    AccessibleSlider (double normalStepIn, double fineStepIn)
        : normalStep (normalStepIn), fineStep (fineStepIn)
    {
        setWantsKeyboardFocus (true);
    }

    void setKeyboardSteps (double normalStepIn, double fineStepIn)
    {
        normalStep = normalStepIn;
        fineStep = fineStepIn;
        customStepFunction = nullptr;
    }

    void setCustomStepFunction (std::function<double (double, bool, int)> function)
    {
        customStepFunction = std::move (function);
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

        return juce::Slider::keyPressed (key);
    }

private:
    // More steps than any range holds, so Home and End reach the ends the arrows move towards
    // (Detector Release's top is its longest time, though the slider stores it lowest).
    static constexpr int stepsPastEitherEnd = 1000000;

    void stepBy (int direction, bool fine)
    {
        const juce::Slider::ScopedDragNotification gesture (*this);
        const auto previous = getValue();

        if (customStepFunction != nullptr)
            setValue (customStepFunction (getValue(), fine, direction), juce::sendNotificationSync);
        else
            setValue (getValue() + static_cast<double> (direction) * (fine ? fineStep : normalStep), juce::sendNotificationSync);

        // A new value is announced by the slider itself; at either end nothing changes, so say it again.
        if (juce::exactlyEqual (getValue(), previous))
            if (auto* handler = getAccessibilityHandler())
                handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
    }

    double normalStep = 0.01;
    double fineStep = 0.001;
    std::function<double (double, bool, int)> customStepFunction;
};
