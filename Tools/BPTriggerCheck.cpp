// Offline host for the built BP-Trigger VST3. Loads it as a host does and
// checks its buses, parameters, texts, latency and saved state, and the gain
// it applies in each trigger mode against the curve it was given.
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
        std::vector<float> output;
        ppqAt.clear();
        const auto total = static_cast<int> (seconds * sampleRate);
        juce::MidiBuffer midi;

        for (int start = 0; start < total; start += blockSize)
        {
            const auto count = juce::jmin (blockSize, total - start);
            juce::AudioBuffer<float> buffer (4, count);

            for (int i = 0; i < count; ++i)
            {
                const auto t = playHead.timeInSamples + i;
                buffer.setSample (0, i, main (t));
                buffer.setSample (1, i, main (t));
                buffer.setSample (2, i, sidechain (t));
                buffer.setSample (3, i, sidechain (t));
            }

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

// The default curve: point i of 16 is (i / 15) squared, in the whole percents a host's
// value takes, read with straight lines between points.
float defaultCurve (double phase)
{
    const auto point = [] (int i) { return std::round (std::pow (i / 15.0, 2.0) * 100.0) / 100.0; };
    const auto scaled = juce::jlimit (0.0, 1.0, phase) * 15.0;
    const auto index = juce::jlimit (0, 14, static_cast<int> (scaled));
    return static_cast<float> (point (index) + (point (index + 1) - point (index)) * (scaled - index));
}

using Expected = std::function<float (size_t)>;

// The expected gain of each sample from its place in a cycle of the grid, `cycleBeats` long.
Expected onGrid (const Host& host, float cycleBeats, std::function<float (double)> gainAtPosition)
{
    return [&host, cycleBeats, gainAtPosition] (size_t i)
    {
        const auto beats = static_cast<double> (cycleBeats);
        return gainAtPosition (std::fmod (host.ppqAt[i], beats) / beats);
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
    host.set ("Speed", "2");
    output = host.run (2.0, constant (0.5f), constant (0.0f));
    check (largestGainError (output, rate, 0.5, 2.0, 0.5f, onGrid (host, 1.0f, [] (double p) { return defaultCurve (p * 2.0); })) < 0.002f,
           "Speed 2.00x plays the curve in half the time, then holds full level");

    host.reset();
    host.set ("Speed", "1");
    host.set ("Depth", "50");
    output = host.run (2.0, constant (0.5f), constant (0.0f));
    check (largestGainError (output, rate, 0.5, 2.0, 0.5f, onGrid (host, 1.0f, [] (double p) { return 1.0f - 0.5f * (1.0f - defaultCurve (p)); })) < 0.002f,
           "Depth 50% halves the reduction");

    host.reset();
    host.set ("Depth", "100");
    host.set ("Grid Division", "1/8");
    output = host.run (2.0, constant (0.5f), constant (0.0f));
    check (largestGainError (output, rate, 0.5, 2.0, 0.5f, onGrid (host, 0.5f, defaultCurve)) < 0.002f, "1/8 notes run the curve twice a beat");

    host.reset();
    host.set ("Grid Division", "1/4 triplet");
    output = host.run (2.0, constant (0.5f), constant (0.0f));
    check (largestGainError (output, rate, 0.5, 2.0, 0.5f, onGrid (host, 2.0f / 3.0f, defaultCurve)) < 0.002f, "1/4 triplets run the curve three times in two beats");

    host.reset();
    host.set ("Grid Division", "1/4");
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

    host.set ("Trigger Mode", "Sidechain");
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
    host.set ("Sidechain Threshold", "-40");
    output = host.run (1.0, constant (0.5f), hit (0.02f));
    check (gainAt (output, rate, 0.26, 0.5f) < 0.01f, "with the threshold at -40 dB the same hit ducks");
    host.set ("Sidechain Threshold", "-26");

    std::cout << "\nGrid and Sidechain:" << std::endl;
    host.reset();
    host.set ("Trigger Mode", "Grid and Sidechain");
    host.playHead.playing = true;
    const auto lateHit = static_cast<juce::int64> (0.9 * rate);
    output = host.run (1.2, constant (0.5f), [lateHit] (juce::int64 t) { return t >= lateHit && t < lateHit + 240 ? 0.5f : 0.0f; });
    check (std::abs (gainAt (output, rate, 0.85, 0.5f) - defaultCurve (0.7)) < 0.01f, "the grid shapes the beat");
    check (gainAt (output, rate, 0.91, 0.5f) < 0.01f, "a hit late in the beat takes over and ducks at once");

    std::cout << "\nSidechain Solo:" << std::endl;
    host.reset();
    host.set ("Trigger Mode", "Grid");
    host.set ("Sidechain Solo", "1");
    const auto sine = [rate] (float level) -> Signal
    {
        return [rate, level] (juce::int64 t) { return level * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * 1000.0 * static_cast<double> (t) / rate)); };
    };
    output = host.run (0.5, constant (0.5f), sine (0.3f));
    auto largest = 0.0f;
    for (size_t i = 48; i < output.size(); ++i)
        largest = juce::jmax (largest, std::abs (output[i] - sine (0.3f) (static_cast<juce::int64> (i))));
    check (largest < 1.0e-6f, "a sidechain above the threshold is heard instead of the input");

    host.reset();
    output = host.run (0.5, constant (0.5f), sine (0.01f));
    check (std::all_of (output.begin() + 48, output.end(), [] (float x) { return x == 0.0f; }), "a sidechain below the threshold is silent");
    host.set ("Sidechain Solo", "0");

    std::cout << "\nRingmod:" << std::endl;
    host.reset();
    host.set ("Trigger Mode", "Sidechain");
    host.set ("Ringmod Sidechain", "1");
    output = host.run (0.5, constant (0.5f), constant (0.04f));
    check (std::abs (output[(size_t) (0.4 * rate)] - 0.5f * (1.0f - 2.0f * 0.04f)) < 0.001f, "Ringmod Intensity 100% multiplies by the sidechain");
    host.set ("Ringmod Intensity", "50");
    output = host.run (0.5, constant (0.5f), constant (0.04f));
    check (std::abs (output[(size_t) (0.4 * rate)] - 0.5f * (1.0f - 0.04f)) < 0.001f, "Ringmod Intensity 50% does half as much");

    // A sidechain of 0.3 triggers the curve; 0.1 s in, the gain is ducked to g. Ducked, the output is
    // 0.5 g (1 - 0.6); with Duck Ringmod off the ringmod passes the duck, so it is 0.5 (g - 0.6).
    host.set ("Ringmod Intensity", "100");
    host.reset();
    const auto ducked = host.run (0.2, constant (0.5f), constant (0.3f))[(size_t) (0.1 * rate)];
    const auto duckedGain = ducked / (0.5f * 0.4f);
    host.set ("Duck Ringmod", "0");
    host.reset();
    const auto unducked = host.run (0.2, constant (0.5f), constant (0.3f))[(size_t) (0.1 * rate)];
    check (duckedGain < 0.9f && std::abs (unducked - 0.5f * (duckedGain - 0.6f)) < 0.001f,
           "with Duck Ringmod off, the ringmod plays through the duck (gain " + juce::String (duckedGain, 3) + ")");
    host.set ("Duck Ringmod", "1");
    host.set ("Ringmod Sidechain", "0");
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
    host.set ("Band", "1");
    host.set ("Band Cutoff", "300");
    output = host.run (1.2, lowAndHigh, constant (0.0f));
    const auto high = amplitudeOf (output, rate, 6000.0, 1.01, 0.04);
    const auto low = amplitudeOf (output, rate, 50.0, 1.01, 0.04);
    check (high > 0.37f && low < 0.1f, "with Band the curve ducks below 300 Hz and leaves 6 kHz alone (6 kHz "
                                         + juce::String (high, 3) + ", 50 Hz " + juce::String (low, 3) + ")");

    host.reset();
    host.set ("High Curve", "1");
    output = host.run (1.2, lowAndHigh, constant (0.0f));
    check (amplitudeOf (output, rate, 6000.0, 1.01, 0.04) < 0.01f, "High Curve ducks the high band with its own curve");

    host.reset();
    host.set ("High Depth", "0");
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
    check (plugin.getBusCount (true) == 2 && plugin.getBusCount (false) == 1, "two input buses (main and sidechain) and one output");
    check (plugin.getChannelCountOfBus (true, 0) == 2 && plugin.getChannelCountOfBus (true, 1) == 2 && plugin.getChannelCountOfBus (false, 0) == 2,
           "all stereo, so the sidechain sits on pins 3 and 4 in REAPER");

    juce::StringArray names;
    for (auto* p : plugin.getParameters())
        names.add (p->getName (128));

    auto allThere = true;
    for (auto name : { "Depth", "High Depth", "Mix", "Trigger Mode", "Grid Division", "Speed", "Pre-duck", "Ramp Down", "Ramp Shape",
                       "Sidechain Threshold", "Detector Release", "Sidechain Solo", "Ringmod Sidechain", "Ringmod Intensity", "Duck Ringmod", "Band",
                       "High Curve", "Band Cutoff" })
        allThere = allThere && names.contains (name);
    for (int i = 1; i <= 16; ++i)
        allThere = allThere && names.contains ("Curve Point " + juce::String (i)) && names.contains ("High Curve Point " + juce::String (i));
    check (allThere, "all 18 controls and both sets of 16 curve points are automatable (" + juce::String (names.size()) + " parameters)");

    const auto text = [&host] (const juce::String& name)
    {
        auto* p = host.parameter (name);
        return p != nullptr ? p->getCurrentValueAsText() : juce::String();
    };
    check (text ("Trigger Mode") == "Grid" && text ("Grid Division") == "1/4" && text ("Speed") == "1.00x"
               && text ("Sidechain Threshold") == "-26.0 dB" && text ("Detector Release") == "50 ms" && text ("Band Cutoff") == "300 Hz"
               && text ("Depth") == "100%" && text ("Pre-duck") == "0.0 ms",
           "values read as text: " + text ("Trigger Mode") + ", " + text ("Grid Division") + ", " + text ("Speed") + ", "
               + text ("Sidechain Threshold") + ", " + text ("Detector Release") + ", " + text ("Band Cutoff") + ", "
               + text ("Depth") + ", " + text ("Pre-duck"));

    host.set ("Band Cutoff", "1.5 kHz");
    host.set ("Detector Release", "10");
    check (text ("Band Cutoff") == "1.50 kHz" && text ("Detector Release") == "10 ms", "typed values are understood (1.5 kHz, 10 ms)");

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

    host.set ("Depth", "37");
    host.set ("Trigger Mode", "Grid and Sidechain");
    host.set ("Grid Division", "1/8 triplet");
    host.set ("Curve Point 5", "42");
    host.set ("High Curve Point 9", "7");
    host.set ("Band", "1");
    host.run (0.1, constant (0.0f), constant (0.0f));   // a host hands parameter changes over with the next block
    juce::MemoryBlock state;
    plugin.getStateInformation (state);

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
    check (same && copy.parameter ("Curve Point 5")->getCurrentValueAsText() == "42%", "a saved project restores every setting");
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

    std::cout << "\n" << (failures == 0 ? "All checks passed." : juce::String (failures) + " checks failed.") << std::endl;
    return failures == 0 ? 0 : 1;
}
