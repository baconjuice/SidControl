#pragma once

// Editor state: the patch being edited, the bank of 128 patches shown in the
// librarian, and the operations that move patches between them, files and
// the MIDIbox SID.

#include "ParamTarget.h"
#include "device/SidDevice.h"
#include "sid/Params.h"
#include "sid/SyxFile.h"

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <optional>

class EnsembleSession;

class Session : public ParamTarget,
                private SidDevice::Listener
{
public:
    static constexpr int kSlots = 128;

    explicit Session (SidDevice& d);
    ~Session() override;

    SidDevice& device;

    // ---- edit buffer ------------------------------------------------------
    const sid::Patch& edit() const { return editPatch; }
    sid::Engine engine() const { return editPatch.engine(); }

    // Change one parameter. writeOption mirrors the change to other voices or
    // the other channel (L/R) exactly like the firmware does.
    void setParam (const sid::ParamDef& p, int raw, uint8_t writeOption = 0) override;
    int getParam (const sid::ParamDef& p) const override { return editPatch.getRaw (p); }
    uint8_t getByte (int addr) const override { return editPatch.byte (addr); }
    int defaultRaw (const sid::ParamDef& p) const override;
    void setName (const juce::String& name);

    // Replace the whole edit buffer (sends it to the box when live).
    void loadIntoEditor (const sid::Patch& p, int fromSlot = -1);
    int editorSourceSlot() const { return sourceSlot; }

    bool isLive() const { return live; }
    void setLive (bool shouldSendEdits) { live = shouldSendEdits; }

    void sendEditBuffer();
    void fetchEditBuffer();
    void playPatch();
    void allNotesOff();
    void ping();

    // ---- bank / librarian -------------------------------------------------
    const std::optional<sid::Patch>& slot (int i) const { return slots[(size_t) i]; }
    void setSlot (int i, std::optional<sid::Patch> p);
    int getBankNumber() const { return bankNumber; }
    void setBankNumber (int b);

    void fetchSlot (int i, bool thenEdit);
    void fetchBank();
    void storeEditToSlot (int i);     // writes to the box's BankStick and the bank
    void writeBankToDevice();
    void cancelTransfers();
    bool isBusy() const { return busy; }

    bool loadSyx (const juce::File& f, juce::String& message);
    bool saveBankSyx (const juce::File& f, juce::String& message) const;
    bool savePatchSyx (const juce::File& f, juce::String& message) const;

    // ---- state (plugin project / app settings) ---------------------------
    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& v);

    // ---- notifications ----------------------------------------------------
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void editReplaced() {}
        virtual void editChanged (const std::vector<uint16_t>& addresses) { juce::ignoreUnused (addresses); }
        virtual void bankChanged() {}
        virtual void statusChanged() {}
    };
    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    // The ensemble being edited (separate from the patch).
    EnsembleSession& getEnsemble() { return *ensemble; }

    juce::String getStatus() const { return status; }
    void setStatus (const juce::String& s, float newProgress = -1.0f);
    float getProgress() const { return progress; } // 0..1, < 0 when idle
    const juce::StringArray& getLog() const { return logLines; }

private:
    void sidUnsolicitedDump (const sid::sysex::Message&) override;
    void sidLog (const juce::String&) override;

    void fireReplaced();
    void fireChanged (const std::vector<uint16_t>& addresses);
    void fetchNext (int i, int endExclusive, int got, int failed);
    void writeNext (std::vector<int> remaining, int done, int failed, int total);

    sid::Patch editPatch = sid::Patch::makeInit (sid::Engine::Lead);
    int sourceSlot = -1;
    bool live = true;

    std::array<std::optional<sid::Patch>, kSlots> slots;
    int bankNumber = 0;

    bool busy = false;
    bool cancelRequested = false;
    juce::String status;
    float progress = -1.0f;
    juce::StringArray logLines;

    juce::ListenerList<Listener> listeners;
    std::unique_ptr<EnsembleSession> ensemble;
    JUCE_DECLARE_WEAK_REFERENCEABLE (Session)
};
