# BP-Trigger

BP-Trigger is an accessible, tempo-aware volume shaper for macOS. It combines drawn ducking curves with sidechain triggering, so the same plug-in can create rhythmic pumping, react to incoming hits, or do both at once.

The interface was designed with keyboard and screen-reader use as a first-class requirement. Every control has a descriptive accessible name, the curve is exposed as sixteen individually editable points, and the main interface is organised into navigable groups.

## Features

- AU and VST3 formats for macOS
- Sixteen-point, automatable volume curves
- Host-synchronised, sidechain, and combined trigger modes
- Stereo sidechain input on plug-in channels 3/4 in REAPER
- Thresholded sidechain solo monitoring
- Adjustable sidechain detector release from 1 to 100 ms
- Optional look-ahead pre-duck and ramp-down shaping
- Optional sidechain-driven ring modulation
- Optional two-band processing with an independent high-band curve
- Save and load curve presets

## Signal flow

BP-Trigger receives the audio to be shaped on its stereo main input. Its optional stereo sidechain bus is used for detection and ring modulation. In REAPER, this sidechain bus appears on plug-in input channels 3/4.

The trigger starts a sixteen-point gain curve. A curve point set to 0% applies the selected depth fully; a point at 100% leaves the signal at unity gain. The plug-in interpolates between points, producing a continuous gain movement rather than stepped changes.

The processing mode determines when that curve starts:

- **Grid** — Runs the curve in time with the host transport. Choose a division from one bar down to a sixteenth note, including triplet divisions.
- **Sidechain** — Starts the curve when the sidechain detector crosses the Sidechain Threshold.
- **Grid and Sidechain** — Runs to the grid and also responds to sidechain hits. The sidechain envelope takes priority when it calls for more ducking than the current grid position.

## Sidechain setup in REAPER

1. Place BP-Trigger on the track whose audio you want to shape.
2. Set the track channel count to at least four.
3. Send the trigger source to channels 3/4 of the BP-Trigger track.
4. Select **Sidechain** or **Grid and Sidechain** as the Trigger Mode.
5. Set the Sidechain Threshold, then enable **Sidechain Solo** to audition only the audio that reaches the detector threshold.

BP-Trigger supports a stereo main input and an optional stereo sidechain bus. Keeping the main bus stereo ensures the sidechain remains on REAPER inputs 3/4.

## Controls

### Timing

- **Trigger Mode** selects Grid, Sidechain, or Grid and Sidechain processing.
- **Grid Division** sets the duration of one curve cycle in Grid and Grid and Sidechain modes.
- **Speed** scales how quickly the curve progresses.
- **Ramp Down** creates a shaped transition before the first curve point. The plug-in reports matching latency compensation.
- **Ramp Shape** controls the curvature of that transition.

### Sidechain

- **Sidechain Solo** replaces the normal output with the sidechain monitor. The monitor is silent below the Sidechain Threshold, making it easy to hear exactly what reaches the detector.
- **Ringmod Sidechain** uses the sidechain to modulate the shaped signal.
- **Ringmod Intensity** sets the amount of that modulation.
- **Pre-duck** delays the main audio by up to 10 ms so the gain change can begin before a detected hit.
- **Sidechain Threshold** sets the detector level from -40 dB to 0 dB.
- **Detector Release** sets how quickly the detector resets after a hit, from 1 to 100 ms.

### Output and bands

- **Band** splits the shaped signal at the selected cutoff frequency. The main curve shapes the low band.
- **High Curve** enables a second sixteen-point curve for the high band.
- **Band Cutoff** sets the split frequency from 20 Hz to 20 kHz.
- **Depth** sets the amount of gain reduction for the full signal, or the low band when Band is enabled.
- **High Depth** sets the amount of gain reduction for the high curve.
- **Mix** blends between dry and shaped audio.

## Curves and presets

The main curve, and the optional high-band curve, each provide sixteen points from 0% to 100%. They are exposed as plug-in parameters, so they can be automated from the host.

Use **Curve Presets** to save or load a curve. Presets use the `.triggercurve` extension and are stored in:

`~/Documents/BLIND PRODUCERS PLUGINS/BP-Trigger`

## Accessibility and keyboard navigation

- Use `Tab` and `Shift+Tab` to move through controls.
- Use the arrow keys to adjust a focused control. Hold `Shift` for fine adjustment where supported.
- Use `,` and `.` to move between the Timing, Sidechain, Output, and Curve groups.
- Each curve point is independently focusable and announces its position and value to supported screen readers.

The detailed VoiceOver guide is available at [Accessibility guide/VoiceOver_Accessible_Plugin_UI_Guide.html](Accessibility%20guide/VoiceOver_Accessible_Plugin_UI_Guide.html).

## Building from source

### Requirements

- macOS with Xcode or the Xcode Command Line Tools
- CMake 3.22 or newer
- A local checkout of [JUCE](https://github.com/juce-framework/JUCE)

### Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR=/path/to/JUCE
cmake --build build --target BPTrigger_All -j 4
```

The built plug-ins are placed in:

```text
build/BPTrigger_artefacts/Release/AU/BP-Trigger.component
build/BPTrigger_artefacts/Release/VST3/BP-Trigger.vst3
```

The generated bundles are ad-hoc signed during development. Apply the appropriate signing and notarisation process before distributing a commercial release.

## Project layout

```text
Source/                         Plug-in processor, editor, and accessible controls
Source/UI/                      Curve editor and accessibility helpers
Accessibility guide/            VoiceOver usage guide
CMakeLists.txt                  CMake and JUCE build definition
```

## Contributing

Bug reports and accessibility feedback are welcome. Please include the host, plug-in format, macOS version, and steps to reproduce when reporting an issue.
