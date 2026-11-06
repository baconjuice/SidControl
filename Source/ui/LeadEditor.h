#pragma once

#include "ParamControls.h"

namespace ui
{
// Full editor for the Lead engine, organised in tabs.
class LeadEditor : public juce::Component
{
public:
    explicit LeadEditor (Session& s);
    ~LeadEditor() override;

    void resized() override;
    juce::TabbedComponent& getTabs() { return tabs; }

private:
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
};
} // namespace ui
