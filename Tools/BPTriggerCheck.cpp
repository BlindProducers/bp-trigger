// Offline host for the built BP-Trigger VST3. Loads it as a host does and
// checks its buses, parameters, texts, latency and saved state, and the gain
// it applies in each trigger mode against the curve it was given, from one
// trigger and from both.
//
// BPTriggerCheck <path to BP-Trigger.vst3>

#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <iostream>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;

    if (! ok)
        ++failures;
}

struct TestPlayHead final : public juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setIsPlaying (playing);
        info.setTimeInSamples (timeInSamples);
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }

    double bpm = 120.0;
    double ppq = 0.0;
    juce::int64 timeInSamples = 0;
    bool playing = true;
};

using Signal = std::function<float (juce::int64)>;
// A sample of each input channel: 0 and 1 the main input, 2 and 3 the sidechain, 4 and 5 Sidechain 2.
using Inputs = std::function<float (juce::int64, int)>;

struct Host
{
    Host (juce::AudioPluginFormat& format, const juce::PluginDescription& description, double rate, int block)
        : sampleRate (rate), blockSize (block)
    {
        juce::String error;
        plugin = format.createInstanceFromDescription (description, sampleRate, blockSize, error);

        if (plugin == nullptr)
        {
            std::cout << "  could not load the plug-in: " << error << std::endl;
            return;
        }

        plugin->enableAllBuses();
        plugin->setPlayHead (&playHead);
        plugin->prepareToPlay (sampleRate, blockSize);
    }

    ~Host()
    {
        if (plugin != nullptr)
            plugin->releaseResources();
    }

    juce::AudioProcessorParameter* parameter (const juce::String& name) const
    {
        for (auto* p : plugin->getParameters())
            if (p->getName (128) == name)
                return p;

        std::cout << "  no parameter named " << name << std::endl;
        return nullptr;
    }

    // Sets a parameter from its text, as typed into a host's field.
    void set (const juce::String& name, const juce::String& text)
    {
        if (auto* p = parameter (name))
            p->setValueNotifyingHost (p->getValueForText (text));
    }

    void reset()
    {
        plugin->releaseResources();
        plugin->prepareToPlay (sampleRate, blockSize);
        playHead.ppq = 0.0;
        playHead.timeInSamples = 0;
    }

    // Plays `seconds` of audio; returns the left output.
    std::vector<float> run (double seconds, const Signal& main, const Signal& sidechain)
    {
        return run (seconds, [main, sidechain] (juce::int64 t, int channel) { return channel < 2 ? main (t) : channel < 4 ? sidechain (t) : 0.0f; });
    }

    std::vector<float> run (double seconds, const Inputs& inputs)
    {
        std::vector<float> output;
        ppqAt.clear();
        const auto total = static_cast<int> (seconds * sampleRate);
        juce::MidiBuffer midi;

        for (int start = 0; start < total; start += blockSize)
        {
            const auto count = juce::jmin (blockSize, total - start);
            juce::AudioBuffer<float> buffer (juce::jmax (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), count);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < count; ++i)
                    buffer.setSample (ch, i, inputs (playHead.timeInSamples + i, ch));

            plugin->processBlock (buffer, midi);

            for (int i = 0; i < count; ++i)
            {
                output.push_back (buffer.getSample (0, i));
                ppqAt.push_back (playHead.ppq + static_cast<double> (i) * playHead.bpm / (60.0 * sampleRate));
            }

            playHead.timeInSamples += count;
            playHead.ppq += count * playHead.bpm / (60.0 * sampleRate);
        }

        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        return output;
    }

    double sampleRate;
    int blockSize;
    TestPlayHead playHead;
    std::vector<double> ppqAt;   // the beat position of each sample of the last run, as the plug-in computes it
    std::unique_ptr<juce::AudioPluginInstance> plugin;
};

// The default curve: from 0% to 100% along the square of the way through it.
float defaultCurve (double phase)
{
    const auto p = juce::jlimit (0.0, 1.0, phase);
    return static_cast<float> (p * p);
}

using Expected = std::function<float (size_t)>;

// The expected gain of each sample from its place in a cycle of the grid, `cycleBeats` long.
Expected onGrid (const Host& host, double cycleBeats, std::function<float (double)> gainAtPosition)
{
    return [&host, cycleBeats, gainAtPosition] (size_t i)
    {
        return gainAtPosition (std::fmod (host.ppqAt[i], cycleBeats) / cycleBeats);
    };
}

// The expected gain of each sample from its time in seconds.
Expected atTime (double rate, std::function<float (double)> gainAtSeconds)
{
    return [rate, gainAtSeconds] (size_t i) { return gainAtSeconds (static_cast<double> (i) / rate); };
}

// Largest difference over [from, to) seconds between the output's gain and the
// expected gain, smoothed over 1.5 ms as the plug-in smooths it.
float largestGainError (const std::vector<float>& output, double rate, double from, double to, float input,
                        const Expected& expectedGain)
{
    const auto smoothing = std::exp (-1.0 / (0.0015 * rate));
    auto smoothed = 1.0;
    auto largest = 0.0f;
    auto worstAt = 0.0;

    for (size_t i = 0; i < static_cast<size_t> (to * rate) && i < output.size(); ++i)
    {
        const auto seconds = static_cast<double> (i) / rate;
        const auto target = static_cast<double> (expectedGain (i));
        smoothed = target + smoothing * (smoothed - target);

        if (seconds < from)
            continue;

        const auto error = std::abs (output[i] / input - static_cast<float> (smoothed));

        if (error > largest)
        {
            largest = error;
            worstAt = seconds;
        }
    }

    if (largest >= 0.002f)
        std::cout << "         largest gain error " << largest << " at " << worstAt << " s" << std::endl;

    return largest;
}

float gainAt (const std::vector<float>& output, double rate, double seconds, float input)
{
    return output[static_cast<size_t> (seconds * rate)] / input;
}

// Amplitude of the sine at `frequency` in [from, from + length) seconds.
float amplitudeOf (const std::vector<float>& output, double rate, double frequency, double from, double length)
{
    std::complex<double> sum;
    const auto start = static_cast<size_t> (from * rate);
    const auto count = static_cast<size_t> (length * rate);

    for (size_t i = 0; i < count; ++i)
        sum += static_cast<double> (output[start + i])
             * std::polar (1.0, -juce::MathConstants<double>::twoPi * frequency * static_cast<double> (i) / rate);

    return static_cast<float> (2.0 * std::abs (sum) / static_cast<double> (count));
}

bool allFinite (const std::vector<float>& output)
{
    return std::all_of (output.begin(), output.end(), [] (float x) { return std::isfinite (x); });
}

Signal constant (float value)   { return [value] (juce::int64) { return value; }; }

// A hosted VST3's state holds the plug-in's own, in base64, as its IComponent.
std::unique_ptr<juce::XmlElement> pluginState (const juce::MemoryBlock& hostState)
{
    const auto head = juce::AudioProcessor::getXmlFromBinary (hostState.getData(), static_cast<int> (hostState.getSize()));
    const auto* component = head != nullptr ? head->getChildByName ("IComponent") : nullptr;
    juce::MemoryBlock data;

    if (component == nullptr || ! data.fromBase64Encoding (component->getAllSubText()))
        return {};

    return juce::AudioProcessor::getXmlFromBinary (data.getData(), static_cast<int> (data.getSize()));
}

juce::MemoryBlock hostStateHolding (const juce::XmlElement& state)
{
    juce::MemoryBlock data, hostState;
    juce::AudioProcessor::copyXmlToBinary (state, data);
    juce::XmlElement head ("VST3PluginState");
    head.createNewChildElement ("IComponent")->addTextElement (data.toBase64Encoding());
    juce::AudioProcessor::copyXmlToBinary (head, hostState);
    return hostState;
}

void checkGridMode (juce::AudioPluginFormat& format, const juce::PluginDescription& description, double rate)
{
    std::cout << "\nGrid at " << rate << " Hz:" << std::endl;
    Host host (format, description, rate, 512);

    if (host.plugin == nullptr)
        return check (false, "the plug-in loads");

    auto output = host.run (2.0, constant (0.5f), constant (0.0f));
    check (allFinite (output), "the output is clean");
    check (largestGainError (output, rate, 0.5, 2.0, 0.5f, onGrid (host, 1.0f, defaultCurve)) < 0.002f,
           "1/4 notes at 120 BPM follow the curve, 0% at the beat to 100% before the next");
    check (gainAt (output, rate, 1.01, 0.5f) < 0.01f, "the beat ducks to silence");

    host.reset();
    host.set ("Trigger 1 Speed", "2");
    output = host.run (2.0, constant (0.5f), constant (0.0f));
    check (largestGainError (output, rate, 0.5, 2.0, 0.5f, onGrid (host, 1.0f, [] (double p) { return defaultCurve (p * 2.0); })) < 0.002f,
           "Speed 2.00x plays the curve in half the time, then holds full level");

    host.reset();
    host.set ("Trigger 1 Speed", "1");
    host.set ("Trigger 1 Depth", "50");
    output = host.run (2.0, constant (0.5f), constant (0.0f));
    check (largestGainError (output, rate, 0.5, 2.0, 0.5f, onGrid (host, 1.0f, [] (double p) { return 1.0f - 0.5f * (1.0f - defaultCurve (p)); })) < 0.002f,
           "Depth 50% halves the reduction");

    host.reset();
    host.set ("Trigger 1 Depth", "100");
    host.set ("Trigger 1 Grid Division", "1/8");
    output = host.run (2.0, constant (0.5f), constant (0.0f));
    check (largestGainError (output, rate, 0.5, 2.0, 0.5f, onGrid (host, 0.5f, defaultCurve)) < 0.002f, "1/8 notes run the curve twice a beat");

    host.reset();
    host.set ("Trigger 1 Grid Division", "1/4 triplet");
    output = host.run (2.0, constant (0.5f), constant (0.0f));
    check (largestGainError (output, rate, 0.5, 2.0, 0.5f, onGrid (host, 2.0 / 3.0, defaultCurve)) < 0.002f, "1/4 triplets run the curve three times in two beats");

    host.reset();
    host.set ("Trigger 1 Grid Division", "1/4");
    host.set ("Mix", "0");
    output = host.run (1.0, constant (0.5f), constant (0.0f));
    check (largestGainError (output, rate, 0.1, 1.0, 0.5f, atTime (rate, [] (double) { return 1.0f; })) < 1.0e-6f, "Mix 0% leaves the signal dry");

    host.reset();
    host.set ("Mix", "100");
    host.playHead.playing = false;
    output = host.run (1.0, constant (0.5f), constant (0.0f));
    check (largestGainError (output, rate, 0.0, 1.0, 0.5f, atTime (rate, [] (double) { return 1.0f; })) < 1.0e-6f,
           "with the transport stopped, Grid passes the signal untouched");
    host.playHead.playing = true;
}

void checkSidechainModes (juce::AudioPluginFormat& format, const juce::PluginDescription& description)
{
    const auto rate = 48000.0;
    std::cout << "\nSidechain:" << std::endl;
    Host host (format, description, rate, 512);

    if (host.plugin == nullptr)
        return check (false, "the plug-in loads");

    const auto hitAt = static_cast<juce::int64> (0.25 * rate);
    const auto hit = [hitAt] (float level) -> Signal
    {
        return [hitAt, level] (juce::int64 t) { return t >= hitAt && t < hitAt + 240 ? level : 0.0f; };
    };

    host.set ("Trigger 1 Mode", "Sidechain");
    host.playHead.playing = false;
    auto output = host.run (1.0, constant (0.5f), hit (0.5f));
    check (largestGainError (output, rate, 0.05, 0.25, 0.5f, atTime (rate, [] (double) { return 1.0f; })) < 0.001f,
           "before a hit the signal is at full level");
    check (gainAt (output, rate, 0.26, 0.5f) < 0.01f, "a hit above the threshold ducks at once, even with the transport stopped");
    check (largestGainError (output, rate, 0.25, 1.0, 0.5f, atTime (rate, [] (double t) { return t < 0.25 ? 1.0f : defaultCurve ((t - 0.25) / 0.5); })) < 0.002f,
           "then the curve plays over one 1/4 note");
    check (gainAt (output, rate, 0.9, 0.5f) > 0.999f, "and the level stays full afterwards");

    host.reset();
    output = host.run (1.0, constant (0.5f), hit (0.02f));
    check (largestGainError (output, rate, 0.05, 1.0, 0.5f, atTime (rate, [] (double) { return 1.0f; })) < 0.001f,
           "a hit below the -26 dB threshold does nothing");

    host.reset();
    host.set ("Trigger 1 Threshold", "-40");
    output = host.run (1.0, constant (0.5f), hit (0.02f));
    check (gainAt (output, rate, 0.26, 0.5f) < 0.01f, "with the threshold at -40 dB the same hit ducks");
    host.set ("Trigger 1 Threshold", "-26");

    std::cout << "\nGrid and Sidechain:" << std::endl;
    host.reset();
    host.set ("Trigger 1 Mode", "Grid and Sidechain");
    host.playHead.playing = true;
    const auto lateHit = static_cast<juce::int64> (0.9 * rate);
    output = host.run (1.2, constant (0.5f), [lateHit] (juce::int64 t) { return t >= lateHit && t < lateHit + 240 ? 0.5f : 0.0f; });
    check (std::abs (gainAt (output, rate, 0.85, 0.5f) - defaultCurve (0.7)) < 0.01f, "the grid shapes the beat");
    check (gainAt (output, rate, 0.91, 0.5f) < 0.01f, "a hit late in the beat takes over and ducks at once");

    std::cout << "\nSidechain Solo:" << std::endl;
    host.reset();
    host.set ("Trigger 1 Mode", "Grid");
    host.set ("Trigger 1 Sidechain Solo", "1");
    const auto sine = [rate] (float level) -> Signal
    {
        return [rate, level] (juce::int64 t) { return level * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * 1000.0 * static_cast<double> (t) / rate)); };
    };
    output = host.run (0.5, constant (0.5f), sine (0.3f));
    auto largest = 0.0f;
    for (auto i = static_cast<size_t> (0.02 * rate); i < output.size(); ++i)   // after the solo's 1 ms fade-in
        largest = juce::jmax (largest, std::abs (output[i] - sine (0.3f) (static_cast<juce::int64> (i))));
    check (largest < 1.0e-6f, "a sidechain above the threshold is heard instead of the input");

    host.reset();
    output = host.run (0.5, constant (0.5f), sine (0.01f));
    check (std::all_of (output.begin() + 48, output.end(), [] (float x) { return x == 0.0f; }), "a sidechain below the threshold is silent");
    host.set ("Trigger 1 Sidechain Solo", "0");

    // The ringmod and amplitude modulation add nothing at DC, so these use a 1 kHz input at 0.5 and read
    // its level as its peak over one cycle.
    const auto levelAt = [rate] (const std::vector<float>& samples, double seconds)
    {
        const auto first = samples.begin() + static_cast<long> (seconds * rate);
        return std::abs (*std::max_element (first, first + static_cast<long> (0.001 * rate), [] (float a, float b) { return std::abs (a) < std::abs (b); }));
    };

    std::cout << "\nRingmod:" << std::endl;
    host.reset();
    host.set ("Trigger 1 Mode", "Sidechain");
    host.set ("Ringmod Sidechain", "1");
    output = host.run (0.5, sine (0.5f), constant (0.04f));
    check (std::abs (levelAt (output, 0.4) - 0.5f * (1.0f - 2.0f * 0.04f)) < 0.001f, "Ringmod Intensity 100% multiplies by the sidechain");
    host.set ("Ringmod Intensity", "50");
    output = host.run (0.5, sine (0.5f), constant (0.04f));
    check (std::abs (levelAt (output, 0.4) - 0.5f * (1.0f - 0.04f)) < 0.001f, "Ringmod Intensity 50% does half as much");

    // A sidechain of 0.3 triggers the curve; 0.1 s in, the gain is ducked to g. Ducked, the level is
    // 0.5 g (1 - 0.6); with Duck Ringmod off the ringmod passes the duck, so it is 0.5 |g - 0.6|.
    host.set ("Ringmod Intensity", "100");
    host.reset();
    const auto duckedGain = levelAt (host.run (0.2, sine (0.5f), constant (0.3f)), 0.1) / (0.5f * 0.4f);
    host.set ("Duck Ringmod", "0");
    host.reset();
    const auto unducked = levelAt (host.run (0.2, sine (0.5f), constant (0.3f)), 0.1);
    check (duckedGain < 0.9f && std::abs (unducked - 0.5f * std::abs (duckedGain - 0.6f)) < 0.002f,
           "with Duck Ringmod off, the ringmod plays through the duck (gain " + juce::String (duckedGain, 3) + ")");
    host.set ("Duck Ringmod", "1");
    host.set ("Ringmod Sidechain", "0");

    // A 50 Hz sidechain at 0.5 for half a second, then silence. Amplitude Modulation at 100% keeps the input's
    // level at the sidechain's peaks and halves it at its troughs, and gives the input back once it stops.
    std::cout << "\nAmplitude Modulation:" << std::endl;
    host.reset();
    host.set ("Trigger 1 Depth", "0");
    host.set ("Amplitude Modulation", "1");
    const Signal fiftyHertz = [rate] (juce::int64 t)
    {
        return t < static_cast<juce::int64> (0.5 * rate) ? 0.5f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * 50.0 * static_cast<double> (t) / rate)) : 0.0f;
    };
    output = host.run (1.5, sine (0.5f), fiftyHertz);
    auto lowest = 1.0f, highest = 0.0f;
    for (auto seconds = 0.1; seconds < 0.49; seconds += 0.001)
    {
        lowest = juce::jmin (lowest, levelAt (output, seconds));
        highest = juce::jmax (highest, levelAt (output, seconds));
    }
    // A cycle's peak seldom lands on the sidechain's trough, so the lowest level reads a little over 0.25.
    check (highest < 0.5005f && std::abs (lowest - 0.25f) < 0.005f,
           "it moves the input between full level and half at 100% (" + juce::String (lowest, 3) + " to " + juce::String (highest, 3) + ")");
    check (std::abs (levelAt (output, 1.45) - 0.5f) < 0.0005f, "after the sidechain stops, the input comes back unchanged (" + juce::String (levelAt (output, 1.45), 4) + ")");
    host.set ("Amplitude Modulation", "0");
    host.set ("Trigger 1 Depth", "100");
}

void checkBandMode (juce::AudioPluginFormat& format, const juce::PluginDescription& description)
{
    const auto rate = 48000.0;
    std::cout << "\nBand:" << std::endl;
    Host host (format, description, rate, 512);

    if (host.plugin == nullptr)
        return check (false, "the plug-in loads");

    const Signal lowAndHigh = [rate] (juce::int64 t)
    {
        const auto seconds = static_cast<double> (t) / rate;
        return static_cast<float> (0.4 * std::sin (juce::MathConstants<double>::twoPi * 50.0 * seconds)
                                 + 0.4 * std::sin (juce::MathConstants<double>::twoPi * 6000.0 * seconds));
    };

    // The beat starts at 1 second; the curve stays under 1% for its first 50 ms.
    auto output = host.run (1.2, lowAndHigh, constant (0.0f));
    check (amplitudeOf (output, rate, 6000.0, 1.01, 0.04) < 0.01f, "without Band the whole signal ducks");

    host.reset();
    host.set ("Trigger 1 Band", "1");
    host.set ("Band Cutoff", "300");
    output = host.run (1.2, lowAndHigh, constant (0.0f));
    const auto high = amplitudeOf (output, rate, 6000.0, 1.01, 0.04);
    const auto low = amplitudeOf (output, rate, 50.0, 1.01, 0.04);
    check (high > 0.37f && low < 0.1f, "with Band the curve ducks below 300 Hz and leaves 6 kHz alone (6 kHz "
                                         + juce::String (high, 3) + ", 50 Hz " + juce::String (low, 3) + ")");

    host.reset();
    host.set ("Trigger 1 High Curve", "1");
    output = host.run (1.2, lowAndHigh, constant (0.0f));
    check (amplitudeOf (output, rate, 6000.0, 1.01, 0.04) < 0.01f, "High Curve ducks the high band with its own curve");

    host.reset();
    host.set ("Trigger 1 High Depth", "0");
    output = host.run (1.2, lowAndHigh, constant (0.0f));
    check (amplitudeOf (output, rate, 6000.0, 1.01, 0.04) > 0.37f, "High Depth 0% leaves the high band alone");
}

void checkHostView (juce::AudioPluginFormat& format, const juce::PluginDescription& description)
{
    std::cout << "\nWhat the host sees:" << std::endl;
    Host host (format, description, 48000.0, 512);

    if (host.plugin == nullptr)
        return check (false, "the plug-in loads");

    auto& plugin = *host.plugin;
    check (description.name == "BP-Trigger" && description.manufacturerName == "Blind Producers", "BP-Trigger by Blind Producers");
    check (plugin.hasEditor(), "it has an editor");
    check (plugin.getBusCount (true) == 3 && plugin.getBusCount (false) == 1, "three input buses (main, Sidechain and Sidechain 2) and one output");
    check (plugin.getChannelCountOfBus (true, 0) == 2 && plugin.getChannelCountOfBus (true, 1) == 2 && plugin.getChannelCountOfBus (true, 2) == 2
               && plugin.getChannelCountOfBus (false, 0) == 2,
           "all stereo, so the sidechains sit on pins 3/4 and 5/6 in REAPER");

    juce::StringArray names;
    for (auto* p : plugin.getParameters())
        names.add (p->getName (128));

    auto allThere = true;
    for (auto trigger : { "Trigger 1 ", "Trigger 2 " })
        for (auto name : { "Mode", "Grid Division", "Speed", "Source", "Threshold", "Detector Release", "Sidechain Solo", "Depth", "High Depth",
                           "Band", "High Curve" })
            allThere = allThere && names.contains (trigger + juce::String (name));
    for (auto name : { "Ramp Down", "Ramp Shape", "Pre-duck", "Ringmod Sidechain", "Ringmod Intensity", "Duck Ringmod",
                       "Amplitude Modulation", "Amplitude Modulation Depth", "Band Cutoff", "Mix" })
        allThere = allThere && names.contains (name);
    names.removeString ("Bypass");   // added by JUCE's VST3 wrapper
    check (allThere && names.size() == 32, "both triggers' 11 controls and the 10 shared ones are automatable (" + names.joinIntoString (", ") + ")");

    const auto text = [&host] (const juce::String& name)
    {
        auto* p = host.parameter (name);
        return p != nullptr ? p->getCurrentValueAsText() : juce::String();
    };
    check (text ("Trigger 2 Mode") == "Off" && text ("Trigger 1 Source") == "Sidechain" && text ("Trigger 2 Source") == "Sidechain 2",
           "Trigger 2 starts off, on Sidechain 2; Trigger 1 listens to the sidechain");
    check (text ("Trigger 1 Mode") == "Grid" && text ("Trigger 1 Grid Division") == "1/4" && text ("Trigger 1 Speed") == "1.00x"
               && text ("Trigger 1 Threshold") == "-26.0 dB" && text ("Trigger 1 Detector Release") == "50.5 ms" && text ("Band Cutoff") == "300 Hz"
               && text ("Trigger 1 Depth") == "100%" && text ("Pre-duck") == "0.0 ms",
           "values read as text: " + text ("Trigger 1 Mode") + ", " + text ("Trigger 1 Grid Division") + ", " + text ("Trigger 1 Speed") + ", "
               + text ("Trigger 1 Threshold") + ", " + text ("Trigger 1 Detector Release") + ", " + text ("Band Cutoff") + ", "
               + text ("Trigger 1 Depth") + ", " + text ("Pre-duck"));

    host.set ("Band Cutoff", "1.5 kHz");
    host.set ("Trigger 1 Detector Release", "10");
    check (text ("Band Cutoff") == "1.5 kHz" && text ("Trigger 1 Detector Release") == "10 ms", "typed values are understood (1.5 kHz, 10 ms)");

    host.set ("Pre-duck", "5");
    host.run (0.1, constant (0.0f), constant (0.0f));
    check (plugin.getLatencySamples() == 240, "Pre-duck 5 ms reports 240 samples of latency (" + juce::String (plugin.getLatencySamples()) + ")");
    host.set ("Ramp Down", "4");
    host.run (0.1, constant (0.0f), constant (0.0f));
    check (plugin.getLatencySamples() == 432, "with Ramp Down 4 ms it reports 432 (" + juce::String (plugin.getLatencySamples()) + ")");
    host.set ("Pre-duck", "0");
    host.set ("Ramp Down", "0");
    host.run (0.1, constant (0.0f), constant (0.0f));
    check (plugin.getLatencySamples() == 0, "and none without them");

    host.set ("Trigger 1 Depth", "37");
    host.set ("Trigger 1 Mode", "Grid and Sidechain");
    host.set ("Trigger 1 Grid Division", "1/8 triplet");
    host.set ("Trigger 2 Mode", "Sidechain");
    host.set ("Trigger 2 Source", "Sidechain Right");
    host.set ("Trigger 1 Band", "1");
    host.run (0.1, constant (0.0f), constant (0.0f));   // a host hands parameter changes over with the next block

    // A curve with a point added, as the curve editor saves it in the project.
    juce::MemoryBlock state;
    plugin.getStateInformation (state);
    auto xml = pluginState (state);
    auto* trigger2High = xml != nullptr && xml->getChildByName ("Curves") != nullptr ? xml->getChildByName ("Curves")->getChildByName ("Trigger2High") : nullptr;
    check (trigger2High != nullptr && trigger2High->getNumChildElements() == 2, "the project holds each curve's points");

    if (trigger2High == nullptr)
        return;

    auto* point = new juce::XmlElement ("Point");
    point->setAttribute ("position", 40);
    point->setAttribute ("value", 900);
    point->setAttribute ("curve", -30);
    trigger2High->insertChildElement (point, 1);
    xml->setAttribute ("editTrigger", 1);
    state = hostStateHolding (*xml);

    Host copy (format, description, 48000.0, 512);
    copy.plugin->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    auto same = true;
    for (int i = 0; i < plugin.getParameters().size(); ++i)
    {
        if (std::abs (plugin.getParameters()[i]->getValue() - copy.plugin->getParameters()[i]->getValue()) >= 1.0e-5f)
        {
            std::cout << "         " << plugin.getParameters()[i]->getName (128) << " differs" << std::endl;
            same = false;
        }
    }

    juce::MemoryBlock copied;
    copy.plugin->getStateInformation (copied);
    const auto copiedXml = pluginState (copied);
    check (same && copiedXml != nullptr && copiedXml->isEquivalentTo (xml.get(), false), "a saved project restores every setting and curve");

    // A project from before curves had points of their own held Trigger 1's curves as 16 parameters each.
    juce::XmlElement old ("BPTriggerState");
    for (int i = 0; i < 16; ++i)
    {
        auto* param = old.createNewChildElement ("PARAM");
        param->setAttribute ("id", "curvePoint" + juce::String (i).paddedLeft ('0', 2));
        param->setAttribute ("value", i == 15 ? 1.0 : 0.5);
    }
    auto* oldDepth = old.createNewChildElement ("PARAM");
    oldDepth->setAttribute ("id", "depth");
    oldDepth->setAttribute ("value", 0.37);
    const auto oldState = hostStateHolding (old);
    copy.plugin->setStateInformation (oldState.getData(), static_cast<int> (oldState.getSize()));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    copy.plugin->getStateInformation (copied);
    const auto converted = pluginState (copied);
    const auto* curve = converted != nullptr ? converted->getChildByName ("Curves")->getChildByName ("Trigger1") : nullptr;
    check (curve != nullptr && curve->getNumChildElements() == 16 && curve->getChildElement (1)->getIntAttribute ("position") == 7
               && curve->getChildElement (1)->getIntAttribute ("value") == 500 && curve->getChildElement (15)->getIntAttribute ("value") == 1000
               && copy.parameter ("Trigger 1 Depth")->getCurrentValueAsText() == "37%"
               && converted->getChildByName ("PARAM") != nullptr && converted->getChildByAttribute ("id", "curvePoint00") == nullptr,
           "an older project's 16 curve points become the curve's points");
}

void checkTwoTriggers (juce::AudioPluginFormat& format, const juce::PluginDescription& description)
{
    const auto rate = 48000.0;
    std::cout << "\nTwo triggers:" << std::endl;
    Host host (format, description, rate, 512);

    if (host.plugin == nullptr)
        return check (false, "the plug-in loads");

    // Hits 5 ms long at 0.5 on the input channels given.
    const auto hits = [rate] (std::vector<std::pair<double, int>> at) -> Inputs
    {
        return [rate, at] (juce::int64 t, int channel)
        {
            if (channel < 2)
                return 0.5f;

            for (const auto& [seconds, hitChannel] : at)
            {
                const auto start = static_cast<juce::int64> (seconds * rate);
                if (channel == hitChannel && t >= start && t < start + 240)
                    return 0.5f;
            }

            return 0.0f;
        };
    };

    host.playHead.playing = false;
    host.set ("Trigger 1 Mode", "Sidechain");
    host.set ("Trigger 1 Source", "Sidechain Left");
    auto output = host.run (0.6, hits ({ { 0.1, 3 }, { 0.35, 2 } }));
    check (gainAt (output, rate, 0.11, 0.5f) > 0.999f && gainAt (output, rate, 0.36, 0.5f) < 0.01f,
           "with Source on Sidechain Left, a hit on the right does nothing and one on the left ducks");

    host.reset();
    host.set ("Trigger 1 Source", "Sidechain");
    host.set ("Trigger 1 Depth", "50");
    host.set ("Trigger 2 Mode", "Sidechain");
    output = host.run (1.4, hits ({ { 0.1, 2 }, { 0.7, 4 }, { 0.7, 2 } }));
    check (std::abs (gainAt (output, rate, 0.2, 0.5f) - (1.0f - 0.5f * (1.0f - defaultCurve (0.2)))) < 0.005f,
           "a hit on the sidechain ducks with Trigger 1's Depth of 50%");
    check (largestGainError (output, rate, 0.7, 1.4, 0.5f, atTime (rate, [] (double t) { return t < 0.7 ? 1.0f : defaultCurve ((t - 0.7) / 0.5); })) < 0.002f,
           "where both triggers duck, the deeper duck wins: Trigger 2's 100%");

    host.reset();
    host.set ("Trigger 2 Sidechain Solo", "1");
    output = host.run (0.3, hits ({ { 0.1, 4 }, { 0.2, 2 } }));
    // 4.5 ms in, the solo's 1 ms fade-in is at 99%.
    check (output[static_cast<size_t> (0.1045 * rate)] > 0.49f && std::abs (output[static_cast<size_t> (0.2045 * rate)]) < 1.0e-3f,
           "Trigger 2's Sidechain Solo plays Sidechain 2 only");
    host.set ("Trigger 2 Sidechain Solo", "0");

    host.reset();
    host.set ("Trigger 2 Mode", "Off");
    output = host.run (0.6, hits ({ { 0.1, 4 } }));
    check (gainAt (output, rate, 0.11, 0.5f) > 0.999f, "with Trigger 2 off, Sidechain 2 does nothing");
    host.set ("Trigger 1 Depth", "100");

    // A kick on the sidechain ducks everything; a snare on Sidechain 2 ducks only the highs, with Band and
    // High Curve on Trigger 2 alone and its Depth, the low band's, at 0%.
    host.reset();
    host.set ("Trigger 2 Mode", "Sidechain");
    host.set ("Trigger 2 Band", "1");
    host.set ("Trigger 2 High Curve", "1");
    host.set ("Trigger 2 Depth", "0");
    host.set ("Band Cutoff", "300");
    const auto withHits = hits ({ { 0.3, 4 }, { 0.8, 2 } });
    output = host.run (1.0, [rate, withHits] (juce::int64 t, int channel)
                       {
                           if (channel >= 2)
                               return withHits (t, channel);

                           const auto seconds = static_cast<double> (t) / rate;
                           return static_cast<float> (0.4 * std::sin (juce::MathConstants<double>::twoPi * 50.0 * seconds)
                                                    + 0.4 * std::sin (juce::MathConstants<double>::twoPi * 6000.0 * seconds));
                       });
    const auto snareLow = amplitudeOf (output, rate, 50.0, 0.31, 0.02), snareHigh = amplitudeOf (output, rate, 6000.0, 0.31, 0.02);
    const auto kickLow = amplitudeOf (output, rate, 50.0, 0.81, 0.02), kickHigh = amplitudeOf (output, rate, 6000.0, 0.81, 0.02);
    // The gentle split leaves about 5% of 6 kHz in the low band, which the snare leaves alone.
    check (snareLow > 0.35f && snareHigh < 0.03f && kickLow < 0.01f && kickHigh < 0.01f,
           "with Band on Trigger 2 only, the snare ducks the highs and leaves the lows, the kick ducks both (snare 50 Hz "
               + juce::String (snareLow, 3) + ", 6 kHz " + juce::String (snareHigh, 3) + "; kick " + juce::String (kickLow, 3)
               + ", " + juce::String (kickHigh, 3) + ")");
}
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    if (argc < 2)
    {
        std::cout << "usage: BPTriggerCheck <path to BP-Trigger.vst3>" << std::endl;
        return 2;
    }

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]).getFullPathName());

    if (found.size() != 1)
    {
        std::cout << "no BP-Trigger in " << argv[1] << std::endl;
        return 1;
    }

    const auto& description = *found[0];
    checkHostView (format, description);
    checkGridMode (format, description, 44100.0);
    checkGridMode (format, description, 48000.0);
    checkGridMode (format, description, 96000.0);
    checkSidechainModes (format, description);
    checkBandMode (format, description);
    checkTwoTriggers (format, description);

    std::cout << "\n" << (failures == 0 ? "All checks passed." : juce::String (failures) + " checks failed.") << std::endl;
    return failures == 0 ? 0 : 1;
}
