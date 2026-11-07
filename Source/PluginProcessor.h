#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Curve.h"
#include <array>
#include <optional>

class SidechainerAudioProcessor final
    : public juce::AudioProcessor,
      public juce::ChangeBroadcaster,
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

    static constexpr int triggerCount = 2;
    static constexpr int curveCount = triggerCount * 2;
    static constexpr int curveSlot (int trigger, bool high) noexcept   { return trigger * 2 + (high ? 1 : 0); }

    // A trigger's parameter ID: Trigger 1 keeps the IDs it had before there were two.
    static juce::String triggerParameterId (int trigger, const juce::String& trigger1Id);

    // The curves, read and set on the message thread; listeners hear when a project or preset changes them.
    Curve getCurve (int slot) const;
    void setCurve (int slot, const Curve& curve);

    // The trigger the editor shows, saved with the project.
    int getEditTrigger() const noexcept               { return editTrigger.load(); }
    void setEditTrigger (int trigger) noexcept        { editTrigger = juce::jlimit (0, triggerCount - 1, trigger); }

    // False where the host gives the plug-in one sidechain only (Audio Units).
    bool hasSecondSidechain() const noexcept          { return getBusCount (true) > 2; }

    // Detector Release is stored as a 0 to 1 "sensitivity", highest for the shortest release.
    static float detectorReleaseMsFromSensitivity (float sensitivity) noexcept;
    static float sensitivityFromDetectorReleaseMs (float ms) noexcept;

private:
    enum class TriggerMode { off, grid, sidechain, gridAndSidechain };

    static BusesProperties makeBusesProperties();

    // Curve tables passed to the audio thread without locks or allocation: the message thread fills
    // the back table and swaps it into the middle; the audio thread swaps the middle to the front
    // when it holds a new table.
    class CurveTables
    {
    public:
        // Before the audio thread runs: every table holds the curve.
        void fill (const Curve& curve) noexcept;
        void publish (const Curve& curve) noexcept;
        const Curve::Table& acquire() noexcept;

    private:
        static constexpr int fresh = 4;
        std::array<Curve::Table, 3> tables {};
        std::atomic<int> middle { 1 };
        int back = 2, front = 0;
    };

    struct TriggerParameters
    {
        std::atomic<float>* mode = nullptr;
        std::atomic<float>* division = nullptr;
        std::atomic<float>* speed = nullptr;
        std::atomic<float>* source = nullptr;
        std::atomic<float>* threshold = nullptr;
        std::atomic<float>* sensitivity = nullptr;
        std::atomic<float>* solo = nullptr;
        std::atomic<float>* depth = nullptr;
        std::atomic<float>* highDepth = nullptr;
        std::atomic<float>* band = nullptr;
        std::atomic<float>* highCurve = nullptr;
    };

    struct TriggerSettings
    {
        TriggerMode mode = TriggerMode::grid;
        int division = 3;
        float speed = 1.0f;
        int source = 0;
        float threshold = 0.05f;
        float sensitivity = 0.5f;
        bool solo = false;
        float depth = 1.0f;
        float highDepth = 1.0f;
        bool band = false;
        bool highCurve = false;
    };

    struct TriggerState
    {
        double freeRunningGridPhase = 0.0;
        double elapsedSamples = 1.0e12;
        double samplesSinceTrigger = 0.0;
        float detectorEnvelope = 0.0f;
        float soloGateGain = 0.0f;
        float smoothedGain = 1.0f;
        float smoothedHighGain = 1.0f;
        bool armed = true;
    };

    // A source's sample: its level for the detector, and its left and right for listening and modulating.
    struct SourceSample
    {
        float level = 0.0f, left = 0.0f, right = 0.0f;
    };

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void refreshParameterCache();
    double blockModulationDc (double input, size_t channel) noexcept;
    void updateLatencyForPreduck();
    static float getCurveValue (float phase, const Curve::Table& table) noexcept;
    float getPhaseFromElapsedSamples (double elapsedSamples, double cycleSamples, double rampSamples, float speed) const noexcept;
    float getGainForPhase (float phase, float curveValue, float depth) const noexcept;
    static double getDivisionBeats (int division) noexcept;

    APVTS parameters;

    std::array<TriggerParameters, triggerCount> triggerParameters {};
    std::atomic<float>* preduckParam = nullptr;
    std::atomic<float>* rampDownParam = nullptr;
    std::atomic<float>* rampShapeParam = nullptr;
    std::atomic<float>* ringmodEnabledParam = nullptr;
    std::atomic<float>* ringmodIntensityParam = nullptr;
    std::atomic<float>* ringmodDuckedParam = nullptr;
    std::atomic<float>* amplitudeModulationEnabledParam = nullptr;
    std::atomic<float>* amplitudeModulationDepthParam = nullptr;
    std::atomic<float>* bandCutoffParam = nullptr;
    std::atomic<float>* mixParam = nullptr;

    juce::CriticalSection curveLock;
    std::array<Curve, curveCount> curves;
    std::array<CurveTables, curveCount> curveTables;
    std::atomic<int> editTrigger { 0 };

    juce::AudioBuffer<float> mainDelay;
    juce::AudioBuffer<float> sidechainDelay;
    std::array<float, 2> bandLowpassState {};
    std::array<float, 2> plainBandLowpassState {};
    std::array<std::array<double, 2>, 2> modulationDcState {};
    double modulationDcB0 = 1.0, modulationDcB1 = -2.0, modulationDcA1 = -2.0, modulationDcA2 = 1.0;
    int delayWritePosition = 0;
    int maxLookaheadSamples = 1;
    std::atomic<int> reportedLatencySamples { 0 };

    double currentSampleRate = 44100.0;
    std::array<TriggerState, triggerCount> triggerStates {};
    std::array<float, 2> smoothedRingmodDepth {};
    float smoothedRingmodDucked = 1.0f;
    float smoothedAmplitudeModulationDepth = 0.0f;
    std::array<float, 2> amplitudeModulationEnvelope {};
    float clockedProcessingBlend = 0.0f;

    std::array<TriggerSettings, triggerCount> cachedTriggers {};
    float cachedMix = 1.0f;
    float cachedPreduckMs = 0.0f;
    float cachedRampDownMs = 0.0f;
    float cachedRampShape = 1.0f;
    float cachedRingmodIntensity = 1.0f;
    float cachedAmplitudeModulationDepth = 1.0f;
    float cachedBandCutoffHz = 300.0f;
    bool cachedRingmodEnabled = false;
    bool cachedRingmodDucked = true;
    bool cachedAmplitudeModulationEnabled = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SidechainerAudioProcessor)
};
