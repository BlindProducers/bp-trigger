#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr auto stateId = "BPTriggerState";
constexpr auto maxPreduckMs = 10.0f;
constexpr auto maxRampDownMs = 10.0f;
constexpr auto gainSmoothingMs = 1.5f;
constexpr auto transportSmoothingMs = 10.0f;
constexpr auto minimumRingmodSmoothingMs = 1.0f;
constexpr auto maximumRingmodSmoothingMs = 10.0f;
constexpr auto ringmodFullScaleDrive = 2.0f;
constexpr auto minimumRingmodTriggerFadeMs = 0.5f;
constexpr auto minimumThresholdDb = -40.0f;
constexpr auto minimumDetectorReleaseMs = 1.0f;
constexpr auto maximumDetectorReleaseMs = 100.0f;
constexpr auto minimumBandCutoffHz = 20.0f;
constexpr auto maximumBandCutoffHz = 20000.0f;

juce::String curvePointId (int index)
{
    return "curvePoint" + juce::String (index).paddedLeft ('0', 2);
}

juce::String highCurvePointId (int index)
{
    return "highCurvePoint" + juce::String (index).paddedLeft ('0', 2);
}

std::unique_ptr<juce::RangedAudioParameter> makeFloatParam (const juce::String& id,
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
        juce::ParameterID { id, 1 }, name, range, defaultValue,
        attributes);
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

std::unique_ptr<juce::RangedAudioParameter> makePercentParam (const juce::String& id,
                                                              const juce::String& name,
                                                              float min,
                                                              float max,
                                                              float defaultValue)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name,
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

std::unique_ptr<juce::RangedAudioParameter> makeThresholdParam()
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "threshold", 1 }, "Sidechain Threshold",
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

std::unique_ptr<juce::RangedAudioParameter> makeDetectorReleaseParam()
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "sensitivity", 1 }, "Detector Release",
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

SidechainerAudioProcessor::SidechainerAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, stateId, createParameterLayout())
{
    depthParam = parameters.getRawParameterValue ("depth");
    highDepthParam = parameters.getRawParameterValue ("highDepth");
    mixParam = parameters.getRawParameterValue ("mix");
    modeParam = parameters.getRawParameterValue ("triggerMode");
    divisionParam = parameters.getRawParameterValue ("division");
    speedParam = parameters.getRawParameterValue ("speed");
    preduckParam = parameters.getRawParameterValue ("preduck");
    rampDownParam = parameters.getRawParameterValue ("rampDown");
    rampShapeParam = parameters.getRawParameterValue ("rampShape");
    thresholdParam = parameters.getRawParameterValue ("threshold");
    sensitivityParam = parameters.getRawParameterValue ("sensitivity");
    sidechainSoloParam = parameters.getRawParameterValue ("sidechainSolo");
    ringmodEnabledParam = parameters.getRawParameterValue ("ringmodEnabled");
    ringmodIntensityParam = parameters.getRawParameterValue ("ringmodIntensity");
    bandEnabledParam = parameters.getRawParameterValue ("bandEnabled");
    highCurveEnabledParam = parameters.getRawParameterValue ("highCurveEnabled");
    bandCutoffParam = parameters.getRawParameterValue ("bandCutoff");

    for (int i = 0; i < curvePointCount; ++i)
        curvePoints[(size_t) i] = parameters.getRawParameterValue (curvePointId (i));

    for (int i = 0; i < curvePointCount; ++i)
        highCurvePoints[(size_t) i] = parameters.getRawParameterValue (highCurvePointId (i));

    for (auto id : { "triggerMode", "division", "speed", "preduck", "rampDown", "rampShape", "threshold", "sensitivity",
                     "sidechainSolo", "ringmodEnabled", "ringmodIntensity", "bandEnabled", "highCurveEnabled",
                     "bandCutoff", "depth", "highDepth", "mix" })
        parameters.addParameterListener (id, this);

    refreshParameterCache();
}

SidechainerAudioProcessor::~SidechainerAudioProcessor()
{
    for (auto id : { "triggerMode", "division", "speed", "preduck", "rampDown", "rampShape", "threshold", "sensitivity",
                     "sidechainSolo", "ringmodEnabled", "ringmodIntensity", "bandEnabled", "highCurveEnabled",
                     "bandCutoff", "depth", "highDepth", "mix" })
        parameters.removeParameterListener (id, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout SidechainerAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (makePercentParam ("depth", "Depth", 0.0f, 1.0f, 1.0f));
    params.push_back (makePercentParam ("highDepth", "High Depth", 0.0f, 1.0f, 1.0f));
    params.push_back (makePercentParam ("mix", "Mix", 0.0f, 1.0f, 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "triggerMode", 1 }, "Trigger Mode",
        juce::StringArray { "Grid", "Sidechain", "Grid and Sidechain" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "division", 1 }, "Grid Division",
        juce::StringArray { "1 bar", "1/2", "1/2 triplet", "1/4", "1/4 triplet",
                            "1/8", "1/8 triplet", "1/16" },
        3));
    params.push_back (makeFloatParam ("speed", "Speed", 0.25f, 4.0f, 1.0f, "x", 0.01f, 2));
    params.push_back (makeFloatParam ("preduck", "Pre-duck", 0.0f, maxPreduckMs, 0.0f, "ms", 0.1f, 1));
    params.push_back (makeFloatParam ("rampDown", "Ramp Down", 0.0f, maxRampDownMs, 0.0f, "ms", 0.1f, 1));
    params.push_back (makeFloatParam ("rampShape", "Ramp Shape", 0.25f, 4.0f, 1.0f, "x", 0.01f, 2));
    params.push_back (makeThresholdParam());
    params.push_back (makeDetectorReleaseParam());
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "sidechainSolo", 1 }, "Sidechain Solo", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "ringmodEnabled", 1 }, "Ringmod Sidechain", false));
    params.push_back (makePercentParam ("ringmodIntensity", "Ringmod Intensity", 0.0f, 1.0f, 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bandEnabled", 1 }, "Band", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "highCurveEnabled", 1 }, "High Curve", false));
    params.push_back (makeBandCutoffParam());

    for (int i = 0; i < curvePointCount; ++i)
    {
        const auto position = static_cast<float> (i) / static_cast<float> (curvePointCount - 1);
        const auto defaultValue = std::round (position * position * 100.0f) / 100.0f; // whole percents
        params.push_back (makePercentParam (curvePointId (i), "Curve Point " + juce::String (i + 1),
                                            0.0f, 1.0f, defaultValue));
    }

    for (int i = 0; i < curvePointCount; ++i)
    {
        const auto position = static_cast<float> (i) / static_cast<float> (curvePointCount - 1);
        const auto defaultValue = std::round (position * position * 100.0f) / 100.0f; // whole percents
        params.push_back (makePercentParam (highCurvePointId (i), "High Curve Point " + juce::String (i + 1),
                                            0.0f, 1.0f, defaultValue));
    }

    return { params.begin(), params.end() };
}

bool SidechainerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    // Keep the auxiliary sidechain on plugin pins 3/4 in REAPER.
    // Mono or wider main input layouts move the sidechain bus to other pins.
    if (mainIn != juce::AudioChannelSet::stereo()
        || mainOut != juce::AudioChannelSet::stereo())
        return false;

    const auto sidechain = layouts.getChannelSet (true, 1);
    return sidechain.isDisabled() || sidechain == juce::AudioChannelSet::mono() || sidechain == juce::AudioChannelSet::stereo();
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

    sidechainEnvelopePhase = 1.0f;
    sidechainEnvelopeElapsedSamples = 1.0e12;
    samplesSinceLastSidechainTrigger = sampleRate;
    detectorEnvelope = 0.0f;
    previousDetectorEnvelope = 0.0f;
    smoothedGain = 1.0f;
    smoothedHighGain = 1.0f;
    smoothedRingmodDepth = {};
    clockedProcessingBlend = 0.0f;
    sidechainTriggerArmed = true;
    bandLowpassState.fill (0.0f);
    freeRunningGridPhase = 0.0;
    refreshParameterCache();
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

    const auto totalInputChannels = getTotalNumInputChannels();
    const auto totalOutputChannels = getTotalNumOutputChannels();
    const auto mainChannels = getMainBusNumInputChannels();
    const auto mainAudioChannels = juce::jmin (2, juce::jmin (mainChannels, totalOutputChannels));
    const auto sidechainChannels = getChannelCountOfBus (true, 1);
    const auto numSamples = buffer.getNumSamples();
    const auto delaySize = mainDelay.getNumSamples();

    for (auto ch = totalInputChannels; ch < totalOutputChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (delaySize <= 0)
        return;

    const auto mainDelaySamples = juce::jlimit (0, maxLookaheadSamples, reportedLatencySamples);
    const auto mode = static_cast<TriggerMode> (cachedMode);
    const auto gridCycleBeats = static_cast<double> (getDivisionBeats());
    const auto gainSmoothingCoeff = std::exp (-1.0f / static_cast<float> (0.001 * gainSmoothingMs * currentSampleRate));
    const auto transportSmoothingCoeff = std::exp (-1.0f / static_cast<float> (0.001 * transportSmoothingMs * currentSampleRate));
    const auto ringmodSmoothingMs = cachedRingmodEnabled
                                        ? juce::jlimit (minimumRingmodSmoothingMs,
                                                       maximumRingmodSmoothingMs,
                                                       juce::jmax (cachedRampDownMs, cachedPreduckMs))
                                        : minimumRingmodSmoothingMs;
    const auto ringmodSmoothingCoeff = std::exp (-1.0f / static_cast<float> (0.001 * ringmodSmoothingMs * currentSampleRate));
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

    const auto bypassClockedProcessing = transportIsExplicitlyStopped
                                         && (mode == TriggerMode::grid || mode == TriggerMode::gridAndSidechain);
    const auto gridCycleSamples = juce::jmax (1.0, gridCycleBeats * 60.0 / bpm * currentSampleRate);
    const auto rampDownSamples = juce::jmin (gridCycleSamples * 0.5,
                                             juce::jmax (0.0, static_cast<double> (cachedRampDownMs) * currentSampleRate / 1000.0));

    for (int sample = 0; sample < numSamples; ++sample)
    {
        for (int ch = 0; ch < mainAudioChannels; ++ch)
            mainDelay.setSample (ch, delayWritePosition, buffer.getSample (ch, sample));

        sidechainDelay.setSample (0, delayWritePosition, 0.0f);
        sidechainDelay.setSample (1, delayWritePosition, 0.0f);

        float sidechainLevel = 0.0f;
        int sidechainSources = 0;

        // REAPER exposes this stereo sidechain bus as plugin input channels 3/4.
        auto addSidechainBus = [&]
        {
            auto added = false;

            for (int ch = 0; ch < juce::jmin (2, sidechainChannels); ++ch)
            {
                const auto sourceChannel = mainChannels + ch;
                if (sourceChannel < totalInputChannels)
                {
                    const auto existing = sidechainDelay.getSample (ch, delayWritePosition);
                    sidechainDelay.setSample (ch, delayWritePosition, existing + buffer.getSample (sourceChannel, sample));
                    added = true;
                }
            }

            if (added)
                sidechainSources = juce::jmax (sidechainSources, juce::jmin (2, sidechainChannels));
        };

        addSidechainBus();

        const auto sidechainReadPos = delayWritePosition;
        for (int ch = 0; ch < juce::jmax (1, sidechainSources); ++ch)
            sidechainLevel += std::abs (sidechainDelay.getSample (ch, sidechainReadPos));

        sidechainLevel /= static_cast<float> (juce::jmax (1, sidechainSources));
        const auto sidechainMonitorLeft = sidechainDelay.getSample (0, sidechainReadPos);
        const auto sidechainMonitorRight = sidechainSources == 1 ? sidechainMonitorLeft
                                                                 : sidechainDelay.getSample (1, sidechainReadPos);

        const auto releaseMs = detectorReleaseMsFromSensitivity (cachedSensitivity);
        const auto releaseCoeff = std::exp (-1.0f / static_cast<float> (0.001 * releaseMs * currentSampleRate));
        detectorEnvelope = sidechainLevel > detectorEnvelope
                               ? sidechainLevel
                               : releaseCoeff * detectorEnvelope + (1.0f - releaseCoeff) * sidechainLevel;
        const auto thresholdedSidechainMonitor = detectorEnvelope >= cachedThreshold;

        if (bypassClockedProcessing)
        {
            sidechainEnvelopePhase = 1.0f;
            sidechainEnvelopeElapsedSamples = 1.0e12;
            sidechainTriggerArmed = true;
            samplesSinceLastSidechainTrigger = currentSampleRate;
        }
        else if (mode != TriggerMode::grid)
        {
            const auto resetThreshold = cachedThreshold * 0.45f;
            const auto minRetriggerSamples = currentSampleRate * 0.015;

            if (sidechainTriggerArmed
                && detectorEnvelope >= cachedThreshold
                && samplesSinceLastSidechainTrigger > minRetriggerSamples)
            {
                triggerSidechainEnvelope();
                sidechainTriggerArmed = false;
            }
            else if (! sidechainTriggerArmed && detectorEnvelope <= resetThreshold)
            {
                sidechainTriggerArmed = true;
            }
        }

        previousDetectorEnvelope = detectorEnvelope;
        samplesSinceLastSidechainTrigger += 1.0;

        float phase = 1.0f;
        if (! bypassClockedProcessing && (mode == TriggerMode::grid || mode == TriggerMode::gridAndSidechain))
        {
            if (blockStartPpq.has_value() && gridCycleBeats > 0.0)
            {
                const auto ppq = *blockStartPpq + (static_cast<double> (sample) * bpm / (60.0 * currentSampleRate));
                const auto gridPosition = std::fmod (ppq, gridCycleBeats) / gridCycleBeats;
                phase = getPhaseFromElapsedSamples (gridPosition * gridCycleSamples, gridCycleSamples, rampDownSamples);
            }
            else
            {
                phase = getPhaseFromElapsedSamples (freeRunningGridPhase * gridCycleSamples, gridCycleSamples, rampDownSamples);
            }
        }

        sidechainEnvelopePhase = getPhaseFromElapsedSamples (sidechainEnvelopeElapsedSamples, gridCycleSamples, rampDownSamples);

        if (! bypassClockedProcessing
            && (mode == TriggerMode::sidechain || (mode == TriggerMode::gridAndSidechain && sidechainEnvelopePhase < phase)))
            phase = sidechainEnvelopePhase;

        const auto curve = getCurveValue (phase, curvePoints);
        const auto highCurve = getCurveValue (phase, highCurvePoints);
        const auto targetGain = bypassClockedProcessing
                                    ? 1.0f
                                    : getGainForPhase (phase, curve, cachedDepth);
        const auto targetHighGain = ! bypassClockedProcessing && cachedBandEnabled && cachedHighCurveEnabled
                                        ? getGainForPhase (phase, highCurve, cachedHighDepth)
                                        : 1.0f;
        auto ringmodTriggerFade = 1.0f;
        if (cachedRingmodEnabled && mode != TriggerMode::grid && sidechainEnvelopeElapsedSamples < 1.0e11)
        {
            const auto fadeMs = juce::jmax (minimumRingmodTriggerFadeMs, juce::jmax (cachedRampDownMs, cachedPreduckMs));
            const auto fadeSamples = juce::jmax (1.0, static_cast<double> (fadeMs) * currentSampleRate / 1000.0);

            if (sidechainEnvelopeElapsedSamples < fadeSamples)
            {
                const auto progress = juce::jlimit (0.0f, 1.0f,
                                                    static_cast<float> (sidechainEnvelopeElapsedSamples / fadeSamples));
                ringmodTriggerFade = std::pow (progress, juce::jlimit (0.25f, 4.0f, cachedRampShape));
            }
        }
        const auto envelopeSmoothingCoeff = bypassClockedProcessing ? transportSmoothingCoeff : gainSmoothingCoeff;
        smoothedGain = targetGain + envelopeSmoothingCoeff * (smoothedGain - targetGain);
        smoothedHighGain = targetHighGain + envelopeSmoothingCoeff * (smoothedHighGain - targetHighGain);

        if (mode == TriggerMode::sidechain)
        {
            clockedProcessingBlend = 1.0f;
        }
        else
        {
            clockedProcessingBlend = bypassClockedProcessing
                                         ? juce::jmax (0.0f, clockedProcessingBlend - transportBlendStep)
                                         : juce::jmin (1.0f, clockedProcessingBlend + transportBlendStep);
        }
        const auto readPos = (delayWritePosition + delaySize - mainDelaySamples) % delaySize;

        for (int ch = 0; ch < totalOutputChannels; ++ch)
        {
            if (cachedSidechainSolo)
            {
                const auto monitor = thresholdedSidechainMonitor
                                         ? (totalOutputChannels == 1 ? 0.5f * (sidechainMonitorLeft + sidechainMonitorRight)
                                                                     : (ch == 0 ? sidechainMonitorLeft : sidechainMonitorRight))
                                         : 0.0f;
                buffer.setSample (ch, sample, monitor);
            }
            else
            {
                const auto sourceChannel = juce::jmin (ch, mainAudioChannels - 1);
                const auto dry = mainDelay.getSample (sourceChannel, readPos);
                const auto ringSidechain = totalOutputChannels == 1 ? 0.5f * (sidechainMonitorLeft + sidechainMonitorRight)
                                                                    : (ch == 0 ? sidechainMonitorLeft : sidechainMonitorRight);
                const auto targetRingDepth = cachedRingmodEnabled ? cachedRingmodIntensity : 0.0f;
                auto& ringDepth = smoothedRingmodDepth[(size_t) sourceChannel];
                ringDepth = targetRingDepth + ringmodSmoothingCoeff * (ringDepth - targetRingDepth);
                const auto ringmodDelta = dry * -std::abs (ringSidechain) * ringmodFullScaleDrive;
                const auto shapedInput = dry + ringmodTriggerFade * ringDepth * ringmodDelta;
                auto wet = shapedInput * smoothedGain;

                if (cachedBandEnabled)
                {
                    auto& lowpassState = bandLowpassState[(size_t) sourceChannel];
                    lowpassState += bandCoeff * (shapedInput - lowpassState);

                    const auto lowBand = lowpassState;
                    const auto highBand = shapedInput - lowBand;
                    wet = highBand * smoothedHighGain + lowBand * smoothedGain;
                }
                else
                {
                    bandLowpassState[(size_t) sourceChannel] = shapedInput;
                }

                buffer.setSample (ch, sample, dry + cachedMix * clockedProcessingBlend * (wet - dry));
            }
        }

        if (bypassClockedProcessing)
        {
            freeRunningGridPhase = 0.0;
        }
        else
        {
            sidechainEnvelopeElapsedSamples += 1.0;
            freeRunningGridPhase += 1.0 / juce::jmax (1.0, gridCycleBeats * 60.0 / bpm * currentSampleRate);
            while (freeRunningGridPhase >= 1.0)
                freeRunningGridPhase -= 1.0;
        }

        delayWritePosition = (delayWritePosition + 1) % delaySize;
    }
}

void SidechainerAudioProcessor::triggerSidechainEnvelope() noexcept
{
    sidechainEnvelopeElapsedSamples = 0.0;
    sidechainEnvelopePhase = cachedRampDownMs > 0.0f ? -1.0f : 0.0f;
    samplesSinceLastSidechainTrigger = 0.0;
}

float SidechainerAudioProcessor::getCurveValue (float phase,
                                                const std::array<std::atomic<float>*, curvePointCount>& points) const noexcept
{
    phase = juce::jlimit (0.0f, 1.0f, phase);
    const auto scaled = phase * static_cast<float> (curvePointCount - 1);
    const auto index = juce::jlimit (0, curvePointCount - 2, static_cast<int> (scaled));
    const auto frac = scaled - static_cast<float> (index);
    const auto a = points[(size_t) index]->load();
    const auto b = points[(size_t) index + 1]->load();
    return juce::jlimit (0.0f, 1.0f, a + (b - a) * frac);
}

float SidechainerAudioProcessor::getPhaseFromElapsedSamples (double elapsedSamples,
                                                             double cycleSamples,
                                                             double rampSamples) const noexcept
{
    cycleSamples = juce::jmax (1.0, cycleSamples);
    rampSamples = juce::jlimit (0.0, cycleSamples * 0.5, rampSamples);

    if (rampSamples > 0.0 && elapsedSamples < rampSamples)
    {
        const auto rampProgress = juce::jlimit (0.0, 1.0, elapsedSamples / rampSamples);
        return static_cast<float> (rampProgress - 1.0);
    }

    const auto curveElapsed = juce::jmax (0.0, elapsedSamples - rampSamples);
    return static_cast<float> (juce::jlimit (0.0, 1.0,
                                             curveElapsed * static_cast<double> (cachedSpeed) / cycleSamples));
}

float SidechainerAudioProcessor::getGainForPhase (float phase,
                                                  float curveValue,
                                                  float depth,
                                                  bool curveEnabled) const noexcept
{
    if (! curveEnabled)
        return 1.0f;

    const auto curveGain = juce::jlimit (0.0f, 1.5f, 1.0f - depth * (1.0f - curveValue));

    if (phase >= 0.0f)
        return curveGain;

    const auto rampProgress = juce::jlimit (0.0f, 1.0f, phase + 1.0f);
    const auto shapedProgress = std::pow (rampProgress, juce::jlimit (0.25f, 4.0f, cachedRampShape));
    return juce::jlimit (0.0f, 1.5f, 1.0f + shapedProgress * (curveGain - 1.0f));
}

float SidechainerAudioProcessor::getDivisionBeats() const noexcept
{
    switch (cachedDivision)
    {
        case 0: return 4.0f;
        case 1: return 2.0f;
        case 2: return 4.0f / 3.0f;
        case 3: return 1.0f;
        case 4: return 2.0f / 3.0f;
        case 5: return 0.5f;
        case 6: return 1.0f / 3.0f;
        case 7: return 0.25f;
        default: return 1.0f;
    }
}

void SidechainerAudioProcessor::parameterChanged (const juce::String&, float)
{
    refreshParameterCache();
}

void SidechainerAudioProcessor::refreshParameterCache()
{
    cachedDepth = depthParam->load();
    cachedHighDepth = highDepthParam->load();
    cachedMix = mixParam->load();
    cachedMode = juce::roundToInt (modeParam->load());
    cachedDivision = juce::roundToInt (divisionParam->load());
    cachedSpeed = juce::jmax (0.01f, speedParam->load());
    cachedPreduckMs = preduckParam->load();
    cachedRampDownMs = rampDownParam->load();
    cachedRampShape = rampShapeParam->load();
    cachedThreshold = thresholdParam->load();
    cachedSensitivity = sensitivityParam->load();
    cachedSidechainSolo = sidechainSoloParam->load() >= 0.5f;
    cachedRingmodEnabled = ringmodEnabledParam->load() >= 0.5f;
    cachedRingmodIntensity = ringmodIntensityParam->load();
    cachedBandEnabled = bandEnabledParam->load() >= 0.5f;
    cachedHighCurveEnabled = highCurveEnabledParam->load() >= 0.5f;
    cachedBandCutoffHz = bandCutoffParam->load();
    updateLatencyForPreduck();
}

void SidechainerAudioProcessor::updateLatencyForPreduck()
{
    const auto latencySamples = juce::jlimit (0, maxLookaheadSamples,
                                             juce::roundToInt (currentSampleRate * (cachedPreduckMs + cachedRampDownMs) / 1000.0));

    if (latencySamples != reportedLatencySamples)
    {
        reportedLatencySamples = latencySamples;
        setLatencySamples (reportedLatencySamples);
    }
}

void SidechainerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SidechainerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xmlState = getXmlFromBinary (data, sizeInBytes))
        if (xmlState->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
}

juce::AudioProcessorEditor* SidechainerAudioProcessor::createEditor()
{
    return new SidechainerAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SidechainerAudioProcessor();
}
