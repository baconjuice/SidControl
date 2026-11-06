#include "LeadEditor.h"
#include "LookAndFeel.h"

#include "sid/SysEx.h"

namespace ui
{
namespace
{
const sid::ParamTable& T() { return sid::leadParams(); }

juce::String signedText (int v) { return v > 0 ? "+" + juce::String (v) : juce::String (v); }

// ---------------------------------------------------------------------------
// Scrollable page: content laid out by a callback that returns its height.
// ---------------------------------------------------------------------------
class Page : public juce::Component
{
public:
    Page()
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, true);
        addAndMakeVisible (viewport);
    }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        layout (viewport.getMaximumVisibleWidth()); // measures minContentWidth
        const int w = juce::jmax (viewport.getMaximumVisibleWidth(), minContentWidth);
        content.setSize (w, juce::jmax (layout (w), viewport.getMaximumVisibleHeight()));
        layout (w);
    }

    template <typename C>
    C& add (std::unique_ptr<C> c)
    {
        auto& ref = *c;
        content.addAndMakeVisible (ref);
        owned.push_back (std::move (c));
        return ref;
    }

    ParamGroup& group (Session& s, const juce::String& title, WriteOptionFn wopt = {})
    {
        return add (std::make_unique<ParamGroup> (s, T(), title, std::move (wopt)));
    }

    // Lays groups out in equal-width columns, row by row. Returns the bottom y.
    static int columns (std::vector<ParamGroup*> groups, int cols, juce::Rectangle<int> area, int gap = 8)
    {
        const int colW = (area.getWidth() - (cols - 1) * gap) / cols;
        int y = area.getY();
        for (size_t i = 0; i < groups.size(); i += (size_t) cols)
        {
            int rowH = 0;
            for (int c = 0; c < cols && i + (size_t) c < groups.size(); ++c)
                rowH = juce::jmax (rowH, groups[i + (size_t) c]->preferredHeight (colW));
            for (int c = 0; c < cols && i + (size_t) c < groups.size(); ++c)
                groups[i + (size_t) c]->setBounds (area.getX() + c * (colW + gap), y, colW, rowH);
            y += rowH + gap;
        }
        return y;
    }

    std::function<int (int width)> layout = [] (int) { return 0; };
    int minContentWidth = 0;

protected:
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<juce::Component>> owned;
};

// A small labelled selector used for "link" modes and similar page options.
std::unique_ptr<juce::ComboBox> makeSelector (std::initializer_list<const char*> items, int initial = 1)
{
    auto c = std::make_unique<juce::ComboBox>();
    int id = 1;
    for (auto* i : items)
        c->addItem (i, id++);
    c->setSelectedId (initial, juce::dontSendNotification);
    return c;
}

std::unique_ptr<juce::Label> makeLabel (const juce::String& text, bool heading = false)
{
    auto l = std::make_unique<juce::Label> (juce::String(), text);
    l->setFont (font (heading ? 13.0f : 12.0f, heading));
    l->setColour (juce::Label::textColourId, heading ? colours::accent2 : colours::dimText);
    return l;
}

// ---------------------------------------------------------------------------
// Voices
// ---------------------------------------------------------------------------
class VoicesPage : public Page
{
public:
    explicit VoicesPage (Session& s)
    {
        auto& sideSel = add (makeSelector ({ "Left SID: OSC 1-3", "Right SID: OSC 4-6" }));
        auto& linkSel = add (makeSelector ({ "Edit one voice", "Link L/R (1+4, 2+5, 3+6)", "Link OSC 1-3 / 4-6", "Link all 6 voices" }));
        auto& linkLbl = add (makeLabel ("Edit mode"));
        linkSel.setTooltip ("Mirror edits to other voices, like the link function on the MIDIbox SID");
        side = &sideSel;
        link = &linkSel;
        linkLabel = &linkLbl;
        sideSel.onChange = [this] { resized(); };

        WriteOptionFn wopt = [this]
        {
            switch (link->getSelectedId())
            {
                case 2: return sid::sysex::wopt::LeftRight;
                case 3: return sid::sysex::wopt::Voices;
                case 4: return sid::sysex::wopt::All;
                default: return sid::sysex::wopt::None;
            }
        };

        for (int v = 1; v <= 6; ++v)
        {
            const std::string p = "v" + std::to_string (v);
            const juce::String osc = "OSC" + juce::String (v) + (v <= 3 ? " (L)" : " (R)");
            Column c;

            auto& g1 = group (s, osc + " Waveform", wopt);
            g1.toggle (p + ".tri", "Tri");
            g1.toggle (p + ".saw", "Saw");
            g1.toggle (p + ".pulse", "Pulse");
            g1.toggle (p + ".noise", "Noise");
            g1.gap (8);
            g1.toggle (p + ".sync", "Sync");
            g1.toggle (p + ".ring", "Ring");
            g1.toggle (p + ".off", "Off");
            g1.lineBreak();
            g1.knob (p + ".pw", "Pulsewidth", [] (int x) { return juce::String (x * 100.0 / 4095.0, 1) + "%"; });
            c.push_back (&g1);

            auto& g2 = group (s, "Envelope (SID ADSR)", wopt);
            g2.knob (p + ".attack");
            g2.knob (p + ".decay");
            g2.knob (p + ".sustain");
            g2.knob (p + ".release");
            g2.knob (p + ".delay", "Delay");
            c.push_back (&g2);

            auto& g3 = group (s, "Pitch", wopt);
            g3.knob (p + ".transpose", {}, signedText);
            g3.knob (p + ".finetune", {}, signedText);
            g3.knob (p + ".pitchrange", "Bend Rng");
            g3.knob (p + ".porta", "Porta");
            g3.lineBreak();
            g3.combo (p + ".portamode", "Portamento Mode", 120);
            g3.toggle (p + ".gae", "Gate Always");
            c.push_back (&g3);

            auto& g4 = group (s, "Arpeggiator", wopt);
            g4.toggle (p + ".arp.on", "Arp On");
            g4.combo (p + ".arp.dir", "Direction", 100);
            g4.lineBreak();
            g4.knob (p + ".arp.speed", "Speed");
            g4.knob (p + ".arp.gate", "Gate");
            g4.knob (p + ".arp.range", "Octaves");
            g4.lineBreak();
            g4.toggle (p + ".arp.sorted", "Sorted");
            g4.toggle (p + ".arp.hold", "Hold");
            g4.toggle (p + ".arp.keysync", "Key Sync");
            g4.toggle (p + ".arp.cac", "Const Cycle");
            g4.toggle (p + ".arp.easychord", "Easy Chord");
            g4.toggle (p + ".arp.oneshot", "Oneshot");
            c.push_back (&g4);

            auto& g5 = group (s, "SwinSID only", wopt);
            g5.toggle (p + ".sw.on", "2nd OSC");
            g5.toggle (p + ".sw.x2", "x2");
            g5.toggle (p + ".sw.rev", "Reverse");
            g5.combo (p + ".sw.wave", "2nd Wave", 60);
            g5.knob (p + ".swphase", "Phase");
            c.push_back (&g5);

            voices.push_back (c);
        }

        layout = [this] (int w)
        {
            auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
            auto top = area.removeFromTop (26);
            side->setBounds (top.removeFromLeft (180));
            top.removeFromLeft (20);
            linkLabel->setBounds (top.removeFromLeft (70));
            link->setBounds (top.removeFromLeft (220));
            area.removeFromTop (8);

            const int first = side->getSelectedId() == 2 ? 3 : 0;
            const int colW = (area.getWidth() - 16) / 3;
            int bottom = area.getY();
            for (int v = 0; v < 6; ++v)
            {
                const bool shown = v >= first && v < first + 3;
                int y = area.getY();
                for (auto* g : voices[(size_t) v])
                {
                    g->setVisible (shown);
                    if (! shown) continue;
                    const int h = g->preferredHeight (colW);
                    g->setBounds (area.getX() + (v - first) * (colW + 8), y, colW, h);
                    y += h + 8;
                }
                bottom = juce::jmax (bottom, y);
            }
            return bottom;
        };
    }

private:
    using Column = std::vector<ParamGroup*>;
    std::vector<Column> voices;
    juce::ComboBox* side = nullptr;
    juce::ComboBox* link = nullptr;
    juce::Label* linkLabel = nullptr;
};

// ---------------------------------------------------------------------------
// Filter & global
// ---------------------------------------------------------------------------
class FilterPage : public Page
{
public:
    explicit FilterPage (Session& s)
    {
        auto& linkBtn = add (std::make_unique<juce::TextButton> ("Link L/R filters"));
        linkBtn.setClickingTogglesState (true);
        linkBtn.setToggleState (true, juce::dontSendNotification);
        linkBtn.setTooltip ("Edits to one filter are copied to the other channel");
        linkButton = &linkBtn;
        WriteOptionFn wopt = [this] { return linkButton->getToggleState() ? sid::sysex::wopt::LeftRight : sid::sysex::wopt::None; };

        for (int f = 0; f < 2; ++f)
        {
            const std::string p = f == 0 ? "fL" : "fR";
            auto& g = group (s, f == 0 ? "Filter Left SID" : "Filter Right SID", wopt);
            g.toggle (p + ".lp", "LP");
            g.toggle (p + ".bp", "BP");
            g.toggle (p + ".hp", "HP");
            g.gap (12);
            g.toggle (p + ".o1", "OSC1");
            g.toggle (p + ".o2", "OSC2");
            g.toggle (p + ".o3", "OSC3");
            g.toggle (p + ".ext", "Ext In");
            g.gap (12);
            g.toggle (p + ".o3off", "OSC3 Off");
            g.lineBreak();
            g.knob (p + ".cutoff", "CutOff");
            g.knob (p + ".res", "Resonance");
            g.knob (p + ".keytrack", "Keytrack");
            g.toggle (p + ".fip", "Interpolate");
            filters.push_back (&g);
        }

        auto& gl = group (s, "Global");
        gl.knob ("lead.volume", "Volume");
        gl.knob ("lead.detune", "Detune");
        gl.knob ("lead.phase", "Phase Ofs");
        gl.combo ("lead.legato", "Play Mode", 90);
        gl.toggles ({ { "lead.wto", "WT Only" }, { "lead.suskey", "SusKey" } });
        global = &gl;

        auto& hw = group (s, "Hardware / Options");
        hw.combo ("hw.sid", "SID Type", 100);
        hw.combo ("hw.caps", "Filter Caps", 90);
        hw.toggles ({ { "hw.stereo", "Stereo" }, { "opt.abw", "ADSR Bug Workaround" } });
        hardware = &hw;

        layout = [this] (int w)
        {
            auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
            linkButton->setBounds (area.removeFromTop (24).removeFromLeft (140));
            area.removeFromTop (8);
            int y = columns (filters, 2, area);
            area.setY (y);
            return columns ({ global, hardware }, 2, area);
        };
    }

private:
    juce::TextButton* linkButton = nullptr;
    std::vector<ParamGroup*> filters;
    ParamGroup* global = nullptr;
    ParamGroup* hardware = nullptr;
};

// ---------------------------------------------------------------------------
// LFOs
// ---------------------------------------------------------------------------
class LfoPage : public Page
{
public:
    explicit LfoPage (Session& s)
    {
        for (int l = 1; l <= 6; ++l)
        {
            const std::string p = "lfo" + std::to_string (l);
            auto& g = group (s, "LFO " + juce::String (l));
            g.toggle (p + ".on", "On");
            g.toggle (p + ".clksync", "Clock Sync");
            g.toggle (p + ".oneshot", "Oneshot");
            g.combo (p + ".wave", "Waveform", 110);
            g.lineBreak();
            g.knob (p + ".depth", {}, signedText);
            const auto& syncDef = T().get (p + ".clksync");
            g.knob (p + ".rate", {}, [&s, &syncDef] (int v)
            {
                if (s.getParam (syncDef) != 0)
                    return juce::String (sid::lfoClockRates()[(size_t) juce::jlimit (0, 31, v / 8)]);
                return juce::String (v);
            });
            g.knob (p + ".delay");
            g.knob (p + ".phase");
            groups.push_back (&g);
        }
        layout = [this] (int w) { return columns (groups, 3, juce::Rectangle<int> (0, 0, w, 10000).reduced (8)); };
    }

private:
    std::vector<ParamGroup*> groups;
};

// ---------------------------------------------------------------------------
// Envelopes
// ---------------------------------------------------------------------------
class EnvelopeView : public juce::Component,
                     private Session::Listener
{
public:
    EnvelopeView (Session& s, std::string prefix) : session (s), p (std::move (prefix)) { session.addListener (this); }
    ~EnvelopeView() override { session.removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (colours::background);
        g.fillRoundedRectangle (r, 4.0f);
        r = r.reduced (8.0f);

        auto v = [this] (const char* id) { return (float) session.getParam (T().get (p + "." + id)); };

        struct Pt { float t, l; };
        std::vector<Pt> pts;
        float t = 0.0f;
        pts.push_back ({ t, 0.0f });
        t += v ("delay") / 255.0f;  pts.push_back ({ t, 0.0f });
        t += rateLen (v ("attack1")); pts.push_back ({ t, v ("attacklvl") / 255.0f });
        t += rateLen (v ("attack2")); pts.push_back ({ t, 1.0f });
        t += rateLen (v ("decay1")); pts.push_back ({ t, v ("decaylvl") / 255.0f });
        t += rateLen (v ("decay2")); pts.push_back ({ t, v ("sustain") / 255.0f });
        const float sustainEnd = t + 0.6f;
        t = sustainEnd; pts.push_back ({ t, v ("sustain") / 255.0f });
        t += rateLen (v ("release1")); pts.push_back ({ t, v ("releaselvl") / 255.0f });
        t += rateLen (v ("release2")); pts.push_back ({ t, 0.0f });

        const float total = juce::jmax (t, 0.01f);
        juce::Path path;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const float x = r.getX() + r.getWidth() * pts[i].t / total;
            const float y = r.getBottom() - r.getHeight() * pts[i].l;
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        g.setColour (colours::accent.withAlpha (0.18f));
        juce::Path fill (path);
        fill.lineTo (r.getRight(), r.getBottom());
        fill.closeSubPath();
        g.fillPath (fill);
        g.setColour (colours::accent);
        g.strokePath (path, juce::PathStrokeType (2.0f));

        const float sx = r.getX() + r.getWidth() * (sustainEnd - 0.6f) / total;
        g.setColour (colours::dimText);
        g.setFont (font (11.0f));
        g.drawText ("sustain", juce::Rectangle<float> (sx, r.getY(), r.getWidth() * 0.6f / total, 14.0f),
                    juce::Justification::centred);
    }

private:
    // MBSID V2 envelope rates: 0 = ~2 ms, 255 = ~30 s on a roughly exponential
    // curve, so the raw value is a fair visual length.
    static float rateLen (float rate) { return 0.03f + rate / 255.0f; }

    void editReplaced() override { repaint(); }
    void editChanged (const std::vector<uint16_t>&) override { repaint(); }

    Session& session;
    std::string p;
};

class EnvPage : public Page
{
public:
    explicit EnvPage (Session& s)
    {
        for (int e = 1; e <= 2; ++e)
        {
            const std::string p = "env" + std::to_string (e);
            auto& g = group (s, "ENV " + juce::String (e));
            g.knob (p + ".depth", {}, signedText);
            g.knob (p + ".delay");
            g.gap (10);
            g.knob (p + ".attack1", "Attack 1");
            g.knob (p + ".attacklvl", "Atk Level");
            g.knob (p + ".attack2", "Attack 2");
            g.knob (p + ".decay1", "Decay 1");
            g.knob (p + ".decaylvl", "Dec Level");
            g.knob (p + ".decay2", "Decay 2");
            g.knob (p + ".sustain");
            g.knob (p + ".release1", "Release 1");
            g.knob (p + ".releaselvl", "Rel Level");
            g.knob (p + ".release2", "Release 2");
            g.lineBreak();
            g.knob (p + ".acurve", "Atk Curve", signedText);
            g.knob (p + ".dcurve", "Dec Curve", signedText);
            g.knob (p + ".rcurve", "Rel Curve", signedText);
            g.gap (10);
            g.combo (p + ".loopbegin", "Loop Begin", 100);
            g.combo (p + ".loopend", "Loop End", 100);
            g.toggle (p + ".clksync", "Clock Sync");
            groups.push_back (&g);
            views.push_back (&add (std::make_unique<EnvelopeView> (s, p)));
        }

        layout = [this] (int w)
        {
            auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
            int y = area.getY();
            for (size_t i = 0; i < groups.size(); ++i)
            {
                const int viewW = 260;
                const int gw = area.getWidth() - viewW - 8;
                const int h = groups[i]->preferredHeight (gw);
                groups[i]->setBounds (area.getX(), y, gw, h);
                views[i]->setBounds (area.getRight() - viewW, y, viewW, h);
                y += h + 8;
            }
            return y;
        };
    }

private:
    std::vector<ParamGroup*> groups;
    std::vector<EnvelopeView*> views;
};

// ---------------------------------------------------------------------------
// Modulation matrix
// ---------------------------------------------------------------------------
class ModPage : public Page
{
public:
    explicit ModPage (Session& s)
    {
        static const char* targetShort[] = { "P1", "P2", "P3", "W1", "W2", "W3", "F", "V" };

        auto header = [this] (const juce::String& text, int col)
        {
            auto& l = add (makeLabel (text));
            l.setJustificationType (juce::Justification::centredLeft);
            headers.push_back ({ &l, col });
        };

        for (int m = 1; m <= 8; ++m)
        {
            const std::string p = "mp" + std::to_string (m);
            Row row;
            row.label = &add (makeLabel ("MP" + juce::String (m), true));
            auto combo = [&] (const std::string& id, int w)
            {
                auto& c = add (std::make_unique<ParamCombo> (s, T().get (id), juce::String(), w));
                c.setCompact (true);
                row.cells.push_back (&c);
            };
            auto toggle = [&] (const std::string& id, const juce::String& text)
            {
                auto& t = add (std::make_unique<ParamToggle> (s, T().get (id), text));
                row.cells.push_back (&t);
            };
            combo (p + ".src1", 88);
            combo (p + ".src2", 88);
            combo (p + ".op", 60);
            auto& k = add (std::make_unique<ParamKnob> (s, T().get (p + ".depth"), juce::String(), signedText));
            k.setCompact (true);
            row.cells.push_back (&k);
            toggle (p + ".inv1", "Inv1");
            toggle (p + ".inv2", "Inv2");
            for (int i = 0; i < 8; ++i) toggle (p + ".l" + std::to_string (i), targetShort[i]);
            for (int i = 0; i < 8; ++i) toggle (p + ".r" + std::to_string (i), targetShort[i]);
            combo (p + ".tgt1", 96);
            combo (p + ".tgt2", 96);
            rows.push_back (row);
        }

        header ("Source 1", 0);
        header ("Source 2", 1);
        header ("Operator", 2);
        header ("Depth", 3);
        header ("Invert", 4);
        header ("Direct targets, left SID", 6);
        header ("Direct targets, right SID", 14);
        header ("Target 1", 22);
        header ("Target 2", 23);

        layout = [this] (int w)
        {
            juce::ignoreUnused (w);
            const int x0 = 8 + 40, rowH = 54;
            int y = 8;
            // Column x positions from the first row's sizes.
            std::vector<int> colX;
            int x = x0;
            for (auto* c : rows.front().cells)
            {
                colX.push_back (x);
                auto* pc = dynamic_cast<ParamControl*> (c);
                const int cw = pc != nullptr ? pc->preferredSize().x : 40;
                const bool isTargetToggle = colX.size() > 6 && colX.size() <= 22;
                x += (isTargetToggle ? kTargetW : cw) + (colX.size() == 6 || colX.size() == 14 || colX.size() == 22 ? 12 : 3);
            }
            for (auto& [label, col] : headers)
                label->setBounds (colX[(size_t) col], y, col == 6 || col == 14 ? (kTargetW + 3) * 8 : 100, 18);
            y += 22;

            for (auto& row : rows)
            {
                row.label->setBounds (8, y + 12, 40, 24);
                for (size_t i = 0; i < row.cells.size(); ++i)
                {
                    auto* pc = dynamic_cast<ParamControl*> (row.cells[i]);
                    auto size = pc->preferredSize();
                    if (i >= 6 && i < 22) size.x = kTargetW;
                    row.cells[i]->setBounds (colX[i], y + (rowH - size.y) / 2, size.x, size.y);
                }
                y += rowH;
            }
            minContentWidth = x + 8;
            return y + 8;
        };
    }

private:
    static constexpr int kTargetW = 28;
    struct Row
    {
        juce::Label* label = nullptr;
        std::vector<juce::Component*> cells;
    };
    std::vector<Row> rows;
    std::vector<std::pair<juce::Label*, int>> headers;
};

// ---------------------------------------------------------------------------
// Trigger matrix
// ---------------------------------------------------------------------------
class TriggerGrid : public juce::Component,
                    private Session::Listener
{
public:
    explicit TriggerGrid (Session& s) : session (s) { session.addListener (this); }
    ~TriggerGrid() override { session.removeListener (this); }

    static constexpr int kLabelW = 110, kHeaderH = 52, kCell = 30;
    static int preferredWidth() { return kLabelW + 24 * kCell + 2; }
    static int preferredHeight() { return kHeaderH + 14 * kCell + 2; }

    void paint (juce::Graphics& g) override
    {
        static const char* rowNames[] = { "Note On", "Note Off", "ENV1 Sustain", "ENV2 Sustain", "LFO1 Period",
                                          "LFO2 Period", "LFO3 Period", "LFO4 Period", "LFO5 Period", "LFO6 Period",
                                          "Clock", "Clock / 6", "Clock / 24", "MIDI Start" };
        static const char* colNames[] = { "O1L", "O2L", "O3L", "O1R", "O2R", "O3R", "E1", "E2",
                                          "E1", "E2", "L1", "L2", "L3", "L4", "L5", "L6",
                                          "W1", "W2", "W3", "W4", "W1", "W2", "W3", "W4" };
        struct Span { int from, count; const char* name; };
        static const Span spans[] = { { 0, 6, "OSC Gate" }, { 6, 2, "ENV Atk" }, { 8, 2, "ENV Rel" },
                                      { 10, 6, "LFO Reset" }, { 16, 4, "WT Reset" }, { 20, 4, "WT Step" } };

        g.setFont (font (11.5f));
        for (auto& sp : spans)
        {
            auto r = juce::Rectangle<int> (kLabelW + sp.from * kCell, 0, sp.count * kCell, 22).reduced (2, 2);
            g.setColour (colours::panelEdge);
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
            g.setColour (colours::accent2);
            g.drawText (sp.name, r, juce::Justification::centred);
        }
        g.setColour (colours::dimText);
        for (int c = 0; c < 24; ++c)
            g.drawText (colNames[c], kLabelW + c * kCell, 24, kCell, 24, juce::Justification::centred);

        for (int r = 0; r < 14; ++r)
        {
            const int y = kHeaderH + r * kCell;
            g.setColour (colours::text);
            g.drawText (rowNames[r], 0, y, kLabelW - 6, kCell, juce::Justification::centredRight);
            for (int c = 0; c < 24; ++c)
            {
                auto cell = juce::Rectangle<int> (kLabelW + c * kCell, y, kCell, kCell).reduced (3);
                const bool on = session.getParam (def (r, c)) != 0;
                g.setColour (on ? colours::accent : colours::background);
                g.fillRoundedRectangle (cell.toFloat(), 3.0f);
                g.setColour (colours::panelEdge);
                g.drawRoundedRectangle (cell.toFloat(), 3.0f, 1.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int c = (e.x - kLabelW) / kCell, r = (e.y - kHeaderH) / kCell;
        if (e.x < kLabelW || e.y < kHeaderH || c >= 24 || r >= 14)
            return;
        const auto& d = def (r, c);
        dragValue = session.getParam (d) == 0 ? 1 : 0;
        session.setParam (d, dragValue);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int c = (e.x - kLabelW) / kCell, r = (e.y - kHeaderH) / kCell;
        if (e.x < kLabelW || e.y < kHeaderH || c < 0 || r < 0 || c >= 24 || r >= 14)
            return;
        session.setParam (def (r, c), dragValue);
    }

private:
    static const sid::ParamDef& def (int r, int c)
    {
        return T().get ("trg." + std::to_string (r) + "." + std::to_string (c));
    }
    void editReplaced() override { repaint(); }
    void editChanged (const std::vector<uint16_t>& a) override
    {
        for (auto x : a)
            if (x >= 0x140 && x < 0x16a) { repaint(); return; }
    }

    Session& session;
    int dragValue = 1;
};

class TriggerPage : public Page
{
public:
    explicit TriggerPage (Session& s)
    {
        grid = &add (std::make_unique<TriggerGrid> (s));
        info = &add (makeLabel ("Click or drag to connect a source event (rows) to an action (columns)."));
        layout = [this] (int)
        {
            info->setBounds (8, 8, 600, 20);
            grid->setBounds (8, 34, TriggerGrid::preferredWidth(), TriggerGrid::preferredHeight());
            return grid->getBottom() + 8;
        };
    }

private:
    TriggerGrid* grid = nullptr;
    juce::Label* info = nullptr;
};

// ---------------------------------------------------------------------------
// Wavetables
// ---------------------------------------------------------------------------
class WavetableView : public juce::Component,
                      private Session::Listener
{
public:
    explicit WavetableView (Session& s) : session (s) { session.addListener (this); }
    ~WavetableView() override { session.removeListener (this); }

    std::function<void (int step)> onSelect;
    int selected = 0;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (colours::background);
        g.fillRect (r);
        auto plot = plotArea();
        const float w = (float) plot.getWidth() / 128.0f;

        // Absolute values (0x80..0xff) and relative values (0x00..0x7f) in different colours.
        for (int i = 0; i < 128; ++i)
        {
            const int v = session.edit().byte (0x180 + i);
            const bool abs = v >= 0x80;
            const float x = (float) plot.getX() + (float) i * w;
            if (abs)
            {
                const float h = (float) plot.getHeight() * (float) (v - 0x80) / 127.0f;
                g.setColour (i == selected ? colours::text : colours::accent);
                g.fillRect (x + 0.5f, (float) plot.getBottom() - h, w - 1.0f, h);
            }
            else
            {
                const int rel = v - 0x40; // offset binary: 0x40 = no change
                const float mid = (float) plot.getCentreY();
                const float h = (float) plot.getHeight() * 0.5f * (float) rel / 64.0f;
                g.setColour (i == selected ? colours::text : colours::accent2);
                g.fillRect (x + 0.5f, juce::jmin (mid, mid - h), w - 1.0f, std::abs (h) + 1.0f);
            }
        }

        // WT1..4 start/end/loop markers.
        static const juce::Colour wtColours[] = { juce::Colour (0xff5fd38d), juce::Colour (0xffff6b6b),
                                                  juce::Colour (0xffc792ea), juce::Colour (0xff4fd6e0) };
        g.setFont (font (11.0f));
        for (int t = 0; t < 4; ++t)
        {
            const std::string p = "wt" + std::to_string (t + 1);
            const int start = session.getParam (T().get (p + ".start"));
            const int end = session.getParam (T().get (p + ".end"));
            const int loop = session.getParam (T().get (p + ".loop"));
            const int y = 2 + t * 9;
            const float x1 = (float) plot.getX() + (float) juce::jmin (start, end) * w;
            const float x2 = (float) plot.getX() + (float) (juce::jmax (start, end) + 1) * w;
            g.setColour (wtColours[t]);
            g.fillRect (x1, (float) y + 3.0f, x2 - x1, 2.0f);
            g.fillRect ((float) plot.getX() + (float) loop * w, (float) y, 2.0f, 8.0f);
            g.drawText ("WT" + juce::String (t + 1), r.getRight() - 34, y - 2, 32, 11, juce::Justification::centredRight);
        }

        g.setColour (colours::panelEdge);
        g.drawRect (plot);
        g.drawHorizontalLine (plot.getCentreY(), (float) plot.getX(), (float) plot.getRight());
    }

    void mouseDown (const juce::MouseEvent& e) override { edit (e, true); }
    void mouseDrag (const juce::MouseEvent& e) override { edit (e, false); }

private:
    juce::Rectangle<int> plotArea() const { return getLocalBounds().withTrimmedTop (40).withTrimmedRight (36).reduced (2); }

    void edit (const juce::MouseEvent& e, bool down)
    {
        auto plot = plotArea();
        const int step = juce::jlimit (0, 127, (int) ((float) (e.x - plot.getX()) * 128.0f / (float) plot.getWidth()));
        if (down || step != selected)
        {
            selected = step;
            if (onSelect) onSelect (step);
        }
        if (! e.mods.isShiftDown() && down)
            return; // a plain click selects; shift-click or drag draws

        // Drawing keeps the step's mode (absolute or relative).
        const int old = session.edit().byte (0x180 + step);
        const float norm = juce::jlimit (0.0f, 1.0f, 1.0f - (float) (e.y - plot.getY()) / (float) plot.getHeight());
        int v;
        if (old >= 0x80)
            v = 0x80 + juce::roundToInt (norm * 127.0f);
        else
        {
            const int rel = juce::jlimit (-64, 63, juce::roundToInt ((norm - 0.5f) * 128.0f));
            v = rel + 0x40;
        }
        session.setParam (T().get ("wtdata." + std::to_string (step)), v);
    }

    void editReplaced() override { repaint(); }
    void editChanged (const std::vector<uint16_t>& a) override
    {
        for (auto x : a)
            if (x >= 0x16c) { repaint(); return; }
    }

    Session& session;
};

// Editor for the selected wavetable step: mode + value.
class StepEditor : public juce::Component,
                   private Session::Listener
{
public:
    explicit StepEditor (Session& s) : session (s)
    {
        title.setFont (font (12.5f, true));
        title.setColour (juce::Label::textColourId, colours::accent2);
        addAndMakeVisible (title);
        mode.addItem ("Absolute (0..127)", 1);
        mode.addItem ("Relative (-64..+63)", 2);
        mode.onChange = [this] { apply(); };
        addAndMakeVisible (mode);
        value.setSliderStyle (juce::Slider::LinearHorizontal);
        value.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
        value.onValueChange = [this] { apply(); };
        addAndMakeVisible (value);
        hint.setText ("Click a step to select it. Drag or shift-click to draw.", juce::dontSendNotification);
        hint.setFont (font (11.5f));
        hint.setColour (juce::Label::textColourId, colours::dimText);
        addAndMakeVisible (hint);
        session.addListener (this);
        setStep (0);
    }
    ~StepEditor() override { session.removeListener (this); }

    void setStep (int s)
    {
        step = s;
        refresh();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        title.setBounds (r.removeFromLeft (90));
        mode.setBounds (r.removeFromLeft (160).reduced (0, 2));
        r.removeFromLeft (10);
        value.setBounds (r.removeFromLeft (260));
        r.removeFromLeft (10);
        hint.setBounds (r);
    }

private:
    void refresh()
    {
        const int v = session.edit().byte (0x180 + step);
        title.setText ("Step " + juce::String (step) + " (0x" + juce::String::toHexString (step).paddedLeft ('0', 2) + ")",
                       juce::dontSendNotification);
        const bool abs = v >= 0x80;
        mode.setSelectedId (abs ? 1 : 2, juce::dontSendNotification);
        if (abs) value.setRange (0, 127, 1); else value.setRange (-64, 63, 1);
        value.setValue (abs ? v - 0x80 : v - 0x40, juce::dontSendNotification);
    }

    void apply()
    {
        const bool abs = mode.getSelectedId() == 1;
        const int x = (int) value.getValue();
        const int v = abs ? 0x80 + juce::jlimit (0, 127, x) : 0x40 + juce::jlimit (-64, 63, x);
        session.setParam (T().get ("wtdata." + std::to_string (step)), v);
        refresh();
    }

    void editReplaced() override { refresh(); }
    void editChanged (const std::vector<uint16_t>& a) override
    {
        for (auto x : a)
            if (x == 0x180 + step) { refresh(); return; }
    }

    Session& session;
    int step = 0;
    juce::Label title, hint;
    juce::ComboBox mode;
    juce::Slider value;
};

class WavetablePage : public Page
{
public:
    explicit WavetablePage (Session& s)
    {
        const auto parChoices = parameterNumberChoices (sid::Engine::Lead);
        for (int w = 1; w <= 4; ++w)
        {
            const std::string p = "wt" + std::to_string (w);
            auto& g = group (s, "Wavetable Sequencer " + juce::String (w));
            g.combo (p + ".par", "Parameter", 210, parChoices);
            g.toggles ({ { p + ".left", "Left" }, { p + ".right", "Right" } });
            g.lineBreak();
            g.knob (p + ".div", "Clock Div");
            g.knob (p + ".start");
            g.knob (p + ".end");
            g.knob (p + ".loop");
            g.lineBreak();
            g.toggle (p + ".startmod", "Pos by Mod");
            g.toggle (p + ".endkey", "Pos by Key");
            g.toggle (p + ".oneshot", "Oneshot");
            groups.push_back (&g);
        }
        view = &add (std::make_unique<WavetableView> (s));
        stepEditor = &add (std::make_unique<StepEditor> (s));
        view->onSelect = [this] (int step) { stepEditor->setStep (step); view->repaint(); };

        layout = [this] (int w)
        {
            auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
            int y = columns (groups, 4, area);
            stepEditor->setBounds (area.getX(), y, area.getWidth(), 28);
            y += 34;
            view->setBounds (area.getX(), y, area.getWidth(), 260);
            return view->getBottom() + 8;
        };
    }

private:
    std::vector<ParamGroup*> groups;
    WavetableView* view = nullptr;
    StepEditor* stepEditor = nullptr;
};

// ---------------------------------------------------------------------------
// Knobs, external outputs
// ---------------------------------------------------------------------------
class KnobsPage : public Page
{
public:
    explicit KnobsPage (Session& s)
    {
        const auto parChoices = parameterNumberChoices (sid::Engine::Lead);
        static const char* ids[] = { "k1", "k2", "k3", "k4", "k5", "kV", "kP", "kA" };
        static const char* names[] = { "Knob 1 (ModWheel)", "Knob 2 (CC#17)", "Knob 3 (CC#18)", "Knob 4 (CC#19)",
                                       "Knob 5 (CC#20)", "Velocity", "Pitch Bender", "Aftertouch" };
        for (int k = 0; k < 8; ++k)
        {
            const std::string p = ids[k];
            auto& g = group (s, names[k]);
            g.combo (p + ".par1", "Assign 1", 200, parChoices);
            g.combo (p + ".par2", "Assign 2", 200, parChoices);
            g.lineBreak();
            g.knob (p + ".init", "Value");
            g.knob (p + ".min");
            g.knob (p + ".max");
            groups.push_back (&g);
        }

        auto& ext = group (s, "External AOUT values (12 bit)",
                           [] { return sid::sysex::wopt::None; });
        for (int i = 1; i <= 8; ++i)
            ext.knob ("ext" + std::to_string (i));
        extGroup = &ext;

        auto& sw = group (s, "External switches");
        for (int i = 1; i <= 8; ++i)
            sw.toggle ("extsw." + std::to_string (i));
        swGroup = &sw;

        layout = [this] (int w)
        {
            auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
            int y = columns (groups, 4, area);
            area.setY (y);
            return columns ({ extGroup, swGroup }, 2, area);
        };
    }

private:
    std::vector<ParamGroup*> groups;
    ParamGroup* extGroup = nullptr;
    ParamGroup* swGroup = nullptr;
};
} // namespace

// ---------------------------------------------------------------------------
LeadEditor::LeadEditor (Session& s)
{
    const auto bg = colours::background;
    tabs.setTabBarDepth (30);
    tabs.setOutline (0);
    tabs.addTab ("Voices", bg, new VoicesPage (s), true);
    tabs.addTab ("Filter & Global", bg, new FilterPage (s), true);
    tabs.addTab ("LFOs", bg, new LfoPage (s), true);
    tabs.addTab ("Envelopes", bg, new EnvPage (s), true);
    tabs.addTab ("Mod Matrix", bg, new ModPage (s), true);
    tabs.addTab ("Trigger Matrix", bg, new TriggerPage (s), true);
    tabs.addTab ("Wavetables", bg, new WavetablePage (s), true);
    tabs.addTab ("Knobs & Ext", bg, new KnobsPage (s), true);
    addAndMakeVisible (tabs);
}

LeadEditor::~LeadEditor() = default;

void LeadEditor::resized()
{
    tabs.setBounds (getLocalBounds());
}
} // namespace ui
