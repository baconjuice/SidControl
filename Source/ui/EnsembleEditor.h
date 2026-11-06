#pragma once

#include "EditorCommon.h"
#include "model/EnsembleSession.h"

namespace ui
{
// Editor for the ensemble: per SID core the patch it plays, clock, options,
// analog outputs, filter calibration and the 6 instruments' MIDI setup.
class EnsembleEditor : public juce::Component,
                       private ParamTarget::Listener
{
public:
    explicit EnsembleEditor (EnsembleSession& e);
    ~EnsembleEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    EngineEditor& getPages() { return pages; }

private:
    void targetReplaced() override { refresh(); }
    void refresh();

    EnsembleSession& ensemble;
    juce::Label numberLabel;
    juce::ComboBox numberBox;
    juce::TextButton getButton { "Get Current" }, sendButton { "Send Current" }, fetchButton { "Fetch" },
        storeButton { "Store" }, openButton { "Open .syx" }, saveButton { "Save .syx" };
    EngineEditor pages;
    juce::Label emptyHint;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace ui
