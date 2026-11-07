# BP-Trigger

BP-Trigger is a volume shaper for rhythmic ducking, sidechain-triggered movement, and creative modulation. Draw a gain curve, run it from the DAW clock, trigger it from an incoming signal, or combine both approaches.

## Triggering the curve

The plug-in plays a gain curve. Where the curve is at 0%, it applies the full amount set by Depth; at 100%, it leaves the signal unchanged. The curve runs from the initial duck back to full level.

That curve can be started in three ways:

- Grid follows the host transport. Pick a division from one bar to a sixteenth note, with triplet options included.
- Sidechain starts the curve when the sidechain signal crosses the threshold.
- Grid and Sidechain follows the grid but lets sidechain hits take over when the sidechain input crosses the threshold.

The Speed control changes the rate at which the curve is read. Ramp Down and Ramp Shape let you shape the movement immediately before the curve starts. Pre-duck adds up to 10 ms of look-ahead to the main signal, so the duck can begin just ahead of a detected hit.

## Two triggers

BP-Trigger has two triggers, so one instance can duck to a kick with one curve and to a snare with another. Each trigger has its own Trigger Mode, Grid Division, Speed, Source, Threshold, Detector Release, Sidechain Solo, Depth, Band, High Curve, High Depth and curves. Edit Trigger picks the trigger the controls and curve editors show. Trigger 2 starts set to Off.

Source sets what a trigger listens to: Sidechain, its left or right channel, or Sidechain 2, its left or right channel. In REAPER, Sidechain is plug-in pins 3/4 and Sidechain 2 is pins 5/6, so a kick sent to 3/4 and a snare sent to 5/6 can drive one trigger each. With one stereo send, the left and right channels can do the same. Audio Units have no Sidechain 2.

Where both triggers duck at once, the deeper duck wins, in each band. Ramp Down, Ramp Shape, Pre-duck, the sidechain effects, Band Cutoff and Mix are shared by both triggers.

## Listening to the sidechain

Turn on Sidechain Solo when setting a trigger's threshold. It mutes the normal output and plays only the trigger's source while the detector is over the threshold, fading out at the Detector Release time, so you can hear what is actually being picked up.

Detector Release controls how quickly the detector resets after a hit. Its range is 1 to 100 ms. Faster settings are useful for short, closely spaced transients; longer settings keep the detector open for longer material.

## Additional sound shaping

The ringmod and amplitude modulation follow Trigger 1's source.

### Ring modulation

Ringmod Sidechain uses the incoming sidechain signal to modulate the main audio, in time with the sidechain. Ringmod Intensity sets how strongly it is applied. With Duck Ringmod on, the duck turns the ringmod down with the rest of the audio; turn it off to hear the ringmod through the duck. This is independent of the trigger mode, so it can be used as a texture tool as well as part of a sidechain setup.

### Amplitude modulation

Amplitude Modulation makes the main audio's level follow the sidechain's waveform, rising and falling at the sidechain's pitch, so an 808 on the sidechain makes a melody flutter. Amplitude Modulation Depth sets how far the level drops at each trough. The flutter follows the sidechain's level, so it fades with the 808, and the main audio plays on unchanged once the sidechain stops. It never makes the main audio louder. To hear the flutter without the duck, set Depth to 0%.

### Band mode

Enable a trigger's Band to split the signal at Band Cutoff (20 Hz to 20 kHz). That trigger's curve then shapes the low band only. Enable its High Curve to give the high band its own curve and High Depth control. A trigger without Band ducks the whole signal. This makes it possible to keep a kick-driven duck focused in the low end, or to let a snare duck only the highs: turn on Band and High Curve for the snare's trigger and set its Depth to 0%.

### Depth and mix

Depth sets the amount of reduction for a trigger's curve, or its low curve in Band mode. With High Curve enabled, High Depth sets the amount of reduction for the high band. Mix blends the dry and shaped signals.

## Editing a curve

A curve is a line through points from 0% to 100% of the way through it. The points at 0% and 100% are always there; you can add more between them. Each point has a value, and a curve from -100% to 100% for its line to the next point: 0% is a straight line, positive values hold the point's value longer before moving to the next, and negative values move sooner.

The curve editor is edited from the keyboard. A cursor moves along the curve, and each key's result is spoken:

| Key | Action |
|---|---|
| `Left`, `Right` | Move the cursor 5%; with `Shift`, 1% |
| `Page Up`, `Page Down` | Move the cursor 10% |
| `Home`, `End` | Go to 0% or 100% |
| `,`, `.` | Go to the previous or next point |
| `A` | Add a point at the cursor |
| `E` | Remove the point at the cursor |
| `Up`, `Down` | Change the point's value by 5%; with `Shift`, 1% |
| `Shift+Page Up`, `Shift+Page Down` | Change the point's value by 10% |
| `]`, `[` | Change the point's curve by 5%; with `Shift`, 1% |
| `Backspace`, `Delete` | Clear the curve |

On a point, the editor reads its position, value and curve; between points, it reads the position and the curve's value there. Clearing returns the curve to its default, a rise from 0% to 100% with a curve of 50%.

Use Curve Presets to save or load a curve. Presets are stored as `.triggercurve` files in:

- macOS: `~/Documents/BLIND PRODUCERS PLUGINS/BP-Trigger`
- Windows: `Documents\BLIND PRODUCERS PLUGINS\BP-Trigger` in your user folder

The files are the same on both systems, so curves can be shared between them. Presets saved by earlier versions, with 16 points, still load. Projects from earlier versions open with their curves.

## Keyboard and screen-reader navigation

The interface is fully keyboard accessible. When the plug-in window opens, the focus is on Edit Trigger. `Tab` and `Shift+Tab` move through every control in every section. `,` and `.` jump to the previous or next section: Trigger, Ramp, Sidechain Effects, Output, and the curves. In a curve editor, they move between points instead.

Change the focused value with the arrow keys. Hold `Shift` for a step ten times finer. `Page Up` and `Page Down` move ten steps, `Home` goes to the top of the range and `End` to the bottom, and `Backspace` or `Delete` returns to the default. In the lists, such as Trigger Mode and Source, `Home` and `End` pick the first and last choice. To type a value, press `Enter` on a slider, type the value and press `Enter` again, or `Escape` to leave it unchanged. Press `Enter` on a check box to turn it on or off.

Every control is exposed to supported screen readers with its name and value. On macOS that is VoiceOver; on Windows, NVDA, JAWS and Narrator.

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

### Tests

Configure with `-DBP_TRIGGER_BUILD_TESTS=ON` to add the test tools. `BPTriggerCheck` loads the built VST3 as a host does and checks the sound in every trigger mode, with one trigger and with both. `BPTriggerCurveTest` checks the curves and what each curve editor key does and says.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBP_TRIGGER_BUILD_TESTS=ON
cmake --build build --target BPTrigger_VST3 BPTriggerCheck BPTriggerCurveTest -j 10
build/BPTriggerCurveTest_artefacts/Release/BPTriggerCurveTest
build/BPTriggerCheck_artefacts/Release/BPTriggerCheck build/BPTrigger_artefacts/Release/VST3/BP-Trigger.vst3
```

On Windows, `BPTriggerUiTest` also drives the editor from the keyboard while `BPTriggerUiaProbe` records what UI Automation tells a screen reader; with `--vst3` it also opens the built VST3 in a host window and sends it the key messages a host passes on. The test windows stay in the background, so a running screen reader does not read them.

```bat
cmake --build build --config Release --target BPTrigger_VST3 BPTriggerCheck BPTriggerCurveTest BPTriggerUiTest
build\BPTriggerCurveTest_artefacts\Release\BPTriggerCurveTest.exe
build\BPTriggerCheck_artefacts\Release\BPTriggerCheck.exe build\BPTrigger_artefacts\Release\VST3\BP-Trigger.vst3
build\BPTriggerUiTest_artefacts\Release\BPTriggerUiTest.exe --vst3 build\BPTrigger_artefacts\Release\VST3\BP-Trigger.vst3
```
