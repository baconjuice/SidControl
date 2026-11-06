#include "MultiEditor.h"
#include "LookAndFeel.h"

#include "sid/SysEx.h"

namespace ui
{
namespace
{
const sid::ParamTable& T() { return sid::multiParams(); }

class InstrumentPage : public Page
{
public:
    explicit InstrumentPage (Session& s) : Page (s, T())
    {
        for (int i = 0; i < 6; ++i)
        {
            auto& b = add (std::make_unique<juce::TextButton> ("Instrument " + juce::String (i + 1)));
            b.setRadioGroupId (1003);
            b.setClickingTogglesState (true);
            b.onClick = [this] { resized(); };
            insButtons.push_back (&b);
        }
        insButtons[0]->setToggleState (true, juce::dontSendNotification);
        link = &add (makeSelector ({ "Edit one instrument", "Link 1+4, 2+5, 3+6", "Link 1-3 / 4-6", "Link all 6" }));
        link->setTooltip ("Mirror edits to other instruments, like the link function on the MIDIbox SID");

        auto* linkSel = link;
        WriteOptionFn wopt = [linkSel]
        {
            switch (linkSel->getSelectedId())
            {
                case 2: return sid::sysex::wopt::LeftRight;
                case 3: return sid::sysex::wopt::Voices;
                case 4: return sid::sysex::wopt::All;
                default: return sid::sysex::wopt::None;
            }
        };

        const auto parChoices = parameterNumberChoices (sid::Engine::Multi);
        for (int i = 1; i <= 6; ++i)
        {
            const std::string p = "m" + std::to_string (i);
            Columns c;
            c[0] = addVoiceSections (*this, p, "Instrument " + juce::String (i), wopt, true, false);

            auto& play = group ("Play Mode", wopt);
            play.combo (p + ".legato", "Mode", 90);
            play.combo (p + ".voice", "Voice", 80);
            play.toggles ({ { p + ".poly", "Poly" }, { p + ".suskey", "SusKey" } });
            play.toggle (p + ".wto", "WT Only");
            play.lineBreak();
            play.combo (p + ".velpar", "Velocity To", 180, parChoices);
            play.combo (p + ".pbpar", "PitchBend To", 180, parChoices);
            c[1].push_back (&play);

            auto& wt = group ("Wavetable Sequencer", wopt);
            wt.combo (p + ".wt.par", "Parameter", 200, parChoices);
            wt.lineBreak();
            wt.knob (p + ".wt.div", "Clock Div");
            wt.knob (p + ".wt.start", "Start");
            wt.knob (p + ".wt.end", "End");
            wt.knob (p + ".wt.loop", "Loop");
            wt.lineBreak();
            wt.toggle (p + ".wt.oneshot", "Oneshot");
            wt.toggle (p + ".wt.w16", "W+16");
            c[1].push_back (&wt);

            c[2].push_back (&addLfo7Section (*this, p + ".lfo1", "LFO 1", wopt));
            c[2].push_back (&addLfo7Section (*this, p + ".lfo2", "LFO 2", wopt));
            c[2].push_back (&addEnv9Section (*this, p + ".env", "Envelope", wopt));
            instruments.push_back (c);
        }

        layout = [this] (int w)
        {
            auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
            auto row = area.removeFromTop (26);
            int selected = 0;
            for (size_t i = 0; i < insButtons.size(); ++i)
            {
                insButtons[i]->setBounds (row.removeFromLeft (100));
                row.removeFromLeft (4);
                if (insButtons[i]->getToggleState()) selected = (int) i;
            }
            row.removeFromLeft (16);
            link->setBounds (row.removeFromLeft (200));
            area.removeFromTop (8);

            const int colW = (area.getWidth() - 16) / 3;
            int bottom = 0;
            for (int i = 0; i < 6; ++i)
            {
                int x = area.getX();
                for (auto& col : instruments[(size_t) i])
                {
                    int y = area.getY();
                    for (auto* g : col)
                    {
                        g->setVisible (i == selected);
                        if (i != selected) continue;
                        const int h = g->preferredHeight (colW);
                        g->setBounds (x, y, colW, h);
                        y += h + 8;
                    }
                    bottom = juce::jmax (bottom, y);
                    x += colW + 8;
                }
            }
            return bottom;
        };
    }

private:
    using Columns = std::array<std::vector<ParamGroup*>, 3>;
    std::vector<Columns> instruments;
    std::vector<juce::TextButton*> insButtons;
    juce::ComboBox* link = nullptr;
};

class WavetablePage : public Page
{
public:
    explicit WavetablePage (Session& s) : Page (s, T())
    {
        std::vector<WavetableView::Track> tracks;
        for (int i = 1; i <= 6; ++i)
            tracks.push_back ({ "m" + std::to_string (i) + ".wt", "I" + juce::String (i) });
        view = &add (std::make_unique<WavetableView> (s, T(), tracks));
        stepEditor = &add (std::make_unique<StepEditor> (s));
        view->onSelect = [this] (int step) { stepEditor->setStep (step); };
        info = &add (makeLabel ("All 6 instruments share these 128 wavetable steps; the coloured bars show the range each "
                                "instrument's sequencer uses (set on the Instruments page)."));
        layout = [this] (int w)
        {
            auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
            info->setBounds (area.removeFromTop (20));
            area.removeFromTop (6);
            stepEditor->setBounds (area.removeFromTop (28));
            area.removeFromTop (6);
            view->setBounds (area.removeFromTop (320));
            return view->getBottom() + 8;
        };
    }

private:
    WavetableView* view = nullptr;
    StepEditor* stepEditor = nullptr;
    juce::Label* info = nullptr;
};

class FilterPage : public Page
{
public:
    explicit FilterPage (Session& s) : Page (s, T())
    {
        filters = addFilterSections (*this, linkButton);
        auto& gl = group ("Global");
        gl.knob ("eng.volume", "Volume");
        global = &gl;
        hardware = &addHardwareSection (*this);
        layout = [this] (int w)
        {
            auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
            linkButton->setBounds (area.removeFromTop (24).removeFromLeft (140));
            area.removeFromTop (8);
            area.setY (columns (filters, 2, area));
            return columns ({ global, hardware }, 2, area);
        };
    }

private:
    juce::TextButton* linkButton = nullptr;
    std::vector<ParamGroup*> filters;
    ParamGroup* global = nullptr;
    ParamGroup* hardware = nullptr;
};
} // namespace

MultiEditor::MultiEditor (Session& s)
{
    addPage ("Instruments", std::make_unique<InstrumentPage> (s));
    addPage ("Wavetable", std::make_unique<WavetablePage> (s));
    addPage ("Filter & Global", std::make_unique<FilterPage> (s));
    addPage ("Knobs & Ext", makeKnobsPage (s, T(), sid::Engine::Multi));
}
} // namespace ui
