# BP-Trigger

BP-Trigger is a volume shaper for rhythmic ducking, sidechain-triggered movement, and creative modulation. Draw a gain curve, run it from the DAW clock, trigger it from an incoming signal, or combine both approaches.

## Triggering the curve

The plug-in plays a sixteen-point gain curve. At 0%, a point applies the full amount set by Depth; at 100%, it leaves the signal unchanged. Points between those values define the movement between the initial duck and the return to full level.

That curve can be started in three ways:

- Grid follows the host transport. Pick a division from one bar to a sixteenth note, with triplet options included.
- Sidechain starts the curve when the sidechain signal crosses the threshold.
- Grid and Sidechain follows the grid but lets sidechain hits take over when the sidechain input crosses the threshold.

The Speed control changes the rate at which the curve is read. Ramp Down and Ramp Shape let you shape the movement immediately before the first point. Pre-duck adds up to 10 ms of look-ahead to the main signal, so the duck can begin just ahead of a detected hit.

## Listening to the sidechain

Turn on Sidechain Solo when setting the threshold. It mutes the normal output and plays only the sidechain signal that reaches the detector threshold, so you can hear what is actually being picked up.

Detector Release controls how quickly the detector resets after a hit. Its range is 1 to 100 ms. Faster settings are useful for short, closely spaced transients; longer settings keep the detector open for longer material.

## Additional sound shaping

### Ring modulation

Ringmod Sidechain uses the incoming sidechain signal to modulate the shaped audio. Ringmod Intensity sets how strongly it is applied. This is independent of the trigger mode, so it can be used as a texture tool as well as part of a sidechain setup.

### Band mode

Enable Band to split the signal at Band Cutoff (20 Hz to 20 kHz). The main curve then shapes the low band. Enable High Curve to give the high band its own sixteen-point curve and High Depth control. This makes it possible to keep a kick-driven duck focused in the low end, or create different movement above and below the split.

### Depth and mix

Depth sets the amount of reduction for the main curve, or the low curve in Band mode. Mix blends the dry and shaped signals. With High Curve enabled, High Depth sets the amount of reduction for the high band.

## Curves and presets

Both the main curve and the optional high curve have sixteen editable, automatable points. Use Curve Presets to save or load a shape. Presets are stored as `.triggercurve` files in:

- macOS: `~/Documents/BLIND PRODUCERS PLUGINS/BP-Trigger`
- Windows: `Documents\BLIND PRODUCERS PLUGINS\BP-Trigger` in your user folder

The files are the same on both systems, so curves can be shared between them.

## Keyboard and screen-reader navigation

The interface is fully keyboard accessible. When the plug-in window opens, the focus is on Trigger Mode. `Tab` and `Shift+Tab` move through every control in every section. `,` and `.` jump to the previous or next section: Timing, Sidechain, Output, and the curves.

Change the focused value with the arrow keys. Hold `Shift` for finer adjustment where available. `Page Up` and `Page Down` move ten steps, `Home` goes to the top of the range and `End` to the bottom. In Trigger Mode and Grid Division, `Home` and `End` pick the first and last choice. To type a value, press `Enter` on a slider, type the value and press `Enter` again, or `Escape` to leave it unchanged. Press `Enter` on a check box to turn it on or off.

Every curve point is individually focusable and exposed to supported screen readers with its position and value. On macOS that is VoiceOver; on Windows, NVDA, JAWS and Narrator.

## Build from source

Clone the repository:

```sh
git clone <repository-url>
cd bp-trigger
```

On the first configure, CMake downloads the pinned JUCE source snapshot automatically. An internet connection is required for that first step. To use a JUCE checkout you already have at the same commit, add `-DFETCHCONTENT_SOURCE_DIR_JUCE=<path to JUCE>` to the first `cmake` command.

### macOS

With CMake 3.22 or newer installed, build the plug-in:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BPTrigger_All -j 10
```

The build outputs are placed in:

```text
build/BPTrigger_artefacts/Release/AU/BP-Trigger.component
build/BPTrigger_artefacts/Release/VST3/BP-Trigger.vst3
```

The development bundles are ad-hoc signed.

### Windows

Install Visual Studio 2022 with the Desktop development with C++ workload, which includes CMake. In a Developer PowerShell:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target BPTrigger_VST3
```

Windows builds the VST3 only; Audio Units are a macOS format. The build output is placed in:

```text
build\BPTrigger_artefacts\Release\VST3\BP-Trigger.vst3
```

Copy the whole `BP-Trigger.vst3` folder to `C:\Program Files\Common Files\VST3`, or to `%LOCALAPPDATA%\Programs\Common\VST3` for your user only, and rescan plug-ins in your host. No Visual C++ redistributable is needed.

### Windows tests

Configure with `-DBP_TRIGGER_BUILD_TESTS=ON` to add the test tools. `BPTriggerCheck` loads the built VST3 as a host does and checks the sound in every trigger mode. `BPTriggerUiTest` drives the editor from the keyboard while `BPTriggerUiaProbe` records what UI Automation tells a screen reader; with `--vst3` it also opens the built VST3 in a host window and sends it the key messages a host passes on. The test windows stay in the background, so a running screen reader does not read them.

```bat
cmake --build build --config Release --target BPTrigger_VST3 BPTriggerCheck BPTriggerUiTest
build\BPTriggerCheck_artefacts\Release\BPTriggerCheck.exe build\BPTrigger_artefacts\Release\VST3\BP-Trigger.vst3
build\BPTriggerUiTest_artefacts\Release\BPTriggerUiTest.exe --vst3 build\BPTrigger_artefacts\Release\VST3\BP-Trigger.vst3
```
