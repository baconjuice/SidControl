#include "Automation.h"
#include "sid/SysEx.h"

#include <cmath>

using sid::sysex::wopt::All;
using sid::sysex::wopt::LeftRight;

// ---------------------------------------------------------------------------
class Automation::SlotParameter : public juce::AudioParameterFloat
{
public:
    SlotParameter (Automation& a, int i)
        : juce::AudioParameterFloat (juce::ParameterID { "slot" + juce::String (i + 1), 1 },
                                     "Slot " + juce::String (i + 1), juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                     juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                                         [&a, i] (float v, int) { return a.valueText (i, v); })),
          owner (a), index (i)
    {
    }

    juce::String getName (int maximumLength) const override
    {
        return owner.slotLabel (index).substring (0, maximumLength);
    }

    std::atomic<bool> dirty { false };

private:
    void valueChanged (float) override { dirty = true; }

    Automation& owner;
    int index;
};

// ---------------------------------------------------------------------------
Automation::Automation (juce::AudioProcessor& p, Session& s) : processor (p), session (s)
{
    for (int e = 0; e < 4; ++e)
        slots[(size_t) e] = defaultsFor (e);
    currentEngine = (int) session.engine();

    for (int i = 0; i < kSlots; ++i)
    {
        auto param = std::make_unique<SlotParameter> (*this, i);
        params[(size_t) i] = param.get();
        processor.addParameter (param.release());
    }
    session.addListener (this);
    session.setAutomation (this);
    editReplaced();
    startTimer (15);
}

Automation::~Automation()
{
    stopTimer();
    session.setAutomation (nullptr);
    session.removeListener (this);
}

std::array<Automation::Assignment, Automation::kSlots> Automation::defaultsFor (int engine)
{
    std::array<Assignment, kSlots> a {};
    auto set = [&a] (int slot, const std::string& id, uint8_t w = 0) { a[(size_t) slot - 1] = { id, w }; };
    set (1, "fL.cutoff", LeftRight);
    set (2, "fL.res", LeftRight);
    switch ((sid::Engine) engine)
    {
        case sid::Engine::Lead:
            set (3, "lead.volume");
            set (4, "v1.pw", All);
            set (5, "lead.detune");
            set (6, "v1.porta", All);
            set (7, "lfo1.rate");
            set (8, "lfo1.depth");
            set (9, "lfo2.rate");
            set (10, "lfo2.depth");
            set (11, "env1.attack1");
            set (12, "env1.decay1");
            set (13, "env1.sustain");
            set (14, "env1.release1");
            set (15, "env1.depth");
            set (16, "fL.keytrack", LeftRight);
            break;
        case sid::Engine::Bassline:
            set (3, "eng.volume");
            set (4, "bL.pw", LeftRight);
            set (5, "bL.accent", LeftRight);
            set (6, "bL.env.depthf", LeftRight);
            set (7, "bL.env.decay", LeftRight);
            set (8, "bL.env.attack", LeftRight);
            set (9, "bL.env.sustain", LeftRight);
            set (10, "bL.env.release", LeftRight);
            set (11, "bL.lfo1.rate", LeftRight);
            set (12, "bL.lfo1.depthf", LeftRight);
            set (13, "bL.lfo1.depthpw", LeftRight);
            set (14, "bL.porta", LeftRight);
            set (15, "bL.accentdecay", LeftRight);
            set (16, "fL.keytrack", LeftRight);
            break;
        case sid::Engine::Drum:
            set (3, "eng.volume");
            for (int d = 1; d <= 6; ++d) // slots 4..15: tune and decay of drums 1-6
            {
                set (2 + d * 2, "d" + std::to_string (d) + ".tune");
                set (3 + d * 2, "d" + std::to_string (d) + ".decay");
            }
            set (16, "fL.keytrack", LeftRight);
            break;
        case sid::Engine::Multi:
            set (3, "eng.volume");
            set (4, "m1.pw");
            set (5, "m1.env.depthf");
            set (6, "m1.env.attack");
            set (7, "m1.env.decay");
            set (8, "m1.env.sustain");
            set (9, "m1.env.release");
            set (10, "m1.lfo1.rate");
            set (11, "m1.lfo1.depthp");
            set (12, "m1.lfo1.depthf");
            set (13, "m1.lfo2.rate");
            set (14, "m1.porta");
            set (15, "m1.finetune");
            set (16, "fL.keytrack", LeftRight);
            break;
    }
    return a;
}

const sid::ParamDef* Automation::defFor (int engine, int slot) const
{
    const juce::ScopedLock sl (lock);
    const auto& a = slots[(size_t) engine][(size_t) slot];
    return a.paramId.empty() ? nullptr : sid::paramsFor ((sid::Engine) engine).find (a.paramId);
}

int Automation::slotFor (int engine, const std::string& paramId) const
{
    if (engine < 0 || engine > 3)
        return -1;
    const juce::ScopedLock sl (lock);
    for (int i = 0; i < kSlots; ++i)
        if (slots[(size_t) engine][(size_t) i].paramId == paramId)
            return i;
    return -1;
}

juce::String Automation::slotLabel (int slot) const
{
    const int e = currentEngine.load();
    const auto* d = defFor (e, slot);
    juce::String label = juce::String (slot + 1) + ": ";
    if (d == nullptr)
        return label + "(unused)";
    uint8_t w;
    {
        const juce::ScopedLock sl (lock);
        w = slots[(size_t) e][(size_t) slot].writeOption;
    }

    // Readable names: "v1.pw" with all voices linked -> "OSC Pulsewidth",
    // "bL.env.decay" -> "Bass ENV Decay", "d3.tune" -> "Drum 3 Tune".
    const juce::String id (d->id);
    const auto head = id.upToFirstOccurrenceOf (".", false, false);
    const auto num = head.retainCharacters ("0123456789");
    juce::String prefix;
    if (head == "fL" || head == "fR") prefix = w != 0 ? "Filter" : "Filter " + head.substring (1);
    else if (head == "lead" || head == "eng") prefix = {};
    else if (head.startsWith ("v")) prefix = w != 0 ? "OSC" : "OSC" + num;
    else if (head == "bL" || head == "bR") prefix = w != 0 ? "Bass" : "Bass " + head.substring (1);
    else if (head.startsWith ("m") && num.isNotEmpty()) prefix = "Inst " + num;
    else if (head.startsWith ("d") && num.isNotEmpty()) prefix = "Drum " + num;
    else prefix = head.toUpperCase();

    const auto middle = id.fromFirstOccurrenceOf (".", false, false).upToFirstOccurrenceOf (".", false, false);
    if (id.fromFirstOccurrenceOf (".", false, false).contains (".") && (middle.startsWith ("lfo") || middle == "env"))
        prefix << " " << middle.toUpperCase();
    return label + (prefix.isEmpty() ? juce::String() : prefix + " ") + juce::String (d->name);
}

juce::String Automation::valueText (int slot, float v) const
{
    const auto* d = defFor (currentEngine.load(), slot);
    if (d == nullptr)
        return "-";
    const int raw = juce::roundToInt (v * (float) d->maxRaw());
    if (! d->choices.empty() && raw < (int) d->choices.size())
        return d->choices[(size_t) raw];
    return juce::String (raw - d->offset);
}

void Automation::assign (int slot, int engine, const std::string& paramId, uint8_t writeOption)
{
    if (slot < 0 || slot >= kSlots || engine < 0 || engine > 3)
        return;
    {
        const juce::ScopedLock sl (lock);
        auto& e = slots[(size_t) engine];
        for (auto& a : e)
            if (a.paramId == paramId)
                a = {};
        e[(size_t) slot] = { paramId, writeOption };
    }
    assignmentsChanged();
}

void Automation::unassign (int engine, const std::string& paramId)
{
    if (engine < 0 || engine > 3)
        return;
    {
        const juce::ScopedLock sl (lock);
        for (auto& a : slots[(size_t) engine])
            if (a.paramId == paramId)
                a = {};
    }
    assignmentsChanged();
}

void Automation::resetToDefaults (int engine)
{
    if (engine < 0 || engine > 3)
        return;
    {
        const juce::ScopedLock sl (lock);
        slots[(size_t) engine] = defaultsFor (engine);
    }
    assignmentsChanged();
}

void Automation::assignmentsChanged()
{
    for (int i = 0; i < kSlots; ++i)
        pushToHost (i);
    processor.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withParameterInfoChanged (true));
    notify();
}

float Automation::normalised (const sid::ParamDef& d) const
{
    return d.maxRaw() > 0 ? (float) session.getParam (d) / (float) d.maxRaw() : 0.0f;
}

void Automation::pushToHost (int slot)
{
    auto* p = params[(size_t) slot];
    const auto* d = defFor (currentEngine.load(), slot);
    if (p == nullptr || d == nullptr)
        return;
    const float v = normalised (*d);
    if (std::abs (p->get() - v) > 1.0e-6f)
    {
        p->setValueNotifyingHost (v);
        p->dirty = false; // our own value, nothing to apply
    }
}

void Automation::editReplaced()
{
    const int e = (int) session.engine();
    if (e != currentEngine.exchange (e))
    {
        assignmentsChanged(); // names change with the engine
        return;
    }
    for (int i = 0; i < kSlots; ++i)
        pushToHost (i);
}

void Automation::editChanged (const std::vector<uint16_t>& addresses)
{
    if (applyingFromHost)
        return;
    const int e = currentEngine.load();
    for (int i = 0; i < kSlots; ++i)
        if (const auto* d = defFor (e, i))
            for (auto a : d->addresses())
                if (std::find (addresses.begin(), addresses.end(), a) != addresses.end())
                {
                    pushToHost (i);
                    break;
                }
}

void Automation::timerCallback()
{
    const int e = currentEngine.load();
    for (int i = 0; i < kSlots; ++i)
    {
        auto* p = params[(size_t) i];
        if (p == nullptr || ! p->dirty.exchange (false))
            continue;
        const auto* d = defFor (e, i);
        if (d == nullptr)
            continue;
        const int raw = juce::jlimit (0, d->maxRaw(), juce::roundToInt (p->get() * (float) d->maxRaw()));
        if (raw == session.getParam (*d))
            continue;
        uint8_t w;
        {
            const juce::ScopedLock sl (lock);
            w = slots[(size_t) e][(size_t) i].writeOption;
        }
        const juce::ScopedValueSetter<bool> svs (applyingFromHost, true);
        session.setParam (*d, raw, w);
    }
}

juce::ValueTree Automation::toValueTree() const
{
    juce::ValueTree v ("Automation");
    const juce::ScopedLock sl (lock);
    for (int e = 0; e < 4; ++e)
        for (int i = 0; i < kSlots; ++i)
        {
            const auto& a = slots[(size_t) e][(size_t) i];
            juce::ValueTree c ("Slot");
            c.setProperty ("engine", e, nullptr);
            c.setProperty ("slot", i, nullptr);
            c.setProperty ("param", juce::String (a.paramId), nullptr);
            c.setProperty ("wopt", (int) a.writeOption, nullptr);
            v.appendChild (c, nullptr);
        }
    return v;
}

void Automation::fromValueTree (const juce::ValueTree& v)
{
    if (! v.isValid())
        return;
    {
        const juce::ScopedLock sl (lock);
        for (const auto& c : v)
        {
            const int e = c.getProperty ("engine", -1), i = c.getProperty ("slot", -1);
            if (e < 0 || e > 3 || i < 0 || i >= kSlots)
                continue;
            const auto id = c.getProperty ("param").toString().toStdString();
            if (! id.empty() && sid::paramsFor ((sid::Engine) e).find (id) == nullptr)
                continue;
            slots[(size_t) e][(size_t) i] = { id, (uint8_t) (int) c.getProperty ("wopt", 0) };
        }
    }
    currentEngine = (int) session.engine();
    assignmentsChanged();
}
