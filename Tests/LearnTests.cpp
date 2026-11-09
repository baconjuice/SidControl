// MIDI learn end to end: controllers go through SidDevice::forwardMidi (the
// play-through path), get mapped and change the edit buffer. No ports needed.

#include "device/SidDevice.h"
#include "model/MidiLearn.h"
#include "model/Session.h"

#include <cstdio>

static int failures = 0;
#define CHECK(c) do { if (! (c)) { std::printf ("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void sendCC (SidDevice& d, int ch, int cc, int value)
{
    juce::MidiBuffer b;
    b.addEvent (juce::MidiMessage::controllerEvent (ch, cc, value), 0);
    d.forwardMidi (b);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    SidDevice device;
    Session session (device);
    auto& ml = *session.getMidiLearn();
    const auto& T = sid::paramsFor (sid::Engine::Lead);
    const auto* cutoff = T.find ("fL.cutoff");
    CHECK (cutoff != nullptr);
    if (cutoff == nullptr) return 1;

    // Not learning, nothing mapped: CC is ignored by the editor.
    const int before = session.getParam (*cutoff);
    sendCC (device, 1, 21, 0);
    CHECK (session.getParam (*cutoff) == before);

    // Learn: first CC that arrives gets mapped and applied.
    ml.startLearning ((int) sid::Engine::Lead, "fL.cutoff", 0);
    CHECK (device.isLearning());
    sendCC (device, 3, 74, 127);
    CHECK (! device.isLearning());
    const auto* m = ml.find ((int) sid::Engine::Lead, "fL.cutoff");
    CHECK (m != nullptr && m->channel == 3 && m->cc == 74);
    CHECK (session.getParam (*cutoff) == 4095);
    sendCC (device, 3, 74, 0);
    CHECK (session.getParam (*cutoff) == 0);
    sendCC (device, 1, 74, 100); // other channel: not mapped
    CHECK (session.getParam (*cutoff) == 0);

    // Scaling.
    sid::ParamDef b; b.kind = sid::ParamDef::Kind::Bool; b.bits = 1;
    CHECK (MidiLearn::scaledRaw (b, 63) == 0 && MidiLearn::scaledRaw (b, 64) == 1);
    sid::ParamDef c; c.kind = sid::ParamDef::Kind::Choice; c.bits = 3; c.choices = { "a", "b", "c", "d" };
    CHECK (MidiLearn::scaledRaw (c, 0) == 0 && MidiLearn::scaledRaw (c, 127) == 3 && MidiLearn::scaledRaw (c, 64) == 2);
    sid::ParamDef i8; i8.bits = 8;
    CHECK (MidiLearn::scaledRaw (i8, 127) == 255 && MidiLearn::scaledRaw (i8, 0) == 0);

    // Re-learning the same CC for another parameter in the same engine moves it.
    ml.startLearning ((int) sid::Engine::Lead, "fL.res", 0);
    sendCC (device, 3, 74, 50);
    CHECK (ml.find ((int) sid::Engine::Lead, "fL.cutoff") == nullptr);
    CHECK (ml.find ((int) sid::Engine::Lead, "fL.res") != nullptr);

    // Other engines don't react to Lead mappings.
    session.loadIntoEditor (sid::Patch::makeInit (sid::Engine::Bassline));
    const auto bassBefore = session.edit().data;
    sendCC (device, 3, 74, 127);
    CHECK (session.edit().data == bassBefore);

    // State round trip.
    auto state = session.toValueTree();
    ml.clearAll();
    CHECK (ml.all().empty());
    session.fromValueTree (state);
    CHECK (ml.all().size() == 1);

    // Cancel learning.
    ml.startLearning ((int) sid::Engine::Lead, "fL.cutoff", 0);
    ml.stopLearning();
    CHECK (! device.isLearning());

    std::printf (failures == 0 ? "learn tests passed\n" : "%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
