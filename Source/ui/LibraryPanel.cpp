#include "LibraryPanel.h"
#include "LookAndFeel.h"

namespace ui
{
namespace
{
juce::String bankLetter (int b) { return juce::String::charToString ((juce::juce_wchar) ('A' + b)); }
}

LibraryPanel::LibraryPanel (Session& s) : session (s)
{
    for (int b = 0; b < 8; ++b)
        bankBox.addItem ("Bank " + bankLetter (b), b + 1);
    bankBox.setSelectedId (session.getBankNumber() + 1, juce::dontSendNotification);
    bankBox.setTooltip ("BankStick bank on the MIDIbox SID used for fetch/store");
    bankBox.onChange = [this] { session.setBankNumber (bankBox.getSelectedId() - 1); };
    addAndMakeVisible (bankBox);

    list.setRowHeight (20);
    list.setColour (juce::ListBox::backgroundColourId, colours::background);
    list.setMultipleSelectionEnabled (false);
    addAndMakeVisible (list);

    fetchBankButton.setTooltip ("Read all 128 patches of this bank from the MIDIbox SID");
    fetchBankButton.onClick = [this] { session.fetchBank(); };
    writeBankButton.setTooltip ("Write every patch in this list to the MIDIbox SID's BankStick");
    writeBankButton.onClick = [this]
    {
        confirmThen ("Write bank", "Overwrite bank " + bankLetter (session.getBankNumber())
                                       + " on the MIDIbox SID with the patches in this list?",
                     [this] { session.writeBankToDevice(); });
    };
    fetchButton.setTooltip ("Read the selected patch from the MIDIbox SID and open it");
    fetchButton.onClick = [this] { if (selectedRow() >= 0) session.fetchSlot (selectedRow(), true); };
    storeButton.setTooltip ("Store the patch in the editor into the selected slot on the MIDIbox SID");
    storeButton.onClick = [this]
    {
        if (const int row = selectedRow(); row >= 0)
            showStoreDialog (row);
    };
    openButton.onClick = [this] { openSyx(); };
    saveButton.onClick = [this] { saveBank(); };

    for (auto* b : { &fetchBankButton, &writeBankButton, &fetchButton, &storeButton, &openButton, &saveButton })
        addAndMakeVisible (b);

    lastDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    session.addListener (this);
}

LibraryPanel::~LibraryPanel()
{
    session.removeListener (this);
}

void LibraryPanel::bankChanged()
{
    bankBox.setSelectedId (session.getBankNumber() + 1, juce::dontSendNotification);
    list.updateContent();
    list.repaint();
}

void LibraryPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (selected)
        g.fillAll (colours::accent.withAlpha (0.35f));
    else if (row == session.editorSourceSlot())
        g.fillAll (colours::panelEdge.withAlpha (0.6f));
    else if (row % 2)
        g.fillAll (colours::panel.withAlpha (0.4f));

    const auto& p = session.slot (row);
    g.setFont (font (12.5f));
    g.setColour (colours::dimText);
    g.drawText (bankLetter (session.getBankNumber()) + juce::String (row + 1).paddedLeft ('0', 3), 6, 0, 40, h,
                juce::Justification::centredLeft);
    if (p)
    {
        static const juce::Colour engineColours[] = { colours::accent, colours::accent2, colours::good, colours::bad };
        g.setColour (engineColours[(int) p->engine()]);
        g.drawText (juce::String::charToString ((juce::juce_wchar) sid::engineLetter (p->engine())), 46, 0, 14, h,
                    juce::Justification::centred);
        g.setColour (colours::text);
        g.drawText (juce::String (p->name()), 64, 0, w - 68, h, juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (colours::panelEdge);
        g.drawText ("(not loaded)", 64, 0, w - 68, h, juce::Justification::centredLeft);
    }
}

void LibraryPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    openSlotInEditor (row);
}

void LibraryPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showSlotMenu (row);
}

void LibraryPanel::openSlotInEditor (int row)
{
    if (const auto& p = session.slot (row))
        session.loadIntoEditor (*p, row);
    else
        session.fetchSlot (row, true);
}

void LibraryPanel::showSlotMenu (int row)
{
    list.selectRow (row);
    const auto& p = session.slot (row);
    const auto slotLabel = bankLetter (session.getBankNumber()) + juce::String (row + 1).paddedLeft ('0', 3);

    juce::PopupMenu m;
    m.addSectionHeader (slotLabel + (p ? "  " + juce::String (p->name()) : juce::String()));
    m.addItem (1, "Open in editor", p.has_value());
    m.addItem (2, "Fetch from MIDIbox SID");
    m.addItem (3, "Store editor patch here (on MIDIbox SID)");
    m.addSeparator();
    m.addItem (4, "Copy", p.has_value());
    m.addItem (5, "Paste (list only)", clipboard.has_value());
    m.addItem (6, "Put editor patch here (list only)");
    juce::PopupMenu init;
    for (int e = 0; e < 4; ++e)
        init.addItem (10 + e, sid::engineName ((sid::Engine) e));
    m.addSubMenu ("Init patch here (list only)", init);
    m.addItem (7, "Clear (list only)", p.has_value());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&list).withMousePosition(),
                     [this, row] (int result)
                     {
                         switch (result)
                         {
                             case 1: openSlotInEditor (row); break;
                             case 2: session.fetchSlot (row, false); break;
                             case 3: list.selectRow (row); storeButton.triggerClick(); break;
                             case 4: clipboard = session.slot (row); break;
                             case 5: if (clipboard) session.setSlot (row, clipboard); break;
                             case 6: session.setSlot (row, session.edit()); break;
                             case 7: session.setSlot (row, std::nullopt); break;
                             default:
                                 if (result >= 10 && result < 14)
                                     session.setSlot (row, sid::Patch::makeInit ((sid::Engine) (result - 10)));
                                 break;
                         }
                     });
}

// Store asks for the name (a random patch or a new one usually needs one)
// and warns when the slot already holds a different patch.
void LibraryPanel::showStoreDialog (int row)
{
    const auto slotName = bankLetter (session.getBankNumber()) + juce::String (row + 1).paddedLeft ('0', 3);
    const auto& existing = session.slot (row);
    juce::String message = "Name the patch and store it in " + slotName + " on the MIDIbox SID.";
    if (existing && *existing != session.edit())
        message << "\n\nThis replaces \"" << juce::String (existing->name()).trimEnd() << "\" in " << slotName << ".";

    auto* w = new juce::AlertWindow ("Store patch in " + slotName, message,
                                     existing ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", juce::String (session.edit().name()).trimEnd(), "Name");
    if (auto* ed = w->getTextEditor ("name"))
    {
        ed->setInputRestrictions (sid::kNameLength);
        ed->setSelectAllWhenFocused (true);
    }
    w->addButton (existing && *existing != session.edit() ? "Overwrite" : "Store", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<LibraryPanel> self (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([self, w, row] (int result)
                        {
                            if (result != 1 || self == nullptr)
                                return;
                            auto name = w->getTextEditorContents ("name").trim();
                            if (name.isNotEmpty())
                                self->session.setName (name);
                            self->session.storeEditToSlot (row);
                        }),
                        true);
    if (auto* ed = w->getTextEditor ("name"))
        ed->grabKeyboardFocus();
}

void LibraryPanel::confirmThen (const juce::String& title, const juce::String& text, std::function<void()> action)
{
    auto opts = juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::WarningIcon)
                    .withTitle (title)
                    .withMessage (text)
                    .withButton ("Overwrite", 1)
                    .withButton ("Cancel", 0)
                    .withAssociatedComponent (this);
    juce::AlertWindow::showAsync (opts, [fn = std::move (action)] (int result) { if (result == 1) fn(); });
}

void LibraryPanel::openSyx()
{
    chooser = std::make_unique<juce::FileChooser> ("Open MIDIbox SID patches", lastDir, "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              lastDir = f.getParentDirectory();
                              juce::String msg;
                              if (! session.loadSyx (f, msg))
                                  juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (
                                                                    juce::MessageBoxIconType::WarningIcon, "Open .syx", msg),
                                                                nullptr);
                          });
}

void LibraryPanel::saveBank()
{
    auto start = lastDir.getChildFile ("SID Bank " + bankLetter (session.getBankNumber()) + ".syx");
    chooser = std::make_unique<juce::FileChooser> ("Save bank", start, "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              lastDir = f.getParentDirectory();
                              juce::String msg;
                              if (! session.saveBankSyx (f.withFileExtension ("syx"), msg))
                                  juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (
                                                                    juce::MessageBoxIconType::WarningIcon, "Save bank", msg),
                                                                nullptr);
                          });
}

void LibraryPanel::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRect (getLocalBounds());
    g.setColour (colours::panelEdge);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void LibraryPanel::resized()
{
    auto r = getLocalBounds().reduced (8).withTrimmedRight (1);
    bankBox.setBounds (r.removeFromTop (26));
    r.removeFromTop (6);

    auto bottom = r.removeFromBottom (26 * 3 + 8);
    auto row = [&bottom]
    {
        auto x = bottom.removeFromTop (26);
        bottom.removeFromTop (4);
        return x;
    };
    auto pair = [] (juce::Rectangle<int> rr, juce::Button& a, juce::Button& b)
    {
        a.setBounds (rr.removeFromLeft (rr.getWidth() / 2 - 2));
        b.setBounds (rr.withTrimmedLeft (4));
    };
    pair (row(), fetchButton, storeButton);
    pair (row(), fetchBankButton, writeBankButton);
    pair (row(), openButton, saveButton);

    r.removeFromBottom (6);
    list.setBounds (r);
}
} // namespace ui
