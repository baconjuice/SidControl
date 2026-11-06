#pragma once

#include "PluginProcessor.h"
#include "ui/ConnectionBar.h"
#include "ui/LibraryPanel.h"
#include "ui/LookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

class SidControlEditor : public juce::AudioProcessorEditor,
                         private Session::Listener,
                         private juce::Timer
{
public:
    explicit SidControlEditor (SidControlProcessor&);
    ~SidControlEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void editReplaced() override;
    void editChanged (const std::vector<uint16_t>& addresses) override;
    void statusChanged() override;
    void timerCallback() override;

    void rebuildEngineEditor();
    void refreshHeader();
    void showNewPatchMenu();
    void savePatch();
    void showLog();
    void showEnsemble (bool show);
    void writeSnapshots (const juce::File& dir);

    SidControlProcessor& proc;
    Session& session;
    ui::LookAndFeel lnf;

    ui::ConnectionBar connectionBar;
    ui::LibraryPanel library;

    juce::Label nameLabel, engineLabel, sourceLabel;
    juce::TextEditor nameEditor;
    juce::TextButton ensembleButton { "Ensemble" };
    std::unique_ptr<juce::Component> ensembleEditor;
    juce::TextButton newButton { "New Patch..." }, openButton { "Open .syx" }, savePatchButton { "Save Patch .syx" };

    std::unique_ptr<juce::Component> engineEditor;
    sid::Engine shownEngine = sid::Engine::Lead;
    bool hasEngineEditor = false;

    juce::Label statusLabel;
    double progressValue = -1.0;
    juce::ProgressBar progressBar { progressValue };
    juce::TextButton cancelButton { "Cancel" }, logButton { "Log" };

    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SidControlEditor)
};
