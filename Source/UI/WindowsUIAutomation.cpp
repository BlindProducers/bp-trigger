// UI Automation events for Windows screen readers. Built without the JUCE
// headers, so <windows.h> and UI Automation stay out of the JUCE sources.

#ifndef NOMINMAX
 #define NOMINMAX
#endif

#include <windows.h>
#include <oleauto.h>
#include <uiautomation.h>

namespace WindowsUIAutomation
{
namespace
{
using ClientsAreListening = BOOL (WINAPI*)();
using RaiseNotificationEvent = HRESULT (WINAPI*) (IRawElementProviderSimple*, NotificationKind, NotificationProcessing, BSTR, BSTR);
using RaisePropertyChangedEvent = HRESULT (WINAPI*) (IRawElementProviderSimple*, PROPERTYID, VARIANT, VARIANT);

struct Functions
{
    ClientsAreListening clientsAreListening = nullptr;
    RaiseNotificationEvent raiseNotificationEvent = nullptr;
    RaisePropertyChangedEvent raisePropertyChangedEvent = nullptr;
};

const Functions& functions()
{
    static const Functions loaded = []
    {
        Functions result;

        if (auto* module = LoadLibraryExW (L"UIAutomationCore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
        {
            result.clientsAreListening = reinterpret_cast<ClientsAreListening> (GetProcAddress (module, "UiaClientsAreListening"));
            // Notifications arrived in Windows 10 version 1709.
            result.raiseNotificationEvent = reinterpret_cast<RaiseNotificationEvent> (GetProcAddress (module, "UiaRaiseNotificationEvent"));
            result.raisePropertyChangedEvent = reinterpret_cast<RaisePropertyChangedEvent> (GetProcAddress (module, "UiaRaiseAutomationPropertyChangedEvent"));
        }

        return result;
    }();

    return loaded;
}

bool clientsAreListening()
{
    const auto& uia = functions();
    return uia.clientsAreListening != nullptr && uia.clientsAreListening() != FALSE;
}

// JUCE's native accessibility element (AccessibilityHandler::getNativeImplementation)
// derives first from IRawElementProviderSimple, so it starts with that interface's IUnknown.
IRawElementProviderSimple* providerFor (void* nativeElement)
{
    IRawElementProviderSimple* provider = nullptr;

    if (nativeElement != nullptr)
        static_cast<IUnknown*> (nativeElement)->QueryInterface (IID_PPV_ARGS (&provider));

    return provider;
}
}

bool raiseNotification (void* nativeElement, const wchar_t* text, bool important)
{
    const auto& uia = functions();

    if (uia.raiseNotificationEvent == nullptr || text == nullptr || *text == L'\0' || ! clientsAreListening())
        return false;

    auto* provider = providerFor (nativeElement);

    if (provider == nullptr)
        return false;

    // NVDA speaks these after what it is saying now; "important" ones go first in its queue.
    auto message = SysAllocString (text);
    auto activity = SysAllocString (L"BP-Trigger");
    const auto result = uia.raiseNotificationEvent (provider,
                                                    important ? NotificationKind_ActionCompleted : NotificationKind_Other,
                                                    important ? NotificationProcessing_ImportantAll : NotificationProcessing_All,
                                                    message,
                                                    activity);
    SysFreeString (activity);
    SysFreeString (message);
    provider->Release();
    return SUCCEEDED (result);
}

bool raiseToggleStateChanged (void* nativeElement, bool isOn)
{
    const auto& uia = functions();

    if (uia.raisePropertyChangedEvent == nullptr || ! clientsAreListening())
        return false;

    auto* provider = providerFor (nativeElement);

    if (provider == nullptr)
        return false;

    VARIANT oldValue, newValue;
    VariantInit (&oldValue);
    VariantInit (&newValue);
    oldValue.vt = VT_I4;
    oldValue.lVal = isOn ? ToggleState_Off : ToggleState_On;
    newValue.vt = VT_I4;
    newValue.lVal = isOn ? ToggleState_On : ToggleState_Off;

    const auto result = uia.raisePropertyChangedEvent (provider, UIA_ToggleToggleStatePropertyId, oldValue, newValue);
    provider->Release();
    return SUCCEEDED (result);
}
}
