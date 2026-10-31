#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Screen reader output that JUCE handles differently on Windows. On macOS
// these do what JUCE does. On Windows, JUCE speaks announcements with the
// SAPI voice rather than the running screen reader, raises no event when a
// toggle flips, and leaves the screen reader on a popup menu after it closes,
// so these raise the UI Automation events that NVDA, JAWS and Narrator read.
namespace ScreenReader
{
    // NVDA, JAWS and Narrator say a group's name as the focus moves into it.
   #if JUCE_WINDOWS
    inline constexpr bool readsGroupNameOnFocus = true;
   #else
    inline constexpr bool readsGroupNameOnFocus = false;
   #endif

    // Speaks text through the running screen reader. On Windows the text is
    // raised on the focused control, or on source when nothing has focus.
    void announce (juce::Component& source,
                   const juce::String& text,
                   juce::AccessibilityHandler::AnnouncementPriority priority);

    // Lets the screen reader say "checked" or "not checked" when a toggle flips.
    void toggleStateChanged (juce::Button& button);

    // Moves the screen reader back to the focused control after a popup menu closes.
    void restoreFocusAfterMenu();
}
