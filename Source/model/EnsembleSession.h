#pragma once

// The ensemble being edited: settings for up to 4 SID cores (which patch
// each plays, clock, MIDI channels and splits, filter calibration).

#include "ParamTarget.h"
#include "sid/SysEx.h"

#include <juce_data_structures/juce_data_structures.h>

class Session;

class EnsembleSession : public ParamTarget
{
public:
    explicit EnsembleSession (Session& s);

    uint8_t getByte (int addr) const override { return ens.byte (addr); }
    int getParam (const sid::ParamDef& p) const override { return ens.getRaw (p); }
    void setParam (const sid::ParamDef& p, int raw, uint8_t writeOption = 0) override;

    MidiLearn* getMidiLearn() override;
    int learnScope() const override { return 4; } // MidiLearn::kEnsembleScope

    const sid::Ensemble& data() const { return ens; }
    bool hasData() const { return loaded; }

    int getNumber() const { return number; }      // ensemble 0..127 used for fetch/store
    void setNumber (int n) { number = juce::jlimit (0, 127, n); }

    void fetchEditBuffer();   // what the box is using right now
    void sendEditBuffer();
    void fetchStored();       // ensemble <number> from the box's memory
    void storeToBox();        // write into ensemble <number>
    void replace (const sid::Ensemble& e, bool fromBox);

    bool loadSyx (const juce::File& f, juce::String& message);
    bool saveSyx (const juce::File& f, juce::String& message) const;

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& v);

private:
    Session& session;
    sid::Ensemble ens;
    bool loaded = false;
    int number = 0;
    JUCE_DECLARE_WEAK_REFERENCEABLE (EnsembleSession)
};
