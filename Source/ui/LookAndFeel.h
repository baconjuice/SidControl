#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
namespace colours
{
    const juce::Colour background { 0xff1b1d23 };
    const juce::Colour panel      { 0xff252831 };
    const juce::Colour panelEdge  { 0xff353946 };
    const juce::Colour text       { 0xffd8dbe2 };
    const juce::Colour dimText    { 0xff8a90a0 };
    const juce::Colour accent     { 0xff6c8cff }; // C64 light blue
    const juce::Colour accent2    { 0xffffb454 };
    const juce::Colour good       { 0xff5fd38d };
    const juce::Colour bad        { 0xffff6b6b };
} // namespace colours

juce::Font font (float height, bool bold = false);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                           juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool over, bool down) override;
};
} // namespace ui
