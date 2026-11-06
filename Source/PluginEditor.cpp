#include "PluginEditor.h"
#include "model/EnsembleSession.h"
#include "ui/BasslineEditor.h"
#include "ui/DrumEditor.h"
#include "ui/EnsembleEditor.h"
#include "ui/LeadEditor.h"
#include "ui/MultiEditor.h"

using namespace ui;

SidControlEditor::SidControlEditor (SidControlProcessor& p)
    : AudioProcessorEditor (p), proc (p), session (p.session), connectionBar (p.session), library (p.session)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);

    addAndMakeVisible (connectionBar);
    addAndMakeVisible (library);

    nameLabel.setText ("Patch", juce::dontSendNotification);
    nameLabel.setFont (font (12.5f));
    nameLabel.setColour (juce::Label::textColourId, colours::dimText);
    addAndMakeVisible (nameLabel);

    nameEditor.setFont (font (16.0f, true));
    nameEditor.setInputRestrictions (sid::kNameLength);
    nameEditor.setTooltip ("Patch name (16 characters)");
    nameEditor.onTextChange = [this] { session.setName (nameEditor.getText()); };
    addAndMakeVisible (nameEditor);

    engineLabel.setFont (font (13.0f, true));
    engineLabel.setColour (juce::Label::textColourId, colours::accent);
    addAndMakeVisible (engineLabel);
    sourceLabel.setFont (font (12.0f));
    sourceLabel.setColour (juce::Label::textColourId, colours::dimText);
    addAndMakeVisible (sourceLabel);

    ensembleButton.setClickingTogglesState (true);
    ensembleButton.setTooltip ("Show the ensemble editor (patch per SID core, MIDI channels, splits, clock)");
    ensembleButton.onClick = [this] { showEnsemble (ensembleButton.getToggleState()); };
    addAndMakeVisible (ensembleButton);
    ensembleEditor = std::make_unique<EnsembleEditor> (session.getEnsemble());
    addChildComponent (*ensembleEditor);

    newButton.onClick = [this] { showNewPatchMenu(); };
    openButton.onClick = [this] { library.openSyx(); };
    savePatchButton.onClick = [this] { savePatch(); };
    for (auto* b : { &newButton, &openButton, &savePatchButton })
        addAndMakeVisible (b);

    statusLabel.setFont (font (12.5f));
    statusLabel.setColour (juce::Label::textColourId, colours::text);
    addAndMakeVisible (statusLabel);
    addChildComponent (progressBar);
    cancelButton.onClick = [this] { session.cancelTransfers(); };
    addChildComponent (cancelButton);
    logButton.onClick = [this] { showLog(); };
    addAndMakeVisible (logButton);

    session.addListener (this);
    rebuildEngineEditor();
    refreshHeader();
    statusChanged();

    setResizable (true, true);
    setResizeLimits (1260, 700, 2400, 1600);
    setSize (proc.editorWidth, proc.editorHeight);
    startTimerHz (10);

    // Developer aid: SIDCONTROL_SNAPSHOT_DIR=/some/dir renders every page to PNG and quits.
    // Optionally load a bank first: SIDCONTROL_SNAPSHOT_SYX=/file.syx SIDCONTROL_SNAPSHOT_SLOT=n
    if (auto dir = juce::SystemStats::getEnvironmentVariable ("SIDCONTROL_SNAPSHOT_DIR", {}); dir.isNotEmpty())
    {
        if (auto syx = juce::SystemStats::getEnvironmentVariable ("SIDCONTROL_SNAPSHOT_SYX", {}); syx.isNotEmpty())
        {
            juce::String msg;
            session.loadSyx (juce::File (syx), msg);
            const int slot = juce::SystemStats::getEnvironmentVariable ("SIDCONTROL_SNAPSHOT_SLOT", "0").getIntValue();
            if (const auto& patch = session.slot (slot))
                session.loadIntoEditor (*patch, slot);
        }
        juce::Timer::callAfterDelay (1500, [this, dir] { writeSnapshots (juce::File (dir)); });
    }
}

void SidControlEditor::writeSnapshots (const juce::File& dir)
{
    dir.createDirectory();
    auto save = [this, &dir] (const juce::String& name)
    {
        auto img = createComponentSnapshot (getLocalBounds(), true, 1.0f);
        juce::FileOutputStream out (dir.getChildFile (name + ".png"));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (img, out);
    };
    auto saveTabs = [&save] (EngineEditor& ed, const juce::String& prefix)
    {
        auto& tabs = ed.getTabs();
        for (int i = 0; i < tabs.getNumTabs(); ++i)
        {
            tabs.setCurrentTabIndex (i);
            save (prefix + juce::String (i) + "_" + tabs.getTabNames()[i].removeCharacters (" &()"));
        }
        tabs.setCurrentTabIndex (0);
    };

    if (juce::SystemStats::getEnvironmentVariable ("SIDCONTROL_SNAPSHOT_ALL", {}).isNotEmpty())
    {
        session.setLive (false);
        for (int e = 0; e < 4; ++e)
        {
            session.loadIntoEditor (sid::Patch::makeInit ((sid::Engine) e));
            if (auto* ed = dynamic_cast<EngineEditor*> (engineEditor.get()))
                saveTabs (*ed, juce::String (sid::engineName ((sid::Engine) e)) + "_");
        }
        // A plausible ensemble: SID1 on channel 1..6, splits over the keyboard.
        sid::Ensemble ens;
        for (int sidIdx = 0; sidIdx < 4; ++sidIdx)
        {
            const int b = sidIdx * 0x40;
            ens.setByte (b + 2, 0x02);
            ens.setByte (b + 5, 120);
            ens.setByte (b + 9, 0x10);
            ens.setByte (b + 11, 0xf0);
            for (int i = 0; i < 6; ++i)
            {
                ens.setByte (b + 0x10 + 8 * i, (uint8_t) i);
                ens.setByte (b + 0x12 + 8 * i, 0x7f);
                ens.setByte (b + 0x13 + 8 * i, 0x40);
            }
        }
        session.getEnsemble().replace (ens, true);
        showEnsemble (true);
        saveTabs (static_cast<EnsembleEditor&> (*ensembleEditor).getPages(), "Ensemble_");
    }
    else if (auto* ed = dynamic_cast<EngineEditor*> (engineEditor.get()))
    {
        saveTabs (*ed, {});
    }
    else
    {
        save ("editor");
    }
    if (auto* app = juce::JUCEApplicationBase::getInstance())
        app->systemRequestedQuit();
}

SidControlEditor::~SidControlEditor()
{
    session.removeListener (this);
    engineEditor.reset();
    ensembleEditor.reset();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void SidControlEditor::rebuildEngineEditor()
{
    const auto e = session.engine();
    if (hasEngineEditor && e == shownEngine)
        return;
    shownEngine = e;
    hasEngineEditor = true;

    switch (e)
    {
        case sid::Engine::Lead:     engineEditor = std::make_unique<LeadEditor> (session); break;
        case sid::Engine::Bassline: engineEditor = std::make_unique<BasslineEditor> (session); break;
        case sid::Engine::Drum:     engineEditor = std::make_unique<DrumEditor> (session); break;
        case sid::Engine::Multi:    engineEditor = std::make_unique<MultiEditor> (session); break;
    }
    addChildComponent (*engineEditor);
    engineEditor->setVisible (! ensembleButton.getToggleState());
    resized();
}

void SidControlEditor::showEnsemble (bool show)
{
    ensembleButton.setToggleState (show, juce::dontSendNotification);
    ensembleEditor->setVisible (show);
    if (engineEditor != nullptr)
        engineEditor->setVisible (! show);
}

void SidControlEditor::refreshHeader()
{
    const juce::String name (session.edit().name());
    if (nameEditor.getText().trimEnd() != name)
        nameEditor.setText (name, juce::dontSendNotification);
    engineLabel.setText (juce::String (sid::engineName (session.engine())) + " Engine", juce::dontSendNotification);

    const int s = session.editorSourceSlot();
    sourceLabel.setText (s >= 0 ? "from " + juce::String::charToString ((juce::juce_wchar) ('A' + session.getBankNumber()))
                                      + juce::String (s + 1).paddedLeft ('0', 3)
                                : juce::String(),
                         juce::dontSendNotification);
}

void SidControlEditor::editReplaced()
{
    rebuildEngineEditor();
    refreshHeader();
}

void SidControlEditor::editChanged (const std::vector<uint16_t>& addresses)
{
    for (auto a : addresses)
    {
        if (a == sid::kEngineAddr) { rebuildEngineEditor(); refreshHeader(); return; }
        if (a < sid::kNameLength && ! nameEditor.hasKeyboardFocus (true)) { refreshHeader(); return; }
    }
}

void SidControlEditor::statusChanged()
{
    statusLabel.setText (session.getStatus(), juce::dontSendNotification);
    progressValue = session.getProgress();
    const bool busy = session.isBusy();
    progressBar.setVisible (busy && progressValue >= 0.0);
    cancelButton.setVisible (busy);
}

void SidControlEditor::timerCallback()
{
    // Remember the window size for the next session / project load.
    proc.editorWidth = getWidth();
    proc.editorHeight = getHeight();
    if (cancelButton.isVisible() != session.isBusy())
        statusChanged();
}

void SidControlEditor::showNewPatchMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("New patch from firmware init preset");
    for (int e = 0; e < 4; ++e)
        m.addItem (e + 1, sid::engineName ((sid::Engine) e));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&newButton),
                     [this] (int r)
                     {
                         if (r > 0)
                             session.loadIntoEditor (sid::Patch::makeInit ((sid::Engine) (r - 1)));
                     });
}

void SidControlEditor::savePatch()
{
    auto name = juce::File::createLegalFileName (juce::String (session.edit().name()).trim());
    if (name.isEmpty()) name = "Patch";
    chooser = std::make_unique<juce::FileChooser> (
        "Save patch", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name + ".syx"), "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              juce::String msg;
                              session.savePatchSyx (f.withFileExtension ("syx"), msg);
                              statusLabel.setText (msg, juce::dontSendNotification);
                          });
}

void SidControlEditor::showLog()
{
    auto* text = new juce::TextEditor();
    text->setMultiLine (true);
    text->setReadOnly (true);
    text->setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
    text->setText (session.getLog().joinIntoString ("\n"));
    text->setSize (620, 360);
    text->moveCaretToEnd();
    juce::CallOutBox::launchAsynchronously (std::unique_ptr<juce::Component> (text), logButton.getScreenBounds(), nullptr);
}

void SidControlEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    auto status = getLocalBounds().removeFromBottom (28);
    g.setColour (colours::panel);
    g.fillRect (status);
    g.setColour (colours::panelEdge);
    g.drawHorizontalLine (status.getY(), 0.0f, (float) getWidth());
}

void SidControlEditor::resized()
{
    auto r = getLocalBounds();
    connectionBar.setBounds (r.removeFromTop (40));

    auto status = r.removeFromBottom (28).reduced (8, 4);
    logButton.setBounds (status.removeFromRight (44));
    status.removeFromRight (6);
    cancelButton.setBounds (status.removeFromRight (64));
    status.removeFromRight (6);
    progressBar.setBounds (status.removeFromRight (200));
    statusLabel.setBounds (status);

    library.setBounds (r.removeFromLeft (280));

    auto main = r.reduced (8, 6);
    auto header = main.removeFromTop (30);
    nameLabel.setBounds (header.removeFromLeft (44));
    nameEditor.setBounds (header.removeFromLeft (200));
    header.removeFromLeft (10);
    engineLabel.setBounds (header.removeFromLeft (110));
    sourceLabel.setBounds (header.removeFromLeft (90));
    savePatchButton.setBounds (header.removeFromRight (120).reduced (0, 2));
    header.removeFromRight (6);
    openButton.setBounds (header.removeFromRight (84).reduced (0, 2));
    header.removeFromRight (6);
    newButton.setBounds (header.removeFromRight (100).reduced (0, 2));
    header.removeFromRight (16);
    ensembleButton.setBounds (header.removeFromRight (90).reduced (0, 2));
    main.removeFromTop (6);

    if (engineEditor != nullptr)
        engineEditor->setBounds (main);
    if (ensembleEditor != nullptr)
        ensembleEditor->setBounds (main);
}
