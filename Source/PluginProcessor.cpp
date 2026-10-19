#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

//==============================================================================
// External MIDI port
bool ExternalMidiInput::open (const String& identifier)
{
    close();
    device = MidiInput::openDevice (identifier, this);
    if (device == nullptr)
        return false;
    currentId = identifier;
    device->start();
    return true;
}

void ExternalMidiInput::close()
{
    if (device != nullptr)
    {
        device->stop();
        device.reset();
    }
    currentId = {};
    running = false;
    tickInterval = 0.0;
}

void ExternalMidiInput::handleIncomingMidiMessage (MidiInput*, const MidiMessage& m)
{
    const auto* d = m.getRawData();
    const int size = m.getRawDataSize();
    if (size < 1)
        return;

    const uint8 status = d[0];

    if (status == 0xF8) // clock tick, 24 per quarter note
    {
        const double now = Time::getMillisecondCounterHiRes() * 0.001;
        if (now - lastTickTime.load() > 0.25)
            tickFill = 0;

        tickTimes[(size_t) tickRing] = now;
        tickRing = (tickRing + 1) % (int) tickTimes.size();
        tickFill = jmin ((int) tickTimes.size(), tickFill + 1);

        if (tickFill >= 4)
        {
            const int oldestIndex = (tickRing - tickFill + (int) tickTimes.size()) % (int) tickTimes.size();
            tickInterval = (now - tickTimes[(size_t) oldestIndex]) / (double) (tickFill - 1);
        }
        lastTickTime = now;
        if (running.load())
            ++ticks;
        return;
    }
    if (status == 0xFA) { ticks = 0; running = true; ++starts; return; } // Start
    if (status == 0xFB) { running = true; return; }                       // Continue
    if (status == 0xFC) { running = false; return; }                      // Stop
    if (status >= 0xF0) return;                                           // other system messages

    Msg msg;
    msg.b0 = status;
    msg.b1 = size > 1 ? d[1] : 0;
    msg.b2 = size > 2 ? d[2] : 0;

    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0)      queue[(size_t) scope.startIndex1] = msg;
    else if (scope.blockSize2 > 0) queue[(size_t) scope.startIndex2] = msg;
}

int ExternalMidiInput::pop (Msg* dest, int maxNum)
{
    const auto scope = fifo.read (jmin (maxNum, fifo.getNumReady()));
    int n = 0;
    for (int i = 0; i < scope.blockSize1; ++i) dest[n++] = queue[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[n++] = queue[(size_t) (scope.startIndex2 + i)];
    return n;
}

double ExternalMidiInput::getBpm() const
{
    const double ti = tickInterval.load();
    return ti > 0.0 ? 60.0 / (ti * 24.0) : 0.0;
}

bool ExternalMidiInput::isLocked() const
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    return tickInterval.load() > 0.0 && now - lastTickTime.load() < 0.5;
}

double ExternalMidiInput::getPpqNow() const
{
    if (! running.load())
        return -1.0;
    const double ti = tickInterval.load();
    if (ti <= 0.0)
        return -1.0;
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const double frac = jlimit (0.0, 1.0, (now - lastTickTime.load()) / ti);
    return ((double) ticks.load() + frac) / 24.0;
}

//==============================================================================
namespace
{
    std::unique_ptr<AudioParameterFloat> floatParam (const char* id, const char* name, float lo, float hi, float step,
                                                     float def, std::function<String (float)> fmt, float centre = 0.0f)
    {
        NormalisableRange<float> range (lo, hi, step);
        if (centre > 0.0f)
            range.setSkewForCentre (centre);
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([fmt] (float v, int) { return fmt (v); }));
    }

    std::unique_ptr<AudioParameterChoice> choiceParam (const char* id, const char* name, const StringArray& items, int def)
    {
        return std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def);
    }

    std::unique_ptr<AudioParameterBool> boolParam (const char* id, const char* name, bool def)
    {
        return std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def);
    }

    String pct (float v)  { return String (v * 100.0f, 1) + " %"; }
    String pct0 (float v) { return String (roundToInt (v * 100.0f)) + " %"; }

    struct HannTable
    {
        static constexpr int size = 1024;
        float t[size + 1];
        HannTable() { for (int i = 0; i <= size; ++i) t[i] = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * (float) i / (float) size); }
    };
    const HannTable hann;

    const double isoPulsesPerBeat[] = { 0, 1, 2, 3, 4, 6, 8 };
    const double delayBeats[]       = { 0, 1, 0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0 };
}

// Biotron incoming CC map (Playtronica "Biotron MIDI CC" test map). Channel-specific commands use
// MIDI channel 1 for the plant and channel 2 for the light; global commands are sent on channel 1.
namespace
{
    enum BioKind { bioRaw, bioOnOff, bioScale, bioNote, bioHold };
    struct BioCC { const char* id; const char* name; int cc; int ch; BioKind kind; int def; };
    const BioCC bioTable[HyphaeGrainProcessor::numBio] = {
        { "bioPMaxVel",    "Plant max velocity",          9,  0, bioRaw,   127 },
        { "bioPTempo",     "Plant tempo division",        14, 0, bioRaw,   64 },
        { "bioPMinDist",   "Plant min note distance",     20, 0, bioRaw,   0 },
        { "bioPHumanize",  "Plant humanize",              26, 0, bioOnOff, 0 },
        { "bioPHumMin",    "Plant humanize min velocity", 25, 0, bioRaw,   64 },
        { "bioPMute",      "Plant mute",                  31, 0, bioOnOff, 0 },
        { "bioLMaxVel",    "Light max velocity",          9,  1, bioRaw,   127 },
        { "bioLTempo",     "Light tempo division",        14, 1, bioRaw,   64 },
        { "bioLMinDist",   "Light min note distance",     20, 1, bioRaw,   0 },
        { "bioLHumanize",  "Light humanize",              26, 1, bioOnOff, 0 },
        { "bioLHumMin",    "Light humanize min velocity", 25, 1, bioRaw,   64 },
        { "bioLMute",      "Light mute",                  31, 1, bioOnOff, 0 },
        { "bioFilter",     "Biotron input filter",        3,  0, bioRaw,   64 },
        { "bioUltra",      "Biotron ultra sensitivity",   15, 0, bioOnOff, 0 },
        { "bioWake",       "Biotron wake-up",             23, 0, bioRaw,   64 },
        { "bioPerf",       "Biotron performance mode",    30, 0, bioRaw,   0 },
        { "bioHold",       "Biotron note hold",           21, 0, bioHold,  99 },
        { "bioStep",       "Biotron step size",           22, 0, bioRaw,   64 },
        { "bioScale",      "Biotron scale",               24, 0, bioScale, 0 },
        { "bioSwing",      "Biotron swing",               86, 0, bioRaw,   0 },
        { "bioCentre",     "Biotron centre note",         85, 0, bioNote,  60 },
        { "bioBtnMute",    "Biotron button mute mode",    87, 0, bioRaw,   0 },
        { "bioLightBend",  "Light pitch bend mode",       27, 0, bioOnOff, 0 },
        { "bioLightRange", "Light note range",            28, 0, bioRaw,   24 } };

    String bioText (BioKind k, int v)
    {
        if (k == bioScale)
        {
            switch (v) { case 0: return "Major"; case 20: return "Chromatic"; case 40: return "Mixolydian";
                         case 69: return "Minor blues"; case 119: return "Hirajoshi"; default: return String (v); }
        }
        if (k == bioNote) return MidiMessage::getMidiNoteName (v, true, true, 3) + " (" + String (v) + ")";
        if (k == bioHold)
        {
            if (v == 0) return "1/64 beat";
            if (v == 99) return "1/2 beat";
            if (v == 127) return "1 beat";
        }
        return String (v);
    }
}

AudioProcessorValueTreeState::ParameterLayout HyphaeGrainProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    StringArray keys;   for (auto* k : music::keyNames) keys.add (k);
    StringArray scales; for (auto& s : music::scales()) scales.add (s.name);
    StringArray harm;   for (auto* h : music::harmonyNames) harm.add (h);

    // Source
    p.push_back (choiceParam ("source", "Source", { "Sample", "Oscillator", "Live input" }, 0));
    p.push_back (choiceParam ("wave", "Waveform", { "Sine", "Triangle", "Saw", "Square" }, 2));

    // Grains
    p.push_back (floatParam ("position", "Position", 0.0f, 1.0f, 0.001f, 0.32f, pct));
    p.push_back (floatParam ("spray", "Spray", 0.0f, 0.5f, 0.001f, 0.06f, pct));
    p.push_back (floatParam ("scan", "Scan", -0.2f, 0.2f, 0.001f, 0.0f, [] (float v) { return (v >= 0 ? "+" : "") + String (v, 3) + " /s"; }));
    p.push_back (floatParam ("size", "Grain size", 10.0f, 500.0f, 1.0f, 140.0f, [] (float v) { return String (roundToInt (v)) + " ms"; }, 120.0f));
    p.push_back (floatParam ("density", "Density", 1.0f, 80.0f, 1.0f, 22.0f, [] (float v) { return String (roundToInt (v)) + " /s"; }, 20.0f));
    p.push_back (floatParam ("pitch", "Pitch", -24.0f, 24.0f, 1.0f, 0.0f, [] (float v) { return (v > 0 ? "+" : "") + String (roundToInt (v)) + " st"; }));
    p.push_back (floatParam ("detune", "Detune", 0.0f, 100.0f, 1.0f, 10.0f, [] (float v) { return "+/-" + String (roundToInt (v)) + " ct"; }));
    p.push_back (floatParam ("stereo", "Stereo", 0.0f, 1.0f, 0.01f, 0.5f, pct0));
    p.push_back (floatParam ("reverse", "Reverse", 0.0f, 1.0f, 0.01f, 0.1f, pct0));

    // Isochronic
    p.push_back (boolParam ("isoOn", "Iso gate", true));
    p.push_back (choiceParam ("isoSync", "Iso sync", { "Free (Hz)", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 0));
    p.push_back (floatParam ("isoRate", "Iso rate", 0.5f, 45.0f, 0.1f, 10.0f, [] (float v) { return String (v, 1) + " Hz"; }, 10.0f));
    p.push_back (floatParam ("isoDepth", "Iso depth", 0.0f, 1.0f, 0.01f, 0.85f, pct0));
    p.push_back (floatParam ("isoDuty", "Iso duty", 0.1f, 0.9f, 0.01f, 0.5f, pct0));
    p.push_back (floatParam ("isoEdge", "Iso edge", 1.0f, 25.0f, 0.5f, 4.0f, [] (float v) { return String (v, 1) + " ms"; }));

    // Musical
    p.push_back (choiceParam ("key", "Key", keys, 0));
    p.push_back (choiceParam ("scale", "Scale", scales, 9));
    p.push_back (choiceParam ("harmony", "Harmony", harm, 0));
    p.push_back (boolParam ("quantize", "Quantize to scale", true));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { "transpose", 1 }, "Transpose", -24, 24, 0,
                     AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return (v > 0 ? "+" : "") + String (v) + " st"; })));
    p.push_back (choiceParam ("keyFollow", "Key follow", { "Off", "Notes on channel 16", "Notes from Traktor port" }, 0));
    p.push_back (boolParam ("drone", "Drone", false));

    // Channels: per-plant settings for MIDI channel 1 and 2 (Cubase tracks)
    for (int c = 1; c <= 2; ++c)
    {
        const String n (c);
        const String who (c == 1 ? "Plant" : "Light");
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "ch" + n + "Notes", 1 }, who + " notes (ch " + n + ")", true));
        p.push_back (std::make_unique<AudioParameterInt> (ParameterID { "ch" + n + "Transpose", 1 }, who + " transpose (ch " + n + ")", -24, 24, 0,
                         AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return (v > 0 ? "+" : "") + String (v) + " st"; })));
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "ch" + n + "Level", 1 }, who + " volume (ch " + n + ")",
                         NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f,
                         AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })));
    }

    // Tempo
    for (auto& b : bioTable)
    {
        if (b.kind == bioOnOff)
            p.push_back (std::make_unique<AudioParameterBool> (ParameterID { b.id, 1 }, b.name, b.def > 63));
        else
        {
            const BioKind k = b.kind;
            p.push_back (std::make_unique<AudioParameterInt> (ParameterID { b.id, 1 }, b.name, 0, 127, b.def,
                             AudioParameterIntAttributes().withStringFromValueFunction ([k] (int v, int) { return bioText (k, v); })));
        }
    }

    p.push_back (choiceParam ("tempoSource", "Tempo source", { "Internal", "Cubase", "Traktor MIDI clock" }, 1));
    p.push_back (floatParam ("bpm", "Internal BPM", 40.0f, 200.0f, 0.1f, 120.0f, [] (float v) { return String (v, 1) + " BPM"; }));

    // Master FX
    p.push_back (choiceParam ("dlySync", "Delay sync", { "Free (ms)", "1/4", "1/8 dotted", "1/8", "1/8T", "1/16", "1/16T", "Iso pulse", "2x Iso pulse" }, 0));
    p.push_back (floatParam ("dlyTime", "Delay time", 20.0f, 1500.0f, 1.0f, 400.0f, [] (float v) { return String (roundToInt (v)) + " ms"; }, 400.0f));
    p.push_back (floatParam ("dlyFb", "Delay feedback", 0.0f, 0.92f, 0.01f, 0.45f, pct0));
    p.push_back (floatParam ("dlyTone", "Delay tone", 0.5f, 12.0f, 0.1f, 3.5f, [] (float v) { return String (v, 1) + " kHz"; }, 3.0f));
    p.push_back (floatParam ("dlySend", "Delay send", 0.0f, 1.0f, 0.01f, 0.3f, pct0));
    p.push_back (floatParam ("revSize", "Reverb size", 0.0f, 1.0f, 0.01f, 0.7f, pct0));
    p.push_back (floatParam ("revDamp", "Reverb damping", 1.0f, 16.0f, 0.1f, 6.0f, [] (float v) { return String (v, 1) + " kHz"; }));
    p.push_back (floatParam ("revPre", "Reverb pre-delay", 0.0f, 200.0f, 1.0f, 20.0f, [] (float v) { return String (roundToInt (v)) + " ms"; }));
    p.push_back (floatParam ("revSend", "Reverb send", 0.0f, 1.0f, 0.01f, 0.35f, pct0));
    p.push_back (boolParam ("fxPulse", "Pulse the FX tails", false));
    p.push_back (floatParam ("gain", "Volume", 0.0f, 1.0f, 0.01f, 0.8f, pct0));

    return { p.begin(), p.end() };
}

//==============================================================================
HyphaeGrainProcessor::HyphaeGrainProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input", AudioChannelSet::stereo(), true)
                        .withInput  ("Live In (Side-Chain)", AudioChannelSet::stereo(), true)
                        .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "HyphaeGrain", createLayout())
{
    for (auto& m : ccMap) m = -1;

    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pSource = get ("source");   pWave = get ("wave");         pGain = get ("gain");
    pPosition = get ("position"); pSpray = get ("spray");     pScan = get ("scan");
    pSize = get ("size");       pDensity = get ("density");   pPitch = get ("pitch");
    pDetune = get ("detune");   pStereo = get ("stereo");     pReverse = get ("reverse");
    pIsoOn = get ("isoOn");     pIsoSync = get ("isoSync");   pIsoRate = get ("isoRate");
    pIsoDepth = get ("isoDepth"); pIsoDuty = get ("isoDuty"); pIsoEdge = get ("isoEdge");
    pKey = get ("key");         pScale = get ("scale");       pHarmony = get ("harmony");
    pQuantize = get ("quantize"); pTranspose = get ("transpose"); pKeyFollow = get ("keyFollow");
    pDrone = get ("drone");     pTempoSource = get ("tempoSource"); pBpm = get ("bpm");
    pDlySync = get ("dlySync"); pDlyTime = get ("dlyTime");   pDlyFb = get ("dlyFb");
    pDlyTone = get ("dlyTone"); pDlySend = get ("dlySend");   pRevSize = get ("revSize");
    pRevDamp = get ("revDamp"); pRevPre = get ("revPre");     pRevSend = get ("revSend");
    pFxPulse = get ("fxPulse");
    for (int c = 0; c < 2; ++c)
    {
        const String n (c + 1);
        pChNotes[c] = apvts.getRawParameterValue ("ch" + n + "Notes");
        pChTranspose[c] = apvts.getRawParameterValue ("ch" + n + "Transpose");
        pChLevel[c] = apvts.getRawParameterValue ("ch" + n + "Level");
    }

    for (int i = 0; i < numBio; ++i)
        bioParams[(size_t) i] = apvts.getParameter (bioTable[i].id);

    buildWavetables();
    oscReceiver.addListener (this);
    startTimer (5);

    // Built-in source so the plugin makes sound before you load a file.
    {
        const double fsr = 44100.0;
        const int len = (int) (fsr * 6.0);
        AudioBuffer<float> b (2, len);
        auto* d = b.getWritePointer (0);
        const double parts[6][3] = { {110,.30,.07}, {164.8,.22,.11}, {220,.18,.05}, {329.6,.12,.17}, {493.9,.08,.23}, {659.3,.05,.31} };
        double a = 0, c = 0;
        Random r (7);
        for (int i = 0; i < len; ++i)
        {
            const double t = i / fsr;
            double s = 0;
            for (auto& pp : parts)
                s += pp[1] * (0.55 + 0.45 * std::sin (MathConstants<double>::twoPi * pp[2] * t + pp[0])) * std::sin (MathConstants<double>::twoPi * pp[0] * t);
            const double nz = r.nextDouble() * 2.0 - 1.0;
            a += 0.05 * (nz - a);
            c += 0.004 * (a - c);
            const double g0 = 0.5 + 0.5 * std::sin (MathConstants<double>::twoPi * 0.21 * t) * std::sin (MathConstants<double>::twoPi * 0.13 * t + 1.0);
            s += (a - c) * 2.2 * (0.25 + 0.75 * g0 * g0);
            d[i] = (float) s;
        }
        const double bells[6][2] = { {.7,1318.5}, {1.9,987.8}, {2.6,1568}, {3.4,1174.7}, {4.5,1975.5}, {5.2,880} };
        for (auto& bl : bells)
        {
            const int st = (int) (bl[0] * fsr);
            for (int j = 0; j < (int) (fsr * 0.6) && st + j < len; ++j)
                d[st + j] += (float) (0.35 * std::exp (-j / (fsr * 0.09)) * std::sin (MathConstants<double>::twoPi * bl[1] * j / fsr));
        }
        const float pk = b.getMagnitude (0, 0, len);
        b.applyGain (0, 0, len, pk > 0 ? 0.9f / pk : 1.0f);
        b.applyGainRamp (0, 0, 2205, 0.0f, 1.0f);
        b.applyGainRamp (0, len - 2205, 2205, 1.0f, 0.0f);
        b.copyFrom (1, 0, b, 0, 0, len);
        sample = std::move (b);
        sampleRateOfFile = fsr;
    }

    // Waveform overview for the editor
    {
        const int bins = 800, len = sample.getNumSamples();
        peakMin.assign (bins, 0.0f); peakMax.assign (bins, 0.0f);
        for (int i = 0; i < bins; ++i)
        {
            const int s0 = (int) ((int64) i * len / bins), s1 = jmax (s0 + 1, (int) ((int64) (i + 1) * len / bins));
            auto range = FloatVectorOperations::findMinAndMax (sample.getReadPointer (0) + s0, s1 - s0);
            peakMin[(size_t) i] = range.getStart(); peakMax[(size_t) i] = range.getEnd();
        }
    }
}

HyphaeGrainProcessor::~HyphaeGrainProcessor()
{
    stopTimer();
    oscReceiver.removeListener (this);
    oscReceiver.disconnect();
    oscSender.disconnect();
    { const ScopedLock sl (bioLock); bioOut.reset(); }
    external.close();
}

void HyphaeGrainProcessor::buildWavetables()
{
    for (int w = 0; w < 4; ++w)
    {
        auto& t = wavetables[(size_t) w];
        float peak = 0;
        for (int i = 0; i < tableSize; ++i)
        {
            const double ph = MathConstants<double>::twoPi * i / tableSize;
            double s = 0;
            for (int k = 1; k <= 32; ++k)
            {
                double amp = 0;
                if (w == 0)      amp = (k == 1) ? 1.0 : 0.0;
                else if (w == 1) amp = (k % 2) ? ((((k - 1) / 2) % 2) ? -1.0 : 1.0) / (k * k) : 0.0;
                else if (w == 2) amp = 1.0 / k;
                else             amp = (k % 2) ? 1.0 / k : 0.0;
                if (amp != 0.0) s += amp * std::sin (k * ph);
            }
            t[(size_t) i] = (float) s;
            peak = jmax (peak, std::abs ((float) s));
        }
        for (auto& v : t) v *= 0.8f / peak;
    }
}

bool HyphaeGrainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != AudioChannelSet::stereo())
        return false;
    for (auto& in : layouts.inputBuses)
        if (! (in.isDisabled() || in == AudioChannelSet::mono() || in == AudioChannelSet::stereo()))
            return false;
    return true;
}

void HyphaeGrainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    const int maxBlock = jmax (512, samplesPerBlock);
    gBuf.setSize (2, maxBlock); dBuf.setSize (2, maxBlock); rBuf.setSize (2, maxBlock);
    gateBuf.assign ((size_t) maxBlock, 1.0f);

    liveRing.setSize (2, (int) (sr * 4.0) + maxBlock);
    liveRing.clear();
    liveWrite = 0;

    dL.prepare ((int) (sr * 2.0)); dR.prepare ((int) (sr * 2.0));
    preL.prepare ((int) (sr * 0.25)); preR.prepare ((int) (sr * 0.25));
    dlySmoothed = *pDlyTime * 0.001 * sr;
    lpL = lpR = 0;

    reverb.setSampleRate (sr);
    reverb.reset();

    dsp::ProcessSpec spec { sr, (uint32) maxBlock, 2 };
    limiter.prepare (spec);
    limiter.setThreshold (-1.0f);
    limiter.setRelease (120.0f);

    for (auto& g : grains) g.active = false;
    isoPhase = 0;
}

//==============================================================================

void HyphaeGrainProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    if (gBuf.getNumSamples() < n)
    {
        gBuf.setSize (2, n, false, false, true); dBuf.setSize (2, n, false, false, true); rBuf.setSize (2, n, false, false, true);
        gateBuf.resize ((size_t) n);
    }

    // 1. Live input (main input + side-chain) into the ring buffer
    {
        const float* inL[2] = { nullptr, nullptr };
        const float* inR[2] = { nullptr, nullptr };
        int numIn = 0;
        for (int b = 0; b < jmin (2, getBusCount (true)); ++b)
        {
            auto* bus = getBus (true, b);
            if (bus == nullptr || ! bus->isEnabled()) continue;
            auto bb = getBusBuffer (buffer, true, b);
            const int nc = bb.getNumChannels();
            if (nc == 0) continue;
            inL[numIn] = bb.getReadPointer (0);
            inR[numIn] = bb.getReadPointer (nc > 1 ? 1 : 0);
            ++numIn;
        }
        const int ringLen = liveRing.getNumSamples();
        auto* rl = liveRing.getWritePointer (0);
        auto* rr = liveRing.getWritePointer (1);
        float level = 0;
        for (int i = 0; i < n; ++i)
        {
            float l = 0, r = 0;
            for (int k = 0; k < numIn; ++k) { l += inL[k][i]; r += inR[k][i]; }
            rl[liveWrite] = l; rr[liveWrite] = r;
            level = jmax (level, std::abs (l), std::abs (r));
            if (++liveWrite >= ringLen) liveWrite = 0;
        }
        liveActive = level > 0.0005f ? 1 : 0;
    }
    buffer.clear();

    // 2. MIDI from Cubase, then from the external (Traktor) port
    for (const auto md : midi)
        if (md.numBytes >= 1 && md.data[0] < 0xF0)
            handleMidi (0, md.data[0], md.numBytes > 1 ? md.data[1] : 0, md.numBytes > 2 ? md.data[2] : 0);
    midi.clear();
    if (bioSendMode.load() == 0)   // Biotron CCs leave through the plugin's MIDI output
    {
        const auto scope = bioFifo.read (bioFifo.getNumReady());
        auto add = [&midi] (uint32 m) { midi.addEvent (MidiMessage ((int) ((m >> 16) & 0xFF), (int) ((m >> 8) & 0xFF), (int) (m & 0xFF)), 0); };
        for (int i = 0; i < scope.blockSize1; ++i) add (bioQueue[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i) add (bioQueue[(size_t) (scope.startIndex2 + i)]);
    }
    {
        ExternalMidiInput::Msg msgs[256];
        const int k = external.pop (msgs, 256);
        for (int i = 0; i < k; ++i)
            handleMidi (1, msgs[i].b0, msgs[i].b1, msgs[i].b2);
    }

    // 3. Parameters for this block
    bp.src = (int) *pSource;      bp.wave = jlimit (0, 3, (int) *pWave);
    bp.position = *pPosition;     bp.spray = *pSpray;   bp.size = *pSize;   bp.density = *pDensity;
    bp.pitch = *pPitch;           bp.detune = *pDetune; bp.stereo = *pStereo; bp.reverse = *pReverse;
    bp.key = (int) *pKey;         bp.scale = (int) *pScale; bp.harmony = (int) *pHarmony;
    bp.transpose = (int) *pTranspose; bp.quantize = *pQuantize > 0.5f; bp.spawnOffset = 0;
    for (int c = 0; c < 2; ++c)
    {
        bp.chTranspose[c] = (int) *pChTranspose[c];
        bp.chLevel[c] = *pChLevel[c];
        if (*pChNotes[c] < 0.5f)   // notes switched off: release that plant's voices
            for (auto& v : voices)
                if (v.active && v.id >= 0 && v.id / 128 == c) v.active = false;
    }

    SpinLock::ScopedTryLockType sampleGuard (sampleLock);
    bp.haveSample = sampleGuard.isLocked() && sample.getNumSamples() > 1;

    // 4. Tempo
    const int tempoSrc = (int) *pTempoSource;
    double bpm = *pBpm, ppq = 0;
    bool havePpq = false;
    int tState = 0;
    if (tempoSrc == 1)
    {
        tState = 1;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm()) bpm = *b;
                if (pos->getIsPlaying())
                    if (auto q = pos->getPpqPosition()) { ppq = *q; havePpq = true; }
            }
    }
    else if (tempoSrc == 2)
    {
        if (external.isLocked())
        {
            tState = 2;
            bpm = external.getBpm();
            const double q = external.getPpqNow();
            if (q >= 0) { ppq = q; havePpq = true; }
        }
        else tState = 3;
    }
    bpm = jlimit (20.0, 300.0, bpm);
    currentBpm = bpm;
    tempoState = tState;
    if (external.starts.load() != lastExternalStarts) { lastExternalStarts = external.starts.load(); isoPhase = 0; }

    // 5. Drone voice and voice count
    {
        const bool droneOn = *pDrone > 0.5f;
        int di = -1;
        for (int i = 0; i < (int) voices.size(); ++i) if (voices[(size_t) i].active && voices[(size_t) i].id == -1) di = i;
        if (droneOn && di < 0) noteOn (-1, 60 + bp.key, 0.8f);
        else if (droneOn && di >= 0) voices[(size_t) di].note = 60 + bp.key;
        else if (! droneOn && di >= 0) voices[(size_t) di].active = false;
    }
    int numVoices = 0;
    for (auto& v : voices) if (v.active) ++numVoices;

    if (numVoices > 0 && *pScan != 0.0f)
    {
        scanOffset += *pScan * n / sr;
        scanOffset -= std::floor (scanOffset);
    }
    effectivePosition = (float) std::fmod (bp.position + scanOffset, 1.0);

    // 6. Isochronic gate for every sample
    const int isoSync = jlimit (0, 6, (int) *pIsoSync);
    const double rate = isoSync == 0 ? (double) *pIsoRate : bpm / 60.0 * isoPulsesPerBeat[isoSync];
    gateHz = (float) rate;
    const float gateTarget = *pIsoOn > 0.5f ? 1.0f : 0.0f;
    const float mixCoef = 1.0f - std::exp (-1.0f / (0.01f * (float) sr));
    const double ppqPerSample = bpm / 60.0 / sr;
    for (int i = 0; i < n; ++i)
    {
        double ph;
        if (isoSync > 0 && havePpq)
        {
            const double cycles = (ppq + i * ppqPerSample) * isoPulsesPerBeat[isoSync];
            ph = cycles - std::floor (cycles);
            isoPhase = ph;
        }
        else
        {
            isoPhase += rate / sr;
            isoPhase -= std::floor (isoPhase);
            ph = isoPhase;
        }
        if (ph < prevIsoPhase && gateTarget > 0.5f) ++pulseCount;   // new isochronic pulse
        prevIsoPhase = ph;
        {
            double beatPos;
            if (havePpq) beatPos = ppq + i * ppqPerSample;
            else { beatAcc += ppqPerSample; beatPos = beatAcc; }
            if (std::floor (beatPos) != std::floor (prevBeatPos)) ++beatCount;
            prevBeatPos = beatPos;
        }
        gateMix += mixCoef * (gateTarget - gateMix);
        const float g = computeGate (ph, rate);
        gateBuf[(size_t) i] = 1.0f - gateMix * (1.0f - g);
    }
    gateLevel = gateBuf[(size_t) n - 1];

    // 7. Spawn grains (sample-accurate start offsets), then render them
    gBuf.clear (0, n);
    for (int i = 0; i < n; ++i)
        for (auto& v : voices)
        {
            if (! v.active) continue;
            v.countdown -= 1.0;
            if (v.countdown <= 0.0)
            {
                bp.spawnOffset = i;
                spawnForVoice (v, numVoices);
                v.countdown += sr / jmax (1.0f, bp.density) * (0.75 + 0.5 * rng.nextDouble());
            }
        }

    {
        auto* gl = gBuf.getWritePointer (0);
        auto* gr = gBuf.getWritePointer (1);
        const int frames = sample.getNumSamples();
        const float* s0 = bp.haveSample ? sample.getReadPointer (0) : nullptr;
        const float* s1 = bp.haveSample ? sample.getReadPointer (sample.getNumChannels() > 1 ? 1 : 0) : nullptr;
        const auto& table = wavetables[(size_t) bp.wave];
        const int ringLen = liveRing.getNumSamples();
        const float* r0 = liveRing.getReadPointer (0);
        const float* r1 = liveRing.getReadPointer (1);

        for (auto& g : grains)
        {
            if (! g.active) continue;
            const int start = g.age < 0 ? jmin (n, -g.age) : 0;   // grains spawned this block start late
            if (g.age < 0) g.age = 0;

            for (int i = start; i < n; ++i)
            {
                if (g.age >= g.length) { g.active = false; break; }
                const float w = hann.t[(g.age * HannTable::size) / g.length] * g.amp;
                float l = 0, r = 0;
                if (g.src == 0)
                {
                    if (s0 == nullptr || g.pos < 0.0 || g.pos >= frames - 1) { g.active = false; break; }
                    const int i0 = (int) g.pos; const float fr = (float) (g.pos - i0);
                    l = s0[i0] + fr * (s0[i0 + 1] - s0[i0]);
                    r = s1[i0] + fr * (s1[i0 + 1] - s1[i0]);
                }
                else if (g.src == 1)
                {
                    const int i0 = (int) g.pos; const int i1 = (i0 + 1) & (tableSize - 1);
                    const float fr = (float) (g.pos - i0);
                    l = r = table[(size_t) i0] + fr * (table[(size_t) i1] - table[(size_t) i0]);
                }
                else
                {
                    const int i0 = (int) g.pos; const int i1 = (i0 + 1) % ringLen;
                    const float fr = (float) (g.pos - i0);
                    l = r0[i0] + fr * (r0[i1] - r0[i0]);
                    r = r1[i0] + fr * (r1[i1] - r1[i0]);
                }
                gl[i] += l * w * g.gL;
                gr[i] += r * w * g.gR;
                g.pos += g.inc;
                if (g.src == 1)      { if (g.pos >= tableSize) g.pos -= tableSize; else if (g.pos < 0) g.pos += tableSize; }
                else if (g.src == 2) { if (g.pos >= ringLen) g.pos -= ringLen; else if (g.pos < 0) g.pos += ringLen; }
                ++g.age;
            }
        }
    }

    // 8. Gate before FX (default) or after FX ("Pulse the FX tails")
    const bool fxPulse = *pFxPulse > 0.5f;
    if (! fxPulse)
        for (int c = 0; c < 2; ++c)
            FloatVectorOperations::multiply (gBuf.getWritePointer (c), gateBuf.data(), n);

    // 9. Ping-pong delay and reverb sends
    {
        const int dsync = jlimit (0, 8, (int) *pDlySync);
        double dSec;
        if (dsync == 0)      dSec = *pDlyTime * 0.001;
        else if (dsync <= 6) dSec = delayBeats[dsync] * 60.0 / bpm;
        else                 dSec = (dsync == 7 ? 1.0 : 2.0) / jmax (0.5, rate);
        const double targetDelay = jlimit (0.01, 1.9, dSec) * sr;
        const double dCoef = 1.0 - std::exp (-1.0 / (0.08 * sr));
        const float fb = *pDlyFb, send = *pDlySend, rsend = *pRevSend;
        const float a = 1.0f - std::exp (-MathConstants<float>::twoPi * *pDlyTone * 1000.0f / (float) sr);
        const double preSamples = *pRevPre * 0.001 * sr;

        auto* gl = gBuf.getReadPointer (0); auto* gr = gBuf.getReadPointer (1);
        auto* dl = dBuf.getWritePointer (0); auto* dr = dBuf.getWritePointer (1);
        auto* rl = rBuf.getWritePointer (0); auto* rr = rBuf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            dlySmoothed += dCoef * (targetDelay - dlySmoothed);
            const float oL = dL.read (dlySmoothed), oR = dR.read (dlySmoothed);
            lpL += a * (oL - lpL); lpR += a * (oR - lpR);
            const float inMono = 0.5f * (gl[i] + gr[i]) * send;
            dL.push (inMono + lpR * fb);
            dR.push (lpL * fb);
            dl[i] = oL; dr[i] = oR;
            preL.push ((gl[i] + oL) * rsend);
            preR.push ((gr[i] + oR) * rsend);
            rl[i] = preL.read (preSamples);
            rr[i] = preR.read (preSamples);
        }

        Reverb::Parameters rp;
        rp.roomSize = 0.3f + 0.68f * *pRevSize;
        rp.damping = jlimit (0.0f, 1.0f, 1.0f - (*pRevDamp - 1.0f) / 15.0f);
        rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f; rp.freezeMode = 0.0f;
        reverb.setParameters (rp);
        reverb.processStereo (rl, rr, n);
    }

    // 10. Sum, optional post-FX gate, limiter, volume
    for (int c = 0; c < 2; ++c)
    {
        auto* out = buffer.getWritePointer (c);
        const auto* g = gBuf.getReadPointer (c);
        const auto* d = dBuf.getReadPointer (c);
        const auto* r = rBuf.getReadPointer (c);
        for (int i = 0; i < n; ++i)
            out[i] = g[i] + d[i] + 0.6f * r[i];
        if (fxPulse)
            FloatVectorOperations::multiply (out, gateBuf.data(), n);
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);

    {
        auto outBlock = dsp::AudioBlock<float> (buffer).getSubsetChannelBlock (0, 2);
        limiter.process (dsp::ProcessContextReplacing<float> (outBlock));
    }
    buffer.applyGain (0, 0, n, *pGain);
    buffer.applyGain (1, 0, n, *pGain);

    const float pk = buffer.getMagnitude (0, 0, n);
    if (pk > outPeak.load()) outPeak = pk;
}

float HyphaeGrainProcessor::computeGate (double ph, double rateHz)
{
    const double duty = *pIsoDuty;
    const double low = 1.0 - *pIsoDepth;
    double e = *pIsoEdge * 0.001 * rateHz;               // edge length in phase units
    e = jlimit (1.0e-4, jmin (duty * 0.5, (1.0 - duty) * 0.5), e);
    double g;
    if (ph < e)              g = ph / e;
    else if (ph < duty - e)  g = 1.0;
    else if (ph < duty)      g = (duty - ph) / e;
    else                     g = 0.0;
    return (float) (low + (1.0 - low) * g);
}

//==============================================================================
void HyphaeGrainProcessor::handleMidi (int src, uint8 b0, uint8 b1, uint8 b2)
{
    lastMidi = ((uint32) (src + 1) << 24) | ((uint32) b0 << 16) | ((uint32) b1 << 8) | (uint32) b2;
    const int type = b0 & 0xF0, ch = b0 & 0x0F;

    if (type == 0xB0)
    {
        if (b1 == 90 && ch == 0) plantIntensity = (float) b2 / 127.0f;   // Biotron plant sensor
        if (src == 0 && (b1 == 120 || b1 == 123))
            for (auto& v : voices) if (v.id != -1) v.active = false;
        handleCC (src, ch, b1, b2);
        return;
    }

    const int id = (src * 16 + ch) * 128 + b1;
    if (type == 0x90 && b2 > 0)
    {
        const int follow = (int) *pKeyFollow;
        if ((follow == 1 && src == 0 && ch == 15) || (follow == 2 && src == 1))
        {
            if (auto* kp = apvts.getParameter ("key"))
                kp->setValueNotifyingHost (kp->convertTo0to1 ((float) (b1 % 12)));
            return;
        }
        pushNoteEvent (src * 16 + ch + 1, b1, b2);           // visuals still get the note
        if (src == 0 && ch < 2 && *pChNotes[ch] < 0.5f)      // channel set to CC only
            return;
        noteOn (id, b1, (float) b2 / 127.0f);
    }
    else if (type == 0x80 || type == 0x90)
    {
        noteOff (id);
        pushNoteEvent (src * 16 + ch + 1, b1, 0);
    }
}

void HyphaeGrainProcessor::handleCC (int src, int ch, int cc, int value)
{
    const int idx = mapIndex (src, ch, cc);
    const int target = learnTarget.load();
    if (target >= 0 && ccMap[(size_t) idx].load() != target)
    {
        for (auto& m : ccMap) if (m.load() == target) m = -1;
        ccMap[(size_t) idx] = target;
        ++mapVersion;
    }

    const int pi = ccMap[(size_t) idx].load();
    const auto& params = getParameters();
    if (pi < 0 || pi >= params.size()) return;

    auto* param = dynamic_cast<RangedAudioParameter*> (params[pi]);
    if (param == nullptr) return;

    float norm;
    if (auto* c = dynamic_cast<AudioParameterChoice*> (param))
    {
        const int num = c->choices.size();
        norm = c->convertTo0to1 ((float) jmin (num - 1, value * num / 128));
    }
    else if (dynamic_cast<AudioParameterBool*> (param) != nullptr)
        norm = value >= 64 ? 1.0f : 0.0f;
    else
        norm = (float) value / 127.0f;

    if (std::abs (param->getValue() - norm) > 1.0e-6f)
        param->setValueNotifyingHost (norm);
}

void HyphaeGrainProcessor::noteOn (int id, int note, float vel)
{
    for (auto& v : voices)
        if (v.active && v.id == id) { v.vel = vel; v.note = note; return; }
    Voice* slot = nullptr;
    for (auto& v : voices) if (! v.active) { slot = &v; break; }
    if (slot == nullptr)
        for (auto& v : voices) if (v.id != -1) { slot = &v; break; }
    if (slot == nullptr) return;
    slot->active = true; slot->id = id; slot->note = note; slot->vel = vel; slot->countdown = 0.0;
}

void HyphaeGrainProcessor::noteOff (int id)
{
    for (auto& v : voices)
        if (v.active && v.id == id) v.active = false;
}

void HyphaeGrainProcessor::spawnForVoice (const Voice& v, int numVoices)
{
    int tones[6];
    int transpose = bp.transpose;
    float level = 1.0f;
    if (v.id >= 0 && v.id / 128 < 2)   // Cubase channel 1 or 2
    {
        transpose += bp.chTranspose[v.id / 128];
        level = bp.chLevel[v.id / 128];
    }
    if (level <= 0.0f) return;
    const int num = music::chord (v.note + transpose, bp.key, bp.scale, bp.harmony, bp.quantize, tones);
    const float sizeSec = bp.size * 0.001f;
    const float norm = 0.5f / std::sqrt (jmax (1.0f, bp.density * sizeSec * jmax (1.0f, numVoices * 0.7f) * (float) num));
    const float amp = norm * (0.35f + 0.65f * v.vel) * level;
    for (int t = 0; t < num; ++t)
    {
        const double cents = (tones[t] - 60 + bp.pitch) * 100.0 + bp.detune * (rng.nextDouble() * 2.0 - 1.0);
        startGrain ((int) std::round (cents), amp);
    }
}

void HyphaeGrainProcessor::startGrain (int cents, float amp)
{
    Grain* g = nullptr;
    for (auto& gr : grains) if (! gr.active) { g = &gr; break; }
    if (g == nullptr) return;

    const int len = jmax (32, (int) (bp.size * 0.001 * sr));
    const double ratio = std::pow (2.0, cents / 1200.0);
    double pos = std::fmod (bp.position + scanOffset, 1.0) + (rng.nextDouble() * 2.0 - 1.0) * bp.spray;
    pos = jlimit (0.0, 1.0, pos);
    const bool rev = rng.nextFloat() < bp.reverse;

    if (bp.src == 0)
    {
        if (! bp.haveSample) return;
        const double r = ratio * sampleRateOfFile / sr;
        const double frames = sample.getNumSamples() - 2;
        const double span = len * r;
        double start = pos * frames;
        if (rev) start = jlimit (jmin (span, frames), frames, start);
        else     start = jlimit (0.0, jmax (0.0, frames - span), start);
        g->pos = start; g->inc = rev ? -r : r;
    }
    else if (bp.src == 1)
    {
        const double inc = 261.6255653 * ratio * tableSize / sr;
        g->pos = rng.nextDouble() * (tableSize - 1);
        g->inc = rev ? -inc : inc;
    }
    else
    {
        const int ringLen = liveRing.getNumSamples();
        const double minBack = jmax (0.0, ratio - 1.0) * len + 1024.0;
        const double maxBack = jmax (minBack + 1.0, jmin (3.5 * sr, ringLen - len * ratio - 1024.0));
        const double back = minBack + pos * (maxBack - minBack);
        double start = (double) liveWrite - back;
        while (start < 0) start += ringLen;
        g->pos = start; g->inc = rev ? -ratio : ratio;
    }

    const float p = (float) ((rng.nextDouble() * 2.0 - 1.0) * bp.stereo);
    const float angle = (p + 1.0f) * MathConstants<float>::pi * 0.25f;
    g->gL = std::cos (angle) * 1.2f;
    g->gR = std::sin (angle) * 1.2f;
    g->src = bp.src;
    g->length = len;
    g->age = -bp.spawnOffset;   // negative age = start offset inside the current block
    g->amp = amp;
    g->active = true;
    ++grainsSpawned;
}

//==============================================================================
bool HyphaeGrainProcessor::loadSample (const File& file)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) return false;

    const int len = (int) jmin<int64> (reader->lengthInSamples, (int64) (reader->sampleRate * 120.0));
    if (len < 64) return false;
    AudioBuffer<float> tmp (2, len);
    tmp.clear();
    reader->read (&tmp, 0, len, 0, true, true);
    if (reader->numChannels == 1)
        tmp.copyFrom (1, 0, tmp, 0, 0, len);

    std::vector<float> mn (800), mx (800);
    for (int i = 0; i < 800; ++i)
    {
        const int s0 = (int) ((int64) i * len / 800), s1 = jmax (s0 + 1, (int) ((int64) (i + 1) * len / 800));
        auto range = FloatVectorOperations::findMinAndMax (tmp.getReadPointer (0) + s0, s1 - s0);
        mn[(size_t) i] = range.getStart(); mx[(size_t) i] = range.getEnd();
    }

    {
        const SpinLock::ScopedLockType sl (sampleLock);
        std::swap (sample, tmp);
        sampleRateOfFile = reader->sampleRate;
    }
    peakMin = std::move (mn); peakMax = std::move (mx);
    sampleFile = file;
    return true;
}

bool HyphaeGrainProcessor::openExternalPort (const String& identifier)
{
    if (identifier.isEmpty()) { external.close(); return true; }
    return external.open (identifier);
}

void HyphaeGrainProcessor::clearMappingFor (int paramIndex)
{
    for (auto& m : ccMap) if (m.load() == paramIndex) m = -1;
    ++mapVersion;
}

String HyphaeGrainProcessor::mappingTextFor (int paramIndex) const
{
    for (int i = 0; i < (int) ccMap.size(); ++i)
        if (ccMap[(size_t) i].load() == paramIndex)
        {
            const int src = i / (16 * 128), ch = (i / 128) % 16, cc = i % 128;
            return String (src == 0 ? "Cubase" : "Traktor") + " ch" + String (ch + 1) + " CC" + String (cc);
        }
    return {};
}

int HyphaeGrainProcessor::paramIndexFor (const String& paramID) const
{
    const auto& params = getParameters();
    for (int i = 0; i < params.size(); ++i)
        if (auto* p = dynamic_cast<AudioProcessorParameterWithID*> (params[i]))
            if (p->paramID == paramID) return i;
    return -1;
}

//==============================================================================
void HyphaeGrainProcessor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    ValueTree extra ("Extra");
    extra.setProperty ("sample", sampleFile.getFullPathName(), nullptr);
    extra.setProperty ("port", external.getIdentifier(), nullptr);
    extra.setProperty ("oscOut", oscOutOn.load(), nullptr);
    extra.setProperty ("oscHost", oscHostName, nullptr);
    extra.setProperty ("oscOutPort", oscPortOut, nullptr);
    extra.setProperty ("oscIn", oscInOn, nullptr);
    extra.setProperty ("oscInPort", oscPortIn, nullptr);
    extra.setProperty ("bioMode", bioSendMode.load(), nullptr);
    extra.setProperty ("bioPort", bioPortId, nullptr);
    const auto& params = getParameters();
    for (int i = 0; i < (int) ccMap.size(); ++i)
    {
        const int p = ccMap[(size_t) i].load();
        if (p < 0 || p >= params.size()) continue;
        if (auto* pw = dynamic_cast<AudioProcessorParameterWithID*> (params[p]))
        {
            ValueTree m ("Map");
            m.setProperty ("slot", i, nullptr);
            m.setProperty ("param", pw->paramID, nullptr);
            extra.appendChild (m, nullptr);
        }
    }
    state.appendChild (extra, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void HyphaeGrainProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    auto tree = ValueTree::fromXml (*xml);
    auto extra = tree.getChildWithName ("Extra");
    if (extra.isValid()) tree.removeChild (extra, nullptr);
    apvts.replaceState (tree);
    bioResync = true;   // restoring a project never sends CCs to the Biotron

    if (! extra.isValid()) return;
    for (auto& m : ccMap) m = -1;
    for (const auto& m : extra)
        if (m.hasType ("Map"))
        {
            const int slot = m.getProperty ("slot", -1);
            const int p = paramIndexFor (m.getProperty ("param").toString());
            if (slot >= 0 && slot < (int) ccMap.size() && p >= 0) ccMap[(size_t) slot] = p;
        }
    ++mapVersion;

    const File f (extra.getProperty ("sample").toString());
    if (f.existsAsFile()) loadSample (f);
    const String port = extra.getProperty ("port").toString();
    if (port.isNotEmpty()) openExternalPort (port);

    setOscOut ((bool) extra.getProperty ("oscOut", false), extra.getProperty ("oscHost", "127.0.0.1").toString(), (int) extra.getProperty ("oscOutPort", 7000));
    setOscIn ((bool) extra.getProperty ("oscIn", false), (int) extra.getProperty ("oscInPort", 9000));
    bioSendMode = (int) extra.getProperty ("bioMode", 0);
    const String bport = extra.getProperty ("bioPort").toString();
    if (bport.isNotEmpty()) openBioPort (bport);
}

//==============================================================================
// OSC
void HyphaeGrainProcessor::pushNoteEvent (int ch, int note, int vel)
{
    NoteEvt e; e.ch = (uint8) ch; e.note = (uint8) note; e.vel = (uint8) vel;
    const auto scope = noteFifo.write (1);
    if (scope.blockSize1 > 0)      noteQueue[(size_t) scope.startIndex1] = e;
    else if (scope.blockSize2 > 0) noteQueue[(size_t) scope.startIndex2] = e;
}

void HyphaeGrainProcessor::setOscOut (bool enabled, const String& host, int port)
{
    const ScopedLock sl (oscLock);
    oscSender.disconnect();
    oscHostName = host.isNotEmpty() ? host : String ("127.0.0.1");
    oscPortOut = jlimit (1, 65535, port);
    bool ok = false;
    if (enabled) ok = oscSender.connect (oscHostName, oscPortOut);
    oscOutOn = ok;
    oscForceAll = true;
}

void HyphaeGrainProcessor::setOscIn (bool enabled, int port)
{
    oscReceiver.disconnect();
    oscPortIn = jlimit (1, 65535, port);
    oscInOn = enabled;
    oscInOk = enabled && oscReceiver.connect (oscPortIn);
}

String HyphaeGrainProcessor::getOscStatus() const
{
    String s = oscOutOn.load() ? "Sending to " + oscHostName + ":" + String (oscPortOut) + " · " + String (oscSentPerSec.load()) + " msg/s"
                               : String ("Sending is off");
    s << "\n";
    if (! oscInOn) s << "Receiving is off";
    else if (! oscInOk) s << "Port " << oscPortIn << " is busy. Try another port.";
    else s << "Listening on port " << oscPortIn << " · " << oscReceived.load() << " received";
    return s;
}

void HyphaeGrainProcessor::hiResTimerCallback()
{
    if (++bioTickCounter % 10 == 0) bioTick();   // every 50 ms
    if (! oscOutOn.load()) return;
    const ScopedTryLock sl (oscLock);
    if (! sl.isLocked()) return;

    const String P (oscPrefix);
    int sent = 0;
    auto send = [&] (const String& addr, auto... args) { if (oscSender.send (OSCAddressPattern (P + addr), args...)) ++sent; };

    // Events: notes, pulses, beats (every 5 ms)
    NoteEvt evs[64];
    {
        const auto scope = noteFifo.read (jmin (64, noteFifo.getNumReady()));
        int k = 0;
        for (int i = 0; i < scope.blockSize1; ++i) evs[k++] = noteQueue[(size_t) (scope.startIndex1 + i)];
        for (int i = 0; i < scope.blockSize2; ++i) evs[k++] = noteQueue[(size_t) (scope.startIndex2 + i)];
        for (int i = 0; i < k; ++i)
        {
            const String ch = "/ch" + String ((int) evs[i].ch);
            send ("/note", (int) evs[i].ch, (int) evs[i].note, (int) evs[i].vel);
            send (ch + "/note", (float) evs[i].note / 127.0f);
            send (ch + "/velocity", (float) evs[i].vel / 127.0f);
            send (ch + "/gate", evs[i].vel > 0 ? 1 : 0);
        }
    }
    const uint32 pc = pulseCount.load();
    if (pc != sentPulse) { sentPulse = pc; send ("/pulse", 1); send ("/pulse/count", (int) pc); }
    const uint32 bc = beatCount.load();
    if (bc != sentBeat) { sentBeat = bc; send ("/beat", 1); send ("/beat/count", (int) bc); send ("/beat/bar", (int) (bc % 4) + 1); }

    // Continuous values (50 Hz)
    if (++oscTick % 4 == 0)
    {
        const float pk = outPeak.exchange (0.0f);
        levelSmoothed = jmax (pk, levelSmoothed * 0.85f);
        const uint32 gs = grainsSpawned.load();
        grainRate += 0.2f * ((float) (gs - lastGrains) * 50.0f - grainRate);
        lastGrains = gs;
        const float hz = gateHz.load();
        const double bpm = currentBpm.load();

        send ("/gate", gateLevel.load());
        send ("/level", jlimit (0.0f, 1.0f, levelSmoothed));
        send ("/position", effectivePosition.load());
        send ("/grains", grainRate);
        send ("/grains/norm", jlimit (0.0f, 1.0f, grainRate / 400.0f));
        send ("/isohz", hz);
        send ("/isohz/norm", jlimit (0.0f, 1.0f, hz / 45.0f));
        send ("/band", hz < 4.0f ? 0 : hz < 8.0f ? 1 : hz < 13.0f ? 2 : hz < 30.0f ? 3 : 4);
        send ("/bpm", (float) bpm);
        send ("/bpm/norm", (float) jlimit (0.0, 1.0, (bpm - 40.0) / 160.0));
        send ("/plant/intensity", plantIntensity.load());
    }

    // Parameters: send only what changed (10 Hz), everything after (re)connecting
    if (oscTick % 20 == 0)
    {
        const auto& params = getParameters();
        const bool all = oscForceAll.exchange (false);
        if ((int) lastSentParam.size() != params.size()) lastSentParam.assign ((size_t) params.size(), -1.0f);
        for (int i = 0; i < params.size(); ++i)
        {
            auto* pw = dynamic_cast<AudioProcessorParameterWithID*> (params[i]);
            if (pw == nullptr) continue;
            const float v = params[i]->getValue();
            if (! all && std::abs (v - lastSentParam[(size_t) i]) < 1.0e-4f) continue;
            lastSentParam[(size_t) i] = v;
            send ("/param/" + pw->paramID, v);
            if (pw->paramID == "key" || pw->paramID == "scale" || pw->paramID == "harmony")
                if (auto* c = dynamic_cast<AudioParameterChoice*> (params[i]))
                    send ("/" + pw->paramID, c->getIndex());
        }
    }

    sentThisSecond += sent;
    if (oscTick % 200 == 0) { oscSentPerSec = sentThisSecond; sentThisSecond = 0; }
}

void HyphaeGrainProcessor::oscMessageReceived (const OSCMessage& m)
{
    ++oscReceived;
    if (m.isEmpty()) return;
    String addr = m.getAddressPattern().toString();
    const String pre = String (oscPrefix) + "/param/";
    if (! addr.startsWith (pre)) return;
    addr = addr.substring (pre.length());
    const bool realUnits = addr.endsWith ("/value");
    if (realUnits) addr = addr.dropLastCharacters (6);

    auto* p = apvts.getParameter (addr);
    if (p == nullptr) return;
    const auto& a = m[0];
    float v;
    if (a.isFloat32())    v = a.getFloat32();
    else if (a.isInt32()) v = (float) a.getInt32();
    else return;
    const float norm = realUnits ? p->convertTo0to1 (v) : jlimit (0.0f, 1.0f, v);
    p->beginChangeGesture();
    p->setValueNotifyingHost (norm);
    p->endChangeGesture();
}

//==============================================================================
// Biotron: send a CC only when its control changes (never on load), at most 4 per 50 ms.
void HyphaeGrainProcessor::bioTick()
{
    if (bioResync.exchange (false))
    {
        for (int i = 0; i < numBio; ++i) bioLast[(size_t) i] = bioParams[(size_t) i]->getValue();
        return;
    }
    int sent = 0;
    for (int i = 0; i < numBio && sent < 4; ++i)
    {
        const float v = bioParams[(size_t) i]->getValue();
        if (std::abs (v - bioLast[(size_t) i]) > 1.0e-6f)
        {
            bioLast[(size_t) i] = v;
            sendBioCC (i);
            ++sent;
        }
    }
    int idx = bioSendAllIndex.load();
    if (idx >= 0)
    {
        for (int k = 0; k < 2 && idx < numBio; ++k) sendBioCC (idx++);
        bioSendAllIndex = idx >= numBio ? -1 : idx;
    }
}

void HyphaeGrainProcessor::sendBioCC (int i)
{
    auto* prm = bioParams[(size_t) i];
    const auto& b = bioTable[i];
    const int value = b.kind == bioOnOff ? (prm->getValue() >= 0.5f ? 127 : 0)
                                         : jlimit (0, 127, roundToInt (prm->convertFrom0to1 (prm->getValue())));
    const int status = 0xB0 | b.ch;
    const uint32 packed = ((uint32) status << 16) | ((uint32) b.cc << 8) | (uint32) value;
    lastBioSent = packed;
    ++bioSentTotal;

    if (bioSendMode.load() == 1)
    {
        const ScopedLock sl (bioLock);
        if (bioOut != nullptr) bioOut->sendMessageNow (MidiMessage::controllerEvent (b.ch + 1, b.cc, value));
    }
    else
    {
        const auto scope = bioFifo.write (1);
        if (scope.blockSize1 > 0)      bioQueue[(size_t) scope.startIndex1] = packed;
        else if (scope.blockSize2 > 0) bioQueue[(size_t) scope.startIndex2] = packed;
    }
}

bool HyphaeGrainProcessor::openBioPort (const String& identifier)
{
    const ScopedLock sl (bioLock);
    bioOut.reset();
    bioPortId = identifier;
    if (identifier.isEmpty()) return true;
    bioOut = MidiOutput::openDevice (identifier);
    return bioOut != nullptr;
}

AudioProcessorEditor* HyphaeGrainProcessor::createEditor() { return new HyphaeGrainEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HyphaeGrainProcessor(); }
