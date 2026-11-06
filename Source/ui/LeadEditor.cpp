#include "LeadEditor.h"
#include "EditorCommon.h"
#include "LookAndFeel.h"

#include "sid/SysEx.h"

namespace ui
{
namespace
{
const sid::ParamTable& T() { return sid::leadParams(); }

// ---------------------------------------------------------------------------
// Voices: three voices of one SID side by side
// ---------------------------------------------------------------------------
class VoicesPage : public Page
{
public:
    explicit VoicesPage (Session& s) : Page (s, T())
    {
        side = &add (makeSelector ({ "Left SID: OSC 1-3", "Right SID: OSC 4-6" }));
        link = &add (makeSelector ({ "Edit one voice", "Link L/R (1+4, 2+5, 3+6)", "Link OSC 1-3 / 4-6", "Link all 6 voices" }));
        linkLabel = &add (makeLabel ("Edit mode"));
        link->setTooltip ("Mirror edits to other voices, like the link function on the MIDIbox SID");
        side->onChange = [this] { resized(); };

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

        for (int v = 1; v <= 6; ++v)
            voices.push_back (addVoiceSections (*this, "v" + std::to_string (v),
                                                "OSC" + juce::String (v) + (v <= 3 ? " (L)" : " (R)"), wopt, true, true));

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
    std::vector<std::vector<ParamGroup*>> voices;
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
    explicit FilterPage (Session& s) : Page (s, T())
    {
        filters = addFilterSections (*this, linkButton);

        auto& gl = group ("Global");
        gl.knob ("lead.volume", "Volume");
        gl.knob ("lead.detune", "Detune");
        gl.knob ("lead.phase", "Phase Ofs");
        gl.combo ("lead.legato", "Play Mode", 90);
        gl.toggles ({ { "lead.wto", "WT Only" }, { "lead.suskey", "SusKey" } });
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

// ---------------------------------------------------------------------------
// LFOs
// ---------------------------------------------------------------------------
class LfoPage : public Page
{
public:
    explicit LfoPage (Session& s) : Page (s, T())
    {
        for (int l = 1; l <= 6; ++l)
        {
            const std::string p = "lfo" + std::to_string (l);
            auto& g = group ("LFO " + juce::String (l));
            g.toggle (p + ".on", "On");
            g.toggle (p + ".clksync", "Clock Sync");
            g.toggle (p + ".oneshot", "Oneshot");
            g.combo (p + ".wave", "Waveform", 110);
            g.lineBreak();
            g.knob (p + ".depth", {}, signedText);
            g.knob (p + ".rate", {}, lfoRateFormat (s, T().get (p + ".clksync")));
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
                     private ParamTarget::Listener
{
public:
    EnvelopeView (ParamTarget& s, std::string prefix) : session (s), p (std::move (prefix)) { session.addTargetListener (this); }
    ~EnvelopeView() override { session.removeTargetListener (this); }

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

    void targetReplaced() override { repaint(); }
    void targetChanged (const std::vector<uint16_t>&) override { repaint(); }

    ParamTarget& session;
    std::string p;
};

class EnvPage : public Page
{
public:
    explicit EnvPage (Session& s) : Page (s, T())
    {
        for (int e = 1; e <= 2; ++e)
        {
            const std::string p = "env" + std::to_string (e);
            auto& g = group ("ENV " + juce::String (e));
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
    explicit ModPage (Session& s) : Page (s, T())
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
                    private ParamTarget::Listener
{
public:
    explicit TriggerGrid (ParamTarget& s) : session (s) { session.addTargetListener (this); }
    ~TriggerGrid() override { session.removeTargetListener (this); }

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
    void targetReplaced() override { repaint(); }
    void targetChanged (const std::vector<uint16_t>& a) override
    {
        for (auto x : a)
            if (x >= 0x140 && x < 0x16a) { repaint(); return; }
    }

    ParamTarget& session;
    int dragValue = 1;
};

class TriggerPage : public Page
{
public:
    explicit TriggerPage (Session& s) : Page (s, T())
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

class WavetablePage : public Page
{
public:
    explicit WavetablePage (Session& s) : Page (s, T())
    {
        const auto parChoices = parameterNumberChoices (sid::Engine::Lead);
        for (int w = 1; w <= 4; ++w)
        {
            const std::string p = "wt" + std::to_string (w);
            auto& g = group ("Wavetable Sequencer " + juce::String (w));
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
        view = &add (std::make_unique<WavetableView> (s, T(), std::vector<WavetableView::Track> {
                         { "wt1", "WT1" }, { "wt2", "WT2" }, { "wt3", "WT3" }, { "wt4", "WT4" } }));
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

} // namespace

// ---------------------------------------------------------------------------
LeadEditor::LeadEditor (Session& s)
{
    addPage ("Voices", std::make_unique<VoicesPage> (s));
    addPage ("Filter & Global", std::make_unique<FilterPage> (s));
    addPage ("LFOs", std::make_unique<LfoPage> (s));
    addPage ("Envelopes", std::make_unique<EnvPage> (s));
    addPage ("Mod Matrix", std::make_unique<ModPage> (s));
    addPage ("Trigger Matrix", std::make_unique<TriggerPage> (s));
    addPage ("Wavetables", std::make_unique<WavetablePage> (s));
    addPage ("Knobs & Ext", makeKnobsPage (s, T(), sid::Engine::Lead));
}
} // namespace ui
