#include "ScreenReader.h"

#if JUCE_WINDOWS
namespace WindowsUIAutomation
{
bool raiseNotification (void* nativeElement, const wchar_t* text, bool important);
bool raiseToggleStateChanged (void* nativeElement, bool isOn);
}

namespace
{
void* nativeElementFor (juce::Component& component)
{
    if (auto* handler = component.getAccessibilityHandler())
        return handler->getNativeImplementation();

    return nullptr;
}
}
#endif

namespace ScreenReader
{
void announce (juce::Component& source,
               const juce::String& text,
               juce::AccessibilityHandler::AnnouncementPriority priority)
{
   #if JUCE_WINDOWS
    auto* target = juce::Component::getCurrentlyFocusedComponent();

    if (target == nullptr)
        target = &source;

    WindowsUIAutomation::raiseNotification (nativeElementFor (*target),
                                            text.toWideCharPointer(),
                                            priority == juce::AccessibilityHandler::AnnouncementPriority::high);
   #else
    juce::ignoreUnused (source);
    juce::AccessibilityHandler::postAnnouncement (text, priority);
   #endif
}

void toggleStateChanged (juce::Button& button)
{
   #if JUCE_WINDOWS
    if (button.isShowing())
        WindowsUIAutomation::raiseToggleStateChanged (nativeElementFor (button), button.getToggleState());
   #else
    juce::ignoreUnused (button);
   #endif
}

void restoreFocusAfterMenu()
{
   #if JUCE_WINDOWS
    if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
        if (auto* handler = focused->getAccessibilityHandler())
            handler->grabFocus();
   #endif
}
}
