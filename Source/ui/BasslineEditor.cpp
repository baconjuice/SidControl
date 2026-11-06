#include "BasslineEditor.h"
#include "LookAndFeel.h"

#include "sid/SysEx.h"

namespace ui
{
namespace
{
const sid::ParamTable& T() { return sid::basslineParams(); }

juce::String staticNoteText (int v) { return v == 0 ? juce::String ("Off") : noteText (v); }

// ---------------------------------------------------------------------------
// One bassline (left or right SID)
// ---------------------------------------------------------------------------
class BasslinePage : public Page
{
public:
    BasslinePage (Session& s, int side) : Page (s, T())
    {
        const std::string p = side == 0 ? "bL" : "bR";
        auto& linkBtn = add (std::make_unique<juce::TextButton> ("Edit both basslines"));
        linkBtn.setClickingTogglesState (true);
        linkBtn.setTooltip ("Copy every edit to the other bassline as well");
        linkButton = &linkBtn;
        auto* lb = &linkBtn;
        WriteOptionFn wopt = [lb] { return lb->getToggleState() ? sid::sysex::wopt::LeftRight : sid::sysex::wopt::None; };

        // Column 1: OSC1 (the bassline voice)
        col1 = addVoiceSections (*this, p, "OSC1", wopt, false, false);
        auto& acc = group ("Accent", wopt);
        acc.knob (p + ".accent", "Accent");
        acc.knob (p + ".accentdecay", "Acc. Decay");
        col1.push_back (&acc);

        // Column 2: OSC2/3 and play mode
        for (int o = 2; o <= 3; ++o)
        {
            const std::string q = p + ".o" + std::to_string (o);
            auto& g = group ("OSC" + juce::String (o), wopt);
            g.toggle (q + ".tri", "Tri");
            g.toggle (q + ".saw", "Saw");
            g.toggle (q + ".pulse", "Pulse");
            g.toggle (q + ".noise", "Noise");
            g.gap (8);
            g.toggle (q + ".sync", "Sync");
            g.toggle (q + ".ring", "Ring");
            g.toggle (q + ".off", "Off");
            g.lineBreak();
            g.knob (q + ".pw", "Pulsewidth", [] (int x) { return juce::String (x * 100.0 / 4095.0, 1) + "%"; });
            g.combo (q + ".oct", "Octave", 70);
            g.knob (q + ".note", "Static Note", staticNoteText);
            col2.push_back (&g);
        }
        auto& play = group ("Play Mode", wopt);
        play.combo (p + ".legato", "Mode", 90);
        play.toggles ({ { p + ".seqmode", "Sequencer" }, { p + ".suskey", "SusKey" } });
        play.toggle (p + ".oscsync", "OSC Phase Sync");
        col2.push_back (&play);

        auto& seq = group ("Sequencer", wopt);
        seq.combo (p + ".seq.num", "Sequence", 80);
        seq.knob (p + ".seq.steps", "Steps");
        seq.knob (p + ".seq.div", "Clock Div");
        seq.lineBreak();
        seq.combo (p + ".seq.par", "Parameter Track", 200, parameterNumberChoices (sid::Engine::Bassline));
        seq.toggle (p + ".seq.measure", "Sync to Measure");
        col2.push_back (&seq);

        // Column 3: modulation
        col3.push_back (&addLfo7Section (*this, p + ".lfo1", "LFO 1", wopt));
        col3.push_back (&addLfo7Section (*this, p + ".lfo2", "LFO 2", wopt));
        col3.push_back (&addEnv9Section (*this, p + ".env", "Envelope", wopt));

        layout = [this] (int w)
        {
            auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
            linkButton->setBounds (area.removeFromTop (24).removeFromLeft (160));
            area.removeFromTop (8);
            const int colW = (area.getWidth() - 16) / 3;
            int bottom = 0;
            int x = area.getX();
            for (auto* col : { &col1, &col2, &col3 })
            {
                int y = area.getY();
                for (auto* g : *col)
                {
                    const int h = g->preferredHeight (colW);
                    g->setBounds (x, y, colW, h);
                    y += h + 8;
                }
                bottom = juce::jmax (bottom, y);
                x += colW + 8;
            }
            return bottom;
        };
    }

private:
    juce::TextButton* linkButton = nullptr;
    std::vector<ParamGroup*> col1, col2, col3;
};

// ---------------------------------------------------------------------------
// Step sequencer: 8 sequences x 16 steps
//   low byte  (0x100 + seq*16 + step): [3:0] note, [5:4] octave, [6] glide, [7] gate
//   high byte (0x180 + seq*16 + step): [6:0] parameter value, [7] accent
// ---------------------------------------------------------------------------
class SequenceGrid : public juce::Component,
                     private ParamTarget::Listener
{
public:
    explicit SequenceGrid (ParamTarget& t) : target (t) { target.addTargetListener (this); }
    ~SequenceGrid() override { target.removeTargetListener (this); }

    int sequence = 0;
    static constexpr int kLabelW = 70, kRowH = 30, kParamH = 90;
    enum Row { Gate, Accent, Glide, Note, Octave, Param, NumRows };

    static int preferredHeight() { return 22 + (NumRows - 1) * kRowH + kParamH + 2; }

    void paint (juce::Graphics& g) override
    {
        static const char* rowNames[] = { "Gate", "Accent", "Glide", "Note", "Octave", "Parameter" };
        const float cw = cellW();
        g.setFont (font (11.5f));
        for (int s = 0; s < 16; ++s)
        {
            g.setColour (s % 4 == 0 ? colours::text : colours::dimText);
            g.drawText (juce::String (s + 1), kLabelW + (int) ((float) s * cw), 0, (int) cw, 20, juce::Justification::centred);
        }
        for (int r = 0; r < NumRows; ++r)
        {
            auto row = rowBounds (r);
            g.setColour (colours::text);
            g.drawText (rowNames[r], 0, row.getY(), kLabelW - 8, kRowH, juce::Justification::centredRight);
            for (int s = 0; s < 16; ++s)
                paintCell (g, r, s, cellBounds (r, s));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        hit = cellHit (e.position);
        if (hit.row < 0) return;
        dragStartY = e.position.y;
        const int lo = low (hit.step), hi = high (hit.step);
        switch (hit.row)
        {
            case Gate:   paintValue = (lo & 0x80) ? 0 : 1; setLow (hit.step, paintValue ? lo | 0x80 : lo & ~0x80); break;
            case Glide:  paintValue = (lo & 0x40) ? 0 : 1; setLow (hit.step, paintValue ? lo | 0x40 : lo & ~0x40); break;
            case Accent: paintValue = (hi & 0x80) ? 0 : 1; setHigh (hit.step, paintValue ? hi | 0x80 : hi & ~0x80); break;
            case Octave:
            {
                // cycle 0 -> +1 -> +2 -> -1 -> 0 (raw: 0, 1=Up, 3=Up2, 2=Down)
                static const int next[] = { 1, 3, 0, 2 };
                setLow (hit.step, (lo & ~0x30) | (next[(lo >> 4) & 3] << 4));
                break;
            }
            case Note:   dragStartValue = lo & 0x0f; break;
            case Param:  setParamFromY (hit.step, e.position.y); break;
            default: break;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (hit.row < 0) return;
        if (hit.row == Note)
        {
            const int v = juce::jlimit (0, 11, dragStartValue + (int) ((dragStartY - e.position.y) / 8.0f));
            setLow (hit.step, (low (hit.step) & 0xf0) | v);
            return;
        }
        auto h = cellHit (e.position);
        if (h.step < 0) return;
        if (hit.row == Param)
        {
            setParamFromY (h.step, e.position.y);
            return;
        }
        const int lo = low (h.step), hi = high (h.step);
        if (hit.row == Gate)   setLow (h.step, paintValue ? lo | 0x80 : lo & ~0x80);
        if (hit.row == Glide)  setLow (h.step, paintValue ? lo | 0x40 : lo & ~0x40);
        if (hit.row == Accent) setHigh (h.step, paintValue ? hi | 0x80 : hi & ~0x80);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        auto h = cellHit (e.position);
        if (h.row != Note || w.deltaY == 0.0f) return;
        const int lo = low (h.step);
        setLow (h.step, (lo & 0xf0) | juce::jlimit (0, 11, (lo & 0x0f) + (w.deltaY > 0 ? 1 : -1)));
    }

private:
    struct Hit { int row = -1, step = -1; };

    float cellW() const { return (float) (getWidth() - kLabelW) / 16.0f; }
    juce::Rectangle<int> rowBounds (int r) const
    {
        const int y = 22 + r * kRowH;
        return { 0, y, getWidth(), r == Param ? kParamH : kRowH };
    }
    juce::Rectangle<int> cellBounds (int r, int s) const
    {
        auto row = rowBounds (r);
        const float cw = cellW();
        return juce::Rectangle<int> (kLabelW + (int) ((float) s * cw), row.getY(), (int) cw, row.getHeight()).reduced (2);
    }
    Hit cellHit (juce::Point<float> p) const
    {
        Hit h;
        if (p.x < (float) kLabelW) return h;
        h.step = juce::jlimit (0, 15, (int) ((p.x - (float) kLabelW) / cellW()));
        for (int r = 0; r < NumRows; ++r)
            if (rowBounds (r).contains (p.toInt())) h.row = r;
        return h;
    }

    int lowAddr (int s) const  { return 0x100 + sequence * 16 + s; }
    int highAddr (int s) const { return 0x180 + sequence * 16 + s; }
    int low (int s) const  { return target.getByte (lowAddr (s)); }
    int high (int s) const { return target.getByte (highAddr (s)); }
    void setLow (int s, int v)  { target.setByteValue ((uint16_t) lowAddr (s), (uint8_t) v); }
    void setHigh (int s, int v) { target.setByteValue ((uint16_t) highAddr (s), (uint8_t) v); }

    void setParamFromY (int s, float y)
    {
        auto c = cellBounds (Param, s);
        const int v = juce::jlimit (0, 127, juce::roundToInt (((float) c.getBottom() - y) * 127.0f / (float) c.getHeight()));
        setHigh (s, (high (s) & 0x80) | v);
    }

    void paintCell (juce::Graphics& g, int r, int s, juce::Rectangle<int> c)
    {
        const int lo = low (s), hi = high (s);
        const bool gate = (lo & 0x80) != 0;
        g.setColour (s % 4 == 0 ? colours::panelEdge.brighter (0.1f) : colours::panel);
        g.fillRoundedRectangle (c.toFloat(), 3.0f);

        auto onCell = [&] (bool on, juce::Colour col)
        {
            if (on) { g.setColour (col); g.fillRoundedRectangle (c.toFloat().reduced (2.0f), 3.0f); }
        };
        switch (r)
        {
            case Gate:   onCell (gate, colours::accent); break;
            case Accent: onCell ((hi & 0x80) != 0, colours::accent2); break;
            case Glide:  onCell ((lo & 0x40) != 0, colours::good); break;
            case Note:
            {
                static const int octOffset[] = { 0, 12, -12, 24 };
                const int note = 0x3c + (lo & 0x0f) + octOffset[(lo >> 4) & 3];
                g.setColour (gate ? colours::text : colours::dimText);
                g.setFont (font (12.0f, gate));
                g.drawText (noteText (note), c, juce::Justification::centred);
                break;
            }
            case Octave:
            {
                static const char* names[] = { "", "+1", "-1", "+2" };
                g.setColour (colours::dimText);
                g.setFont (font (12.0f));
                g.drawText (names[(lo >> 4) & 3], c, juce::Justification::centred);
                break;
            }
            case Param:
            {
                const float h = (float) c.getHeight() * (float) (hi & 0x7f) / 127.0f;
                g.setColour (colours::accent.withAlpha (gate ? 0.9f : 0.4f));
                g.fillRect (c.toFloat().withTop ((float) c.getBottom() - h).reduced (3.0f, 0.0f));
                break;
            }
            default: break;
        }
    }

    void targetReplaced() override { repaint(); }
    void targetChanged (const std::vector<uint16_t>& a) override
    {
        for (auto x : a)
            if (x >= 0x100) { repaint(); return; }
    }

    ParamTarget& target;
    Hit hit;
    int paintValue = 1, dragStartValue = 0;
    float dragStartY = 0.0f;
};

class SequencesPage : public Page
{
public:
    explicit SequencesPage (Session& s) : Page (s, T())
    {
        for (int i = 0; i < 8; ++i)
        {
            auto& b = add (std::make_unique<juce::TextButton> ("Seq " + juce::String (i + 1)));
            b.setRadioGroupId (1001);
            b.setClickingTogglesState (true);
            b.onClick = [this, i] { grid->sequence = i; grid->repaint(); };
            seqButtons.push_back (&b);
        }
        seqButtons[0]->setToggleState (true, juce::dontSendNotification);
        grid = &add (std::make_unique<SequenceGrid> (s));
        help = &add (makeLabel ("Click to toggle gate, accent and glide (drag to paint). Drag a note up/down or use the "
                                "mouse wheel; click the octave to cycle it. Drag the bars to set the parameter track."));

        auto& g = group ("Sequencer per bassline");
        g.combo ("bL.seq.num", "Left plays", 80);
        g.knob ("bL.seq.steps", "L Steps");
        g.knob ("bL.seq.div", "L Clock Div");
        g.toggle ("bL.seqmode", "L Seq On");
        g.gap (20);
        g.combo ("bR.seq.num", "Right plays", 80);
        g.knob ("bR.seq.steps", "R Steps");
        g.knob ("bR.seq.div", "R Clock Div");
        g.toggle ("bR.seqmode", "R Seq On");
        settings = &g;

        layout = [this] (int w)
        {
            auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
            const int h = settings->preferredHeight (area.getWidth());
            settings->setBounds (area.removeFromTop (h));
            area.removeFromTop (10);
            auto row = area.removeFromTop (26);
            for (auto* b : seqButtons)
            {
                b->setBounds (row.removeFromLeft (64));
                row.removeFromLeft (4);
            }
            area.removeFromTop (8);
            grid->setBounds (area.removeFromTop (SequenceGrid::preferredHeight()));
            area.removeFromTop (6);
            help->setBounds (area.removeFromTop (20));
            return area.getY() + 8;
        };
    }

private:
    std::vector<juce::TextButton*> seqButtons;
    SequenceGrid* grid = nullptr;
    juce::Label* help = nullptr;
    ParamGroup* settings = nullptr;
};

// ---------------------------------------------------------------------------
class FilterPage : public Page
{
public:
    explicit FilterPage (Session& s) : Page (s, T())
    {
        filters = addFilterSections (*this, linkButton);
        auto& gl = group ("Global");
        gl.knob ("eng.volume", "Volume");
        gl.knob ("eng.detune", "Detune");
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

BasslineEditor::BasslineEditor (Session& s)
{
    addPage ("Bassline L", std::make_unique<BasslinePage> (s, 0));
    addPage ("Bassline R", std::make_unique<BasslinePage> (s, 1));
    addPage ("Sequences", std::make_unique<SequencesPage> (s));
    addPage ("Filter & Global", std::make_unique<FilterPage> (s));
    addPage ("Knobs & Ext", makeKnobsPage (s, T(), sid::Engine::Bassline));
}
} // namespace ui
