#pragma once

// DAW automation: 16 host parameters ("slots"), each driving one patch
// parameter of the current engine. Every engine has its own assignments,
// starting with useful defaults (cutoff, resonance, volume, ...). Changes
// from the host are applied on the message thread through the Session, so
// they reach the SID like any other live edit; edits made in the editor are
// reported back to the host so they can be recorded.

#include "model/AutomationLink.h"
#include "model/Session.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

class Automation : public AutomationLink,
                   private Session::Listener,
                   private juce::Timer
{
public:
    static constexpr int kSlots = 16;

    // Creates the host parameters and adds them to the processor.
    Automation (juce::AudioProcessor& processor, Session& session);
    friend class SlotParameter;
    ~Automation() override;

    // AutomationLink
    int numSlots() const override { return kSlots; }
    int slotFor (int engine, const std::string& paramId) const override;
    juce::String slotLabel (int slot) const override;
    void assign (int slot, int engine, const std::string& paramId, uint8_t writeOption) override;
    void unassign (int engine, const std::string& paramId) override;
    void resetToDefaults (int engine) override;

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& v);

    struct Assignment
    {
        std::string paramId; // empty = unused
        uint8_t writeOption = 0;
    };
    static std::array<Assignment, kSlots> defaultsFor (int engine);

private:
    class SlotParameter;

    void editReplaced() override;
    void editChanged (const std::vector<uint16_t>& addresses) override;
    void timerCallback() override;

    const sid::ParamDef* defFor (int engine, int slot) const;
    float normalised (const sid::ParamDef& d) const;
    void pushToHost (int slot);
    juce::String valueText (int slot, float normalisedValue) const;
    void assignmentsChanged();

    juce::AudioProcessor& processor;
    Session& session;
    std::array<SlotParameter*, kSlots> params {};
    std::array<std::array<Assignment, kSlots>, 4> slots; // [engine][slot]
    mutable juce::CriticalSection lock;                  // slots are read by the host's threads
    std::atomic<int> currentEngine { 0 };
    bool applyingFromHost = false;
};
