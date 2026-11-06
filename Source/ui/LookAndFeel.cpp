#include "LookAndFeel.h"

namespace ui
{
juce::Font font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

LookAndFeel::LookAndFeel()
{
    using namespace colours;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::backgroundColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextButton::buttonColourId, panelEdge);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, background);
    setColour (juce::ComboBox::outlineColourId, panelEdge);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, dimText);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::TextEditor::backgroundColourId, background);
    setColour (juce::TextEditor::outlineColourId, panelEdge);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::ListBox::backgroundColourId, background);
    setColour (juce::ListBox::outlineColourId, panelEdge);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, panelEdge);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, panelEdge);
    setColour (juce::TabbedComponent::backgroundColourId, background);
    setColour (juce::TabbedComponent::outlineColourId, panelEdge);
    setColour (juce::TabbedButtonBar::tabOutlineColourId, panelEdge);
    setColour (juce::ProgressBar::backgroundColourId, panel);
    setColour (juce::ProgressBar::foregroundColourId, accent);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                                    juce::Slider& s)
{
    auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax (2.0f, radius * 0.18f);
    const float arcR = radius - lineW * 0.5f;
    const float angle = start + pos * (end - start);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (colours::panelEdge);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar parameters fill from the centre.
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float from = bipolar ? start + (end - start) * (float) ((0.0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum()))
                               : start;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (s.isEnabled() ? s.findColour (juce::Slider::rotarySliderFillColourId) : colours::dimText);
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float dotR = arcR - lineW * 1.6f;
    g.setColour (colours::text);
    g.fillEllipse (juce::Rectangle<float> (lineW, lineW)
                       .withCentre ({ centre.x + dotR * std::sin (angle), centre.y - dotR * std::cos (angle) }));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bg;
    if (down) c = c.brighter (0.2f);
    else if (over) c = c.brighter (0.08f);
    if (! b.isEnabled()) c = c.withMultipliedAlpha (0.5f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 3.0f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return font (juce::jmin (13.0f, (float) h * 0.6f)); }
juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return font (12.5f); }
juce::Font LookAndFeel::getLabelFont (juce::Label& l) { return l.getFont().withHeight (juce::jmin (l.getFont().getHeight(), 14.0f)); }
juce::Font LookAndFeel::getPopupMenuFont() { return font (13.0f); }

void LookAndFeel::drawTabButton (juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f, 2.0f);
    const bool front = b.isFrontTab();
    g.setColour (front ? colours::accent : (over ? colours::panelEdge : colours::panel));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (front ? juce::Colours::white : colours::text);
    g.setFont (font (13.0f, front));
    g.drawText (b.getButtonText(), r, juce::Justification::centred);
}
} // namespace ui
