#pragma once

// Building blocks shared by the engine editors: scrolling pages, common
// parameter sections (filters, knobs, LFOs, envelopes, voices) and the
// wavetable editor.

#include "ParamControls.h"

namespace ui
{
juce::String signedText (int v);
juce::String noteText (int midiNote);

std::unique_ptr<juce::ComboBox> makeSelector (std::initializer_list<const char*> items, int initial = 1);
std::unique_ptr<juce::Label> makeLabel (const juce::String& text, bool heading = false);

// Scrollable page whose content is laid out by `layout`, which returns the
// content height for a given width.
class Page : public juce::Component
{
public:
    Page (ParamTarget& target, const sid::ParamTable& table);

    void resized() override;

    template <typename C>
    C& add (std::unique_ptr<C> c)
    {
        auto& ref = *c;
        content.addAndMakeVisible (ref);
        owned.push_back (std::move (c));
        return ref;
    }

    ParamGroup& group (const juce::String& title, WriteOptionFn wopt = {});

    // Lays groups out in equal-width columns, row by row. Returns the bottom y.
    static int columns (const std::vector<ParamGroup*>& groups, int cols, juce::Rectangle<int> area, int gap = 8);

    std::function<int (int width)> layout = [] (int) { return 0; };
    int minContentWidth = 0;

    ParamTarget& target;
    const sid::ParamTable& table;

protected:
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<juce::Component>> owned;
};

// ---- sections (each adds groups to a page and returns them) ---------------

// Waveform, pulsewidth, SID ADSR, pitch, portamento (voice structure).
std::vector<ParamGroup*> addVoiceSections (Page& page, const std::string& p, const juce::String& title, WriteOptionFn wopt,
                                          bool withArp, bool withSwinSid);

// LFO / envelope of the Bassline and Multi engines.
ParamGroup& addLfo7Section (Page& page, const std::string& p, const juce::String& title, WriteOptionFn wopt = {});
ParamGroup& addEnv9Section (Page& page, const std::string& p, const juce::String& title, WriteOptionFn wopt = {});

// Filters of both SIDs, with a "link" button that mirrors edits to the other side.
std::vector<ParamGroup*> addFilterSections (Page& page, juce::TextButton*& linkButton);
ParamGroup& addHardwareSection (Page& page);

// Lead engine LFO rate shows note values when clock sync is on.
FormatFn lfoRateFormat (ParamTarget& t, const sid::ParamDef& clockSyncFlag);

// ---- pages ----------------------------------------------------------------

// Knob assignments, external AOUTs and switches (identical for all engines).
std::unique_ptr<Page> makeKnobsPage (ParamTarget& t, const sid::ParamTable& table, sid::Engine engine);

// Wavetable steps (0x180..0x1ff) with the ranges used by each sequencer.
class WavetableView : public juce::Component,
                      private ParamTarget::Listener
{
public:
    struct Track { std::string prefix; juce::String label; }; // prefix.start/.end/.loop
    WavetableView (ParamTarget& t, const sid::ParamTable& table, std::vector<Track> tracks);
    ~WavetableView() override;

    std::function<void (int step)> onSelect;
    int selected = 0;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override { edit (e, true); }
    void mouseDrag (const juce::MouseEvent& e) override { edit (e, false); }

private:
    juce::Rectangle<int> plotArea() const;
    void edit (const juce::MouseEvent& e, bool down);
    void targetReplaced() override { repaint(); }
    void targetChanged (const std::vector<uint16_t>&) override { repaint(); }

    ParamTarget& target;
    const sid::ParamTable& table;
    std::vector<Track> tracks;
};

// Value editor for the selected wavetable step.
class StepEditor : public juce::Component,
                   private ParamTarget::Listener
{
public:
    explicit StepEditor (ParamTarget& t);
    ~StepEditor() override;
    void setStep (int s);
    void resized() override;

private:
    void refresh();
    void apply();
    void targetReplaced() override { refresh(); }
    void targetChanged (const std::vector<uint16_t>& a) override;

    ParamTarget& target;
    int step = 0;
    juce::Label title, hint;
    juce::ComboBox mode;
    juce::Slider value;
};

// Tabs for an engine editor.
class EngineEditor : public juce::Component
{
public:
    EngineEditor();
    void addPage (const juce::String& name, std::unique_ptr<juce::Component> page);
    void resized() override { tabs.setBounds (getLocalBounds()); }
    juce::TabbedComponent& getTabs() { return tabs; }

private:
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
};
} // namespace ui
