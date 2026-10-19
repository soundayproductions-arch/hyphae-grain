#include "PluginEditor.h"

using namespace juce;
using namespace hg;

namespace
{
    Font uiFont (float size, bool bold = false)
    {
        auto opts = FontOptions (size);
        return Font (bold ? opts.withStyle ("Bold") : opts);
    }

    const char* bandName (float hz)
    {
        if (hz < 4.0f)  return "delta";
        if (hz < 8.0f)  return "theta";
        if (hz < 13.0f) return "alpha";
        if (hz < 30.0f) return "beta";
        return "gamma";
    }

    constexpr int rowH = 30;
}

//==============================================================================
hg::LookAndFeel::LookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, col::bg);
    setColour (Label::textColourId, col::fg);
    setColour (Slider::textBoxTextColourId, col::fg);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (ComboBox::backgroundColourId, col::bg);
    setColour (ComboBox::outlineColourId, col::line);
    setColour (ComboBox::textColourId, col::fg);
    setColour (ComboBox::arrowColourId, col::mute);
    setColour (PopupMenu::backgroundColourId, col::panel);
    setColour (PopupMenu::textColourId, col::fg);
    setColour (PopupMenu::highlightedBackgroundColourId, col::line);
    setColour (PopupMenu::highlightedTextColourId, col::fg);
    setColour (TextButton::buttonColourId, col::panel);
    setColour (TextButton::buttonOnColourId, col::spore);
    setColour (TextButton::textColourOffId, col::fg);
    setColour (TextButton::textColourOnId, col::bg);
    setColour (ToggleButton::textColourId, col::mute);
    setColour (TextEditor::backgroundColourId, col::bg);
    setColour (TextEditor::outlineColourId, col::line);
    setColour (TextEditor::focusedOutlineColourId, col::hypha);
    setColour (TextEditor::textColourId, col::fg);
    setColour (CaretComponent::caretColourId, col::hypha);
}

void hg::LookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                        Slider::SliderStyle, Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f;
    const auto accent = s.findColour (Slider::thumbColourId);
    g.setColour (col::line);
    g.fillRoundedRectangle ((float) x, cy - 2.0f, (float) w, 4.0f, 2.0f);
    g.setColour (accent.withAlpha (0.45f));
    g.fillRoundedRectangle ((float) x, cy - 2.0f, pos - (float) x, 4.0f, 2.0f);
    g.setColour (accent);
    g.fillEllipse (pos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
}

void hg::LookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float bw = 34.0f, bh = 18.0f;
    auto sw = Rectangle<float> (r.getX() + 2.0f, r.getCentreY() - bh * 0.5f, bw, bh);
    const bool on = b.getToggleState();
    g.setColour (on ? col::spore : col::line);
    g.fillRoundedRectangle (sw, bh * 0.5f);
    g.setColour (on ? col::bg : (highlighted ? col::fg : col::mute));
    const float kx = on ? sw.getRight() - bh + 2.0f : sw.getX() + 2.0f;
    g.fillEllipse (kx, sw.getY() + 2.0f, bh - 4.0f, bh - 4.0f);
    g.setColour (on ? col::fg : col::mute);
    g.setFont (uiFont (13.0f));
    g.drawText (on ? "On" : "Off", sw.getRight() + 8.0f, r.getY(), 40.0f, r.getHeight(), Justification::centredLeft);
}

//==============================================================================
ParamRow::ParamRow (HyphaeGrainProcessor& p, const String& paramID, const String& text, Colour accent)
    : labelText (text)
{
    paramIndex = p.paramIndexFor (paramID);
    auto* param = p.apvts.getParameter (paramID);
    jassert (param != nullptr);

    if (auto* c = dynamic_cast<AudioParameterChoice*> (param))
    {
        kind = Kind::combo;
        combo.addItemList (c->choices, 1);
        addAndMakeVisible (combo);
        comboAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, paramID, combo);
    }
    else if (dynamic_cast<AudioParameterBool*> (param) != nullptr)
    {
        kind = Kind::toggle;
        addAndMakeVisible (toggle);
        buttonAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, paramID, toggle);
    }
    else
    {
        slider.setSliderStyle (Slider::LinearHorizontal);
        slider.setTextBoxStyle (Slider::TextBoxRight, false, 72, 20);
        slider.setColour (Slider::thumbColourId, accent);
        addAndMakeVisible (slider);
        sliderAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, paramID, slider);
    }
}

void ParamRow::resized()
{
    auto r = getLocalBounds().withTrimmedLeft (104);
    if (kind == Kind::combo) combo.setBounds (r.reduced (0, 3));
    else if (kind == Kind::toggle) toggle.setBounds (r);
    else slider.setBounds (r);
}

void ParamRow::paint (Graphics& g)
{
    auto r = getLocalBounds().withWidth (100);
    if (mapText.isEmpty())
    {
        g.setColour (col::mute);
        g.setFont (uiFont (13.0f));
        g.drawText (labelText, r, Justification::centredLeft, true);
    }
    else
    {
        g.setColour (col::mute);
        g.setFont (uiFont (13.0f));
        g.drawText (labelText, r.withHeight (getHeight() / 2 + 2), Justification::bottomLeft, true);
        g.setColour (col::hypha);
        g.setFont (uiFont (10.0f));
        g.drawText (mapText, r.withTrimmedTop (getHeight() / 2 + 2), Justification::topLeft, true);
    }
}

//==============================================================================
ParamRow& Section::add (std::unique_ptr<ParamRow> r)
{
    addAndMakeVisible (*r);
    rows.push_back (std::move (r));
    return *rows.back();
}

int Section::preferredHeight() const
{
    int h = 40 + (int) rows.size() * rowH + 10;
    for (auto& e : extras) h += e.second;
    return h;
}

void Section::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (col::fg);
    g.setFont (uiFont (17.0f, true));
    g.drawText (title, 14, 8, getWidth() - 28, 24, Justification::centredLeft);
    g.setColour (col::mute);
    g.setFont (uiFont (10.0f));
    g.drawText (tag, 14, 8, getWidth() - 28, 24, Justification::centredRight);
}

void Section::resized()
{
    auto r = getLocalBounds().reduced (14, 0).withTrimmedTop (40);
    for (auto& e : extras)
        if (e.second > 0 && e.first->getProperties()["top"])
            e.first->setBounds (r.removeFromTop (e.second));
    for (auto& row : rows)
        row->setBounds (r.removeFromTop (rowH));
    for (auto& e : extras)
        if (! e.first->getProperties()["top"])
            e.first->setBounds (r.removeFromTop (e.second));
}

//==============================================================================
void WaveView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

    auto* srcParam = proc.apvts.getRawParameterValue ("source");
    const int src = (int) srcParam->load();
    const float pos = proc.effectivePosition.load();
    const float spray = proc.apvts.getRawParameterValue ("spray")->load();
    const float w = r.getWidth(), h = r.getHeight(), mid = h * 0.5f;

    g.setFont (uiFont (11.0f));
    if (src == 0 && ! proc.peakMax.empty())
    {
        const float sx = (pos - spray) * w, ex = (pos + spray) * w;
        g.setColour (col::spore.withAlpha (0.10f));
        g.fillRect (sx, 0.0f, ex - sx, h);
        const int bins = (int) proc.peakMax.size();
        for (int i = 0; i < bins; ++i)
        {
            const float x = (float) i / (float) bins * w;
            const float top = mid - proc.peakMax[(size_t) i] * mid * 0.85f;
            const float bot = mid - proc.peakMin[(size_t) i] * mid * 0.85f;
            g.setColour ((x >= sx && x <= ex) ? col::fg : col::dim);
            g.fillRect (x, top, jmax (1.0f, w / (float) bins), jmax (1.0f, bot - top));
        }
        g.setColour (col::spore);
        g.fillRect (pos * w - 1.0f, 0.0f, 2.0f, h);
        g.setColour (col::mute);
        auto f = proc.getSampleFile();
        g.drawText (f.existsAsFile() ? f.getFileName() : String ("Spore field (built-in)"), 12, 6, (int) w - 24, 16, Justification::topLeft);
    }
    else if (src == 1)
    {
        const int wv = jlimit (0, 3, (int) proc.apvts.getRawParameterValue ("wave")->load());
        const auto& t = proc.wavetables[(size_t) wv];
        Path p;
        for (int i = 0; i < (int) w; ++i)
        {
            const float v = t[(size_t) ((i * 2 * HyphaeGrainProcessor::tableSize / (int) w) % HyphaeGrainProcessor::tableSize)];
            const float y = mid - v * mid * 0.8f;
            if (i == 0) p.startNewSubPath (0.0f, y); else p.lineTo ((float) i, y);
        }
        g.setColour (col::fg);
        g.strokePath (p, PathStrokeType (1.5f));
        g.setColour (col::mute);
        g.drawText ("Oscillator (two cycles shown)", 12, 6, (int) w - 24, 16, Justification::topLeft);
    }
    else
    {
        g.setColour (col::spore.withAlpha (0.10f));
        g.fillRect ((pos - spray) * w, 0.0f, spray * 2.0f * w, h);
        g.setColour (col::spore);
        g.fillRect (pos * w - 1.0f, 0.0f, 2.0f, h);
        g.setColour (proc.liveActive.load() ? col::hypha : col::mute);
        g.drawText (proc.liveActive.load() ? "Live input: signal on the side-chain"
                                           : "Live input: no signal. In Cubase, activate side-chain on the instrument and send audio to it.",
                    12, 6, (int) w - 24, 16, Justification::topLeft);
        g.setColour (col::mute);
        g.drawText ("now", (int) w - 60, (int) h - 22, 48, 16, Justification::centredRight);
        g.drawText ("3.5 s ago", 12, (int) h - 22, 100, 16, Justification::centredLeft);
    }
}

//==============================================================================
void PlantMeter::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.0f, 4.0f);
    g.setColour (col::bg);
    g.fillRoundedRectangle (r, 4.0f);
    const float v = jlimit (0.0f, 1.0f, proc.plantIntensity.load());
    g.setColour (col::hypha);
    g.fillRoundedRectangle (r.withWidth (r.getWidth() * v), 4.0f);
    g.setColour (col::fg);
    g.setFont (uiFont (11.0f));
    g.drawText (String (roundToInt (v * 127.0f)), r.reduced (6.0f, 0.0f), Justification::centredRight);
}

//==============================================================================
void LearnOverlay::paint (Graphics& g)
{
    for (auto* r : rows)
    {
        if (! r->isShowing()) continue;
        auto b = getLocalArea (r, r->getLocalBounds()).toFloat().expanded (2.0f, 0.0f);
        if (r == selected)
        {
            g.setColour (col::spore.withAlpha (0.16f));
            g.fillRoundedRectangle (b, 4.0f);
            g.setColour (col::spore);
            g.drawRoundedRectangle (b, 4.0f, 2.0f);
        }
        else
        {
            g.setColour (col::hypha.withAlpha (0.55f));
            const float dashes[] = { 4.0f, 3.0f };
            Path p; p.addRoundedRectangle (b, 4.0f);
            Path dashed;
            PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
            g.fillPath (dashed);
        }
    }
}

void LearnOverlay::mouseDown (const MouseEvent& e)
{
    for (auto* r : rows)
        if (r->isShowing() && getLocalArea (r, r->getLocalBounds()).contains (e.getPosition()))
        {
            selected = r;
            if (onSelect) onSelect (r);
            repaint();
            return;
        }
}

//==============================================================================
std::unique_ptr<ParamRow> HyphaeGrainEditor::row (const char* id, const char* label, Colour accent)
{
    return std::make_unique<ParamRow> (proc, id, label, accent);
}

HyphaeGrainEditor::HyphaeGrainEditor (HyphaeGrainProcessor& p)
    : AudioProcessorEditor (&p), proc (p), wave (p)
{
    setLookAndFeel (&lnf);

    // Header
    learnButton.setClickingTogglesState (true);
    learnButton.onClick = [this] { setLearning (learnButton.getToggleState()); };
    addAndMakeVisible (learnButton);
    statusLabel.setFont (uiFont (12.0f));
    statusLabel.setColour (Label::textColourId, col::mute);
    statusLabel.setJustificationType (Justification::centredRight);
    addAndMakeVisible (statusLabel);

    learnLabel.setFont (uiFont (13.0f));
    addAndMakeVisible (learnLabel);
    midiMonitor.setFont (uiFont (12.0f));
    midiMonitor.setColour (Label::textColourId, col::hypha);
    midiMonitor.setJustificationType (Justification::centredRight);
    addAndMakeVisible (midiMonitor);
    removeButton.onClick = [this]
    {
        if (overlay.selected != nullptr) proc.clearMappingFor (overlay.selected->paramIndex);
    };
    doneButton.onClick = [this] { learnButton.setToggleState (false, dontSendNotification); setLearning (false); };
    addChildComponent (removeButton);
    addChildComponent (doneButton);

    addAndMakeVisible (wave);

    // Source
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Load audio for the grain cloud", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile())
            {
                if (proc.loadSample (f)) fileLabel.setText (f.getFileName(), dontSendNotification);
                else fileLabel.setText ("Could not read that file", dontSendNotification);
                if (auto* sp = proc.apvts.getParameter ("source")) sp->setValueNotifyingHost (0.0f);
            }
        });
    };
    fileLabel.setFont (uiFont (12.0f));
    fileLabel.setColour (Label::textColourId, col::hypha);
    {
        auto f = proc.getSampleFile();
        fileLabel.setText (f.existsAsFile() ? f.getFileName() : String ("Spore field (built-in)"), dontSendNotification);
    }
    source.add (row ("source", "Source"));
    source.add (row ("wave", "Waveform"));
    source.addExtra (loadButton, 32);
    source.addExtra (fileLabel, 24);

    // Musical
    musical.add (row ("key", "Key", col::hypha));
    musical.add (row ("scale", "Scale", col::hypha));
    musical.add (row ("harmony", "Harmony", col::hypha));
    musical.add (row ("quantize", "Quantize"));
    musical.add (row ("transpose", "Transpose", col::hypha));
    musical.add (row ("keyFollow", "Key follow"));
    musical.add (row ("drone", "Drone"));

    // Grains
    grains.add (row ("position", "Position"));
    grains.add (row ("spray", "Spray"));
    grains.add (row ("scan", "Scan"));
    grains.add (row ("size", "Grain size"));
    grains.add (row ("density", "Density"));
    grains.add (row ("pitch", "Pitch"));
    grains.add (row ("detune", "Detune"));
    grains.add (row ("stereo", "Stereo"));
    grains.add (row ("reverse", "Reverse"));

    // Isochronic
    const std::pair<const char*, float> bands[5] = { { "Delta", 2.0f }, { "Theta", 6.0f }, { "Alpha", 10.0f }, { "Beta", 18.0f }, { "Gamma", 40.0f } };
    for (int i = 0; i < 5; ++i)
    {
        presets[i].setButtonText (String (bands[i].first) + "\n" + String ((int) bands[i].second) + " Hz");
        const float hz = bands[i].second;
        presets[i].onClick = [this, hz]
        {
            if (auto* s = proc.apvts.getParameter ("isoSync")) { s->beginChangeGesture(); s->setValueNotifyingHost (0.0f); s->endChangeGesture(); }
            if (auto* r = proc.apvts.getParameter ("isoRate")) { r->beginChangeGesture(); r->setValueNotifyingHost (r->convertTo0to1 (hz)); r->endChangeGesture(); }
        };
        presetBox.addAndMakeVisible (presets[i]);
    }
    presetBox.getProperties().set ("top", true);
    iso.addExtra (presetBox, 44);
    iso.add (row ("isoOn", "Gate"));
    iso.add (row ("isoSync", "Sync"));
    iso.add (row ("isoRate", "Rate", col::spore));
    iso.add (row ("isoDepth", "Depth", col::spore));
    iso.add (row ("isoDuty", "Duty", col::spore));
    iso.add (row ("isoEdge", "Edge", col::spore));

    // Tempo
    tempo.add (row ("tempoSource", "Source"));
    tempo.add (row ("bpm", "Internal BPM"));
    portLabel.setText ("Traktor port", dontSendNotification);
    portLabel.setFont (uiFont (13.0f));
    portLabel.setColour (Label::textColourId, col::mute);
    portCombo.onChange = [this]
    {
        const int idx = portCombo.getSelectedId() - 2;
        proc.openExternalPort (idx >= 0 && idx < portIds.size() ? portIds[idx] : String());
    };
    portHolder.addAndMakeVisible (portLabel);
    portHolder.addAndMakeVisible (portCombo);
    tempo.addExtra (portHolder, 32);
    tempoInfo.setFont (uiFont (12.0f));
    tempoInfo.setColour (Label::textColourId, col::hypha);
    tempo.addExtra (tempoInfo, 40);
    tempoInfo.setJustificationType (Justification::topLeft);

    // Master
    master.add (row ("dlySync", "Delay sync"));
    master.add (row ("dlyTime", "Delay time", col::hypha));
    master.add (row ("dlyFb", "Feedback", col::hypha));
    master.add (row ("dlyTone", "Delay tone", col::hypha));
    master.add (row ("dlySend", "Delay send", col::hypha));
    master.add (row ("revSize", "Reverb size", col::hypha));
    master.add (row ("revDamp", "Damping", col::hypha));
    master.add (row ("revPre", "Pre-delay", col::hypha));
    master.add (row ("revSend", "Reverb send", col::hypha));
    master.add (row ("fxPulse", "Pulse FX tails"));
    master.add (row ("gain", "Volume"));

    // Biotron page: plant (ch 1) and light (ch 2)
    for (int c = 0; c < 2; ++c)
    {
        auto& s = c == 0 ? plantSec : lightSec;
        const String n (c + 1), L (c == 0 ? "P" : "L");
        s.add (row (("ch" + n + "Notes").toRawUTF8(), "Notes"));
        s.add (row (("ch" + n + "Transpose").toRawUTF8(), "Transpose", col::hypha));
        s.add (row (("ch" + n + "Level").toRawUTF8(), "Volume", col::hypha));
        s.add (row (("bio" + L + "MaxVel").toRawUTF8(), "Max velocity", col::spore));
        s.add (row (("bio" + L + "Tempo").toRawUTF8(), "Tempo div.", col::spore));
        s.add (row (("bio" + L + "MinDist").toRawUTF8(), "Min distance", col::spore));
        s.add (row (("bio" + L + "Humanize").toRawUTF8(), "Humanize"));
        s.add (row (("bio" + L + "HumMin").toRawUTF8(), "Humanize min", col::spore));
        s.add (row (("bio" + L + "Mute").toRawUTF8(), "Mute"));
    }
    lightSec.add (row ("bioLightBend", "Pitch bend"));
    lightSec.add (row ("bioLightRange", "Note range", col::spore));

    sensorSec.add (row ("bioFilter", "Input filter", col::spore));
    sensorSec.add (row ("bioUltra", "Ultra sens."));
    sensorSec.add (row ("bioWake", "Wake-up", col::spore));
    sensorSec.add (row ("bioPerf", "Performance", col::spore));
    sensorSec.add (row ("bioHold", "Note hold", col::spore));
    sensorSec.add (row ("bioStep", "Step size", col::spore));
    sensorSec.add (row ("bioScale", "Scale", col::spore));
    sensorSec.add (row ("bioSwing", "Swing", col::spore));
    sensorSec.add (row ("bioCentre", "Centre note", col::spore));
    sensorSec.add (row ("bioBtnMute", "Button mute", col::spore));

    for (auto* l : { &bioSendLabel, &bioPortLabel, &bioMeterLabel })
    {
        l->setFont (uiFont (13.0f));
        l->setColour (Label::textColourId, col::mute);
        bioBox.addAndMakeVisible (*l);
    }
    bioSendLabel.setText ("Send via", dontSendNotification);
    bioPortLabel.setText ("Biotron port", dontSendNotification);
    bioMeterLabel.setText ("Plant CC90", dontSendNotification);
    bioSendCombo.addItem ("Cubase MIDI track", 1);
    bioSendCombo.addItem ("Direct to port", 2);
    bioSendCombo.setSelectedId (proc.getBioSendMode() + 1, dontSendNotification);
    bioSendCombo.onChange = [this] { proc.setBioSendMode (bioSendCombo.getSelectedId() - 1); updateStatus(); };
    bioPortCombo.onChange = [this]
    {
        const int idx = bioPortCombo.getSelectedId() - 2;
        proc.openBioPort (idx >= 0 && idx < bioPortIds.size() ? bioPortIds[idx] : String());
        updateStatus();
    };
    bioSendAll.onClick = [this] { proc.requestBioSendAll(); };
    bioMeter = std::make_unique<PlantMeter> (proc);
    bioInfo.setFont (uiFont (12.0f));
    bioInfo.setColour (Label::textColourId, col::hypha);
    bioInfo.setJustificationType (Justification::topLeft);
    for (Component* c : { (Component*) &bioSendCombo, (Component*) &bioPortCombo, (Component*) &bioSendAll, (Component*) bioMeter.get(), (Component*) &bioInfo })
        bioBox.addAndMakeVisible (c);
    linkSec.addExtra (bioBox, 250);

    pageSynth.setClickingTogglesState (true);
    pageBio.setClickingTogglesState (true);
    pageSynth.setRadioGroupId (4711);
    pageBio.setRadioGroupId (4711);
    pageSynth.onClick = [this] { showPage (0); };
    pageBio.onClick = [this] { showPage (1); };
    addAndMakeVisible (pageSynth);
    addAndMakeVisible (pageBio);

    // OSC
    for (auto* l : { &oscOutLabel, &oscToLabel, &oscInLabel })
    {
        l->setFont (uiFont (13.0f));
        l->setColour (Label::textColourId, col::mute);
        oscBox.addAndMakeVisible (*l);
    }
    oscOutLabel.setText ("Send", dontSendNotification);
    oscToLabel.setText ("Send to", dontSendNotification);
    oscInLabel.setText ("Receive", dontSendNotification);
    oscColon.setText (":", dontSendNotification);
    oscColon.setColour (Label::textColourId, col::mute);
    oscBox.addAndMakeVisible (oscColon);
    oscOutToggle.setToggleState (proc.getOscOutEnabled(), dontSendNotification);
    oscInToggle.setToggleState (proc.getOscInEnabled(), dontSendNotification);
    oscHostEdit.setText (proc.getOscHost(), false);
    oscPortEdit.setText (String (proc.getOscOutPort()), false);
    oscInPortEdit.setText (String (proc.getOscInPort()), false);
    oscPortEdit.setInputRestrictions (5, "0123456789");
    oscInPortEdit.setInputRestrictions (5, "0123456789");
    for (auto* e : { &oscHostEdit, &oscPortEdit, &oscInPortEdit })
    {
        e->setFont (uiFont (13.0f));
        e->onReturnKey = [this] { applyOsc(); };
        e->onFocusLost = [this] { applyOsc(); };
        oscBox.addAndMakeVisible (*e);
    }
    oscOutToggle.onClick = [this] { applyOsc(); };
    oscInToggle.onClick = [this] { applyOsc(); };
    oscBox.addAndMakeVisible (oscOutToggle);
    oscBox.addAndMakeVisible (oscInToggle);
    oscStatus.setFont (uiFont (12.0f));
    oscStatus.setColour (Label::textColourId, col::hypha);
    oscStatus.setJustificationType (Justification::topLeft);
    oscBox.addAndMakeVisible (oscStatus);
    osc.addExtra (oscBox, 136);

    for (auto* s : { &source, &musical, &grains, &iso, &tempo, &master, &osc, &plantSec, &lightSec, &sensorSec, &linkSec })
    {
        addAndMakeVisible (*s);
        for (auto& r : s->rows) allRows.push_back (r.get());
    }

    overlay.rows = allRows;
    overlay.onSelect = [this] (ParamRow* r)
    {
        proc.learnTarget = r->paramIndex;
        removeButton.setVisible (r->mapText.isNotEmpty());
        updateStatus();
    };
    addChildComponent (overlay);

    refreshPorts();
    refreshBioPorts();
    showPage (0);
    setSize (1240, 790);
    startTimerHz (20);
    updateStatus();
}

HyphaeGrainEditor::~HyphaeGrainEditor()
{
    proc.learnTarget = -1;
    setLookAndFeel (nullptr);
}

void HyphaeGrainEditor::showPage (int p)
{
    page = p;
    pageSynth.setToggleState (p == 0, dontSendNotification);
    pageBio.setToggleState (p == 1, dontSendNotification);
    for (auto* s : { &source, &musical, &grains, &iso, &tempo, &master, &osc })
        s->setVisible (p == 0);
    for (auto* s : { &plantSec, &lightSec, &sensorSec, &linkSec })
        s->setVisible (p == 1);
    overlay.selected = nullptr;
    proc.learnTarget = -1;
    overlay.toFront (false);
    repaint();
}

void HyphaeGrainEditor::refreshBioPorts()
{
    const auto devices = MidiOutput::getAvailableDevices();
    StringArray ids;
    for (auto& d : devices) ids.add (d.identifier);
    if (ids == bioPortIds && bioPortCombo.getNumItems() > 0) return;
    bioPortIds = ids;
    bioPortCombo.clear (dontSendNotification);
    bioPortCombo.addItem ("None", 1);
    for (int i = 0; i < devices.size(); ++i) bioPortCombo.addItem (devices[i].name, i + 2);
    const int cur = bioPortIds.indexOf (proc.getBioPortId());
    bioPortCombo.setSelectedId (cur >= 0 ? cur + 2 : 1, dontSendNotification);
}

void HyphaeGrainEditor::applyOsc()
{
    proc.setOscOut (oscOutToggle.getToggleState(), oscHostEdit.getText().trim(), oscPortEdit.getText().getIntValue());
    proc.setOscIn (oscInToggle.getToggleState(), oscInPortEdit.getText().getIntValue());
    updateStatus();
}

void HyphaeGrainEditor::setLearning (bool on)
{
    learning = on;
    proc.learnTarget = -1;
    overlay.selected = nullptr;
    overlay.setVisible (on);
    overlay.toFront (false);
    removeButton.setVisible (false);
    doneButton.setVisible (on);
    updateStatus();
    repaint();
}

void HyphaeGrainEditor::refreshPorts()
{
    const auto devices = MidiInput::getAvailableDevices();
    StringArray ids;
    for (auto& d : devices) ids.add (d.identifier);
    if (ids == portIds && portCombo.getNumItems() > 0) return;

    portIds = ids;
    portCombo.clear (dontSendNotification);
    portCombo.addItem ("None", 1);
    for (int i = 0; i < devices.size(); ++i)
        portCombo.addItem (devices[i].name, i + 2);

    const int cur = portIds.indexOf (proc.getExternalPortId());
    portCombo.setSelectedId (cur >= 0 ? cur + 2 : 1, dontSendNotification);
}

void HyphaeGrainEditor::updateStatus()
{
    // Header status: key and tempo
    auto* keyP = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter ("key"));
    auto* scP  = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter ("scale"));
    auto* hP   = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter ("harmony"));
    const float hz = proc.gateHz.load();
    statusLabel.setText (keyP->getCurrentChoiceName() + " " + scP->getCurrentChoiceName() + "  ·  " + hP->getCurrentChoiceName()
                           + "  ·  " + String (proc.currentBpm.load(), 1) + " BPM  ·  gate " + String (hz, 2) + " Hz (" + bandName (hz) + ")",
                         dontSendNotification);

    const int ts = proc.tempoState.load();
    String t;
    if (ts == 0) t = "Using the internal BPM.";
    else if (ts == 1) t = "Following Cubase: " + String (proc.currentBpm.load(), 1) + " BPM.";
    else if (ts == 2) t = "Traktor clock locked: " + String (proc.currentBpm.load(), 1) + " BPM.";
    else t = proc.getExternalPortId().isEmpty() ? "Choose the LoopBe port Traktor sends its clock to."
                                                : "Waiting for clock. Press play under SEND in Traktor's Master Clock.";
    tempoInfo.setText (t, dontSendNotification);
    oscStatus.setText (proc.getOscStatus(), dontSendNotification);
    {
        String b;
        if (proc.getBioSendMode() == 0)
            b = "In Cubase: add a MIDI track with input \"Mycelium Sound System Biotron\" and output = Biotron.";
        else
            b = proc.getBioPortId().isEmpty() ? String ("Choose the Biotron port.")
              : proc.isBioPortOpen() ? String ("Port open.") : String ("Port busy: disable it in Cubase or use the MIDI track.");
        const uint32 m = proc.lastBioSent.load();
        if (proc.bioSentTotal.load() > 0)
            b << "\nSent " << proc.bioSentTotal.load() << " · last CC" << (int) ((m >> 8) & 0xFF) << " = " << (int) (m & 0xFF)
              << " on ch " << (int) (((m >> 16) & 0x0F) + 1);
        b << "\nOnly sent when you move a control. The Biotron does not report its settings back.";
        bioInfo.setText (b, dontSendNotification);
    }

    // Learn bar
    if (learning)
    {
        learnLabel.setColour (Label::textColourId, col::fg);
        if (overlay.selected != nullptr)
            learnLabel.setText ("Linking " + overlay.selected->labelText + ". Move a knob, fader or button on the Biotron, Traktor or your controller.", dontSendNotification);
        else
            learnLabel.setText ("Click any control, then move a knob, fader or button to link it.", dontSendNotification);
    }
    else
    {
        learnLabel.setColour (Label::textColourId, col::mute);
        learnLabel.setText ("MIDI Learn links any control to a CC from Cubase tracks (e.g. the Biotron) or the Traktor port.", dontSendNotification);
    }

    const uint32 m = proc.lastMidi.load();
    if (m != 0)
    {
        const int src = (int) (m >> 24) - 1, st = (int) ((m >> 16) & 0xFF), d1 = (int) ((m >> 8) & 0xFF), d2 = (int) (m & 0xFF);
        const int type = st & 0xF0, ch = (st & 0x0F) + 1;
        String what = type == 0xB0 ? "CC " + String (d1) + " = " + String (d2)
                    : type == 0x90 ? "Note " + MidiMessage::getMidiNoteName (d1, true, true, 3) + " vel " + String (d2)
                    : type == 0x80 ? "Note off " + MidiMessage::getMidiNoteName (d1, true, true, 3)
                                   : "Status " + String::toHexString (st);
        midiMonitor.setText (String ("Last: ") + (src == 0 ? "Cubase" : "Traktor") + " ch" + String (ch) + " · " + what, dontSendNotification);
    }
    else midiMonitor.setText ("No MIDI received yet", dontSendNotification);
}

void HyphaeGrainEditor::timerCallback()
{
    const int mv = proc.mapVersion.load();
    if (mv != seenMapVersion)
    {
        seenMapVersion = mv;
        for (auto* r : allRows)
        {
            const auto t = proc.mappingTextFor (r->paramIndex);
            if (t != r->mapText) { r->mapText = t; r->repaint(); }
        }
        if (overlay.selected != nullptr) removeButton.setVisible (overlay.selected->mapText.isNotEmpty());
    }
    if (++portTimer >= 40) { portTimer = 0; refreshPorts(); refreshBioPorts(); }
    if (page == 1 && bioMeter != nullptr) bioMeter->repaint();
    updateStatus();
    wave.repaint();
    repaint (lampArea);
}

void HyphaeGrainEditor::paint (Graphics& g)
{
    g.fillAll (col::bg);
    g.setFont (uiFont (28.0f, true));
    g.setColour (col::fg);
    g.drawText ("Mycelium", 20, 14, 140, 36, Justification::centredLeft);
    g.setColour (col::spore);
    g.drawText ("Sound System", 152, 14, 200, 36, Justification::centredLeft);
    g.setColour (col::hypha);
    g.drawText ("Biotron", 336, 14, 120, 36, Justification::centredLeft);

    // Gate lamp
    const float lvl = proc.gateLevel.load();
    auto lamp = lampArea.toFloat().withSizeKeepingCentre (14.0f, 14.0f);
    g.setColour (col::dim);
    g.fillEllipse (lamp);
    g.setColour (col::spore.withAlpha (jlimit (0.0f, 1.0f, (lvl - 0.15f) / 0.85f)));
    g.fillEllipse (lamp);

    // Learn bar background
    auto bar = Rectangle<int> (16, 62, getWidth() - 32, 40).toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (bar, 8.0f);
    g.setColour (learning ? col::spore : col::line);
    g.drawRoundedRectangle (bar.reduced (0.5f), 8.0f, 1.0f);
}

void HyphaeGrainEditor::resized()
{
    auto r = getLocalBounds().reduced (16, 0);

    auto header = r.removeFromTop (58).withTrimmedTop (14);
    learnButton.setBounds (header.removeFromRight (120).reduced (0, 6));
    header.removeFromRight (10);
    lampArea = header.removeFromRight (24);
    header.removeFromRight (10);
    pageBio.setBounds (header.removeFromRight (84).reduced (0, 6));
    pageSynth.setBounds (header.removeFromRight (84).reduced (2, 6));
    statusLabel.setBounds (header.withTrimmedLeft (450));

    r.removeFromTop (4);
    auto bar = r.removeFromTop (40).reduced (12, 6);
    doneButton.setBounds (bar.removeFromRight (70));
    bar.removeFromRight (8);
    removeButton.setBounds (bar.removeFromRight (130));
    bar.removeFromRight (8);
    midiMonitor.setBounds (bar.removeFromRight (270));
    learnLabel.setBounds (bar);

    r.removeFromTop (10);
    wave.setBounds (r.removeFromTop (100));
    r.removeFromTop (12);

    auto body = r.withTrimmedBottom (16);
    overlay.setBounds (body);

    const int gap = 12;
    const int colW = (body.getWidth() - gap * 3) / 4;
    auto c1 = body.removeFromLeft (colW); body.removeFromLeft (gap);
    auto c2 = body.removeFromLeft (colW); body.removeFromLeft (gap);
    auto c3 = body.removeFromLeft (colW); body.removeFromLeft (gap);
    auto c4 = body;

    source.setBounds (c1.removeFromTop (source.preferredHeight()));
    c1.removeFromTop (gap);
    musical.setBounds (c1.removeFromTop (musical.preferredHeight()));
    grains.setBounds (c2.removeFromTop (grains.preferredHeight()));
    c2.removeFromTop (gap);
    osc.setBounds (c2.removeFromTop (osc.preferredHeight()));
    iso.setBounds (c3.removeFromTop (iso.preferredHeight()));
    c3.removeFromTop (gap);
    tempo.setBounds (c3.removeFromTop (tempo.preferredHeight()));
    master.setBounds (c4.removeFromTop (master.preferredHeight()));

    // Biotron page uses the same four columns
    plantSec.setBounds (c1.getX(), source.getY(), c1.getWidth(), plantSec.preferredHeight());
    lightSec.setBounds (c2.getX(), grains.getY(), c2.getWidth(), lightSec.preferredHeight());
    sensorSec.setBounds (c3.getX(), iso.getY(), c3.getWidth(), sensorSec.preferredHeight());
    linkSec.setBounds (c4.getX(), master.getY(), c4.getWidth(), linkSec.preferredHeight());
    {
        auto bb = bioBox.getLocalBounds();
        auto r1 = bb.removeFromTop (30);
        bioSendLabel.setBounds (r1.removeFromLeft (104));
        bioSendCombo.setBounds (r1.reduced (0, 3));
        auto r2 = bb.removeFromTop (30);
        bioPortLabel.setBounds (r2.removeFromLeft (104));
        bioPortCombo.setBounds (r2.reduced (0, 3));
        auto r3 = bb.removeFromTop (30);
        bioMeterLabel.setBounds (r3.removeFromLeft (104));
        bioMeter->setBounds (r3);
        bioSendAll.setBounds (bb.removeFromTop (34).reduced (0, 3));
        bioInfo.setBounds (bb.withTrimmedTop (6));
    }

    auto pb = presetBox.getLocalBounds().reduced (0, 4);
    const int bw = pb.getWidth() / 5;
    for (int i = 0; i < 5; ++i) presets[i].setBounds (pb.removeFromLeft (bw).reduced (2, 0));

    {
        auto ob = oscBox.getLocalBounds();
        auto r1 = ob.removeFromTop (30);
        oscOutLabel.setBounds (r1.removeFromLeft (104));
        oscOutToggle.setBounds (r1);
        auto r2 = ob.removeFromTop (30);
        oscToLabel.setBounds (r2.removeFromLeft (104));
        oscPortEdit.setBounds (r2.removeFromRight (56).reduced (0, 3));
        oscColon.setBounds (r2.removeFromRight (12));
        oscHostEdit.setBounds (r2.reduced (0, 3));
        auto r3 = ob.removeFromTop (30);
        oscInLabel.setBounds (r3.removeFromLeft (104));
        oscInPortEdit.setBounds (r3.removeFromRight (56).reduced (0, 3));
        oscInToggle.setBounds (r3);
        oscStatus.setBounds (ob.withTrimmedTop (4));
    }

    auto hb = portHolder.getLocalBounds();
    portLabel.setBounds (hb.removeFromLeft (104));
    portCombo.setBounds (hb.reduced (0, 3));
}
