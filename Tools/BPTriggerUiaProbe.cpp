// UI Automation client for BPTriggerUiTest: what a Windows screen reader is
// told about the editor window. Writes the window's UI Automation tree, then
// every focus change, value or toggle change and notification raised in it,
// one tab-separated line each, until the stop file appears.
//
// BPTriggerUiaProbe <window handle> <log file> <stop file>

#ifndef NOMINMAX
 #define NOMINMAX
#endif

#include <windows.h>
#include <oleauto.h>
#include <share.h>
#include <uiautomation.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <mutex>
#include <string>

namespace
{
std::mutex logLock;
FILE* logFile = nullptr;
DWORD targetProcess = 0;

std::wstring clean (std::wstring text)
{
    for (auto& c : text)
        if (c == L'\t' || c == L'\r' || c == L'\n')
            c = L' ';

    return text;
}

std::wstring fromBstr (BSTR text)
{
    std::wstring result = text != nullptr ? std::wstring (text, SysStringLen (text)) : std::wstring();
    SysFreeString (text);
    return clean (result);
}

void writeLine (const std::wstring& line)
{
    std::lock_guard<std::mutex> lock (logLock);

    if (logFile == nullptr)
        return;

    const auto size = WideCharToMultiByte (CP_UTF8, 0, line.c_str(), (int) line.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8 ((size_t) size, '\0');
    WideCharToMultiByte (CP_UTF8, 0, line.c_str(), (int) line.size(), utf8.data(), size, nullptr, nullptr);
    std::fputs (utf8.c_str(), logFile);
    std::fputc ('\n', logFile);
    std::fflush (logFile);
}

std::wstring controlTypeName (CONTROLTYPEID type)
{
    switch (type)
    {
        case UIA_ButtonControlTypeId:   return L"button";
        case UIA_CheckBoxControlTypeId: return L"check box";
        case UIA_ComboBoxControlTypeId: return L"combo box";
        case UIA_SliderControlTypeId:   return L"slider";
        case UIA_GroupControlTypeId:    return L"grouping";
        case UIA_TextControlTypeId:     return L"text";
        case UIA_EditControlTypeId:     return L"edit";
        case UIA_WindowControlTypeId:   return L"window";
        case UIA_PaneControlTypeId:     return L"pane";
        case UIA_MenuControlTypeId:     return L"menu";
        case UIA_MenuItemControlTypeId: return L"menu item";
        case UIA_ListControlTypeId:     return L"list";
        case UIA_ListItemControlTypeId: return L"list item";
        case UIA_CustomControlTypeId:   return L"custom";
        default:                        return L"type " + std::to_wstring (type);
    }
}

std::wstring cachedName (IUIAutomationElement* element)
{
    BSTR name = nullptr;
    element->get_CachedName (&name);
    return fromBstr (name);
}

std::wstring cachedType (IUIAutomationElement* element)
{
    CONTROLTYPEID type = 0;
    element->get_CachedControlType (&type);
    return controlTypeName (type);
}

bool isFromTarget (IUIAutomationElement* element)
{
    int process = 0;
    element->get_CachedProcessId (&process);
    return (DWORD) process == targetProcess;
}

// What NVDA reads as a control's value: the Value pattern's text when there is one.
std::wstring valueText (IUIAutomationElement* element)
{
    IUIAutomationValuePattern* value = nullptr;

    if (FAILED (element->GetCurrentPatternAs (UIA_ValuePatternId, IID_PPV_ARGS (&value))) || value == nullptr)
        return L"-";

    BSTR text = nullptr;
    value->get_CurrentValue (&text);
    value->Release();
    return fromBstr (text);
}

std::wstring toggleText (IUIAutomationElement* element)
{
    IUIAutomationTogglePattern* toggle = nullptr;

    if (FAILED (element->GetCurrentPatternAs (UIA_TogglePatternId, IID_PPV_ARGS (&toggle))) || toggle == nullptr)
        return L"-";

    ToggleState state = ToggleState_Indeterminate;
    toggle->get_CurrentToggleState (&state);
    toggle->Release();
    return state == ToggleState_On ? L"checked" : (state == ToggleState_Off ? L"not checked" : L"mixed");
}

void writeTree (IUIAutomation* automation, IUIAutomationElement* element, int depth)
{
    BSTR name = nullptr, help = nullptr;
    CONTROLTYPEID type = 0;
    BOOL focusable = FALSE, offscreen = FALSE;
    element->get_CurrentName (&name);
    element->get_CurrentControlType (&type);
    element->get_CurrentHelpText (&help);
    element->get_CurrentIsKeyboardFocusable (&focusable);
    element->get_CurrentIsOffscreen (&offscreen);

    VARIANT fullDescription;
    VariantInit (&fullDescription);
    element->GetCurrentPropertyValue (UIA_FullDescriptionPropertyId, &fullDescription);
    std::wstring descriptionText = fullDescription.vt == VT_BSTR ? clean (std::wstring (fullDescription.bstrVal, SysStringLen (fullDescription.bstrVal)))
                                                                  : std::wstring();
    VariantClear (&fullDescription);

    writeLine (L"TREE\t" + std::to_wstring (depth) + L"\t" + controlTypeName (type) + L"\t" + fromBstr (name)
               + L"\t" + valueText (element) + L"\t" + toggleText (element) + L"\t" + descriptionText
               + L"\t" + fromBstr (help) + L"\t" + (focusable ? L"focusable" : L"-") + L"\t" + (offscreen ? L"offscreen" : L"-"));

    IUIAutomationTreeWalker* walker = nullptr;
    automation->get_RawViewWalker (&walker);

    if (walker == nullptr)
        return;

    IUIAutomationElement* child = nullptr;
    walker->GetFirstChildElement (element, &child);

    while (child != nullptr)
    {
        writeTree (automation, child, depth + 1);
        IUIAutomationElement* next = nullptr;
        walker->GetNextSiblingElement (child, &next);
        child->Release();
        child = next;
    }

    walker->Release();
}

class EventLog final : public IUIAutomationFocusChangedEventHandler,
                       public IUIAutomationPropertyChangedEventHandler,
                       public IUIAutomationNotificationEventHandler
{
public:
    ULONG STDMETHODCALLTYPE AddRef() override   { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override  { return --references; }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID id, void** result) override
    {
        if (id == __uuidof (IUnknown) || id == __uuidof (IUIAutomationFocusChangedEventHandler))
            *result = static_cast<IUIAutomationFocusChangedEventHandler*> (this);
        else if (id == __uuidof (IUIAutomationPropertyChangedEventHandler))
            *result = static_cast<IUIAutomationPropertyChangedEventHandler*> (this);
        else if (id == __uuidof (IUIAutomationNotificationEventHandler))
            *result = static_cast<IUIAutomationNotificationEventHandler*> (this);
        else
        {
            *result = nullptr;
            return E_NOINTERFACE;
        }

        AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent (IUIAutomationElement* sender) override
    {
        if (isFromTarget (sender))
            writeLine (L"FOCUS\t" + cachedType (sender) + L"\t" + cachedName (sender));

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent (IUIAutomationElement* sender, PROPERTYID property, VARIANT newValue) override
    {
        std::wstring propertyName, value;

        switch (property)
        {
            case UIA_ToggleToggleStatePropertyId:
                propertyName = L"ToggleState";
                value = newValue.vt == VT_I4 ? (newValue.lVal == ToggleState_On ? L"checked" : L"not checked") : L"?";
                break;
            case UIA_ValueValuePropertyId:
                propertyName = L"Value";
                value = newValue.vt == VT_BSTR ? clean (std::wstring (newValue.bstrVal, SysStringLen (newValue.bstrVal))) : L"?";
                break;
            case UIA_RangeValueValuePropertyId:
                propertyName = L"RangeValue";
                value = newValue.vt == VT_R8 ? std::to_wstring (newValue.dblVal) : L"?";
                break;
            default:
                propertyName = std::to_wstring (property);
                break;
        }

        writeLine (L"PROPERTY\t" + propertyName + L"\t" + cachedType (sender) + L"\t" + cachedName (sender) + L"\t" + value);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE HandleNotificationEvent (IUIAutomationElement* sender, NotificationKind kind,
                                                       NotificationProcessing processing, BSTR displayString, BSTR activityId) override
    {
        if (! isFromTarget (sender))
            return S_OK;

        const auto text = clean (std::wstring (displayString, SysStringLen (displayString)));
        const auto activity = clean (std::wstring (activityId, SysStringLen (activityId)));
        writeLine (L"NOTIFICATION\t" + std::to_wstring ((int) kind) + L"\t" + std::to_wstring ((int) processing)
                   + L"\t" + text + L"\t" + activity + L"\t" + cachedType (sender) + L"\t" + cachedName (sender));
        return S_OK;
    }

private:
    std::atomic<ULONG> references { 1 };
};
}

int wmain (int argc, wchar_t** argv)
{
    if (argc < 4)
    {
        std::fputs ("usage: BPTriggerUiaProbe <window handle> <log file> <stop file>\n", stderr);
        return 2;
    }

    const auto window = reinterpret_cast<HWND> (static_cast<uintptr_t> (_wcstoui64 (argv[1], nullptr, 10)));
    const std::wstring stopFile = argv[3];

    // Shared, so the test can read the log while the probe writes it.
    logFile = _wfsopen (argv[2], L"wb", _SH_DENYNO);

    if (logFile == nullptr)
        return 3;

    GetWindowThreadProcessId (window, &targetProcess);
    CoInitializeEx (nullptr, COINIT_MULTITHREADED);

    IUIAutomation5* automation = nullptr;

    if (FAILED (CoCreateInstance (__uuidof (CUIAutomation8), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS (&automation))))
    {
        writeLine (L"ERROR\tno UI Automation 5");
        return 4;
    }

    IUIAutomationElement* root = nullptr;

    if (FAILED (automation->ElementFromHandle (window, &root)) || root == nullptr)
    {
        writeLine (L"ERROR\tno element for the window");
        return 5;
    }

    writeTree (automation, root, 0);

    IUIAutomationCacheRequest* cache = nullptr;
    automation->CreateCacheRequest (&cache);
    cache->AddProperty (UIA_NamePropertyId);
    cache->AddProperty (UIA_ControlTypePropertyId);
    cache->AddProperty (UIA_ProcessIdPropertyId);

    EventLog log;
    PROPERTYID properties[] = { UIA_ToggleToggleStatePropertyId, UIA_ValueValuePropertyId, UIA_RangeValueValuePropertyId };
    const auto focusResult = automation->AddFocusChangedEventHandler (cache, &log);
    const auto propertyResult = automation->AddPropertyChangedEventHandlerNativeArray (root, TreeScope_Subtree, cache, &log,
                                                                                      properties, (int) std::size (properties));
    const auto notificationResult = automation->AddNotificationEventHandler (root, TreeScope_Subtree, cache, &log);

    if (FAILED (focusResult) || FAILED (propertyResult) || FAILED (notificationResult))
        writeLine (L"ERROR\tcould not listen for events");

    writeLine (L"READY");

    for (int waited = 0; GetFileAttributesW (stopFile.c_str()) == INVALID_FILE_ATTRIBUTES && waited < 120000; waited += 20)
        Sleep (20);

    automation->RemoveAllEventHandlers();
    cache->Release();
    root->Release();
    automation->Release();
    writeLine (L"END");
    std::fclose (logFile);
    CoUninitialize();
    return 0;
}
