#include "EditorCommon.h"
#include "LookAndFeel.h"

#include "sid/SysEx.h"

namespace ui
{
juce::String signedText (int v) { return v > 0 ? "+" + juce::String (v) : juce::String (v); }
juce::String noteText (int midiNote) { return juce::String (sid::noteName (midiNote)); }

std::unique_ptr<juce::ComboBox> makeSelector (std::initializer_list<const char*> items, int initial)
{
    auto c = std::make_unique<juce::ComboBox>();
    int id = 1;
    for (auto* i : items)
        c->addItem (i, id++);
    c->setSelectedId (initial, juce::dontSendNotification);
    return c;
}

std::unique_ptr<juce::Label> makeLabel (const juce::String& text, bool heading)
{
    auto l = std::make_unique<juce::Label> (juce::String(), text);
    l->setFont (font (heading ? 13.0f : 12.0f, heading));
    l->setColour (juce::Label::textColourId, heading ? colours::accent2 : colours::dimText);
    return l;
}

// ---------------------------------------------------------------------------
// Page
// ---------------------------------------------------------------------------
Page::Page (ParamTarget& t, const sid::ParamTable& tbl) : target (t), table (tbl)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);
}

void Page::resized()
{
    viewport.setBounds (getLocalBounds());
    layout (viewport.getMaximumVisibleWidth()); // measures minContentWidth
    viewport.setScrollBarsShown (true, minContentWidth > viewport.getMaximumVisibleWidth());
    const int w = juce::jmax (viewport.getMaximumVisibleWidth(), minContentWidth);
    content.setSize (w, juce::jmax (layout (w), viewport.getMaximumVisibleHeight()));
    layout (w);
}

ParamGroup& Page::group (const juce::String& title, WriteOptionFn wopt)
{
    return add (std::make_unique<ParamGroup> (target, table, title, std::move (wopt)));
}

int Page::columns (const std::vector<ParamGroup*>& groups, int cols, juce::Rectangle<int> area, int gap)
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

// ---------------------------------------------------------------------------
// Sections
// ---------------------------------------------------------------------------
std::vector<ParamGroup*> addVoiceSections (Page& page, const std::string& p, const juce::String& title, WriteOptionFn wopt,
                                          bool withArp, bool withSwinSid)
{
    std::vector<ParamGroup*> out;

    auto& g1 = page.group (title + " Waveform", wopt);
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
    out.push_back (&g1);

    auto& g2 = page.group ("Envelope (SID ADSR)", wopt);
    g2.knob (p + ".attack");
    g2.knob (p + ".decay");
    g2.knob (p + ".sustain");
    g2.knob (p + ".release");
    g2.knob (p + ".delay", "Delay");
    out.push_back (&g2);

    auto& g3 = page.group ("Pitch", wopt);
    g3.knob (p + ".transpose", {}, signedText);
    g3.knob (p + ".finetune", {}, signedText);
    g3.knob (p + ".pitchrange", "Bend Rng");
    g3.knob (p + ".porta", "Porta");
    g3.lineBreak();
    g3.combo (p + ".portamode", "Portamento Mode", 120);
    g3.toggle (p + ".gae", "Gate Always");
    out.push_back (&g3);

    if (withArp)
    {
        auto& g4 = page.group ("Arpeggiator", wopt);
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
        out.push_back (&g4);
    }

    if (withSwinSid)
    {
        auto& g5 = page.group ("SwinSID only", wopt);
        g5.toggle (p + ".sw.on", "2nd OSC");
        g5.toggle (p + ".sw.x2", "x2");
        g5.toggle (p + ".sw.rev", "Reverse");
        g5.combo (p + ".sw.wave", "2nd Wave", 60);
        g5.knob (p + ".swphase", "Phase");
        out.push_back (&g5);
    }
    return out;
}

FormatFn lfoRateFormat (ParamTarget& t, const sid::ParamDef& clockSyncFlag)
{
    return [&t, &clockSyncFlag] (int v)
    {
        if (t.getParam (clockSyncFlag) != 0)
            return juce::String (sid::lfoClockRates()[(size_t) juce::jlimit (0, 31, v / 8)]);
        return juce::String (v);
    };
}

ParamGroup& addLfo7Section (Page& page, const std::string& p, const juce::String& title, WriteOptionFn wopt)
{
    auto& g = page.group (title, std::move (wopt));
    g.toggle (p + ".on", "On");
    g.toggle (p + ".notesync", "Note Sync");
    g.toggle (p + ".clksync", "Clock Sync");
    g.toggle (p + ".oneshot", "Oneshot");
    g.combo (p + ".wave", "Waveform", 100);
    g.lineBreak();
    g.knob (p + ".rate", {}, lfoRateFormat (page.target, page.table.get (p + ".clksync")));
    g.knob (p + ".delay");
    g.knob (p + ".phase");
    g.gap (6);
    g.knob (p + ".depthp", "> Pitch", signedText);
    g.knob (p + ".depthpw", "> PW", signedText);
    g.knob (p + ".depthf", "> CutOff", signedText);
    return g;
}

ParamGroup& addEnv9Section (Page& page, const std::string& p, const juce::String& title, WriteOptionFn wopt)
{
    auto& g = page.group (title, std::move (wopt));
    g.knob (p + ".attack");
    g.knob (p + ".decay");
    g.knob (p + ".sustain");
    g.knob (p + ".release");
    g.knob (p + ".curve", {}, signedText);
    g.lineBreak();
    g.knob (p + ".depthp", "> Pitch", signedText);
    g.knob (p + ".depthpw", "> PW", signedText);
    g.knob (p + ".depthf", "> CutOff", signedText);
    g.toggles ({ { p + ".curveatk", "Curve Atk" }, { p + ".curvedec", "Curve Dec" } });
    g.toggles ({ { p + ".curverel", "Curve Rel" }, { p + ".clksync", "Clock Sync" } });
    return g;
}

std::vector<ParamGroup*> addFilterSections (Page& page, juce::TextButton*& linkButton)
{
    auto& linkBtn = page.add (std::make_unique<juce::TextButton> ("Link L/R filters"));
    linkBtn.setClickingTogglesState (true);
    linkBtn.setToggleState (true, juce::dontSendNotification);
    linkBtn.setTooltip ("Edits to one filter are copied to the other channel");
    linkButton = &linkBtn;
    auto* btn = &linkBtn;
    WriteOptionFn wopt = [btn] { return btn->getToggleState() ? sid::sysex::wopt::LeftRight : sid::sysex::wopt::None; };

    std::vector<ParamGroup*> out;
    for (int f = 0; f < 2; ++f)
    {
        const std::string p = f == 0 ? "fL" : "fR";
        auto& g = page.group (f == 0 ? "Filter Left SID" : "Filter Right SID", wopt);
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
        out.push_back (&g);
    }
    return out;
}

ParamGroup& addHardwareSection (Page& page)
{
    auto& hw = page.group ("Hardware / Options");
    hw.combo ("hw.sid", "SID Type", 100);
    hw.combo ("hw.caps", "Filter Caps", 90);
    hw.toggles ({ { "hw.stereo", "Stereo" }, { "opt.abw", "ADSR Bug Workaround" } });
    return hw;
}

std::unique_ptr<Page> makeKnobsPage (ParamTarget& t, const sid::ParamTable& table, sid::Engine engine)
{
    auto page = std::make_unique<Page> (t, table);
    auto* pg = page.get();
    const auto parChoices = parameterNumberChoices (engine);
    static const char* ids[] = { "k1", "k2", "k3", "k4", "k5", "kV", "kP", "kA" };
    static const char* names[] = { "Knob 1 (ModWheel)", "Knob 2 (CC#17)", "Knob 3 (CC#18)", "Knob 4 (CC#19)",
                                   "Knob 5 (CC#20)", "Velocity", "Pitch Bender", "Aftertouch" };
    auto groups = std::make_shared<std::vector<ParamGroup*>>();
    for (int k = 0; k < 8; ++k)
    {
        const std::string p = ids[k];
        auto& g = pg->group (names[k]);
        g.combo (p + ".par1", "Assign 1", 200, parChoices);
        g.combo (p + ".par2", "Assign 2", 200, parChoices);
        g.lineBreak();
        g.knob (p + ".init", "Value");
        g.knob (p + ".min");
        g.knob (p + ".max");
        groups->push_back (&g);
    }
    auto& ext = pg->group ("External AOUT values (12 bit)");
    for (int i = 1; i <= 8; ++i)
        ext.knob ("ext" + std::to_string (i));
    auto& sw = pg->group ("External switches");
    for (int i = 1; i <= 8; ++i)
        sw.toggle ("extsw." + std::to_string (i));

    auto* extP = &ext;
    auto* swP = &sw;
    pg->layout = [groups, extP, swP] (int w)
    {
        auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
        int y = Page::columns (*groups, 4, area);
        area.setY (y);
        return Page::columns ({ extP, swP }, 2, area);
    };
    return page;
}

// ---------------------------------------------------------------------------
// WavetableView
// ---------------------------------------------------------------------------
WavetableView::WavetableView (ParamTarget& t, const sid::ParamTable& tbl, std::vector<Track> trk)
    : target (t), table (tbl), tracks (std::move (trk))
{
    target.addTargetListener (this);
}

WavetableView::~WavetableView()
{
    target.removeTargetListener (this);
}

juce::Rectangle<int> WavetableView::plotArea() const
{
    return getLocalBounds().withTrimmedTop (4 + 9 * (int) tracks.size()).withTrimmedRight (36).reduced (2);
}

void WavetableView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (colours::background);
    g.fillRect (r);
    auto plot = plotArea();
    const float w = (float) plot.getWidth() / 128.0f;

    // Values 0x80..0xff are absolute (0..127), 0x00..0x7f relative with
    // 0x40 = no change, drawn around the centre line.
    for (int i = 0; i < 128; ++i)
    {
        const int v = target.getByte (0x180 + i);
        const float x = (float) plot.getX() + (float) i * w;
        if (v >= 0x80)
        {
            const float h = (float) plot.getHeight() * (float) (v - 0x80) / 127.0f;
            g.setColour (i == selected ? colours::text : colours::accent);
            g.fillRect (x + 0.5f, (float) plot.getBottom() - h, w - 1.0f, h);
        }
        else
        {
            const int rel = v - 0x40;
            const float mid = (float) plot.getCentreY();
            const float h = (float) plot.getHeight() * 0.5f * (float) rel / 64.0f;
            g.setColour (i == selected ? colours::text : colours::accent2);
            g.fillRect (x + 0.5f, juce::jmin (mid, mid - h), w - 1.0f, std::abs (h) + 1.0f);
        }
    }

    static const juce::Colour trackColours[] = { juce::Colour (0xff5fd38d), juce::Colour (0xffff6b6b), juce::Colour (0xffc792ea),
                                                 juce::Colour (0xff4fd6e0), juce::Colour (0xffffd166), juce::Colour (0xffef8bd1) };
    g.setFont (font (11.0f));
    for (size_t t = 0; t < tracks.size(); ++t)
    {
        const auto& p = tracks[t].prefix;
        const int start = target.getParam (table.get (p + ".start"));
        const int end = target.getParam (table.get (p + ".end"));
        const int loop = target.getParam (table.get (p + ".loop"));
        const int y = 2 + (int) t * 9;
        const float x1 = (float) plot.getX() + (float) juce::jmin (start, end) * w;
        const float x2 = (float) plot.getX() + (float) (juce::jmax (start, end) + 1) * w;
        g.setColour (trackColours[t % 6]);
        g.fillRect (x1, (float) y + 3.0f, x2 - x1, 2.0f);
        g.fillRect ((float) plot.getX() + (float) loop * w, (float) y, 2.0f, 8.0f);
        g.drawText (tracks[t].label, r.getRight() - 34, y - 2, 32, 11, juce::Justification::centredRight);
    }

    g.setColour (colours::panelEdge);
    g.drawRect (plot);
    g.drawHorizontalLine (plot.getCentreY(), (float) plot.getX(), (float) plot.getRight());
}

void WavetableView::edit (const juce::MouseEvent& e, bool down)
{
    auto plot = plotArea();
    const int step = juce::jlimit (0, 127, (int) ((float) (e.x - plot.getX()) * 128.0f / (float) plot.getWidth()));
    if (down || step != selected)
    {
        selected = step;
        if (onSelect) onSelect (step);
        repaint();
    }
    if (! e.mods.isShiftDown() && down)
        return; // a plain click selects; shift-click or drag draws

    // Drawing keeps the step's mode (absolute or relative).
    const int old = target.getByte (0x180 + step);
    const float norm = juce::jlimit (0.0f, 1.0f, 1.0f - (float) (e.y - plot.getY()) / (float) plot.getHeight());
    const int v = old >= 0x80 ? 0x80 + juce::roundToInt (norm * 127.0f)
                              : 0x40 + juce::jlimit (-64, 63, juce::roundToInt ((norm - 0.5f) * 128.0f));
    target.setByteValue ((uint16_t) (0x180 + step), (uint8_t) v);
}

// ---------------------------------------------------------------------------
// StepEditor
// ---------------------------------------------------------------------------
StepEditor::StepEditor (ParamTarget& t) : target (t)
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
    target.addTargetListener (this);
    setStep (0);
}

StepEditor::~StepEditor()
{
    target.removeTargetListener (this);
}

void StepEditor::setStep (int s)
{
    step = s;
    refresh();
}

void StepEditor::resized()
{
    auto r = getLocalBounds();
    title.setBounds (r.removeFromLeft (90));
    mode.setBounds (r.removeFromLeft (160).reduced (0, 2));
    r.removeFromLeft (10);
    value.setBounds (r.removeFromLeft (260));
    r.removeFromLeft (10);
    hint.setBounds (r);
}

void StepEditor::refresh()
{
    const int v = target.getByte (0x180 + step);
    title.setText ("Step " + juce::String (step) + " (0x" + juce::String::toHexString (step).paddedLeft ('0', 2) + ")",
                   juce::dontSendNotification);
    const bool abs = v >= 0x80;
    mode.setSelectedId (abs ? 1 : 2, juce::dontSendNotification);
    if (abs) value.setRange (0, 127, 1); else value.setRange (-64, 63, 1);
    value.setValue (abs ? v - 0x80 : v - 0x40, juce::dontSendNotification);
}

void StepEditor::apply()
{
    const bool abs = mode.getSelectedId() == 1;
    const int x = (int) value.getValue();
    const int v = abs ? 0x80 + juce::jlimit (0, 127, x) : 0x40 + juce::jlimit (-64, 63, x);
    target.setByteValue ((uint16_t) (0x180 + step), (uint8_t) v);
    refresh();
}

void StepEditor::targetChanged (const std::vector<uint16_t>& a)
{
    for (auto x : a)
        if (x == 0x180 + step) { refresh(); return; }
}

// ---------------------------------------------------------------------------
EngineEditor::EngineEditor()
{
    tabs.setTabBarDepth (30);
    tabs.setOutline (0);
    addAndMakeVisible (tabs);
}

void EngineEditor::addPage (const juce::String& name, std::unique_ptr<juce::Component> page)
{
    tabs.addTab (name, colours::background, page.release(), true);
}
} // namespace ui
