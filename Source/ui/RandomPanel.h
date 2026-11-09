#pragma once

#include "model/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
// The random generator, shown in a call-out from the patch header: one
// button per section of the current engine (like the box's RND menu), an
// amount slider for gentle variations, and undo.
class RandomPanel : public juce::Component,
                    private Session::Listener
{
public:
    explicit RandomPanel (Session& s);
    ~RandomPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void editReplaced() override { refresh(); }
    void refresh();
    void rebuildButtons();
    void updateAmountText();

    Session& session;
    sid::Engine builtFor = sid::Engine::Lead;
    juce::OwnedArray<juce::TextButton> sectionButtons;
    juce::Label amountLabel, hint;
    juce::Slider amount;
    juce::TextButton undoButton { "Undo" };
};
} // namespace ui
