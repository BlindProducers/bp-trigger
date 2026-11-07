#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <optional>

class SidechainerAudioProcessor final
    : public juce::AudioProcessor,
      private juce::AudioProcessorValueTreeState::Listener
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    SidechainerAudioProcessor();
    ~SidechainerAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    APVTS& getValueTreeState() { return parameters; }
    const APVTS& getValueTreeState() const { return parameters; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static constexpr int curvePointCount = 16;

    // Detector Release is stored as a 0 to 1 "sensitivity", highest for the shortest release.
    static float detectorReleaseMsFromSensitivity (float sensitivity) noexcept;
    static float sensitivityFromDetectorReleaseMs (float ms) noexcept;

private:
    enum class TriggerMode { grid = 0, sidechain = 1, gridAndSidechain = 2 };

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void refreshParameterCache();
    void updateLatencyForPreduck();
    float getCurveValue (float phase, const std::array<std::atomic<float>*, curvePointCount>& points) const noexcept;
    float getPhaseFromElapsedSamples (double elapsedSamples, double cycleSamples, double rampSamples) const noexcept;
    float getGainForPhase (float phase,
                           float curveValue,
                           float depth,
                           bool curveEnabled = true) const noexcept;
    float getDivisionBeats() const noexcept;
    void triggerSidechainEnvelope() noexcept;

    APVTS parameters;

    std::array<std::atomic<float>*, curvePointCount> curvePoints {};
    std::atomic<float>* depthParam = nullptr;
    std::atomic<float>* highDepthParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* divisionParam = nullptr;
    std::atomic<float>* speedParam = nullptr;
    std::atomic<float>* preduckParam = nullptr;
    std::atomic<float>* rampDownParam = nullptr;
    std::atomic<float>* rampShapeParam = nullptr;
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* sensitivityParam = nullptr;
    std::atomic<float>* sidechainSoloParam = nullptr;
    std::atomic<float>* ringmodEnabledParam = nullptr;
    std::atomic<float>* ringmodIntensityParam = nullptr;
    std::atomic<float>* ringmodDuckedParam = nullptr;
    std::atomic<float>* bandEnabledParam = nullptr;
    std::atomic<float>* highCurveEnabledParam = nullptr;
    std::atomic<float>* bandCutoffParam = nullptr;

    juce::AudioBuffer<float> mainDelay;
    juce::AudioBuffer<float> sidechainDelay;
    std::array<float, 2> bandLowpassState {};
    std::array<std::atomic<float>*, curvePointCount> highCurvePoints {};
    int delayWritePosition = 0;
    int maxLookaheadSamples = 1;
    int reportedLatencySamples = 0;

    double currentSampleRate = 44100.0;
    double freeRunningGridPhase = 0.0;
    double sidechainEnvelopeElapsedSamples = 1.0e12;
    double samplesSinceLastSidechainTrigger = 0.0;
    float sidechainEnvelopePhase = 1.0f;
    float detectorEnvelope = 0.0f;
    float soloGateGain = 0.0f;
    float smoothedGain = 1.0f;
    float smoothedHighGain = 1.0f;
    std::array<float, 2> smoothedRingmodDepth {};
    float smoothedRingmodDucked = 1.0f;
    float clockedProcessingBlend = 0.0f;
    bool sidechainTriggerArmed = true;

    float cachedDepth = 1.0f;
    float cachedHighDepth = 1.0f;
    float cachedMix = 1.0f;
    float cachedSpeed = 1.0f;
    float cachedPreduckMs = 0.0f;
    float cachedRampDownMs = 0.0f;
    float cachedRampShape = 1.0f;
    float cachedThreshold = 0.05f;
    float cachedSensitivity = 0.5f;
    float cachedRingmodIntensity = 1.0f;
    float cachedBandCutoffHz = 300.0f;
    int cachedMode = 0;
    int cachedDivision = 3;
    bool cachedSidechainSolo = false;
    bool cachedRingmodEnabled = false;
    bool cachedRingmodDucked = true;
    bool cachedBandEnabled = false;
    bool cachedHighCurveEnabled = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SidechainerAudioProcessor)
};
