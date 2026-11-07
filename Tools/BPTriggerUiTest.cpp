// Accessibility test for the BP-Trigger editor on Windows. Opens the real
// editor in a window that never comes to the front, so a running screen
// reader stays quiet, and drives it from the keyboard as a user would. Beside
// it runs BPTriggerUiaProbe, a UI Automation client like NVDA, which records
// the tree and every focus change, toggle, value and spoken notification the
// editor raises; the test then checks what it heard.
//
// BPTriggerUiTest [--tree]   --tree also prints the UI Automation tree.

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <iostream>

extern "C"
{
    __declspec (dllimport) int __stdcall LockSetForegroundWindow (unsigned int lockCode);
    __declspec (dllimport) int __stdcall EnumChildWindows (void* parent, int (__stdcall* callback) (void*, juce::pointer_sized_int),
                                                           juce::pointer_sized_int parameter);
    __declspec (dllimport) void* __stdcall GetWindow (void* window, unsigned int command);
    __declspec (dllimport) void* __stdcall SetFocus (void* window);
    __declspec (dllimport) juce::pointer_sized_int __stdcall SendMessageW (void* window, unsigned int message,
                                                                           juce::pointer_sized_uint wParam, juce::pointer_sized_int lParam);
}

namespace
{
int failures = 0;
bool tookForeground = false;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;

    if (! ok)
        ++failures;
}

void settle (int milliseconds = 40)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    tookForeground = tookForeground || juce::Process::isForegroundProcess();
}

juce::Component* focused()   { return juce::Component::getCurrentlyFocusedComponent(); }

juce::String titleOf (juce::Component* component)
{
    if (component == nullptr)
        return "(nothing)";

    if (auto* handler = component->getAccessibilityHandler())
        return handler->getTitle();

    return component->getTitle();
}

// What a screen reader names a stop: a value box's text field by its text.
juce::String describe (juce::Component* component)
{
    if (auto* box = dynamic_cast<juce::TextEditor*> (component))
        return "edit " + box->getText();

    return titleOf (component);
}

juce::String valueOf (juce::Component* component)
{
    if (component != nullptr)
        if (auto* handler = component->getAccessibilityHandler())
            if (auto* value = handler->getValueInterface())
                return value->getCurrentValueAsString();

    return {};
}

juce::AccessibilityRole roleOf (juce::Component* component)
{
    if (component != nullptr)
        if (auto* handler = component->getAccessibilityHandler())
            return handler->getRole();

    return juce::AccessibilityRole::unspecified;
}

bool hasScreenReaderFocus (juce::Component* component)
{
    auto* handler = component != nullptr ? component->getAccessibilityHandler() : nullptr;
    return handler != nullptr && handler->hasFocus (false);
}

juce::Component* findTitled (juce::Component& root, const juce::String& title)
{
    if (root.getTitle() == title)
        return &root;

    for (auto* child : root.getChildren())
        if (auto* found = findTitled (*child, title))
            return found;

    return nullptr;
}

bool isInAccessibilityTree (juce::Component& root, const juce::String& title)
{
    std::function<bool (juce::AccessibilityHandler&)> search = [&] (juce::AccessibilityHandler& handler)
    {
        if (handler.getTitle() == title)
            return true;

        for (auto* child : handler.getChildren())
            if (search (*child))
                return true;

        return false;
    };

    auto* handler = root.getAccessibilityHandler();
    return handler != nullptr && search (*handler);
}

// A key pressed in the window, as a user presses it.
bool press (juce::Component& window, const juce::KeyPress& key)
{
    auto used = false;

    if (auto* peer = window.getPeer())
        used = peer->handleKeyPress (key);

    settle();
    return used;
}

// A key with no message loop after it: menus close by themselves in a background
// window, so the keys used in an open menu follow the key that opened it at once.
void pressNow (juce::Component& window, const juce::KeyPress& key)
{
    if (auto* peer = window.getPeer())
        peer->handleKeyPress (key);
}

juce::KeyPress key (int code, juce::ModifierKeys modifiers = {})   { return juce::KeyPress (code, modifiers, 0); }
juce::KeyPress character (juce::juce_wchar c)                      { return juce::KeyPress (static_cast<int> (c), {}, c); }

// Types a value into a slider's value box: Return opens it, type, Return.
void typeInto (juce::Component& window, juce::Component* slider, const juce::String& text)
{
    slider->grabKeyboardFocus();
    settle();
    press (window, key (juce::KeyPress::returnKey));

    if (auto* box = dynamic_cast<juce::TextEditor*> (focused()))
    {
        box->setText (text, juce::dontSendNotification);
        press (window, key (juce::KeyPress::returnKey));
    }
}

int modalCount()   { return juce::ModalComponentManager::getInstance()->getNumModalComponents(); }

bool waitForMenu (int milliseconds = 500)
{
    for (int waited = 0; modalCount() == 0 && waited < milliseconds; waited += 2)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2);

    return modalCount() > 0;
}

// Menus open inside the test window, not as top-most windows of their own,
// which a screen reader reads even from a program in the background.
struct InWindowMenus final : public juce::LookAndFeel_V4
{
    juce::Component* getParentComponentForMenuOptions (const juce::PopupMenu::Options& options) override
    {
        if (auto* target = options.getTargetComponent())
            return target->getTopLevelComponent();

        return LookAndFeel_V4::getParentComponentForMenuOptions (options);
    }
};

struct TestWindow final : public juce::Component
{
    explicit TestWindow (juce::Component& content)
    {
        setOpaque (true);
        setTitle ("BP-Trigger UI test");
        addAndMakeVisible (content);
        setBounds (40, 40, content.getWidth(), content.getHeight());
    }

    void paint (juce::Graphics& g) override   { g.fillAll (juce::Colours::white); }
};

struct Probe
{
    juce::File log, stop;
    juce::ChildProcess process;
    juce::StringArray lines;

    bool start (juce::Component& window)
    {
        const auto folder = juce::File::createTempFile ("bptrigger-uia");
        log = folder.withFileExtension ("log");
        stop = folder.withFileExtension ("stop");
        stop.deleteFile();

        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("BPTriggerUiaProbe.exe");
        const auto handle = juce::String (static_cast<juce::int64> (reinterpret_cast<juce::pointer_sized_int> (window.getWindowHandle())));

        if (! exe.existsAsFile())
        {
            std::cout << "  no probe at " << exe.getFullPathName() << std::endl;
            return false;
        }

        if (! process.start (juce::StringArray { exe.getFullPathName(), handle, log.getFullPathName(), stop.getFullPathName() }, 0))
        {
            std::cout << "  the probe did not start" << std::endl;
            return false;
        }

        // The probe reads the tree through this window's message loop, so keep it running.
        for (int waited = 0; waited < 20000; waited += 20)
        {
            settle (20);

            if (log.loadFileAsString().contains ("READY"))
                return true;

            if (! process.isRunning())
                break;
        }

        std::cout << "  the probe stopped (exit code " << (int) process.getExitCode() << "), log:\n"
                  << log.loadFileAsString().substring (0, 2000) << std::endl;
        return false;
    }

    void finish()
    {
        settle (300);
        stop.create();

        for (int waited = 0; waited < 10000 && process.isRunning(); waited += 20)
            settle (20);

        log.readLines (lines);
        log.deleteFile();
        stop.deleteFile();
    }

    std::vector<juce::StringArray> entries (const juce::String& kind) const
    {
        std::vector<juce::StringArray> found;

        for (auto& line : lines)
        {
            auto fields = juce::StringArray::fromTokens (line, "\t", {});

            if (fields[0] == kind)
                found.push_back (fields);
        }

        return found;
    }

    // A tree element: type, name, value, toggle state.
    juce::StringArray element (const juce::String& type, const juce::String& name) const
    {
        for (auto& fields : entries ("TREE"))
            if (fields[2] == type && fields[3] == name)
                return fields;

        return {};
    }

    int indexOf (const juce::String& kind, const juce::String& field, const juce::String& text, int after = -1) const
    {
        for (int i = after + 1; i < lines.size(); ++i)
        {
            auto fields = juce::StringArray::fromTokens (lines[i], "\t", {});

            if (fields[0] == kind && fields.contains (field) && fields.contains (text))
                return i;
        }

        return -1;
    }
};
}

// The built VST3 loaded as a host loads it, in a background window, and driven with the key
// messages Windows sends a focused plug-in window, as REAPER passes them on.
void checkHostedPlugin (const juce::File& vst3)
{
    std::cout << "\nThe built VST3 in a host:" << std::endl;

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, vst3.getFullPathName());

    juce::String error;
    std::unique_ptr<juce::AudioPluginInstance> plugin;

    if (found.size() == 1)
        plugin = format.createInstanceFromDescription (*found[0], 48000.0, 512, error);

    if (plugin == nullptr)
        return check (false, "the VST3 loads " + error);

    plugin->enableAllBuses();
    plugin->prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> pluginEditor (plugin->createEditorAndMakeActive());

    if (pluginEditor == nullptr)
        return check (false, "the VST3 opens its editor");

    TestWindow host (*pluginEditor);
    host.addToDesktop (juce::ComponentPeer::windowHasTitleBar | juce::ComponentPeer::windowAppearsOnTaskbar);
    host.setVisible (true);
    settle (500);

    // The plug-in's own window is the innermost child window of the host's.
    void* pluginWindow = nullptr;
    EnumChildWindows (host.getWindowHandle(),
                      [] (void* child, juce::pointer_sized_int innermost) -> int
                      {
                          if (GetWindow (child, 5 /* GW_CHILD */) == nullptr)
                              *reinterpret_cast<void**> (innermost) = child;

                          return 1;
                      },
                      reinterpret_cast<juce::pointer_sized_int> (&pluginWindow));

    Probe probe;

    if (pluginWindow == nullptr || ! probe.start (host))
        return check (false, "the plug-in's window is found and the probe listens to it");

    const auto keyMessage = [pluginWindow] (int virtualKey)
    {
        SendMessageW (pluginWindow, 0x0100 /* WM_KEYDOWN */, static_cast<juce::pointer_sized_uint> (virtualKey), 1);
        SendMessageW (pluginWindow, 0x0101 /* WM_KEYUP */, static_cast<juce::pointer_sized_uint> (virtualKey), 0xC0000001);
        settle (80);
    };

    // REAPER focuses the plug-in's window when it opens or is tabbed into.
    SetFocus (pluginWindow);
    settle (300);

    for (auto virtualKey : { 0x09, 0x09,      // Tab, Tab
                             0x24, 0x24,      // Home, Home
                             0x22,            // Page Down
                             0xBE, 0xBE,      // period, period
                             0xBC,            // comma
                             0x09, 0x23 })    // Tab, End
        keyMessage (virtualKey);

    probe.finish();

    juce::StringArray focusOrder;
    for (auto& fields : probe.entries ("FOCUS"))    // FOCUS, type, name
        if (fields[2].isNotEmpty() && fields[2] != focusOrder[focusOrder.size() - 1] && ! fields[2].startsWith ("BP-Trigger"))
            focusOrder.add (fields[2]);

    const auto order = focusOrder.joinIntoString (", ");
    check (order == "Trigger Mode, Grid Division, Speed, Sidechain Solo, Band, Sidechain Solo, Ringmod Sidechain",
           "focus on opening, then Tab, period and comma: " + order);

    juce::StringArray speedValues;
    for (auto& fields : probe.entries ("PROPERTY"))
        if (fields[1] == "RangeValue" && fields[3] == "Speed")
            speedValues.add (fields[4]);
    check (speedValues.joinIntoString (", ") == "4.000000, 4.000000, 3.900000",
           "Home, Home again and Page Down on Speed: " + speedValues.joinIntoString (", "));

    auto* speedParameter = [&plugin]() -> juce::AudioProcessorParameter*
    {
        for (auto* parameter : plugin->getParameters())
            if (parameter->getName (64) == "Speed")
                return parameter;

        return nullptr;
    }();
    check (speedParameter != nullptr && speedParameter->getCurrentValueAsText() == "3.90x",
           "and the host sees Speed at " + (speedParameter != nullptr ? speedParameter->getCurrentValueAsText() : juce::String()));

    const auto ringmodChanged = probe.indexOf ("PROPERTY", "ToggleState", "Ringmod Sidechain") >= 0;
    check (! ringmodChanged, "End on a check box changes nothing");

    std::cout << "\n  Events, in order:" << std::endl;
    for (auto& line : probe.lines)
        if (! line.startsWith ("TREE"))
            std::cout << "    " << line.replace ("\t", " | ") << std::endl;

    host.removeFromDesktop();
    pluginEditor = nullptr;
    plugin->releaseResources();
}

int main (int argc, char* argv[])
{
    const juce::StringArray arguments (argv + 1, argc - 1);
    const auto printTree = arguments.contains ("--tree");
    const auto vst3 = arguments.contains ("--vst3")
                          ? juce::File::getCurrentWorkingDirectory().getChildFile (arguments[arguments.indexOf ("--vst3") + 1])
                          : juce::File();
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    InWindowMenus menuLook;
    juce::LookAndFeel::setDefaultLookAndFeel (&menuLook);

    // Shown without being activated, and with foreground changes locked while
    // the test runs, so the screen reader stays on whatever is in front.
    LockSetForegroundWindow (1);

    const auto presetFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                  .getChildFile ("BLIND PRODUCERS PLUGINS")
                                  .getChildFile ("BP-Trigger");
    const auto companyFolderExisted = presetFolder.getParentDirectory().isDirectory();
    const auto presetFolderExisted = presetFolder.isDirectory();

    SidechainerAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    auto& state = processor.getValueTreeState();

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    TestWindow window (*editor);
    window.addToDesktop (juce::ComponentPeer::windowHasTitleBar | juce::ComponentPeer::windowAppearsOnTaskbar);
    window.setVisible (true);
    settle (400);

    auto* triggerMode = findTitled (*editor, "Trigger Mode");
    auto* speed = findTitled (*editor, "Speed");
    auto* threshold = findTitled (*editor, "Sidechain Threshold");
    auto* cutoff = findTitled (*editor, "Band Cutoff");
    auto* solo = findTitled (*editor, "Sidechain Solo");
    auto* ringmod = findTitled (*editor, "Ringmod Sidechain");
    auto* highCurve = findTitled (*editor, "High Curve");
    auto* mainCurve = findTitled (*editor, "Main / Low Curve Editor");
    auto* firstPoint = findTitled (*editor, "Main / Low Curve Editor point 1");
    auto* presets = mainCurve != nullptr ? findTitled (*mainCurve, "Curve Presets") : nullptr;

    if (triggerMode == nullptr || speed == nullptr || threshold == nullptr || cutoff == nullptr || solo == nullptr
        || ringmod == nullptr || highCurve == nullptr || mainCurve == nullptr || firstPoint == nullptr || presets == nullptr)
    {
        std::cout << "The editor is missing controls." << std::endl;
        return 1;
    }

    std::cout << "What a screen reader finds:" << std::endl;
    {
        check (titleOf (editor.get()) == "BP-Trigger", "the editor is titled BP-Trigger");

        auto groupsOk = true;
        for (auto* title : { "Timing", "Sidechain", "Output", "Main / Low Curve Editor" })
            groupsOk = groupsOk && roleOf (findTitled (*editor, title)) == juce::AccessibilityRole::group && isInAccessibilityTree (window, title);
        check (groupsOk, "four named groups: Timing, Sidechain, Output, Main / Low Curve Editor");
        check (! isInAccessibilityTree (window, "High Curve Editor") && ! isInAccessibilityTree (window, "High Depth"),
               "the High Curve Editor and High Depth stay hidden until High Curve is on");

        check (roleOf (triggerMode) == juce::AccessibilityRole::comboBox && valueOf (triggerMode) == "Grid"
                   && valueOf (findTitled (*editor, "Grid Division")) == "1/4",
               "Trigger Mode and Grid Division are combo boxes reading Grid and 1/4");
        check (roleOf (speed) == juce::AccessibilityRole::slider && valueOf (speed) == "1.00x" && valueOf (threshold) == "-26.0 dB"
                   && valueOf (findTitled (*editor, "Detector Release")) == "50 ms" && valueOf (cutoff) == "300 Hz"
                   && valueOf (findTitled (*editor, "Depth")) == "100%" && valueOf (findTitled (*editor, "Ramp Down")) == "0.0 ms",
               "sliders read their values with units: " + valueOf (speed) + ", " + valueOf (threshold) + ", " + valueOf (cutoff));
        check (roleOf (solo) == juce::AccessibilityRole::toggleButton && ! solo->getAccessibilityHandler()->getCurrentState().isChecked(),
               "Sidechain Solo is a check box, not checked");

        auto pointsOk = true;
        for (int i = 1; i <= 16; ++i)
            pointsOk = pointsOk && roleOf (findTitled (*mainCurve, "Main / Low Curve Editor point " + juce::String (i))) == juce::AccessibilityRole::slider;
        check (pointsOk && valueOf (firstPoint) == "0%" && valueOf (findTitled (*mainCurve, "Main / Low Curve Editor point 16")) == "100%",
               "the curve has 16 point sliders, from 0% to 100%");
    }

    Probe probe;
    const auto probeRunning = probe.start (window);
    check (probeRunning, "the UI Automation probe is listening to the window");

    std::cout << "\nMoving around:" << std::endl;
    {
        // A host giving the plug-in's window the focus, as REAPER does when it opens.
        window.grabKeyboardFocus();
        settle();
        check (focused() == triggerMode, "opening the plug-in lands on Trigger Mode (" + describe (focused()) + ")");

        // Tab runs through every control in every section, skipping the value boxes, then wraps.
        juce::StringArray expected { "Grid Division", "Speed", "Ramp Down", "Ramp Shape",
                                     "Sidechain Solo", "Ringmod Sidechain", "Ringmod Intensity", "Pre-duck",
                                     "Sidechain Threshold", "Detector Release",
                                     "Band", "High Curve", "Band Cutoff", "Depth", "Mix",
                                     "Main / Low Curve Editor" };
        for (int i = 1; i <= 16; ++i)
            expected.add ("Main / Low Curve Editor point " + juce::String (i));
        expected.add ("Curve Presets");
        expected.add ("Trigger Mode");

        juce::StringArray order;
        for (int i = 0; i < expected.size() + 5; ++i)
        {
            press (window, key (juce::KeyPress::tabKey));
            order.add (describe (focused()));

            if (focused() == triggerMode)
                break;
        }
        check (order == expected, "Tab runs through all " + juce::String (expected.size()) + " stops in every section and wraps");
        if (order != expected)
            std::cout << "         " << order.joinIntoString (", ") << std::endl;

        press (window, key (juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier));
        check (titleOf (focused()) == "Curve Presets", "Shift+Tab from the first control wraps to the last (" + describe (focused()) + ")");

        // Comma and period move from the section holding the focus, wherever Tab or a click left it.
        triggerMode->grabKeyboardFocus();
        settle();
        press (window, character ('.'));
        check (focused() == solo, "period from Timing: Sidechain (" + describe (focused()) + ")");
        press (window, character ('.'));
        check (titleOf (focused()) == "Band", "period again: Output (" + describe (focused()) + ")");
        press (window, character ('.'));
        check (focused() == mainCurve, "period again: the curve editor (" + describe (focused()) + ")");
        press (window, character ('.'));
        check (focused() == triggerMode, "period again wraps to Timing, skipping the hidden high curve (" + describe (focused()) + ")");
        press (window, character (','));
        check (focused() == mainCurve, "comma from Timing wraps back to the curve editor");

        ringmod->grabKeyboardFocus();
        settle();
        press (window, character ('.'));
        check (titleOf (focused()) == "Band", "after a click on Ringmod Sidechain, period goes on to Output (" + describe (focused()) + ")");

        findTitled (*editor, "Detector Release")->grabKeyboardFocus();
        settle();
        press (window, key (juce::KeyPress::tabKey));
        check (titleOf (focused()) == "Band", "Tab from Sidechain's last stop goes on to Output (" + describe (focused()) + ")");
        press (window, character (','));
        check (focused() == solo, "and comma from there goes back to Sidechain (" + describe (focused()) + ")");

        // Return opens the value box; typing a value and Return sets the slider and goes back to it.
        auto* speedBox = [&]() -> juce::TextEditor*
        {
            speed->grabKeyboardFocus();
            settle();
            press (window, key (juce::KeyPress::returnKey));
            return dynamic_cast<juce::TextEditor*> (focused());
        }();
        if (speedBox != nullptr)
        {
            speedBox->setText ("2", juce::dontSendNotification);
            press (window, key (juce::KeyPress::returnKey));
        }
        check (speedBox != nullptr && valueOf (speed) == "2.00x", "Return, 2 and Return in Speed sets " + valueOf (speed));
        check (focused() == speed, "and the focus is back on Speed (" + describe (focused()) + ")");
        typeInto (window, speed, "1");
        check (valueOf (speed) == "1.00x", "and typing 1 sets it back to " + valueOf (speed));
    }

    std::cout << "\nChanging values:" << std::endl;
    {
        speed->grabKeyboardFocus();
        settle();
        press (window, key (juce::KeyPress::rightKey));
        check (valueOf (speed) == "1.10x", "Right arrow: Speed " + valueOf (speed));
        press (window, key (juce::KeyPress::leftKey));
        check (valueOf (speed) == "1.00x", "Left arrow: Speed " + valueOf (speed));
        press (window, key (juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier));
        check (valueOf (speed) == "1.01x", "Shift+Right: a tenth, Speed " + valueOf (speed));
        press (window, key (juce::KeyPress::backspaceKey));
        check (valueOf (speed) == "1.00x", "Backspace: back to the default, Speed " + valueOf (speed));

        threshold->grabKeyboardFocus();
        settle();
        press (window, key (juce::KeyPress::upKey));
        check (valueOf (threshold) == "-25.0 dB", "Up arrow: one decibel, " + valueOf (threshold));
        press (window, key (juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier));
        check (valueOf (threshold) == "-24.9 dB", "Shift+Up: a tenth, " + valueOf (threshold));

        cutoff->grabKeyboardFocus();
        settle();
        press (window, key (juce::KeyPress::upKey));
        press (window, key (juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier));
        check (valueOf (cutoff) == "410 Hz", "Band Cutoff: 100 Hz up, then 10 Hz with Shift, " + valueOf (cutoff));

        triggerMode->grabKeyboardFocus();
        settle();
        press (window, key (juce::KeyPress::downKey));
        settle (100);
        check (valueOf (triggerMode) == "Sidechain" && juce::roundToInt (state.getRawParameterValue ("triggerMode")->load()) == 1,
               "Down arrow on Trigger Mode: " + valueOf (triggerMode));
        press (window, key (juce::KeyPress::upKey));
        settle (100);
        check (valueOf (triggerMode) == "Grid", "Up arrow: back to Grid");

        firstPoint->grabKeyboardFocus();
        settle();
        press (window, key (juce::KeyPress::upKey));
        check (valueOf (firstPoint) == "1%" && std::abs (state.getRawParameterValue ("curvePoint00")->load() - 0.01f) < 1.0e-4f,
               "Up arrow on point 1: " + valueOf (firstPoint));

        // As in Atlas: Home goes to the top, End to the bottom, Page Up and Page Down move ten steps
        // from the step's grid.
        const auto homeEndPage = [&window] (juce::Component* slider, int pagesAfterwards)
        {
            slider->grabKeyboardFocus();
            settle();
            juce::StringArray values;
            for (auto code : { juce::KeyPress::homeKey, juce::KeyPress::homeKey, juce::KeyPress::endKey })
            {
                press (window, key (code));
                values.add (valueOf (slider));
            }
            for (int i = 0; i < pagesAfterwards; ++i)
                press (window, key (juce::KeyPress::pageUpKey));
            values.add (valueOf (slider));
            return values.joinIntoString (", ");
        };

        auto values = homeEndPage (speed, 1);
        check (values == "4.00x, 4.00x, 0.25x, 1.20x", "Speed: Home, Home again, End, Page Up: " + values);
        typeInto (window, speed, "1");

        values = homeEndPage (threshold, 1);
        check (values == "0.0 dB, 0.0 dB, -40.0 dB, -30.0 dB", "Sidechain Threshold: " + values);
        threshold->grabKeyboardFocus();
        settle();
        press (window, key (juce::KeyPress::pageDownKey));
        check (valueOf (threshold) == "-40.0 dB", "Page Down: ten decibels down, " + valueOf (threshold));
        typeInto (window, threshold, "-26");

        // Its slider stores the longest time lowest; the keys still follow the time.
        auto* release = findTitled (*editor, "Detector Release");
        values = homeEndPage (release, 1);
        check (values == "100 ms, 100 ms, 1 ms, 11 ms", "Detector Release: " + values);
        typeInto (window, release, "50");

        values = homeEndPage (cutoff, 1);
        check (values == "20 kHz, 20 kHz, 20 Hz, 1 kHz", "Band Cutoff: " + values);
        typeInto (window, cutoff, "300");

        values = homeEndPage (firstPoint, 2);
        check (values == "100%, 100%, 0%, 20%", "curve point 1: " + values);

        check (valueOf (speed) == "1.00x" && valueOf (threshold) == "-26.0 dB" && valueOf (release) == "50 ms" && valueOf (cutoff) == "300 Hz",
               "typed values set them back: " + valueOf (speed) + ", " + valueOf (threshold) + ", " + valueOf (release) + ", " + valueOf (cutoff));

        // The lists, top to bottom as they read.
        auto* division = findTitled (*editor, "Grid Division");
        const auto listKeys = [&window] (juce::Component* list)
        {
            list->grabKeyboardFocus();
            settle();
            juce::StringArray read;
            for (auto code : { juce::KeyPress::endKey, juce::KeyPress::homeKey, juce::KeyPress::pageDownKey, juce::KeyPress::pageUpKey })
            {
                press (window, key (code));
                read.add (valueOf (list));
            }
            return read.joinIntoString (", ");
        };
        values = listKeys (triggerMode);
        check (values == "Grid and Sidechain, Grid, Grid and Sidechain, Grid", "Trigger Mode: End, Home, Page Down, Page Up: " + values);
        values = listKeys (division);
        check (values == "1/16, 1 bar, 1/16, 1 bar", "Grid Division: " + values);
        for (int i = 0; i < 3; ++i)
            press (window, key (juce::KeyPress::downKey));
        check (valueOf (division) == "1/4", "Down three times: " + valueOf (division));

        // An open list passes Left and Right back to the combo box behind it.
        triggerMode->grabKeyboardFocus();
        settle();
        pressNow (window, key (juce::KeyPress::returnKey));
        const auto listOpened = waitForMenu();
        pressNow (window, key (juce::KeyPress::rightKey));
        pressNow (window, key (juce::KeyPress::rightKey));
        pressNow (window, key (juce::KeyPress::escapeKey));
        settle (300);
        check (listOpened && valueOf (triggerMode) == "Grid" && hasScreenReaderFocus (triggerMode),
               "Left and Right in the open list leave the choice alone (" + valueOf (triggerMode) + "), and Escape returns to it");
    }

    std::cout << "\nCheck boxes:" << std::endl;
    {
        solo->grabKeyboardFocus();
        settle();
        press (window, key (juce::KeyPress::returnKey));
        settle (100);
        check (solo->getAccessibilityHandler()->getCurrentState().isChecked() && state.getRawParameterValue ("sidechainSolo")->load() > 0.5f,
               "Enter turns Sidechain Solo on");
        press (window, key (juce::KeyPress::returnKey));
        settle (100);
        check (! solo->getAccessibilityHandler()->getCurrentState().isChecked(), "Enter again turns it off");

        highCurve->grabKeyboardFocus();
        settle();
        press (window, key (juce::KeyPress::returnKey));
        settle (100);
        check (isInAccessibilityTree (window, "High Curve Editor") && isInAccessibilityTree (window, "High Depth"),
               "High Curve shows the High Curve Editor and High Depth");

        press (window, character ('.'));
        press (window, character ('.'));
        auto* highCurveEditor = findTitled (*editor, "High Curve Editor");
        check (highCurveEditor != nullptr && focused() == highCurveEditor,
               "period twice from High Curve reaches the High Curve Editor (" + titleOf (focused()) + ")");
        press (window, character ('.'));
        check (focused() == triggerMode, "and period from there wraps to Timing");
    }

    std::cout << "\nCurve presets:" << std::endl;
    {
        presets->grabKeyboardFocus();
        settle();
        pressNow (window, key (juce::KeyPress::returnKey));
        const auto opened = waitForMenu();
        pressNow (window, key (juce::KeyPress::escapeKey));
        settle (300);
        check (opened && modalCount() == 0, "Enter opens the Curve Presets menu, Escape closes it");
        check (focused() == presets && hasScreenReaderFocus (presets), "the screen reader is back on Curve Presets");

        presetFolder.createDirectory();
        const auto testPreset = presetFolder.getChildFile ("BP-Trigger UI test.triggercurve");
        juce::XmlElement curve ("TriggerCurve");
        curve.setAttribute ("format", "BP-Trigger");
        curve.setAttribute ("version", 1);
        curve.setAttribute ("pointCount", 16);
        for (int i = 0; i < 16; ++i)
        {
            auto* point = curve.createNewChildElement ("Point");
            point->setAttribute ("index", i);
            point->setAttribute ("value", "0.5");
        }
        testPreset.replaceWithText (curve.toString());

        juce::Array<juce::File> files;
        presetFolder.findChildFiles (files, juce::File::findFiles, false, "*.triggercurve");
        files.sort();
        const auto presetIndex = files.indexOf (testPreset);

        pressNow (window, key (juce::KeyPress::returnKey));
        const auto menuOpened = waitForMenu();
        for (int i = 0; i < presetIndex + 2; ++i)
            pressNow (window, key (juce::KeyPress::downKey));
        pressNow (window, key (juce::KeyPress::returnKey));
        settle (300);
        testPreset.deleteFile();

        check (menuOpened && valueOf (firstPoint) == "50%" && valueOf (findTitled (*mainCurve, "Main / Low Curve Editor point 16")) == "50%",
               "choosing a saved curve loads it (point 1 now " + valueOf (firstPoint) + ")");
        check (focused() == presets && hasScreenReaderFocus (presets), "and the screen reader is back on Curve Presets");
    }

    if (probeRunning)
    {
        probe.finish();
        std::cout << "\nWhat UI Automation told the screen reader:" << std::endl;

        for (auto& line : probe.lines)
            if (line.startsWith ("ERROR"))
                check (false, line);

        const auto tree = [&probe] (const juce::String& type, const juce::String& name) { return probe.element (type, name); };
        check (tree ("grouping", "Timing").size() > 0 && tree ("grouping", "Sidechain").size() > 0
                   && tree ("grouping", "Output").size() > 0 && tree ("grouping", "Main / Low Curve Editor").size() > 0,
               "groupings: Timing, Sidechain, Output, Main / Low Curve Editor");
        check (tree ("combo box", "Trigger Mode")[4] == "Grid" && tree ("combo box", "Grid Division")[4] == "1/4",
               "combo boxes: Trigger Mode \"" + tree ("combo box", "Trigger Mode")[4] + "\", Grid Division \"" + tree ("combo box", "Grid Division")[4] + "\"");
        check (tree ("slider", "Speed")[4] == "1.00x" && tree ("slider", "Sidechain Threshold")[4] == "-26.0 dB"
                   && tree ("slider", "Detector Release")[4] == "50 ms" && tree ("slider", "Band Cutoff")[4] == "300 Hz"
                   && tree ("slider", "Main / Low Curve Editor point 16")[4] == "100%",
               "sliders give their text: Speed \"" + tree ("slider", "Speed")[4] + "\", Threshold \"" + tree ("slider", "Sidechain Threshold")[4] + "\"");
        check (tree ("check box", "Sidechain Solo")[5] == "not checked" && tree ("check box", "Band")[5] == "not checked",
               "check boxes: Sidechain Solo " + tree ("check box", "Sidechain Solo")[5]);
        check (tree ("button", "Curve Presets").size() > 0, "button: Curve Presets");

        check (probe.indexOf ("FOCUS", "check box", "Sidechain Solo") >= 0 && probe.indexOf ("FOCUS", "grouping", "Main / Low Curve Editor") >= 0
                   && probe.indexOf ("FOCUS", "combo box", "Trigger Mode") >= 0,
               "every move raised a focus change");

        juce::StringArray notifications;
        for (auto& fields : probe.entries ("NOTIFICATION"))
            notifications.add (fields[3] + " (processing " + fields[2] + ")");
        std::cout << "  notifications: " << notifications.joinIntoString ("; ") << std::endl;
        auto sectionNames = false;
        for (auto* section : { "Timing", "Sidechain", "Output", "Main / Low Curve Editor", "High Curve Editor" })
            sectionNames = sectionNames || notifications.contains (juce::String (section) + " (processing 2)");
        check (! sectionNames, "the section keys speak nothing extra: the screen reader reads each group's name as the focus enters it");

        auto speedTopReads = 0;
        for (auto& fields : probe.entries ("PROPERTY"))
            if (fields[1] == "RangeValue" && fields[3] == "Speed" && fields[4] == "4.000000")
                ++speedTopReads;
        check (speedTopReads == 2, "Home at the top of Speed reads 4.00x again (" + juce::String (speedTopReads) + " times in all)");
        check (notifications.contains ("Main / Low Curve Editor curve loaded (processing 0)"),
               "loading a curve speaks \"Main / Low Curve Editor curve loaded\" through the screen reader");

        const auto soloOn = probe.indexOf ("PROPERTY", "ToggleState", "checked");
        check (soloOn >= 0 && probe.lines[soloOn].contains ("Sidechain Solo")
                   && probe.indexOf ("PROPERTY", "ToggleState", "not checked", soloOn) >= 0,
               "Sidechain Solo's changes reach the screen reader as checked, then not checked");
        check (probe.indexOf ("PROPERTY", "ToggleState", "High Curve") >= 0, "and so does High Curve's");
        check (probe.indexOf ("PROPERTY", "Value", "Sidechain") >= 0, "Trigger Mode's new value is sent (Sidechain)");
        check (probe.indexOf ("PROPERTY", "RangeValue", "Speed") >= 0, "Speed's changes are sent");

        const auto loaded = probe.indexOf ("NOTIFICATION", "BP-Trigger", "Main / Low Curve Editor curve loaded");
        auto focusBack = 0;
        for (int i = probe.indexOf ("FOCUS", "check box", "High Curve"); i >= 0 && i < probe.lines.size(); ++i)
            if (probe.lines[i].startsWith ("FOCUS\tbutton\tCurve Presets"))
                ++focusBack;
        check (focusBack >= 3 && loaded >= 0, "after each menu the screen reader's focus returns to Curve Presets ("
                                                   + juce::String (focusBack) + " focus changes there)");

        if (printTree)
        {
            std::cout << "\n  The tree (type | name | value | toggle state | description):" << std::endl;

            for (auto& fields : probe.entries ("TREE"))
                std::cout << "    " << juce::String::repeatedString ("  ", fields[1].getIntValue()) << fields[2] << " | " << fields[3]
                          << (fields[4] != "-" ? " | " + fields[4] : juce::String())
                          << (fields[5] != "-" ? " | " + fields[5] : juce::String())
                          << (fields[6].isNotEmpty() ? " | " + fields[6] : juce::String()) << std::endl;
        }

        std::cout << "\n  Events, in order:" << std::endl;
        for (auto& line : probe.lines)
            if (! line.startsWith ("TREE"))
                std::cout << "    " << line.replace ("\t", " | ") << std::endl;
    }

    window.removeFromDesktop();
    editor = nullptr;
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);

    if (vst3 != juce::File())
        checkHostedPlugin (vst3);

    check (! tookForeground, "the test windows never came to the front");
    LockSetForegroundWindow (2);

    if (! presetFolderExisted && presetFolder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
        presetFolder.deleteRecursively();
    if (! companyFolderExisted && presetFolder.getParentDirectory().getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
        presetFolder.getParentDirectory().deleteRecursively();

    std::cout << "\n" << (failures == 0 ? "All checks passed." : juce::String (failures) + " checks failed.") << std::endl;
    return failures == 0 ? 0 : 1;
}
