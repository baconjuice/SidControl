#include "EnsembleEditor.h"
#include "LookAndFeel.h"

namespace ui
{
namespace
{
const sid::ParamTable& T() { return sid::ensembleParams(); }

class SidPage : public Page
{
public:
    SidPage (EnsembleSession& e, int sidIndex) : Page (e, T())
    {
        const std::string p = "s" + std::to_string (sidIndex + 1);

        auto& patch = group ("Patch");
        patch.combo (p + ".bank", "Bank", 60);
        patch.knob (p + ".patch", "Patch");
        top.push_back (&patch);

        auto& clock = group ("Clock");
        clock.combo (p + ".clock", "Mode", 90);
        clock.knob (p + ".bpm", "BPM");
        clock.toggle (p + ".clkout", "Clock Out");
        top.push_back (&clock);

        auto& opt = group ("Options");
        opt.knob (p + ".superpoly", "SuperPoly");
        opt.toggles ({ { p + ".mono", "Mono" }, { p + ".fillog", "FilLog (8580)" } });
        opt.toggle (p + ".dor", "No OSC Reset");
        top.push_back (&opt);

        auto& aout = group ("Analog Outputs");
        aout.toggle (p + ".f2a", "F2A");
        aout.toggle (p + ".v2a", "V2A");
        aout.toggle (p + ".p2a", "P2A");
        aout.toggle (p + ".k2a", "K2A");
        aout.toggle (p + ".o2a", "O2A");
        aout.lineBreak();
        for (int a = 1; a <= 8; ++a)
            aout.toggle (p + ".ainv." + std::to_string (a), "Inv" + juce::String (a));
        mid.push_back (&aout);

        auto& cal = group ("Filter Calibration");
        cal.knob (p + ".cal.lmin", "Left Min");
        cal.knob (p + ".cal.lmax", "Left Max");
        cal.knob (p + ".cal.rmin", "Right Min");
        cal.knob (p + ".cal.rmax", "Right Max");
        mid.push_back (&cal);

        for (int i = 1; i <= 6; ++i)
        {
            const std::string q = p + ".i" + std::to_string (i);
            auto& g = group ("Instrument " + juce::String (i));
            g.knob (q + ".chn", "MIDI Ch");
            g.toggle (q + ".off", "Disabled");
            g.lineBreak();
            g.knob (q + ".splitlo", "Split Low", noteText);
            g.knob (q + ".splithi", "Split High", noteText);
            g.knob (q + ".transpose", {}, signedText);
            instruments.push_back (&g);
        }

        layout = [this] (int w)
        {
            auto area = juce::Rectangle<int> (0, 0, w, 10000).reduced (8);
            area.setY (columns (top, 3, area));
            area.setY (columns (mid, 2, area));
            return columns (instruments, 3, area);
        };
    }

private:
    std::vector<ParamGroup*> top, mid, instruments;
};
} // namespace

EnsembleEditor::EnsembleEditor (EnsembleSession& e) : ensemble (e)
{
    numberLabel.setText ("Ensemble", juce::dontSendNotification);
    numberLabel.setFont (font (12.5f));
    numberLabel.setColour (juce::Label::textColourId, colours::dimText);
    addAndMakeVisible (numberLabel);
    for (int i = 0; i < 128; ++i)
        numberBox.addItem ("E" + juce::String (i + 1).paddedLeft ('0', 3), i + 1);
    numberBox.setTooltip ("Stored ensemble used by Fetch and Store");
    numberBox.onChange = [this] { ensemble.setNumber (numberBox.getSelectedId() - 1); };
    addAndMakeVisible (numberBox);

    getButton.setTooltip ("Read the ensemble the MIDIbox SID is using right now");
    getButton.onClick = [this] { ensemble.fetchEditBuffer(); };
    sendButton.setTooltip ("Send this ensemble to the MIDIbox SID (not stored)");
    sendButton.onClick = [this] { ensemble.sendEditBuffer(); };
    fetchButton.setTooltip ("Read the selected stored ensemble from the MIDIbox SID");
    fetchButton.onClick = [this] { ensemble.fetchStored(); };
    storeButton.setTooltip ("Store this ensemble into the selected ensemble slot on the MIDIbox SID");
    storeButton.onClick = [this]
    {
        auto opts = juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle ("Store ensemble")
                        .withMessage ("Overwrite ensemble E" + juce::String (ensemble.getNumber() + 1).paddedLeft ('0', 3)
                                      + " on the MIDIbox SID?")
                        .withButton ("Overwrite", 1)
                        .withButton ("Cancel", 0)
                        .withAssociatedComponent (this);
        juce::AlertWindow::showAsync (opts, [this] (int r) { if (r == 1) ensemble.storeToBox(); });
    };
    openButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Open ensemble",
                                                       juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.syx");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult() == juce::File()) return;
                                  juce::String msg;
                                  if (! ensemble.loadSyx (fc.getResult(), msg))
                                      juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (
                                                                        juce::MessageBoxIconType::WarningIcon, "Open ensemble", msg),
                                                                    nullptr);
                                  refresh();
                              });
    };
    saveButton.onClick = [this]
    {
        auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                         .getChildFile ("Ensemble E" + juce::String (ensemble.getNumber() + 1).paddedLeft ('0', 3) + ".syx");
        chooser = std::make_unique<juce::FileChooser> ("Save ensemble", start, "*.syx");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult() == juce::File()) return;
                                  juce::String msg;
                                  ensemble.saveSyx (fc.getResult().withFileExtension ("syx"), msg);
                              });
    };
    for (auto* b : { &getButton, &sendButton, &fetchButton, &storeButton, &openButton, &saveButton })
        addAndMakeVisible (b);

    for (int s = 0; s < 4; ++s)
        pages.addPage ("SID " + juce::String (s + 1) + (s == 0 ? " (Master)" : ""), std::make_unique<SidPage> (e, s));
    addAndMakeVisible (pages);

    emptyHint.setText ("No ensemble loaded yet. Use \"Get Current\" to read the one the MIDIbox SID is using, "
                       "\"Fetch\" for a stored one, or \"Open .syx\".",
                       juce::dontSendNotification);
    emptyHint.setJustificationType (juce::Justification::centred);
    emptyHint.setFont (font (14.0f));
    emptyHint.setColour (juce::Label::textColourId, colours::dimText);
    addChildComponent (emptyHint);

    ensemble.addTargetListener (this);
    refresh();
}

EnsembleEditor::~EnsembleEditor()
{
    ensemble.removeTargetListener (this);
}

void EnsembleEditor::refresh()
{
    numberBox.setSelectedId (ensemble.getNumber() + 1, juce::dontSendNotification);
    pages.setVisible (ensemble.hasData());
    emptyHint.setVisible (! ensemble.hasData());
    sendButton.setEnabled (ensemble.hasData());
    storeButton.setEnabled (ensemble.hasData());
    saveButton.setEnabled (ensemble.hasData());
}

void EnsembleEditor::paint (juce::Graphics&) {}

void EnsembleEditor::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (28);
    numberLabel.setBounds (bar.removeFromLeft (64));
    numberBox.setBounds (bar.removeFromLeft (90).reduced (0, 2));
    bar.removeFromLeft (10);
    fetchButton.setBounds (bar.removeFromLeft (60).reduced (0, 2));
    bar.removeFromLeft (6);
    storeButton.setBounds (bar.removeFromLeft (60).reduced (0, 2));
    bar.removeFromLeft (20);
    getButton.setBounds (bar.removeFromLeft (100).reduced (0, 2));
    bar.removeFromLeft (6);
    sendButton.setBounds (bar.removeFromLeft (105).reduced (0, 2));
    saveButton.setBounds (bar.removeFromRight (80).reduced (0, 2));
    bar.removeFromRight (6);
    openButton.setBounds (bar.removeFromRight (80).reduced (0, 2));
    r.removeFromTop (6);
    pages.setBounds (r);
    emptyHint.setBounds (r);
}
} // namespace ui
