# BP-Trigger

BP-Trigger is a volume shaper for rhythmic ducking, sidechain-triggered movement, and creative modulation. Draw a gain curve, run it from the DAW clock, trigger it from an incoming signal, or combine both approaches.

It is built for producers who want precise control over the shape of a duck, without losing the practical side of working with a sidechain input. AU and VST3 builds are provided for macOS.

## How it works

The plug-in plays a sixteen-point gain curve. At 0%, a point applies the full amount set by **Depth**; at 100%, it leaves the signal unchanged. Points between those values define the movement between the initial duck and the return to full level.

That curve can be started in three ways:

- **Grid** follows the host transport. Pick a division from one bar to a sixteenth note, with triplet options included.
- **Sidechain** starts the curve when the sidechain signal crosses the threshold.
- **Grid and Sidechain** follows the grid but lets sidechain hits take over when they call for a deeper duck.

The **Speed** control changes the rate at which the curve is read. **Ramp Down** and **Ramp Shape** let you shape the movement immediately before the first point. **Pre-duck** adds up to 10 ms of look-ahead to the main signal, so the duck can begin just ahead of a detected hit.

## Sidechain in REAPER

BP-Trigger uses a stereo main input and an optional stereo sidechain bus. In REAPER, the sidechain bus is on plug-in inputs 3/4.

1. Put BP-Trigger on the track you want to shape.
2. Set that track to four channels or more.
3. Send the trigger source to channels 3/4 on the BP-Trigger track.
4. Choose **Sidechain** or **Grid and Sidechain**.
5. Set **Sidechain Threshold** to the level that should start the curve.

Turn on **Sidechain Solo** when setting the threshold. It mutes the normal output and plays only the sidechain signal that reaches the detector threshold, so you can hear what is actually being picked up.

**Detector Release** controls how quickly the detector resets after a hit. Its range is 1 to 100 ms. Faster settings are useful for short, closely spaced transients; longer settings keep the detector open for longer material.

## Sound-shaping controls

### Ring modulation

**Ringmod Sidechain** uses the incoming sidechain signal to modulate the shaped audio. **Ringmod Intensity** sets how strongly it is applied. This is independent of the trigger mode, so it can be used as a texture tool as well as part of a sidechain setup.

### Band mode

Enable **Band** to split the signal at **Band Cutoff** (20 Hz to 20 kHz). The main curve then shapes the low band. Enable **High Curve** to give the high band its own sixteen-point curve and **High Depth** control. This makes it possible to keep a kick-driven duck focused in the low end, or create different movement above and below the split.

### Depth and mix

**Depth** sets the amount of reduction for the main curve, or the low curve in Band mode. **Mix** blends the dry and shaped signals. With High Curve enabled, **High Depth** sets the amount of reduction for the high band.

## Curves and presets

Both the main curve and the optional high curve have sixteen editable, automatable points. Use **Curve Presets** to save or load a shape. Presets are stored as `.triggercurve` files in:

`~/Documents/BLIND PRODUCERS PLUGINS/BP-Trigger`

## Keyboard and screen-reader use

The interface is fully keyboard accessible. Use `Tab` and `Shift+Tab` to move through controls, and the arrow keys to change the focused value. Hold `Shift` for finer adjustment where available. Use `,` and `.` to move between the Timing, Sidechain, Output, and Curve sections.

Every curve point is individually focusable and exposed to supported screen readers with its position and value.

## Building

Requirements:

- macOS with Xcode or the Xcode Command Line Tools
- CMake 3.22 or newer
- A local checkout of [JUCE](https://github.com/juce-framework/JUCE)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR=/path/to/JUCE
cmake --build build --target BPTrigger_All -j 4
```

The build outputs are placed in:

```text
build/BPTrigger_artefacts/Release/AU/BP-Trigger.component
build/BPTrigger_artefacts/Release/VST3/BP-Trigger.vst3
```

The development bundles are ad-hoc signed. Sign and notarise release builds before distributing them.

## Contributing

If you report a problem, please include the plug-in format, host, macOS version, and clear steps to reproduce it. Accessibility feedback is especially useful.
