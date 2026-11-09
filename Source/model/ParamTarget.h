#pragma once

// Something editable through ParamDefs: the patch edit buffer or the ensemble.
// UI controls bind to a ParamTarget and refresh when it changes.

#include "sid/Params.h"

#include <juce_events/juce_events.h>

class MidiLearn;

class ParamTarget
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void targetReplaced() {}
        virtual void targetChanged (const std::vector<uint16_t>& addresses) { juce::ignoreUnused (addresses); }
    };

    virtual ~ParamTarget() = default;

    virtual uint8_t getByte (int addr) const = 0;
    virtual int getParam (const sid::ParamDef& p) const = 0;
    virtual void setParam (const sid::ParamDef& p, int raw, uint8_t writeOption = 0) = 0;

    // Value a control returns to on double-click.
    virtual int defaultRaw (const sid::ParamDef& p) const { return getParam (p); }

    // Write a whole byte (used by grid editors such as sequencers).
    void setByteValue (uint16_t addr, uint8_t value)
    {
        sid::ParamDef d;
        d.addr = addr;
        d.bits = 8;
        setParam (d, value);
    }

    // MIDI learn support: the shared MidiLearn and the scope controls of this
    // target are learned in (engine number, or MidiLearn::kEnsembleScope).
    virtual MidiLearn* getMidiLearn() { return nullptr; }
    virtual int learnScope() const { return -1; }

    void addTargetListener (Listener* l) { targetListeners.add (l); }
    void removeTargetListener (Listener* l) { targetListeners.remove (l); }

protected:
    void notifyReplaced() { targetListeners.call ([] (Listener& l) { l.targetReplaced(); }); }
    void notifyChanged (const std::vector<uint16_t>& a) { targetListeners.call ([&] (Listener& l) { l.targetChanged (a); }); }

    juce::ListenerList<Listener> targetListeners;
};
