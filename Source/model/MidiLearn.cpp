#include "MidiLearn.h"
#include "EnsembleSession.h"
#include "Session.h"

#include <algorithm>
#include <cmath>

namespace
{
const char* scopeName (int scope)
{
    switch (scope)
    {
        case 0: return "Lead";
        case 1: return "Bassline";
        case 2: return "Drum";
        case 3: return "Multi";
        default: return "Ensemble";
    }
}

const sid::ParamTable& tableFor (int scope)
{
    return scope == MidiLearn::kEnsembleScope ? sid::ensembleParams() : sid::paramsFor ((sid::Engine) scope);
}
} // namespace

MidiLearn::MidiLearn (Session& s) : session (s)
{
    session.device.addListener (this);
}

MidiLearn::~MidiLearn()
{
    session.device.setLearning (false);
    session.device.clearControllerMappings();
    session.device.removeListener (this);
}

void MidiLearn::startLearning (int scope, const std::string& paramId, uint8_t writeOption)
{
    Mapping m;
    m.scope = scope;
    m.paramId = paramId;
    m.writeOption = writeOption;
    learnTarget = m;
    session.device.setLearning (true);
    const auto* d = tableFor (scope).find (paramId);
    session.setStatus ("MIDI learn: move a knob or slider on your controller for \""
                       + juce::String (d != nullptr ? d->name : paramId) + "\" (right-click the control to cancel)");
    changed();
}

void MidiLearn::stopLearning()
{
    if (! learnTarget)
        return;
    learnTarget.reset();
    session.device.setLearning (false);
    session.setStatus ("MIDI learn cancelled");
    changed();
}

bool MidiLearn::isLearning (int scope, const std::string& paramId) const
{
    return learnTarget && learnTarget->scope == scope && learnTarget->paramId == paramId;
}

const MidiLearn::Mapping* MidiLearn::find (int scope, const std::string& paramId) const
{
    for (auto& m : mappings)
        if (m.scope == scope && m.paramId == paramId)
            return &m;
    return nullptr;
}

void MidiLearn::forget (int scope, const std::string& paramId)
{
    mappings.erase (std::remove_if (mappings.begin(), mappings.end(),
                                    [&] (const Mapping& m) { return m.scope == scope && m.paramId == paramId; }),
                    mappings.end());
    syncDevice();
    changed();
}

void MidiLearn::clearAll()
{
    mappings.clear();
    syncDevice();
    changed();
}

juce::String MidiLearn::describe (const Mapping& m)
{
    return "CC " + juce::String (m.cc) + ", ch " + juce::String (m.channel);
}

int MidiLearn::scaledRaw (const sid::ParamDef& d, int value)
{
    value = juce::jlimit (0, 127, value);
    if (d.kind == sid::ParamDef::Kind::Bool)
        return value >= 64 ? 1 : 0;
    const int maxRaw = d.choices.empty() ? d.maxRaw() : juce::jmin (d.maxRaw(), (int) d.choices.size() - 1);
    if (maxRaw <= 0)
        return 0;
    if (maxRaw < 127)
        return juce::jmin (maxRaw, value * (maxRaw + 1) / 128); // equal sized zones per step
    return (int) std::lround ((double) value * maxRaw / 127.0);
}

void MidiLearn::sidController (int channel, int cc, int value)
{
    if (learnTarget)
    {
        auto m = *learnTarget;
        m.channel = channel;
        m.cc = cc;
        learnTarget.reset();
        session.device.setLearning (false);

        // One controller per scope, one controller per parameter.
        mappings.erase (std::remove_if (mappings.begin(), mappings.end(),
                                        [&] (const Mapping& o)
                                        {
                                            return o.scope == m.scope
                                                   && (o.paramId == m.paramId || (o.channel == channel && o.cc == cc));
                                        }),
                        mappings.end());
        mappings.push_back (m);
        syncDevice();

        const auto* d = tableFor (m.scope).find (m.paramId);
        session.setStatus (describe (m) + " now controls \"" + juce::String (d != nullptr ? d->name : m.paramId) + "\" ("
                           + scopeName (m.scope) + ")");
        changed();
        apply (m, value);
        return;
    }

    const int engineScope = (int) session.engine();
    for (auto& m : mappings)
        if (m.channel == channel && m.cc == cc && (m.scope == engineScope || m.scope == kEnsembleScope))
            apply (m, value);
}

void MidiLearn::apply (const Mapping& m, int value)
{
    const auto* d = tableFor (m.scope).find (m.paramId);
    if (d == nullptr)
        return;

    ParamTarget* target = &session;
    if (m.scope == kEnsembleScope)
    {
        if (! session.getEnsemble().hasData())
            return;
        target = &session.getEnsemble();
    }
    else if (m.scope != (int) session.engine())
    {
        return;
    }

    const int raw = scaledRaw (*d, value);
    if (raw != target->getParam (*d))
        target->setParam (*d, raw, m.writeOption);
}

void MidiLearn::syncDevice()
{
    session.device.clearControllerMappings();
    for (auto& m : mappings)
        session.device.setControllerMapped (m.channel, m.cc, true);
}

void MidiLearn::changed()
{
    listeners.call ([] (Listener& l) { l.midiLearnChanged(); });
}

juce::ValueTree MidiLearn::toValueTree() const
{
    juce::ValueTree v ("MidiLearn");
    for (auto& m : mappings)
    {
        juce::ValueTree c ("Map");
        c.setProperty ("channel", m.channel, nullptr);
        c.setProperty ("cc", m.cc, nullptr);
        c.setProperty ("scope", m.scope, nullptr);
        c.setProperty ("param", juce::String (m.paramId), nullptr);
        c.setProperty ("wopt", (int) m.writeOption, nullptr);
        v.appendChild (c, nullptr);
    }
    return v;
}

void MidiLearn::fromValueTree (const juce::ValueTree& v)
{
    mappings.clear();
    for (const auto& c : v)
    {
        Mapping m;
        m.channel = juce::jlimit (1, 16, (int) c.getProperty ("channel", 1));
        m.cc = juce::jlimit (0, 119, (int) c.getProperty ("cc", 0));
        m.scope = juce::jlimit (0, kEnsembleScope, (int) c.getProperty ("scope", 0));
        m.paramId = c.getProperty ("param").toString().toStdString();
        m.writeOption = (uint8_t) (int) c.getProperty ("wopt", 0);
        if (tableFor (m.scope).find (m.paramId) != nullptr)
            mappings.push_back (m);
    }
    syncDevice();
    changed();
}
