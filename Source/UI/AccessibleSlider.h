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
        if (key == juce::KeyPress::leftKey || key == juce::KeyPress::downKey)
        {
            stepBy (-1, key.getModifiers().isShiftDown());
            return true;
        }

        if (key == juce::KeyPress::rightKey || key == juce::KeyPress::upKey)
        {
            stepBy (1, key.getModifiers().isShiftDown());
            return true;
        }

        return juce::Slider::keyPressed (key);
    }

private:
    void stepBy (int direction, bool fine)
    {
        if (customStepFunction != nullptr)
        {
            setValue (customStepFunction (getValue(), fine, direction), juce::sendNotificationSync);
            return;
        }

        const auto delta = fine ? fineStep : normalStep;
        setValue (getValue() + static_cast<double> (direction) * delta, juce::sendNotificationSync);
    }

    double normalStep = 0.01;
    double fineStep = 0.001;
    std::function<double (double, bool, int)> customStepFunction;
};
