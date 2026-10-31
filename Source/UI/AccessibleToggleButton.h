#pragma once

#include "ScreenReader.h"

class AccessibleToggleButton final : public juce::ToggleButton
{
public:
    using juce::ToggleButton::ToggleButton;

private:
    void buttonStateChanged() override
    {
        juce::ToggleButton::buttonStateChanged();

        if (getToggleState() != reportedState)
        {
            reportedState = getToggleState();
            ScreenReader::toggleStateChanged (*this);
        }
    }

    bool reportedState = false;
};
