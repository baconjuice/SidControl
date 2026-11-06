#include "DrumEditor.h"
#include "LookAndFeel.h"

namespace ui
{
namespace
{
const sid::ParamTable& T() { return sid::drumParams(); }

// ---------------------------------------------------------------------------
// Instruments
// ---------------------------------------------------------------------------
class InstrumentsPage : public Page
{
public:
    explicit InstrumentsPage (Session& s) : Page (s, T())
    {
        const auto parChoices = parameterNumberChoices (sid::Engine::Drum);
        for (int i = 1; i <= 16; ++i)
        {
            const std::string p = "d" + std::to_string (i);
            const int track = (i - 1) / 2 + 1;
            auto& g = group ("Instrument " + juce::String (i) + "  (track " + juce::String (track)
                             + (i % 2 ? ", A)" : ", B)"));
            g.combo (p + ".model", "Model", 80);
            g.combo (p + ".voice", "Voice", 80);
            g.combo (p + ".velpar", "Velocity To", 150, parChoices);
            g.lineBreak();
            g.knob (p + ".attack", "Atk");
            g.knob (p + ".decay", "Dec");
            g.knob (p + ".sustain", "Sus");
            g.knob (p + ".release", "Rel");
            g.lineBreak();
            g.knob (p + ".tune", {}, signedText);
            g.knob (p + ".gatelength", "Gate Len", signedText);
            g.knob (p + ".speed", {}, signedText);
            g.knob (p + ".par3", "Param 3", signedText);
            groups.push_back (&g);
        }
        layout = [this] (int w) { return columns (groups, 4, juce::Rectangle<int> (0, 0, w, 10000).reduced (8)); };
    }

private:
    std::vector<ParamGroup*> groups;
};

// ---------------------------------------------------------------------------
// Pattern grid: 8 sequences x 8 tracks x 16 steps. Track t plays instruments
// 2t+1 (A) and 2t+2 (B). Per step (gate, accent): 10 = A, 01 = A accented,
// 11 = B, 00 = rest. Bytes per track: gate 1-8, accent 1-8, gate 9-16, accent 9-16.
// ---------------------------------------------------------------------------
class PatternGrid : public juce::Component,
                    private ParamTarget::Listener
{
public:
    explicit PatternGrid (ParamTarget& t) : target (t) { target.addTargetListener (this); }
    ~PatternGrid() override { target.removeTargetListener (this); }

    int sequence = 0;
    static constexpr int kLabelW = 150, kHeaderH = 22, kRowH = 34;
    static int preferredHeight() { return kHeaderH + 8 * kRowH + 2; }

    enum State { Rest = 0, A = 1, AAccent = 2, B = 3 };

    void paint (juce::Graphics& g) override
    {
        const float cw = cellW();
        g.setFont (font (11.5f));
        for (int s = 0; s < 16; ++s)
        {
            g.setColour (s % 4 == 0 ? colours::text : colours::dimText);
            g.drawText (juce::String (s + 1), kLabelW + (int) ((float) s * cw), 0, (int) cw, kHeaderH, juce::Justification::centred);
        }
        const auto& models = sid::drumModels();
        for (int t = 0; t < 8; ++t)
        {
            const int y = kHeaderH + t * kRowH;
            auto modelOf = [&] (int instr)
            {
                const int m = target.getParam (T().get ("d" + std::to_string (instr) + ".model"));
                return m < (int) models.size() ? juce::String (models[(size_t) m]) : juce::String (m);
            };
            g.setColour (colours::text);
            g.setFont (font (12.5f, true));
            g.drawText ("Track " + juce::String (t + 1), 4, y, 60, kRowH, juce::Justification::centredLeft);
            g.setFont (font (11.5f));
            g.setColour (colours::accent);
            g.drawText ("A " + modelOf (2 * t + 1), 66, y, 42, kRowH, juce::Justification::centredLeft);
            g.setColour (colours::good);
            g.drawText ("B " + modelOf (2 * t + 2), 108, y, 42, kRowH, juce::Justification::centredLeft);

            for (int s = 0; s < 16; ++s)
            {
                auto c = juce::Rectangle<float> ((float) kLabelW + (float) s * cw, (float) y, cw, (float) kRowH).reduced (2.5f);
                g.setColour (s % 4 == 0 ? colours::panelEdge.brighter (0.1f) : colours::panel);
                g.fillRoundedRectangle (c, 3.0f);
                const auto st = state (t, s);
                if (st == Rest) continue;
                const auto col = st == B ? colours::good : colours::accent;
                g.setColour (col);
                g.fillRoundedRectangle (c.reduced (3.0f), 3.0f);
                if (st == AAccent)
                {
                    g.setColour (colours::accent2);
                    g.drawRoundedRectangle (c.reduced (2.0f), 3.0f, 2.5f);
                }
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.setFont (font (11.0f, true));
                g.drawText (st == B ? "B" : (st == AAccent ? "A>" : "A"), c, juce::Justification::centred);
            }
        }
    }

    // Click cycles rest -> A -> A accent -> B -> rest. Drag paints the new state.
    void mouseDown (const juce::MouseEvent& e) override
    {
        int t, s;
        if (! cellAt (e.position, t, s)) return;
        paintState = e.mods.isPopupMenu() ? Rest : (State) ((state (t, s) + 1) % 4);
        setState (t, s, paintState);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        int t, s;
        if (cellAt (e.position, t, s) && state (t, s) != paintState)
            setState (t, s, paintState);
    }

private:
    float cellW() const { return (float) (getWidth() - kLabelW) / 16.0f; }

    bool cellAt (juce::Point<float> p, int& t, int& s) const
    {
        if (p.x < (float) kLabelW || p.y < (float) kHeaderH) return false;
        t = (int) ((p.y - (float) kHeaderH) / (float) kRowH);
        s = (int) ((p.x - (float) kLabelW) / cellW());
        return t >= 0 && t < 8 && s >= 0 && s < 16;
    }

    int gateAddr (int t, int s) const { return 0x100 + sequence * 0x20 + t * 4 + (s >= 8 ? 2 : 0); }

    State state (int t, int s) const
    {
        const int bit = 1 << (s % 8);
        const bool gate = (target.getByte (gateAddr (t, s)) & bit) != 0;
        const bool acc = (target.getByte (gateAddr (t, s) + 1) & bit) != 0;
        if (gate && acc) return B;
        if (gate) return A;
        if (acc) return AAccent;
        return Rest;
    }

    void setState (int t, int s, State st)
    {
        const int bit = 1 << (s % 8);
        const int ga = gateAddr (t, s);
        const bool gate = st == A || st == B;
        const bool acc = st == AAccent || st == B;
        const int gv = target.getByte (ga), av = target.getByte (ga + 1);
        target.setByteValue ((uint16_t) ga, (uint8_t) (gate ? gv | bit : gv & ~bit));
        target.setByteValue ((uint16_t) (ga + 1), (uint8_t) (acc ? av | bit : av & ~bit));
    }

    void targetReplaced() override { repaint(); }
    void targetChanged (const std::vector<uint16_t>& a) override
    {
        for (auto x : a)
            if (x >= 0x60) { repaint(); return; } // pattern data or instrument models
    }

    ParamTarget& target;
    State paintState = A;
};

class PatternPage : public Page
{
public:
    explicit PatternPage (Session& s) : Page (s, T())
    {
        auto& g = group ("Sequencer");
        g.toggle ("dr.seq.on", "Sequencer On");
        g.combo ("dr.seq.num", "Play Sequence", 90);
        g.knob ("dr.seq.steps", "Steps");
        g.knob ("dr.seq.div", "Clock Div");
        g.toggle ("dr.seq.measure", "Sync to Measure");
        g.gap (20);
        g.knob ("eng.volume", "Volume");
        settings = &g;

        for (int i = 0; i < 8; ++i)
        {
            auto& b = add (std::make_unique<juce::TextButton> ("Seq " + juce::String (i + 1)));
            b.setRadioGroupId (1002);
            b.setClickingTogglesState (true);
            b.onClick = [this, i] { grid->sequence = i; grid->repaint(); };
            seqButtons.push_back (&b);
        }
        seqButtons[0]->setToggleState (true, juce::dontSendNotification);
        grid = &add (std::make_unique<PatternGrid> (s));
        help = &add (makeLabel ("Click a step to cycle: A (instrument A), A> (A accented), B (instrument B), off. "
                                "Drag to paint, right-click to clear."));

        layout = [this] (int w)
        {
            auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
            settings->setBounds (area.removeFromTop (settings->preferredHeight (area.getWidth())));
            area.removeFromTop (10);
            auto row = area.removeFromTop (26);
            for (auto* b : seqButtons)
            {
                b->setBounds (row.removeFromLeft (64));
                row.removeFromLeft (4);
            }
            area.removeFromTop (8);
            grid->setBounds (area.removeFromTop (PatternGrid::preferredHeight()));
            area.removeFromTop (6);
            help->setBounds (area.removeFromTop (20));
            return area.getY() + 8;
        };
    }

private:
    ParamGroup* settings = nullptr;
    std::vector<juce::TextButton*> seqButtons;
    PatternGrid* grid = nullptr;
    juce::Label* help = nullptr;
};

class FilterPage : public Page
{
public:
    explicit FilterPage (Session& s) : Page (s, T())
    {
        filters = addFilterSections (*this, linkButton);
        hardware = &addHardwareSection (*this);
        layout = [this] (int w)
        {
            auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
            linkButton->setBounds (area.removeFromTop (24).removeFromLeft (140));
            area.removeFromTop (8);
            area.setY (columns (filters, 2, area));
            return columns ({ hardware }, 2, area);
        };
    }

private:
    juce::TextButton* linkButton = nullptr;
    std::vector<ParamGroup*> filters;
    ParamGroup* hardware = nullptr;
};
} // namespace

DrumEditor::DrumEditor (Session& s)
{
    addPage ("Instruments", std::make_unique<InstrumentsPage> (s));
    addPage ("Patterns", std::make_unique<PatternPage> (s));
    addPage ("Filter & Global", std::make_unique<FilterPage> (s));
    addPage ("Knobs & Ext", makeKnobsPage (s, T(), sid::Engine::Drum));
}
} // namespace ui
