#include "RandomPanel.h"
#include "LookAndFeel.h"
#include "sid/Randomizer.h"

namespace ui
{
namespace
{
double lastAmount = 100.0; // remembered while the app runs
constexpr int kCols = 4, kButtonW = 92, kButtonH = 30, kGap = 6, kPad = 12, kTitleH = 28;
} // namespace

RandomPanel::RandomPanel (Session& s) : session (s)
{
    amountLabel.setFont (font (12.5f));
    amountLabel.setColour (juce::Label::textColourId, colours::text);
    addAndMakeVisible (amountLabel);

    amount.setSliderStyle (juce::Slider::LinearHorizontal);
    amount.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    amount.setRange (5.0, 100.0, 5.0);
    amount.setValue (lastAmount, juce::dontSendNotification);
    amount.setDoubleClickReturnValue (true, 100.0);
    amount.onValueChange = [this] { lastAmount = amount.getValue(); updateAmountText(); };
    amount.setTooltip ("100% rolls completely new values. Lower values only nudge the current sound.");
    addAndMakeVisible (amount);

    undoButton.setTooltip ("Go back to the patch before the last roll (repeatable)");
    undoButton.onClick = [this] { session.undoRandom(); };
    addAndMakeVisible (undoButton);

    hint.setText ("Each click rolls that part of the patch and sends it to the SID.", juce::dontSendNotification);
    hint.setFont (font (11.5f));
    hint.setColour (juce::Label::textColourId, colours::dimText);
    addAndMakeVisible (hint);

    session.addListener (this);
    rebuildButtons();
    updateAmountText();
    refresh();
}

RandomPanel::~RandomPanel()
{
    session.removeListener (this);
}

void RandomPanel::rebuildButtons()
{
    builtFor = session.engine();
    sectionButtons.clear();
    for (auto& sec : sid::randomSections (builtFor))
    {
        auto* b = sectionButtons.add (new juce::TextButton (sec.label));
        b->setTooltip (sec.help);
        if (sec.id == "all")
            b->setColour (juce::TextButton::buttonColourId, colours::accent2.withAlpha (0.35f));
        const auto id = sec.id;
        b->onClick = [this, id] { session.randomize (id, (float) (amount.getValue() / 100.0)); };
        addAndMakeVisible (b);
    }
    const int rows = (sectionButtons.size() + kCols - 1) / kCols;
    setSize (kPad * 2 + kCols * kButtonW + (kCols - 1) * kGap,
             kPad + kTitleH + rows * (kButtonH + kGap) + 8 + 24 + 8 + 28 + 6 + 16 + kPad);
    resized();
}

void RandomPanel::refresh()
{
    if (session.engine() != builtFor)
        rebuildButtons();
    undoButton.setEnabled (session.canUndoRandom());
    repaint();
}

void RandomPanel::updateAmountText()
{
    const int pct = (int) amount.getValue();
    amountLabel.setText ("Amount " + juce::String (pct) + "%" + (pct >= 100 ? "  (new values)" : "  (variation)"),
                         juce::dontSendNotification);
}

void RandomPanel::paint (juce::Graphics& g)
{
    g.setColour (colours::accent2);
    g.setFont (font (15.0f, true));
    const char* engines[] = { "Lead", "Bassline", "Drum", "Multi" };
    g.drawText ("Random Generator  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  " + engines[(int) builtFor], kPad, kPad - 2,
                getWidth() - 2 * kPad, kTitleH - 6, juce::Justification::centredLeft);
}

void RandomPanel::resized()
{
    int y = kPad + kTitleH;
    for (int i = 0; i < sectionButtons.size(); ++i)
    {
        const int col = i % kCols, row = i / kCols;
        sectionButtons[i]->setBounds (kPad + col * (kButtonW + kGap), y + row * (kButtonH + kGap), kButtonW, kButtonH);
    }
    const int rows = (sectionButtons.size() + kCols - 1) / kCols;
    y += rows * (kButtonH + kGap) + 8;
    const int w = getWidth() - 2 * kPad;
    amountLabel.setBounds (kPad, y, w, 24);
    y += 24;
    auto row = juce::Rectangle<int> (kPad, y + 4, w, 28);
    undoButton.setBounds (row.removeFromRight (80));
    row.removeFromRight (10);
    amount.setBounds (row);
    y += 28 + 10;
    hint.setBounds (kPad, y, w, 16);
}
} // namespace ui
