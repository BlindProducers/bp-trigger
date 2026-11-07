#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr auto stateId = "BPTriggerState";
constexpr auto maxPreduckMs = 10.0f;
constexpr auto maxRampDownMs = 10.0f;
constexpr auto gainSmoothingMs = 1.5f;
constexpr auto soloGateAttackMs = 1.0f;
constexpr auto transportSmoothingMs = 10.0f;
constexpr auto minimumRingmodSmoothingMs = 1.0f;
constexpr auto maximumRingmodSmoothingMs = 10.0f;
constexpr auto ringmodFullScaleDrive = 2.0f;
constexpr auto amplitudeModulationSmoothingMs = 10.0f;
constexpr auto amplitudeModulationEnvelopeReleaseMs = 100.0f;
constexpr auto modulationDcCutoffHz = 10.0;
constexpr auto minimumThresholdDb = -40.0f;
constexpr auto rearmThresholdRatio = 0.45f;     // a trigger re-arms once its detector falls this far below the threshold
constexpr auto minRetriggerSeconds = 0.015;
constexpr auto minimumDetectorReleaseMs = 1.0f;
constexpr auto maximumDetectorReleaseMs = 100.0f;
constexpr auto minimumBandCutoffHz = 20.0f;
constexpr auto maximumBandCutoffHz = 20000.0f;

constexpr auto curvesTag = "Curves";
constexpr auto editTriggerAttribute = "editTrigger";
constexpr auto curveTags = std::array<const char*, 4> { "Trigger1", "Trigger1High", "Trigger2", "Trigger2High" };

// Before curves had points of their own, each was 16 parameters, "curvePoint00" to "curvePoint15".
constexpr auto oldCurvePointCount = 16;
constexpr auto oldCurvePointPrefixes = std::array<const char*, 2> { "curvePoint", "highCurvePoint" };

const juce::StringArray sourceNames { "Sidechain", "Sidechain Left", "Sidechain Right",
                                      "Sidechain 2", "Sidechain 2 Left", "Sidechain 2 Right" };
const juce::StringArray divisionNames { "1 bar", "1/2", "1/2 triplet", "1/4", "1/4 triplet", "1/8", "1/8 triplet", "1/16" };

std::unique_ptr<juce::RangedAudioParameter> makeFloatParam (const juce::ParameterID& id,
                                                            const juce::String& name,
                                                            float min,
                                                            float max,
                                                            float defaultValue,
                                                            const juce::String& suffix = {},
                                                            float interval = 0.0f,
                                                            int decimalPlaces = 2)
{
    auto range = interval > 0.0f ? juce::NormalisableRange<float> (min, max, interval)
                                 : juce::NormalisableRange<float> (min, max);

    auto attributes = juce::AudioParameterFloatAttributes().withLabel (suffix);

    if (suffix.isNotEmpty())
    {
        attributes = attributes.withStringFromValueFunction (
            [suffix, decimalPlaces] (float value, int)
            {
                const auto separator = suffix == "x" ? "" : " ";
                return juce::String (value, decimalPlaces) + separator + suffix;
            })
            .withValueFromStringFunction (
                [] (const juce::String& text)
                {
                    return text.retainCharacters ("0123456789.-").getFloatValue();
                });
    }

    return std::make_unique<juce::AudioParameterFloat> (
        id, name, range, defaultValue, attributes);
}

// A number with up to the given decimals, without trailing zeros: "50%", "50.5%".
juce::String trimmedNumber (float value, int decimals)
{
    auto text = juce::String (value, decimals);

    if (text.containsChar ('.'))
        text = text.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");

    return text;
}

juce::String percentStringFromValue (float value, int)
{
    return trimmedNumber (value * 100.0f, 1) + "%";
}

float percentValueFromString (const juce::String& text)
{
    return juce::jlimit (0.0f, 1.0f, text.retainCharacters ("0123456789.-").getFloatValue() / 100.0f);
}

std::unique_ptr<juce::RangedAudioParameter> makePercentParam (const juce::ParameterID& id,
                                                              const juce::String& name,
                                                              float min,
                                                              float max,
                                                              float defaultValue)
{
    return std::make_unique<juce::AudioParameterFloat> (
        id, name,
        juce::NormalisableRange<float> (min, max, 0.001f),
        defaultValue,
        juce::AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction (percentStringFromValue)
            .withValueFromStringFunction (percentValueFromString));
}

juce::String thresholdStringFromValue (float value, int)
{
    return juce::String (juce::Decibels::gainToDecibels (juce::jmax (0.000001f, value)), 1) + " dB";
}

float thresholdValueFromString (const juce::String& text)
{
    const auto dB = juce::jlimit (minimumThresholdDb, 0.0f,
                                  text.retainCharacters ("0123456789.-").getFloatValue());
    return juce::jlimit (0.01f, 1.0f, juce::Decibels::decibelsToGain (dB));
}

std::unique_ptr<juce::RangedAudioParameter> makeThresholdParam (const juce::ParameterID& id, const juce::String& name)
{
    return std::make_unique<juce::AudioParameterFloat> (
        id, name,
        juce::NormalisableRange<float> (0.01f, 1.0f),
        0.05f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction (thresholdStringFromValue)
            .withValueFromStringFunction (thresholdValueFromString));
}

juce::String detectorReleaseStringFromValue (float value, int)
{
    return trimmedNumber (SidechainerAudioProcessor::detectorReleaseMsFromSensitivity (value), 1) + " ms";
}

float detectorReleaseValueFromString (const juce::String& text)
{
    const auto ms = juce::jlimit (minimumDetectorReleaseMs, maximumDetectorReleaseMs,
                                  text.retainCharacters ("0123456789.-").getFloatValue());
    return SidechainerAudioProcessor::sensitivityFromDetectorReleaseMs (ms);
}

std::unique_ptr<juce::RangedAudioParameter> makeDetectorReleaseParam (const juce::ParameterID& id, const juce::String& name)
{
    return std::make_unique<juce::AudioParameterFloat> (
        id, name,
        juce::NormalisableRange<float> (0.0f, 1.0f),
        0.5f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("ms")
            .withStringFromValueFunction (detectorReleaseStringFromValue)
            .withValueFromStringFunction (detectorReleaseValueFromString));
}

juce::String frequencyStringFromValue (float value, int)
{
    if (value >= 1000.0f)
        return trimmedNumber (value / 1000.0f, 2) + " kHz";

    return juce::String (juce::roundToInt (value)) + " Hz";
}

float frequencyValueFromString (const juce::String& text)
{
    const auto lower = text.toLowerCase();
    auto value = lower.retainCharacters ("0123456789.-").getFloatValue();

    if (lower.contains ("khz"))
        value *= 1000.0f;

    return juce::jlimit (minimumBandCutoffHz, maximumBandCutoffHz, value);
}

std::unique_ptr<juce::RangedAudioParameter> makeBandCutoffParam()
{
    auto range = juce::NormalisableRange<float> (minimumBandCutoffHz, maximumBandCutoffHz, 1.0f);
    range.setSkewForCentre (1000.0f);

    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "bandCutoff", 1 }, "Band Cutoff",
        range,
        300.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction (frequencyStringFromValue)
            .withValueFromStringFunction (frequencyValueFromString));
}
}


namespace
{
bool isSidechainLayout (const juce::AudioChannelSet& set)
{
    return set.isDisabled() || set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
}
}

juce::AudioProcessor::BusesProperties SidechainerAudioProcessor::makeBusesProperties()
{
    auto buses = BusesProperties()
                     .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                     .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true);

    // Audio Units hosts give an effect one sidechain.
    if (juce::PluginHostType::getPluginLoadedAs() != juce::AudioProcessor::wrapperType_AudioUnit)
        buses = buses.withInput ("Sidechain 2", juce::AudioChannelSet::stereo(), true);

    return buses.withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

float SidechainerAudioProcessor::detectorReleaseMsFromSensitivity (float sensitivity) noexcept
{
    return juce::jmap (juce::jlimit (0.0f, 1.0f, sensitivity),
                       maximumDetectorReleaseMs, minimumDetectorReleaseMs);
}

float SidechainerAudioProcessor::sensitivityFromDetectorReleaseMs (float ms) noexcept
{
    return juce::jlimit (0.0f, 1.0f,
                         (maximumDetectorReleaseMs - ms) / (maximumDetectorReleaseMs - minimumDetectorReleaseMs));
}

juce::String SidechainerAudioProcessor::triggerParameterId (int trigger, const juce::String& trigger1Id)
{
    if (trigger == 0)
        return trigger1Id;

    const auto name = trigger1Id == "triggerMode"     ? juce::String ("Mode")
                    : trigger1Id == "sidechainSolo"   ? juce::String ("Solo")
                                                      : trigger1Id.substring (0, 1).toUpperCase() + trigger1Id.substring (1);
    return "trigger" + juce::String (trigger + 1) + name;
}

void SidechainerAudioProcessor::CurveTables::fill (const Curve& curve) noexcept
{
    for (auto& table : tables)
        curve.fillTable (table);
}

void SidechainerAudioProcessor::CurveTables::publish (const Curve& curve) noexcept
{
    curve.fillTable (tables[(size_t) back]);
    back = middle.exchange (back | fresh, std::memory_order_acq_rel) & ~fresh;
}

const Curve::Table& SidechainerAudioProcessor::CurveTables::acquire() noexcept
{
    if ((middle.load (std::memory_order_relaxed) & fresh) != 0)
        front = middle.exchange (front, std::memory_order_acq_rel) & ~fresh;

    return tables[(size_t) front];
}

SidechainerAudioProcessor::SidechainerAudioProcessor()
    : AudioProcessor (makeBusesProperties()),
      parameters (*this, nullptr, stateId, createParameterLayout())
{
    for (int trigger = 0; trigger < triggerCount; ++trigger)
    {
        const auto raw = [this, trigger] (const char* id) { return parameters.getRawParameterValue (triggerParameterId (trigger, id)); };
        auto& p = triggerParameters[(size_t) trigger];
        p.mode = raw ("triggerMode");
        p.division = raw ("division");
        p.speed = raw ("speed");
        p.source = raw ("source");
        p.threshold = raw ("threshold");
        p.sensitivity = raw ("sensitivity");
        p.solo = raw ("sidechainSolo");
        p.depth = raw ("depth");
        p.highDepth = raw ("highDepth");
        p.band = raw ("bandEnabled");
        p.highCurve = raw ("highCurveEnabled");
    }

    preduckParam = parameters.getRawParameterValue ("preduck");
    rampDownParam = parameters.getRawParameterValue ("rampDown");
    rampShapeParam = parameters.getRawParameterValue ("rampShape");
    ringmodEnabledParam = parameters.getRawParameterValue ("ringmodEnabled");
    ringmodIntensityParam = parameters.getRawParameterValue ("ringmodIntensity");
    ringmodDuckedParam = parameters.getRawParameterValue ("ringmodDucked");
    amplitudeModulationEnabledParam = parameters.getRawParameterValue ("amplitudeModulationEnabled");
    amplitudeModulationDepthParam = parameters.getRawParameterValue ("amplitudeModulationDepth");
    bandCutoffParam = parameters.getRawParameterValue ("bandCutoff");
    mixParam = parameters.getRawParameterValue ("mix");

    curves.fill (Curve::makeDefault());
    for (auto& tables : curveTables)
        tables.fill (curves.front());

    // Pre-duck and Ramp Down set the latency; everything else is read at the start of each block.
    parameters.addParameterListener ("preduck", this);
    parameters.addParameterListener ("rampDown", this);
    refreshParameterCache();
}

SidechainerAudioProcessor::~SidechainerAudioProcessor()
{
    parameters.removeParameterListener ("preduck", this);
    parameters.removeParameterListener ("rampDown", this);
}

juce::AudioProcessorValueTreeState::ParameterLayout SidechainerAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    for (int trigger = 0; trigger < triggerCount; ++trigger)
    {
        // Trigger 2's parameters came in a later version than Trigger 1's.
        const auto version = trigger == 0 ? 1 : 2;
        const auto id = [trigger, version] (const char* trigger1Id) { return juce::ParameterID { triggerParameterId (trigger, trigger1Id), version }; };
        const auto name = "Trigger " + juce::String (trigger + 1) + " ";
        const auto modes = trigger == 0 ? juce::StringArray { "Grid", "Sidechain", "Grid and Sidechain" }
                                        : juce::StringArray { "Off", "Grid", "Sidechain", "Grid and Sidechain" };

        params.push_back (std::make_unique<juce::AudioParameterChoice> (id ("triggerMode"), name + "Mode", modes, 0));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (id ("division"), name + "Grid Division", divisionNames, 3));
        params.push_back (makeFloatParam (id ("speed"), name + "Speed", 0.25f, 4.0f, 1.0f, "x", 0.01f, 2));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { triggerParameterId (trigger, "source"), 2 },
                                                                        name + "Source", sourceNames, trigger == 0 ? 0 : 3));
        params.push_back (makeThresholdParam (id ("threshold"), name + "Threshold"));
        params.push_back (makeDetectorReleaseParam (id ("sensitivity"), name + "Detector Release"));
        params.push_back (std::make_unique<juce::AudioParameterBool> (id ("sidechainSolo"), name + "Sidechain Solo", false));
        params.push_back (makePercentParam (id ("depth"), name + "Depth", 0.0f, 1.0f, 1.0f));
        params.push_back (makePercentParam (id ("highDepth"), name + "High Depth", 0.0f, 1.0f, 1.0f));
        params.push_back (std::make_unique<juce::AudioParameterBool> (id ("bandEnabled"), name + "Band", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> (id ("highCurveEnabled"), name + "High Curve", false));
    }

    params.push_back (makeFloatParam ({ "rampDown", 1 }, "Ramp Down", 0.0f, maxRampDownMs, 0.0f, "ms", 0.1f, 1));
    params.push_back (makeFloatParam ({ "rampShape", 1 }, "Ramp Shape", 0.25f, 4.0f, 1.0f, "x", 0.01f, 2));
    params.push_back (makeFloatParam ({ "preduck", 1 }, "Pre-duck", 0.0f, maxPreduckMs, 0.0f, "ms", 0.1f, 1));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "ringmodEnabled", 1 }, "Ringmod Sidechain", false));
    params.push_back (makePercentParam ({ "ringmodIntensity", 1 }, "Ringmod Intensity", 0.0f, 1.0f, 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "ringmodDucked", 1 }, "Duck Ringmod", true));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "amplitudeModulationEnabled", 1 }, "Amplitude Modulation", false));
    params.push_back (makePercentParam ({ "amplitudeModulationDepth", 1 }, "Amplitude Modulation Depth", 0.0f, 1.0f, 1.0f));
    params.push_back (makeBandCutoffParam());
    params.push_back (makePercentParam ({ "mix", 1 }, "Mix", 0.0f, 1.0f, 1.0f));

    return { params.begin(), params.end() };
}

bool SidechainerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Keep the sidechains on plugin pins 3/4 and 5/6 in REAPER.
    // Mono or wider main input layouts move the sidechain buses to other pins.
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    for (int bus = 1; bus < layouts.inputBuses.size(); ++bus)
        if (! isSidechainLayout (layouts.getChannelSet (true, bus)))
            return false;

    return true;
}

void SidechainerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxLookaheadSamples = juce::jmax (1, juce::roundToInt (sampleRate * (maxPreduckMs + maxRampDownMs) / 1000.0));

    const auto delayLength = maxLookaheadSamples + samplesPerBlock + 8;
    mainDelay.setSize (juce::jmax (2, getTotalNumOutputChannels()), delayLength);
    sidechainDelay.setSize (2, delayLength);
    mainDelay.clear();
    sidechainDelay.clear();
    delayWritePosition = 0;

    for (auto& state : triggerStates)
    {
        state = {};
        state.samplesSinceTrigger = sampleRate;
    }

    smoothedRingmodDepth = {};
    amplitudeModulationEnvelope = {};
    clockedProcessingBlend = 0.0f;
    bandLowpassState.fill (0.0f);
    plainBandLowpassState.fill (0.0f);
    modulationDcState = {};

    // A Butterworth high-pass for what the ringmod and amplitude modulation add.
    const auto w0 = juce::MathConstants<double>::twoPi * modulationDcCutoffHz / sampleRate;
    const auto alpha = std::sin (w0) / juce::MathConstants<double>::sqrt2;   // sin (w0) / 2Q, with Q = 1 / sqrt 2
    const auto a0 = 1.0 + alpha;
    modulationDcB0 = 0.5 * (1.0 + std::cos (w0)) / a0;
    modulationDcB1 = -(1.0 + std::cos (w0)) / a0;
    modulationDcA1 = -2.0 * std::cos (w0) / a0;
    modulationDcA2 = (1.0 - alpha) / a0;
    refreshParameterCache();
    smoothedRingmodDucked = cachedRingmodDucked ? 1.0f : 0.0f;
    smoothedAmplitudeModulationDepth = cachedAmplitudeModulationEnabled ? cachedAmplitudeModulationDepth : 0.0f;
    updateLatencyForPreduck();
}

void SidechainerAudioProcessor::releaseResources()
{
    mainDelay.setSize (0, 0);
    sidechainDelay.setSize (0, 0);
}

void SidechainerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    refreshParameterCache();

    const auto totalOutputChannels = getTotalNumOutputChannels();
    const auto mainAudioChannels = juce::jmin (2, juce::jmin (getMainBusNumInputChannels(), totalOutputChannels));
    const auto numSamples = buffer.getNumSamples();
    const auto delaySize = mainDelay.getNumSamples();

    for (auto ch = getTotalNumInputChannels(); ch < totalOutputChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (delaySize <= 0)
        return;

    // Sidechain and Sidechain 2: each one's left and right channel, the one channel twice for a mono bus.
    // REAPER passes them in on plugin pins 3/4 and 5/6.
    struct SidechainBus
    {
        const float* left = nullptr;
        const float* right = nullptr;
    };

    std::array<SidechainBus, 2> sidechainBuses {};

    for (int i = 0; i < 2; ++i)
    {
        const auto* bus = getBus (true, i + 1);

        if (bus == nullptr || ! bus->isEnabled() || bus->getNumberOfChannels() == 0)
            continue;

        const auto first = bus->getChannelIndexInProcessBlockBuffer (0);

        if (first < buffer.getNumChannels())
        {
            auto& channels = sidechainBuses[(size_t) i];
            channels.left = buffer.getReadPointer (first);
            channels.right = bus->getNumberOfChannels() > 1 && first + 1 < buffer.getNumChannels() ? buffer.getReadPointer (first + 1)
                                                                                                  : channels.left;
        }
    }

    // Sources are numbered as their parameter lists them: the bus, then its left, then its right.
    const auto readSource = [&sidechainBuses] (int source, int sample) noexcept -> SourceSample
    {
        const auto& bus = sidechainBuses[(size_t) juce::jlimit (0, 1, source / 3)];

        if (bus.left == nullptr)
            return {};

        const auto left = bus.left[sample];
        const auto right = bus.right[sample];

        switch (source % 3)
        {
            case 1:  return { std::abs (left), left, left };
            case 2:  return { std::abs (right), right, right };
            default: return { 0.5f * (std::abs (left) + std::abs (right)), left, right };
        }
    };

    const auto mainDelaySamples = juce::jlimit (0, maxLookaheadSamples, reportedLatencySamples.load());
    const auto gainSmoothingCoeff = std::exp (-1.0f / static_cast<float> (0.001 * gainSmoothingMs * currentSampleRate));
    const auto transportSmoothingCoeff = std::exp (-1.0f / static_cast<float> (0.001 * transportSmoothingMs * currentSampleRate));
    const auto ringmodSmoothingMs = cachedRingmodEnabled
                                        ? juce::jlimit (minimumRingmodSmoothingMs,
                                                       maximumRingmodSmoothingMs,
                                                       juce::jmax (cachedRampDownMs, cachedPreduckMs))
                                        : minimumRingmodSmoothingMs;
    const auto ringmodSmoothingCoeff = std::exp (-1.0f / static_cast<float> (0.001 * ringmodSmoothingMs * currentSampleRate));
    const auto soloGateAttackCoeff = std::exp (-1.0f / static_cast<float> (0.001 * soloGateAttackMs * currentSampleRate));
    const auto amplitudeModulationSmoothingCoeff = std::exp (-1.0f / static_cast<float> (0.001 * amplitudeModulationSmoothingMs * currentSampleRate));
    const auto amplitudeModulationReleaseCoeff = std::exp (-1.0f / static_cast<float> (0.001 * amplitudeModulationEnvelopeReleaseMs * currentSampleRate));
    const auto targetAmplitudeModulationDepth = cachedAmplitudeModulationEnabled ? cachedAmplitudeModulationDepth : 0.0f;
    const auto transportBlendStep = 1.0f / static_cast<float> (juce::jmax (1, juce::roundToInt (currentSampleRate * transportSmoothingMs / 1000.0)));
    const auto bandCoeff = 1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                            * juce::jlimit (minimumBandCutoffHz,
                                                           juce::jmin (maximumBandCutoffHz, static_cast<float> (currentSampleRate * 0.45)),
                                                           cachedBandCutoffHz)
                                            / static_cast<float> (currentSampleRate));
    auto bpm = 120.0;
    std::optional<double> blockStartPpq;
    auto transportIsExplicitlyStopped = false;

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (auto positionBpm = position->getBpm())
                bpm = *positionBpm;

            if (auto ppq = position->getPpqPosition())
                blockStartPpq = *ppq;

            transportIsExplicitlyStopped = ! position->getIsPlaying();
        }
    }

    // What each trigger needs for this block. A trigger that follows the grid rests while the transport is stopped.
    struct TriggerBlock
    {
        const Curve::Table* curve = nullptr;
        const Curve::Table* highCurve = nullptr;
        double cycleBeats = 1.0, cycleSamples = 1.0, rampSamples = 0.0;
        float releaseCoeff = 0.0f;
        bool followsGrid = false, resting = false;
    };

    std::array<TriggerBlock, triggerCount> blocks {};
    auto anySidechainOnly = false, anyRunning = false, anySolo = false, bandSplit = false;

    for (int trigger = 0; trigger < triggerCount; ++trigger)
    {
        const auto& settings = cachedTriggers[(size_t) trigger];
        auto& block = blocks[(size_t) trigger];
        block.curve = &curveTables[(size_t) curveSlot (trigger, false)].acquire();
        block.highCurve = &curveTables[(size_t) curveSlot (trigger, true)].acquire();
        block.cycleBeats = getDivisionBeats (settings.division);
        block.cycleSamples = juce::jmax (1.0, block.cycleBeats * 60.0 / bpm * currentSampleRate);
        block.rampSamples = juce::jmin (block.cycleSamples * 0.5,
                                        juce::jmax (0.0, static_cast<double> (cachedRampDownMs) * currentSampleRate / 1000.0));
        block.releaseCoeff = std::exp (-1.0f / static_cast<float> (0.001 * detectorReleaseMsFromSensitivity (settings.sensitivity) * currentSampleRate));
        block.followsGrid = settings.mode == TriggerMode::grid || settings.mode == TriggerMode::gridAndSidechain;
        block.resting = transportIsExplicitlyStopped && block.followsGrid;

        anySidechainOnly = anySidechainOnly || settings.mode == TriggerMode::sidechain;
        anyRunning = anyRunning || (settings.mode != TriggerMode::off && ! block.resting);
        anySolo = anySolo || (settings.mode != TriggerMode::off && settings.solo);
        bandSplit = bandSplit || (settings.mode != TriggerMode::off && settings.band);
    }

    for (int sample = 0; sample < numSamples; ++sample)
    {
        for (int ch = 0; ch < mainAudioChannels; ++ch)
            mainDelay.setSample (ch, delayWritePosition, buffer.getSample (ch, sample));

        // Where both triggers duck, the deeper duck wins, in each band.
        auto gain = 1.0f, highGain = 1.0f;
        auto soloLeft = 0.0f, soloRight = 0.0f;
        SourceSample trigger1Source;

        for (int trigger = 0; trigger < triggerCount; ++trigger)
        {
            const auto& settings = cachedTriggers[(size_t) trigger];
            const auto& block = blocks[(size_t) trigger];
            auto& state = triggerStates[(size_t) trigger];

            if (settings.mode == TriggerMode::off)
            {
                // At rest, so it starts afresh when it's turned on.
                state.detectorEnvelope = 0.0f;
                state.soloGateGain = 0.0f;
                state.armed = true;
                state.elapsedSamples = 1.0e12;
                state.smoothedGain = 1.0f + gainSmoothingCoeff * (state.smoothedGain - 1.0f);
                state.smoothedHighGain = 1.0f + gainSmoothingCoeff * (state.smoothedHighGain - 1.0f);
                gain = std::min (gain, state.smoothedGain);
                highGain = std::min (highGain, state.smoothedHighGain);
                continue;
            }

            const auto source = readSource (settings.source, sample);

            if (trigger == 0)
                trigger1Source = source;

            state.detectorEnvelope = source.level > state.detectorEnvelope
                                         ? source.level
                                         : block.releaseCoeff * state.detectorEnvelope + (1.0f - block.releaseCoeff) * source.level;

            // Sidechain Solo plays the source while the detector is over the threshold. It fades in over
            // a millisecond and out at the Detector Release time, so its edges do not click.
            const auto soloGateTarget = state.detectorEnvelope >= settings.threshold ? 1.0f : 0.0f;
            const auto soloGateCoeff = soloGateTarget > state.soloGateGain ? soloGateAttackCoeff : block.releaseCoeff;
            state.soloGateGain = soloGateTarget + soloGateCoeff * (state.soloGateGain - soloGateTarget);

            if (settings.solo)
            {
                soloLeft += state.soloGateGain * source.left;
                soloRight += state.soloGateGain * source.right;
            }

            if (block.resting)
            {
                state.elapsedSamples = 1.0e12;
                state.armed = true;
                state.samplesSinceTrigger = currentSampleRate;
            }
            else if (settings.mode != TriggerMode::grid)
            {
                if (state.armed
                    && state.detectorEnvelope >= settings.threshold
                    && state.samplesSinceTrigger > currentSampleRate * minRetriggerSeconds)
                {
                    state.elapsedSamples = 0.0;
                    state.samplesSinceTrigger = 0.0;
                    state.armed = false;
                }
                else if (! state.armed && state.detectorEnvelope <= settings.threshold * rearmThresholdRatio)
                {
                    state.armed = true;
                }
            }

            state.samplesSinceTrigger += 1.0;

            auto phase = 1.0f;

            if (! block.resting && block.followsGrid)
            {
                auto gridPosition = state.freeRunningGridPhase;

                if (blockStartPpq.has_value())
                {
                    const auto ppq = *blockStartPpq + (static_cast<double> (sample) * bpm / (60.0 * currentSampleRate));
                    gridPosition = std::fmod (ppq, block.cycleBeats) / block.cycleBeats;
                }

                phase = getPhaseFromElapsedSamples (gridPosition * block.cycleSamples, block.cycleSamples, block.rampSamples, settings.speed);
            }

            const auto sidechainPhase = getPhaseFromElapsedSamples (state.elapsedSamples, block.cycleSamples, block.rampSamples, settings.speed);

            if (! block.resting
                && (settings.mode == TriggerMode::sidechain || (settings.mode == TriggerMode::gridAndSidechain && sidechainPhase < phase)))
                phase = sidechainPhase;

            const auto targetGain = block.resting ? 1.0f
                                                  : getGainForPhase (phase, getCurveValue (phase, *block.curve), settings.depth);
            // Without Band a trigger ducks the high band as the low; with Band it leaves the high band alone,
            // or ducks it with its High Curve.
            const auto targetHighGain = ! settings.band          ? targetGain
                                      : block.resting || ! settings.highCurve ? 1.0f
                                      : getGainForPhase (phase, getCurveValue (phase, *block.highCurve), settings.highDepth);
            const auto smoothingCoeff = block.resting ? transportSmoothingCoeff : gainSmoothingCoeff;
            state.smoothedGain = targetGain + smoothingCoeff * (state.smoothedGain - targetGain);
            state.smoothedHighGain = targetHighGain + smoothingCoeff * (state.smoothedHighGain - targetHighGain);
            gain = std::min (gain, state.smoothedGain);
            highGain = std::min (highGain, state.smoothedHighGain);

            if (block.resting)
            {
                state.freeRunningGridPhase = 0.0;
            }
            else
            {
                state.elapsedSamples += 1.0;
                state.freeRunningGridPhase += 1.0 / block.cycleSamples;

                while (state.freeRunningGridPhase >= 1.0)
                    state.freeRunningGridPhase -= 1.0;
            }
        }

        // A trigger in Sidechain mode works with the transport stopped; the grid modes fade out and back in with it.
        if (anySidechainOnly)
            clockedProcessingBlend = 1.0f;
        else
            clockedProcessingBlend = anyRunning ? juce::jmin (1.0f, clockedProcessingBlend + transportBlendStep)
                                                : juce::jmax (0.0f, clockedProcessingBlend - transportBlendStep);

        // The ringmod and amplitude modulation follow Trigger 1's source. They read it with the main audio's delay,
        // so a hit modulates the main audio where it sits in the host's timeline, not Pre-duck plus Ramp Down earlier.
        sidechainDelay.setSample (0, delayWritePosition, trigger1Source.left);
        sidechainDelay.setSample (1, delayWritePosition, trigger1Source.right);

        const auto readPos = (delayWritePosition + delaySize - mainDelaySamples) % delaySize;
        const auto delayedSidechainLeft = sidechainDelay.getSample (0, readPos);
        const auto delayedSidechainRight = sidechainDelay.getSample (1, readPos);

        // Each channel's envelope jumps to the sidechain's peaks and falls between them, so it never sits
        // below the sidechain's level.
        amplitudeModulationEnvelope[0] = std::max (std::abs (delayedSidechainLeft), amplitudeModulationReleaseCoeff * amplitudeModulationEnvelope[0]);
        amplitudeModulationEnvelope[1] = std::max (std::abs (delayedSidechainRight), amplitudeModulationReleaseCoeff * amplitudeModulationEnvelope[1]);
        smoothedAmplitudeModulationDepth = targetAmplitudeModulationDepth
                                           + amplitudeModulationSmoothingCoeff * (smoothedAmplitudeModulationDepth - targetAmplitudeModulationDepth);
        const auto targetRingmodDucked = cachedRingmodDucked ? 1.0f : 0.0f;
        smoothedRingmodDucked = targetRingmodDucked + ringmodSmoothingCoeff * (smoothedRingmodDucked - targetRingmodDucked);

        for (int ch = 0; ch < totalOutputChannels; ++ch)
        {
            if (anySolo)
            {
                const auto monitor = totalOutputChannels == 1 ? 0.5f * (soloLeft + soloRight) : (ch == 0 ? soloLeft : soloRight);
                buffer.setSample (ch, sample, monitor);
                continue;
            }

            const auto sourceChannel = juce::jmin (ch, mainAudioChannels - 1);
            const auto dry = mainDelay.getSample (sourceChannel, readPos);
            const auto ringSidechain = totalOutputChannels == 1 ? 0.5f * (delayedSidechainLeft + delayedSidechainRight)
                                                                : (ch == 0 ? delayedSidechainLeft : delayedSidechainRight);
            const auto targetRingDepth = cachedRingmodEnabled ? cachedRingmodIntensity : 0.0f;
            auto& ringDepth = smoothedRingmodDepth[(size_t) sourceChannel];
            ringDepth = targetRingDepth + ringmodSmoothingCoeff * (ringDepth - targetRingDepth);
            const auto ringAmount = ringDepth * std::abs (ringSidechain) * ringmodFullScaleDrive;
            const auto shapedInput = dry * (1.0f - smoothedRingmodDucked * ringAmount);
            auto wet = shapedInput * gain;

            if (bandSplit)
            {
                auto& lowpassState = bandLowpassState[(size_t) sourceChannel];
                lowpassState += bandCoeff * (shapedInput - lowpassState);

                const auto lowBand = lowpassState;
                const auto highBand = shapedInput - lowBand;
                wet = highBand * highGain + lowBand * gain;
            }
            else
            {
                bandLowpassState[(size_t) sourceChannel] = shapedInput;
            }

            // With Duck Ringmod off, the ringmod is added after the duck, so it plays through it.
            // Capping its amount at 1 + gain keeps the output within the dry level.
            wet -= (1.0f - smoothedRingmodDucked) * dry * std::min (ringAmount, 1.0f + gain);

            // Amplitude modulation follows the sidechain's waveform: full level at its peaks, down by
            // Depth times its envelope at its troughs, and full level again when it stops.
            const auto envelope = totalOutputChannels == 1 ? std::max (amplitudeModulationEnvelope[0], amplitudeModulationEnvelope[1])
                                                           : amplitudeModulationEnvelope[(size_t) juce::jmin (ch, 1)];
            wet *= juce::jlimit (0.0f, 1.0f, 1.0f - smoothedAmplitudeModulationDepth * 0.5f * (envelope - ringSidechain));

            // The ringmod and amplitude modulation turn main audio at the sidechain's frequencies into DC and
            // sub-bass. What they add goes through a 10 Hz high-pass; the main audio itself is not filtered.
            const auto channel = (size_t) sourceChannel;
            if (ringDepth > 0.0f || smoothedAmplitudeModulationDepth > 0.0f)
            {
                auto plainWet = dry * gain;
                auto& plainLowpass = plainBandLowpassState[channel];

                if (bandSplit)
                {
                    plainLowpass += bandCoeff * (dry - plainLowpass);
                    plainWet = (dry - plainLowpass) * highGain + plainLowpass * gain;
                }
                else
                {
                    plainLowpass = dry;
                }

                wet = plainWet + static_cast<float> (blockModulationDc (wet - plainWet, channel));
            }
            else
            {
                // Nothing is added, so the plain band split is the band split, and the high-pass is at rest.
                plainBandLowpassState[channel] = bandLowpassState[channel];
                modulationDcState[channel] = {};
            }

            buffer.setSample (ch, sample, dry + cachedMix * clockedProcessingBlend * (wet - dry));
        }

        delayWritePosition = (delayWritePosition + 1) % delaySize;
    }
}

double SidechainerAudioProcessor::blockModulationDc (double input, size_t channel) noexcept
{
    auto& state = modulationDcState[channel];
    const auto output = modulationDcB0 * input + state[0];
    state[0] = modulationDcB1 * input - modulationDcA1 * output + state[1];
    state[1] = modulationDcB0 * input - modulationDcA2 * output;
    return output;
}

float SidechainerAudioProcessor::getCurveValue (float phase, const Curve::Table& table) noexcept
{
    const auto position = juce::jlimit (0.0f, 1.0f, phase) * static_cast<float> (Curve::tableSize);
    const auto index = juce::jmin (Curve::tableSize - 1, static_cast<int> (position));
    const auto a = table[(size_t) index];
    return a + (position - static_cast<float> (index)) * (table[(size_t) index + 1] - a);
}

float SidechainerAudioProcessor::getPhaseFromElapsedSamples (double elapsedSamples,
                                                             double cycleSamples,
                                                             double rampSamples,
                                                             float speed) const noexcept
{
    cycleSamples = juce::jmax (1.0, cycleSamples);
    rampSamples = juce::jlimit (0.0, cycleSamples * 0.5, rampSamples);

    if (rampSamples > 0.0 && elapsedSamples < rampSamples)
    {
        const auto rampProgress = juce::jlimit (0.0, 1.0, elapsedSamples / rampSamples);
        return static_cast<float> (rampProgress - 1.0);
    }

    const auto curveElapsed = juce::jmax (0.0, elapsedSamples - rampSamples);
    return static_cast<float> (juce::jlimit (0.0, 1.0, curveElapsed * static_cast<double> (speed) / cycleSamples));
}

float SidechainerAudioProcessor::getGainForPhase (float phase, float curveValue, float depth) const noexcept
{
    const auto curveGain = juce::jlimit (0.0f, 1.5f, 1.0f - depth * (1.0f - curveValue));

    if (phase >= 0.0f)
        return curveGain;

    const auto rampProgress = juce::jlimit (0.0f, 1.0f, phase + 1.0f);
    const auto shapedProgress = std::pow (rampProgress, juce::jlimit (0.25f, 4.0f, cachedRampShape));
    return juce::jlimit (0.0f, 1.5f, 1.0f + shapedProgress * (curveGain - 1.0f));
}

double SidechainerAudioProcessor::getDivisionBeats (int division) noexcept
{
    switch (division)
    {
        case 0: return 4.0;
        case 1: return 2.0;
        case 2: return 4.0 / 3.0;
        case 3: return 1.0;
        case 4: return 2.0 / 3.0;
        case 5: return 0.5;
        case 6: return 1.0 / 3.0;
        case 7: return 0.25;
        default: return 1.0;
    }
}

void SidechainerAudioProcessor::parameterChanged (const juce::String&, float)
{
    updateLatencyForPreduck();
}

void SidechainerAudioProcessor::refreshParameterCache()
{
    for (int trigger = 0; trigger < triggerCount; ++trigger)
    {
        const auto& p = triggerParameters[(size_t) trigger];
        auto& settings = cachedTriggers[(size_t) trigger];
        // Trigger 1 has no Off, so its first choice is Grid.
        const auto mode = juce::roundToInt (p.mode->load()) + (trigger == 0 ? 1 : 0);
        settings.mode = static_cast<TriggerMode> (juce::jlimit (0, 3, mode));
        settings.division = juce::roundToInt (p.division->load());
        settings.speed = juce::jmax (0.01f, p.speed->load());
        settings.source = juce::jlimit (0, sourceNames.size() - 1, juce::roundToInt (p.source->load()));
        settings.threshold = p.threshold->load();
        settings.sensitivity = p.sensitivity->load();
        settings.solo = p.solo->load() >= 0.5f;
        settings.depth = p.depth->load();
        settings.highDepth = p.highDepth->load();
        settings.band = p.band->load() >= 0.5f;
        settings.highCurve = p.highCurve->load() >= 0.5f;
    }

    cachedMix = mixParam->load();
    cachedPreduckMs = preduckParam->load();
    cachedRampDownMs = rampDownParam->load();
    cachedRampShape = rampShapeParam->load();
    cachedRingmodEnabled = ringmodEnabledParam->load() >= 0.5f;
    cachedRingmodIntensity = ringmodIntensityParam->load();
    cachedRingmodDucked = ringmodDuckedParam->load() >= 0.5f;
    cachedAmplitudeModulationEnabled = amplitudeModulationEnabledParam->load() >= 0.5f;
    cachedAmplitudeModulationDepth = amplitudeModulationDepthParam->load();
    cachedBandCutoffHz = bandCutoffParam->load();
}

void SidechainerAudioProcessor::updateLatencyForPreduck()
{
    const auto latencySamples = juce::jlimit (0, maxLookaheadSamples,
                                             juce::roundToInt (currentSampleRate * (preduckParam->load() + rampDownParam->load()) / 1000.0));

    if (reportedLatencySamples.exchange (latencySamples) != latencySamples)
        setLatencySamples (latencySamples);
}

Curve SidechainerAudioProcessor::getCurve (int slot) const
{
    const juce::ScopedLock lock (curveLock);
    return curves[(size_t) slot];
}

void SidechainerAudioProcessor::setCurve (int slot, const Curve& curve)
{
    {
        const juce::ScopedLock lock (curveLock);

        if (curves[(size_t) slot] == curve)
            return;

        curves[(size_t) slot] = curve;
        curveTables[(size_t) slot].publish (curve);
    }

    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void SidechainerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto xml = parameters.copyState().createXml();

    if (xml == nullptr)
        return;

    {
        const juce::ScopedLock lock (curveLock);
        auto* curvesXml = xml->createNewChildElement (curvesTag);

        for (size_t i = 0; i < curves.size(); ++i)
            curvesXml->addChildElement (new juce::XmlElement (curves[i].toXml (curveTags[i])));
    }

    xml->setAttribute (editTriggerAttribute, editTrigger.load());
    copyXmlToBinary (*xml, destData);
}

void SidechainerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    std::array<Curve, curveCount> loaded;
    loaded.fill (Curve::makeDefault());

    if (auto* curvesXml = xml->getChildByName (curvesTag))
    {
        for (size_t i = 0; i < loaded.size(); ++i)
            if (auto* curveXml = curvesXml->getChildByName (curveTags[i]))
                loaded[i].loadFromXml (*curveXml);

        xml->removeChildElement (curvesXml, true);
    }

    // A project from before curves had points of their own: Trigger 1's curves were 16 parameters each.
    for (size_t high = 0; high < oldCurvePointPrefixes.size(); ++high)
    {
        std::vector<float> values (oldCurvePointCount, -1.0f);

        for (auto* param = xml->getFirstChildElement(); param != nullptr;)
        {
            auto* next = param->getNextElement();
            const auto id = param->getStringAttribute ("id");

            if (id.startsWith (oldCurvePointPrefixes[high]) && id.length() == juce::String (oldCurvePointPrefixes[high]).length() + 2)
            {
                const auto index = id.getLastCharacters (2).getIntValue();

                if (juce::isPositiveAndBelow (index, oldCurvePointCount))
                    values[(size_t) index] = static_cast<float> (param->getDoubleAttribute ("value"));

                xml->removeChildElement (param, true);
            }

            param = next;
        }

        if (std::none_of (values.begin(), values.end(), [] (float value) { return value < 0.0f; }))
            loaded[(size_t) curveSlot (0, high == 1)] = Curve::fromEvenPoints (values);
    }

    editTrigger = juce::jlimit (0, triggerCount - 1, xml->getIntAttribute (editTriggerAttribute));
    xml->removeAttribute (editTriggerAttribute);
    parameters.replaceState (juce::ValueTree::fromXml (*xml));

    {
        const juce::ScopedLock lock (curveLock);
        curves = loaded;

        for (size_t i = 0; i < curves.size(); ++i)
            curveTables[i].publish (curves[i]);
    }

    sendChangeMessage();
}

juce::AudioProcessorEditor* SidechainerAudioProcessor::createEditor()
{
    return new SidechainerAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SidechainerAudioProcessor();
}
