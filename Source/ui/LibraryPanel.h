#pragma once

#include "model/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
// Librarian: one bank of 128 patches, transfers to/from the box and .syx files.
class LibraryPanel : public juce::Component,
                     private juce::ListBoxModel,
                     private Session::Listener
{
public:
    explicit LibraryPanel (Session& s);
    ~LibraryPanel() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    void openSyx();
    void saveBank();

private:
    int getNumRows() override { return Session::kSlots; }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    void bankChanged() override;
    void editReplaced() override { list.repaint(); }

    void openSlotInEditor (int row);
    void showSlotMenu (int row);
    int selectedRow() const { return list.getSelectedRow(); }
    void confirmThen (const juce::String& title, const juce::String& text, std::function<void()> action);

    Session& session;
    juce::ComboBox bankBox;
    juce::ListBox list { "Patches", this };
    juce::TextButton fetchBankButton { "Fetch Bank" }, writeBankButton { "Write Bank" },
        fetchButton { "Fetch" }, storeButton { "Store Here" }, openButton { "Open .syx" }, saveButton { "Save Bank" };

    std::optional<sid::Patch> clipboard;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastDir;
};
} // namespace ui
