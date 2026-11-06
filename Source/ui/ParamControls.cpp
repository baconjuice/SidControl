#include "ParamControls.h"
#include "LookAndFeel.h"

#include <algorithm>

namespace ui
{
// ---------------------------------------------------------------------------
// ParamControl
// ---------------------------------------------------------------------------
ParamControl::ParamControl (Session& s, const sid::ParamDef& d) : session (s), def (d)
{
    session.addListener (this);
}

ParamControl::~ParamControl()
{
    session.removeListener (this);
}

void ParamControl::editChanged (const std::vector<uint16_t>& addresses)
{
    for (auto a : def.addresses())
        if (std::find (addresses.begin(), addresses.end(), a) != addresses.end())
        {
            refresh();
            return;
        }
}

void ParamControl::write (int raw)
{
    session.setParam (def, raw, writeOption ? writeOption() : (uint8_t) 0);
}

// ---------------------------------------------------------------------------
// ParamKnob
// ---------------------------------------------------------------------------
ParamKnob::ParamKnob (Session& s, const sid::ParamDef& d, juce::String label, FormatFn format)
    : ParamControl (s, d), formatter (std::move (format))
{
    title.setText (label.isNotEmpty() ? label : juce::String (d.name), juce::dontSendNotification);
    title.setJustificationType (juce::Justification::centred);
    title.setFont (font (11.5f));
    title.setColour (juce::Label::textColourId, colours::dimText);
    title.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (title);

    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 15);
    slider.setRange (d.minDisplay(), d.maxDisplay(), 1.0);
    slider.setMouseDragSensitivity (d.maxRaw() > 255 ? 400 : 180);
    slider.setScrollWheelEnabled (true);
    slider.setDoubleClickReturnValue (
        true, sid::Patch::makeInit (s.engine()).getRaw (d) - d.offset);
    if (formatter)
        slider.textFromValueFunction = [this] (double v) { return formatter ((int) v); };
    slider.onValueChange = [this] { write ((int) slider.getValue() + def.offset); };
    slider.setTooltip (juce::String (d.name) + " (0x" + juce::String::toHexString ((int) d.addr).paddedLeft ('0', 3) + ")");
    addAndMakeVisible (slider);
    refresh();
}

void ParamKnob::resized()
{
    auto r = getLocalBounds();
    if (! compact)
        title.setBounds (r.removeFromTop (14));
    slider.setBounds (r);
}

void ParamKnob::refresh()
{
    slider.setValue (read() - def.offset, juce::dontSendNotification);
    slider.updateText(); // formatter may depend on other parameters
}

// ---------------------------------------------------------------------------
// ParamToggle
// ---------------------------------------------------------------------------
ParamToggle::ParamToggle (Session& s, const sid::ParamDef& d, juce::String label) : ParamControl (s, d)
{
    button.setButtonText (label.isNotEmpty() ? label : juce::String (d.name));
    button.setClickingTogglesState (true);
    button.onClick = [this] { write (button.getToggleState() ? 1 : 0); };
    button.setTooltip (juce::String (d.name) + " (0x" + juce::String::toHexString ((int) d.addr).paddedLeft ('0', 3)
                       + " bit " + juce::String (d.shift) + ")");
    addAndMakeVisible (button);
    refresh();
}

juce::Point<int> ParamToggle::preferredSize() const
{
    const auto w = juce::GlyphArrangement::getStringWidthInt (font (12.5f), button.getButtonText());
    return { juce::jmax (34, w + 16), 22 };
}

// ---------------------------------------------------------------------------
// ParamCombo
// ---------------------------------------------------------------------------
ParamCombo::ParamCombo (Session& s, const sid::ParamDef& d, juce::String label, int w,
                        std::vector<std::string> customChoices)
    : ParamControl (s, d), width (w)
{
    title.setText (label.isNotEmpty() ? label : juce::String (d.name), juce::dontSendNotification);
    title.setFont (font (11.5f));
    title.setColour (juce::Label::textColourId, colours::dimText);
    title.setBorderSize ({ 0, 2, 0, 0 });
    addAndMakeVisible (title);

    const auto& choices = customChoices.empty() ? d.choices : customChoices;
    if (choices.empty())
    {
        for (int i = 0; i <= d.maxRaw(); ++i)
            box.addItem (juce::String (i - d.offset), i + 1);
    }
    else
    {
        for (size_t i = 0; i < choices.size(); ++i)
            box.addItem (choices[i], (int) i + 1);
    }
    box.onChange = [this] { write (box.getSelectedId() - 1); };
    addAndMakeVisible (box);
    refresh();
}

void ParamCombo::resized()
{
    auto r = getLocalBounds();
    if (! compact)
        title.setBounds (r.removeFromTop (16));
    box.setBounds (r.removeFromTop (24));
}

void ParamCombo::refresh()
{
    box.setSelectedId (read() + 1, juce::dontSendNotification);
}

std::vector<std::string> parameterNumberChoices (sid::Engine e)
{
    std::vector<std::string> v;
    for (int i = 0; i < 256; ++i)
    {
        const std::string n = sid::parameterNumberName (e, i);
        v.push_back (std::to_string (i) + ": " + (n.empty() ? "--" : n));
    }
    return v;
}

// ---------------------------------------------------------------------------
// ParamGroup
// ---------------------------------------------------------------------------
namespace
{
constexpr int kPad = 8, kTitleH = 20, kGapX = 6, kGapY = 6;

class ToggleStack : public juce::Component
{
public:
    void add (std::unique_ptr<ParamToggle> t)
    {
        addAndMakeVisible (*t);
        toggles.push_back (std::move (t));
    }
    juce::Point<int> preferredSize() const
    {
        int w = 0;
        for (auto& t : toggles) w = juce::jmax (w, t->preferredSize().x);
        return { w, (int) toggles.size() * 22 + ((int) toggles.size() - 1) * 3 };
    }
    void resized() override
    {
        int y = 0;
        for (auto& t : toggles)
        {
            t->setBounds (0, y, getWidth(), 22);
            y += 25;
        }
    }
    std::vector<std::unique_ptr<ParamToggle>> toggles;
};
} // namespace

ParamGroup::ParamGroup (Session& s, const sid::ParamTable& t, juce::String groupTitle, WriteOptionFn wopt)
    : title (std::move (groupTitle)), session (s), table (t), writeOption (std::move (wopt))
{
}

void ParamGroup::addItem (std::unique_ptr<juce::Component> c, juce::Point<int> size)
{
    addAndMakeVisible (*c);
    items.push_back ({ c.get(), size, false });
    owned.push_back (std::move (c));
}

ParamKnob& ParamGroup::knob (const std::string& id, juce::String label, FormatFn format)
{
    auto k = std::make_unique<ParamKnob> (session, table.get (id), std::move (label), std::move (format));
    k->setWriteOption (writeOption);
    auto& ref = *k;
    addItem (std::move (k), ref.preferredSize());
    return ref;
}

ParamToggle& ParamGroup::toggle (const std::string& id, juce::String label)
{
    auto t = std::make_unique<ParamToggle> (session, table.get (id), std::move (label));
    t->setWriteOption (writeOption);
    auto& ref = *t;
    addItem (std::move (t), ref.preferredSize());
    return ref;
}

ParamCombo& ParamGroup::combo (const std::string& id, juce::String label, int width, std::vector<std::string> customChoices)
{
    auto c = std::make_unique<ParamCombo> (session, table.get (id), std::move (label), width, std::move (customChoices));
    c->setWriteOption (writeOption);
    auto& ref = *c;
    addItem (std::move (c), ref.preferredSize());
    return ref;
}

void ParamGroup::toggles (std::initializer_list<std::pair<std::string, juce::String>> idsAndLabels)
{
    auto stack = std::make_unique<ToggleStack>();
    for (auto& [id, label] : idsAndLabels)
    {
        auto t = std::make_unique<ParamToggle> (session, table.get (id), label);
        t->setWriteOption (writeOption);
        stack->add (std::move (t));
    }
    const auto size = stack->preferredSize();
    addItem (std::move (stack), size);
}

void ParamGroup::lineBreak()
{
    items.push_back ({ nullptr, {}, true });
}

void ParamGroup::gap (int width)
{
    items.push_back ({ nullptr, { width, 0 }, false });
}

int ParamGroup::layout (int width, bool apply)
{
    const int left = kPad, right = width - kPad;
    int x = left, y = kTitleH + 4, rowH = 0;

    for (auto& it : items)
    {
        if (it.lineBreak)
        {
            if (rowH > 0) { y += rowH + kGapY; }
            x = left;
            rowH = 0;
            continue;
        }
        if (x > left && x + it.size.x > right)
        {
            y += rowH + kGapY;
            x = left;
            rowH = 0;
        }
        if (it.comp != nullptr && apply)
            it.comp->setBounds (x, y, it.size.x, it.size.y);
        x += it.size.x + kGapX;
        rowH = juce::jmax (rowH, it.size.y);
    }
    return y + rowH + kPad;
}

int ParamGroup::preferredHeight (int forWidth) const
{
    return const_cast<ParamGroup*> (this)->layout (forWidth, false);
}

void ParamGroup::resized()
{
    layout (getWidth(), true);
}

void ParamGroup::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setColour (colours::accent2);
    g.setFont (font (12.5f, true));
    g.drawText (title, kPad, 3, getWidth() - 2 * kPad, kTitleH - 2, juce::Justification::centredLeft);
}
} // namespace ui
