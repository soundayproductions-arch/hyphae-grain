#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "Music.h"

//==============================================================================
// A MIDI port the plugin opens by itself (e.g. Traktor's clock via LoopBe1).
// Clock bytes are timed here; notes and CCs are queued for the audio thread.
class ExternalMidiInput : public juce::MidiInputCallback
{
public:
    struct Msg { juce::uint8 b0 = 0, b1 = 0, b2 = 0; };

    ~ExternalMidiInput() override { close(); }

    bool open (const juce::String& identifier);
    void close();
    juce::String getIdentifier() const { return currentId; }

    double getBpm() const;
    bool   isLocked() const;
    // Beat position (quarter notes) since the last Start, or -1 when not running.
    double getPpqNow() const;
    int    pop (Msg* dest, int maxNum);

    std::atomic<int> starts { 0 };

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    std::unique_ptr<juce::MidiInput> device;
    juce::String currentId;

    juce::AbstractFifo fifo { 1024 };
    std::array<Msg, 1024> queue;

    std::atomic<juce::int64> ticks { 0 };
    std::atomic<double> lastTickTime { 0.0 }, tickInterval { 0.0 };
    std::atomic<bool> running { false };
    std::array<double, 48> tickTimes {};
    int tickRing = 0, tickFill = 0;
};

//==============================================================================
class HyphaeGrainProcessor : public juce::AudioProcessor
{
public:
    HyphaeGrainProcessor();
    ~HyphaeGrainProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Hyphae Grain"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    // Sample (message thread)
    bool loadSample (const juce::File&);
    juce::File getSampleFile() const { return sampleFile; }
    std::vector<float> peakMin, peakMax;

    // Traktor / external port (message thread)
    bool openExternalPort (const juce::String& identifier);
    juce::String getExternalPortId() const { return external.getIdentifier(); }

    // MIDI learn. Source 0 = Cubase (track MIDI), 1 = external port.
    std::atomic<int> learnTarget { -1 };
    std::atomic<int> mapVersion { 0 };
    void clearMappingFor (int paramIndex);
    juce::String mappingTextFor (int paramIndex) const;
    int paramIndexFor (const juce::String& paramID) const;

    // Status for the editor
    std::atomic<juce::uint32> lastMidi { 0 };   // src<<24 | status<<16 | d1<<8 | d2, 0 = nothing yet
    std::atomic<float>  gateLevel { 1.0f };
    std::atomic<float>  gateHz { 10.0f };
    std::atomic<float>  effectivePosition { 0.32f };
    std::atomic<double> currentBpm { 120.0 };
    std::atomic<int>    tempoState { 0 };        // 0 internal, 1 Cubase, 2 Traktor locked, 3 Traktor waiting
    std::atomic<int>    liveActive { 0 };

    static constexpr int tableSize = 2048;
    std::array<std::array<float, tableSize>, 4> wavetables;

private:
    //==============================================================================
    struct Grain
    {
        bool active = false;
        int src = 0;              // 0 sample, 1 oscillator, 2 live
        double pos = 0, inc = 1;
        int length = 0, age = 0;
        float amp = 0, gL = 0, gR = 0;
    };

    struct Voice
    {
        bool active = false;
        int id = 0;               // unique per source/channel/note
        int note = 60;
        float vel = 1.0f;
        double countdown = 0;
    };

    struct DelayLine
    {
        std::vector<float> d;
        int w = 0;
        void prepare (int n) { d.assign ((size_t) juce::jmax (4, n), 0.0f); w = 0; }
        void push (float x) { d[(size_t) w] = x; if (++w == (int) d.size()) w = 0; }
        float read (double delaySamples) const
        {
            const int n = (int) d.size();
            double rp = (double) w - 1.0 - juce::jlimit (0.0, (double) n - 2.0, delaySamples);
            while (rp < 0) rp += n;
            const int i0 = (int) rp;
            const int i1 = (i0 + 1) % n;
            const float fr = (float) (rp - i0);
            return d[(size_t) i0] + fr * (d[(size_t) i1] - d[(size_t) i0]);
        }
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void buildWavetables();

    void handleMidi (int src, juce::uint8 b0, juce::uint8 b1, juce::uint8 b2);
    void handleCC (int src, int ch, int cc, int value);
    void noteOn (int id, int note, float vel);
    void noteOff (int id);
    void spawnForVoice (const Voice& v, int numVoices);
    void startGrain (int semis100, float amp);   // semis100 = pitch in cents relative to C4
    float computeGate (double phase, double rateHz);

    // Values read once per block (audio thread only)
    struct BlockParams { int src = 0, wave = 2; float position = 0, spray = 0, size = 100, density = 20, pitch = 0, detune = 0, stereo = 0, reverse = 0;
                         int key = 0, scale = 0, harmony = 0, transpose = 0; bool quantize = true, haveSample = false; int spawnOffset = 0; } bp;

    juce::Random rng;
    double sr = 44100.0;

    std::array<Grain, 384> grains;
    std::array<Voice, 32> voices;

    // Sample source
    juce::SpinLock sampleLock;
    juce::AudioBuffer<float> sample;
    double sampleRateOfFile = 44100.0;
    juce::File sampleFile;

    // Live input ring
    juce::AudioBuffer<float> liveRing;
    int liveWrite = 0;

    // FX
    DelayLine dL, dR, preL, preR;
    double dlySmoothed = 0;
    float lpL = 0, lpR = 0;
    juce::Reverb reverb;
    juce::dsp::Limiter<float> limiter;
    juce::AudioBuffer<float> gBuf, dBuf, rBuf;
    std::vector<float> gateBuf;
    float gateMix = 1.0f;

    double isoPhase = 0, scanOffset = 0;
    int lastExternalStarts = 0;

    // MIDI learn table
    std::array<std::atomic<int>, 2 * 16 * 128> ccMap;
    static int mapIndex (int src, int ch, int cc) { return (src * 16 + ch) * 128 + cc; }

    ExternalMidiInput external;

    // Cached parameter pointers
    std::atomic<float> *pSource, *pWave, *pGain, *pPosition, *pSpray, *pScan, *pSize, *pDensity, *pPitch,
                       *pDetune, *pStereo, *pReverse, *pIsoOn, *pIsoSync, *pIsoRate, *pIsoDepth, *pIsoDuty,
                       *pIsoEdge, *pKey, *pScale, *pHarmony, *pQuantize, *pTranspose, *pKeyFollow, *pDrone,
                       *pTempoSource, *pBpm, *pDlySync, *pDlyTime, *pDlyFb, *pDlyTone, *pDlySend,
                       *pRevSize, *pRevDamp, *pRevPre, *pRevSend, *pFxPulse;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HyphaeGrainProcessor)
};
