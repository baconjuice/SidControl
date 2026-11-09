#pragma once

// DAW automation slots as seen by the editor (implemented by the plugin's
// Automation class). A slot is a host parameter that drives one patch
// parameter; each engine has its own slot assignments.

#include <juce_core/juce_core.h>

#include <string>

class AutomationLink
{
public:
    virtual ~AutomationLink() = default;

    virtual int numSlots() const = 0;
    virtual int slotFor (int engine, const std::string& paramId) const = 0; // -1 if none
    virtual juce::String slotLabel (int slot) const = 0;                     // "3: CutOff" (current engine)
    virtual void assign (int slot, int engine, const std::string& paramId, uint8_t writeOption) = 0;
    virtual void unassign (int engine, const std::string& paramId) = 0;
    virtual void resetToDefaults (int engine) = 0;

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void automationChanged() = 0;
    };
    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

protected:
    void notify() { listeners.call ([] (Listener& l) { l.automationChanged(); }); }
    juce::ListenerList<Listener> listeners;
};
