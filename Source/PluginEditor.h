#pragma once

#include "PluginProcessor.h"

namespace hg
{
    namespace col
    {
        const juce::Colour bg     { 0xff11130e };
        const juce::Colour panel  { 0xff191c15 };
        const juce::Colour line   { 0xff2d3226 };
        const juce::Colour fg     { 0xffe7e4d4 };
        const juce::Colour mute   { 0xff9b9b87 };
        const juce::Colour spore  { 0xffe2a94b };   // isochronic / accent
        const juce::Colour hypha  { 0xff8ec2b1 };   // MIDI / musical
        const juce::Colour dim    { 0xff4a4f40 };
    }

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    };

    //==============================================================================
    // One labelled control bound to one parameter. Knows its parameter index for MIDI learn.
    class ParamRow : public juce::Component
    {
    public:
        ParamRow (HyphaeGrainProcessor&, const juce::String& paramID, const juce::String& labelText, juce::Colour accent);
        void resized() override;
        void paint (juce::Graphics&) override;

        juce::String labelText;
        int paramIndex = -1;
        juce::String mapText;

    private:
        juce::Slider slider;
        juce::ComboBox combo;
        juce::ToggleButton toggle;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   sliderAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   buttonAtt;
        enum class Kind { slider, combo, toggle } kind = Kind::slider;
    };

    //==============================================================================
    class Section : public juce::Component
    {
    public:
        Section (const juce::String& t, const juce::String& tg) : title (t), tag (tg) {}
        void paint (juce::Graphics&) override;
        void resized() override;
        ParamRow& add (std::unique_ptr<ParamRow>);
        void addExtra (juce::Component& c, int height) { extras.push_back ({ &c, height }); addAndMakeVisible (c); }
        int preferredHeight() const;

        std::vector<std::unique_ptr<ParamRow>> rows;
    private:
        juce::String title, tag;
        std::vector<std::pair<juce::Component*, int>> extras;
    };

    //==============================================================================
    class WaveView : public juce::Component
    {
    public:
        explicit WaveView (HyphaeGrainProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
    private:
        HyphaeGrainProcessor& proc;
    };

    //==============================================================================
    // Transparent layer shown in MIDI Learn mode: click a control to pick it.
    class LearnOverlay : public juce::Component
    {
    public:
        std::vector<ParamRow*> rows;
        ParamRow* selected = nullptr;
        std::function<void (ParamRow*)> onSelect;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
    };
}

//==============================================================================
class HyphaeGrainEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit HyphaeGrainEditor (HyphaeGrainProcessor&);
    ~HyphaeGrainEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setLearning (bool);
    void refreshPorts();
    void updateStatus();
    std::unique_ptr<hg::ParamRow> row (const char* id, const char* label, juce::Colour accent = hg::col::fg);

    HyphaeGrainProcessor& proc;
    hg::LookAndFeel lnf;

    juce::TextButton learnButton { "MIDI Learn" }, removeButton { "Remove mapping" }, doneButton { "Done" };
    juce::Label learnLabel, midiMonitor, statusLabel;

    hg::WaveView wave;
    hg::Section source { "Source", "INPUT" }, musical { "Musical", "KEY + HARMONY" }, grains { "Grains", "CLOUD" },
                iso { "Isochronic", "GATE" }, tempo { "Tempo", "BPM" }, master { "Master", "FX + OUT" },
                osc { "OSC", "VISUALS" }, channels { "Channels", "CH 1 + CH 2" };

    juce::TextButton loadButton { "Load audio file" };
    juce::Label fileLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Component presetBox;
    juce::TextButton presets[5];

    juce::Component portHolder;
    juce::ComboBox portCombo;
    juce::Label portLabel, tempoInfo;
    juce::StringArray portIds;

    juce::Component oscBox;
    juce::Label oscOutLabel, oscToLabel, oscInLabel, oscStatus, oscColon;
    juce::ToggleButton oscOutToggle, oscInToggle;
    juce::TextEditor oscHostEdit, oscPortEdit, oscInPortEdit;
    void applyOsc();

    hg::LearnOverlay overlay;
    std::vector<hg::ParamRow*> allRows;

    bool learning = false;
    int seenMapVersion = -1, portTimer = 0;
    juce::Rectangle<int> lampArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HyphaeGrainEditor)
};
